#include "node_ota/node_ota.h"
#include "ota_receiver.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "reset_manager/reset_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define ACK_TIMEOUT_MS 15000
#define NODE_OTA_INBOX_SIZE 10
#define BOOT_CONFIRM_TIMEOUT_MS 90000
#define BOOT_RECOVERY_RESTART_MS 120000
#define BOOT_RESUME_OP 8 /* Internal center-to-peripheral control; never a UART opcode. */
#define BOOT_RESUME_DELAY_US 5000000LL
#define SERIAL_LINE_SIZE (sizeof(NODE_OTA_PREFIX) + NODE_OTA_MAX_PACKET * 2 + 2)

static const char *TAG = "node_ota";
static ring_share_t *ring;
static uint8_t role;
static QueueHandle_t inbox;
static SemaphoreHandle_t exchange_lock;
static ota_receiver_t receiver;
static bool serial_started;
static volatile bool application_ready;
static bool maintenance;
static uint32_t maintenance_session;
static int64_t maintenance_activity;
static int64_t boot_release_at;
static uint32_t boot_sequence;
static void boot_coordinator(void *arg);
static bool boot_pending(void);
static volatile unsigned inbox_drops;
static volatile unsigned invalid_packets;
static unsigned reported_drops, reported_invalid;
/* Latched only for the first boot of a newly selected OTA image. Cleared by
 * the next boot, when otadata already marks this image valid. */
static bool boot_recovery_pending;

_Static_assert(NODE_OTA_MAX_PACKET + 1 <= 512, "Must fit sibling callback queue");

/* Always visible with --verbose, even when IDF logging is disabled. Share the
 * ACK output lock so a diagnostic cannot split a serial protocol frame. */
static void diagnostic(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    flockfile(stdout);
    printf("\nI4AOTA_DIAG t_ms=%lld role=%u ",
           (long long)(esp_timer_get_time() / 1000),
           (unsigned)(ring ? role : NODE_OTA_HOST));
    vprintf(format, args);
    putchar('\n');
    fflush(stdout);
    funlockfile(stdout);
    va_end(args);
}

static void report_link_drops(void)
{
    unsigned drops = inbox_drops, invalid = invalid_packets;
    if (drops != reported_drops || invalid != reported_invalid) {
        diagnostic("event=ring_discards drops=%u invalid=%u", drops, invalid);
        reported_drops = drops;
        reported_invalid = invalid;
    }
}

static void set_maintenance(bool active, uint32_t session)
{
    if (maintenance != active) {
        rs_set_maintenance(ring, active);
        maintenance = active;
        diagnostic("event=maintenance_%s session=%lu", active ? "enter" : "leave",
                   (unsigned long)(active ? session : maintenance_session));
    }
    if (active) {
        maintenance_session = session;
        maintenance_activity = esp_timer_get_time();
    }
}

static void expire_receiver(void)
{
    uint8_t phase = receiver.phase;
    uint32_t session = receiver.session, written = receiver.received;
    ota_receiver_expire(&receiver);
    if (boot_release_at && esp_timer_get_time() >= boot_release_at) {
        rs_set_boot_quiet(ring, false);
        boot_release_at = 0;
        diagnostic("event=boot_traffic_resumed");
    }
    if (maintenance && esp_timer_get_time() - maintenance_activity > NODE_OTA_IDLE_TIMEOUT_US)
        set_maintenance(false, 0);
    if (phase != NODE_OTA_IDLE && receiver.phase == NODE_OTA_IDLE)
        diagnostic("event=session_expired session=%lu phase=%u written=%lu",
                   (unsigned long)session, phase, (unsigned long)written);
}

static void handle_request(const node_ota_packet_t *request, node_ota_packet_t *reply)
{
    expire_receiver();
    if (request->op == NODE_OTA_BEGIN && (!application_ready || rs_boot_quiet(ring))) {
        memset(reply, 0, sizeof(*reply));
        reply->op = request->op | NODE_OTA_REPLY;
        reply->targets = request->targets;
        reply->sender = role;
        reply->session = request->session;
        reply->sequence = request->sequence;
        reply->offset = receiver.received;
        reply->length = 5;
        reply->data[4] = receiver.phase;
        node_ota_put_u32(reply->data, ESP_ERR_INVALID_STATE);
        diagnostic("event=begin_not_ready session=%lu seq=%lu",
                   (unsigned long)request->session, (unsigned long)request->sequence);
        return;
    }
    if (request->op == BOOT_RESUME_OP) {
        memset(reply, 0, sizeof(*reply));
        reply->op = request->op | NODE_OTA_REPLY;
        reply->targets = request->targets;
        reply->sender = role;
        reply->session = request->session;
        reply->sequence = request->sequence;
        reply->length = 5;
        reply->data[4] = receiver.phase;
        bool ready = application_ready && !boot_recovery_pending && !boot_pending() &&
                     receiver.phase == NODE_OTA_IDLE && !maintenance;
        node_ota_put_u32(reply->data, ready ? ESP_OK : ESP_ERR_INVALID_STATE);
        if (ready && rs_boot_quiet(ring)) {
            boot_release_at = esp_timer_get_time() + BOOT_RESUME_DELAY_US;
            diagnostic("event=boot_resume_scheduled delay_ms=5000");
        }
        return;
    }
    bool entering = request->op == NODE_OTA_BEGIN && receiver.phase == NODE_OTA_IDLE;
    if (entering) set_maintenance(true, request->session);
    ota_receiver_handle(&receiver, request, reply);
    if (request->op == NODE_OTA_INFO) {
        reply->data[51] = application_ready;
        reply->data[52] = rs_boot_quiet(ring);
        reply->length = 53;
    }
    if (node_ota_get_u32(reply->data) == ESP_OK && request->op != NODE_OTA_INFO && maintenance)
        maintenance_activity = esp_timer_get_time();
    if (receiver.phase == NODE_OTA_IDLE &&
        (entering || request->op == NODE_OTA_ABORT))
        set_maintenance(false, 0);
}

static bool valid_request(const node_ota_packet_t *p)
{
    return ((p->sender == NODE_OTA_HOST && p->op >= NODE_OTA_INFO && p->op <= NODE_OTA_REBOOT) ||
            (p->sender == NODE_OTA_CENTER && p->op == BOOT_RESUME_OP && p->length == 0)) &&
           p->length <= NODE_OTA_CHUNK &&
           (p->targets & (1U << NODE_OTA_CENTER)) && !(p->targets & ~NODE_OTA_ROLES);
}

static void on_ring_message(void *context, const uint8_t *wire, uint16_t length)
{
    node_ota_packet_t p;
    if (!node_ota_decode(wire, length, &p)) {
        ++invalid_packets;
        return;
    }
    bool accept = role == NODE_OTA_CENTER
        ? ((p.op & NODE_OTA_REPLY) && p.sender < NODE_OTA_CENTER && p.length >= 5)
        : (valid_request(&p) && (p.targets & (1U << role)));
    /* Never erase/write flash or wait for ACKs in the shared ring callback.
     * Queue overflow is recovered by the host's exact retransmission. */
    /* Count here, report from the OTA task; never block the ring on stdout. */
    if (accept && !xQueueSend(inbox, &p, 0)) ++inbox_drops;
}

static void receiver_task(void *arg)
{
    node_ota_packet_t request, reply;
    uint8_t wire[NODE_OTA_MAX_PACKET];
    for (;;) {
        report_link_drops();
        if (xQueueReceive(inbox, &request, pdMS_TO_TICKS(1000))) {
            handle_request(&request, &reply);
            size_t length = node_ota_encode(&reply, wire);
            if (!rs_broadcast(ring, RS_NODE_OTA, wire, length))
                diagnostic("event=reply_broadcast_failed op=%u session=%lu seq=%lu offset=%lu drops=%u invalid=%u",
                           request.op, (unsigned long)request.session,
                           (unsigned long)request.sequence, (unsigned long)request.offset,
                           inbox_drops, invalid_packets);
        } else {
            expire_receiver();
        }
    }
}

esp_err_t node_ota_init(ring_share_t *rs, uint8_t device_role)
{
    if (ring || !rs || device_role > NODE_OTA_CENTER) return ESP_ERR_INVALID_STATE;
    role = device_role;
    application_ready = false;
    maintenance = false;
    maintenance_session = 0;
    boot_release_at = 0;
    boot_sequence = 0;
    inbox_drops = invalid_packets = 0;
    reported_drops = reported_invalid = 0;
    inbox = xQueueCreate(NODE_OTA_INBOX_SIZE, sizeof(node_ota_packet_t));
    exchange_lock = xSemaphoreCreateMutex();
    if (!inbox || !exchange_lock) return ESP_ERR_NO_MEM;
    ring = rs;
    ota_receiver_init(&receiver, role);
    receiver.boot_recovery_pending = boot_recovery_pending;
    diagnostic("event=boot_traffic_paused");
    rs_register_component(ring, RS_NODE_OTA,
        (ring_callback_t){.callback = on_ring_message});
    if (role != NODE_OTA_CENTER &&
        xTaskCreate(receiver_task, "ota_receiver", 6144, NULL, 2, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t node_ota_exchange(const node_ota_packet_t *request,
                            node_ota_reply_fn emit, void *context)
{
    if (!ring || role != NODE_OTA_CENTER) return ESP_ERR_INVALID_STATE;
    if (!request || !emit || !valid_request(request)) return ESP_ERR_INVALID_ARG;
    if (request->op == NODE_OTA_BEGIN && (!application_ready || rs_boot_quiet(ring))) {
        node_ota_packet_t reply;
        handle_request(request, &reply);
        emit(&reply, context);
        return ESP_ERR_INVALID_STATE;
    }
    if (!xSemaphoreTake(exchange_lock, 0)) return ESP_ERR_INVALID_STATE;
    report_link_drops();
    expire_receiver();
    /* Every selected peripheral enters/drains maintenance before its BEGIN ACK.
     * DATA is admitted by the host only after all BEGIN replies. The center
     * enters before sending BEGIN so background sends cannot compete. */
    if (request->op == NODE_OTA_BEGIN && receiver.phase == NODE_OTA_IDLE)
        set_maintenance(true, request->session);
    if (maintenance && request->session == maintenance_session && request->op != NODE_OTA_INFO)
        maintenance_activity = esp_timer_get_time();

    node_ota_packet_t reply, directed = *request;
    uint8_t wire[NODE_OTA_MAX_PACKET];
    while (xQueueReceive(inbox, &reply, 0)) { }
    esp_err_t result = ESP_OK;
    int64_t started = esp_timer_get_time();
    int64_t deadline = started + ACK_TIMEOUT_MS * 1000LL;
    unsigned acknowledged = 0, ignored = 0;
    /* Status and cancellation must reach healthy boards even when one peer
     * is absent. Share the existing ACK-wait deadline across selected peers.
     * rs_broadcast itself can block beyond this deadline: it does not bound
     * the complete exchange, and expired deadlines add no further ACK wait. */
    bool visit_all = request->op == NODE_OTA_INFO || request->op == NODE_OTA_ABORT;
    unsigned peer_count = 0;
    for (uint8_t peer = 0; peer < NODE_OTA_CENTER; ++peer)
        if (request->targets & (1U << peer)) ++peer_count;
    int64_t peer_budget = ACK_TIMEOUT_MS * 1000LL;
    if (visit_all && peer_count) peer_budget /= peer_count;
    /* Only one board may modify flash at a time. Flash operations suspend
     * normal tasks/interrupts, including SPI receive/rearm and forwarding.
     * Concurrent erases/writes can therefore swallow another board's ACK.
     * Keep the center bit for compatibility with existing v1 receivers; the
     * center does not consume its own ring broadcast. Every operation uses
     * the same per-peer mask so session checks and cached retries still work. */
    for (uint8_t peer = 0; peer < NODE_OTA_CENTER; ++peer) {
        if (!(request->targets & (1U << peer))) continue;
        directed.targets = (1U << NODE_OTA_CENTER) | (1U << peer);
        size_t length = node_ota_encode(&directed, wire);
        int64_t peer_started = esp_timer_get_time();
        int64_t peer_deadline = visit_all ? peer_started + peer_budget : deadline;
        if (peer_deadline > deadline) peer_deadline = deadline;
        bool broadcast_ok = rs_broadcast(ring, RS_NODE_OTA, wire, length);
        if (!broadcast_ok)
            diagnostic("event=request_broadcast_failed op=%u session=%lu seq=%lu offset=%lu peer=%u elapsed_ms=%lld",
                       request->op, (unsigned long)request->session,
                       (unsigned long)request->sequence, (unsigned long)request->offset,
                       peer, (long long)((esp_timer_get_time() - peer_started) / 1000));
        bool received = false;
        unsigned expired_reads = 0;
        for (;;) {
            int64_t now = esp_timer_get_time();
            TickType_t wait = 0;
            if (now < peer_deadline) {
                int64_t remaining_ms = (peer_deadline - now + 999) / 1000;
                wait = pdMS_TO_TICKS(remaining_ms < 100 ? remaining_ms : 100);
                if (!wait) wait = 1;
            } else if (expired_reads++ >= NODE_OTA_INBOX_SIZE) {
                break;
            }
            /* A blocking physical send may outlive the ACK deadline after
             * already delivering a reply. Drain the bounded inbox without
             * waiting before declaring this peer missing. */
            if (!xQueueReceive(inbox, &reply, wait)) {
                if (!wait) break;
                continue;
            }
            if (reply.op != (request->op | NODE_OTA_REPLY) ||
                reply.session != request->session || reply.sequence != request->sequence ||
                reply.targets != directed.targets || reply.sender != peer) {
                ++ignored;
                continue;
            }
            esp_err_t err = (esp_err_t)node_ota_get_u32(reply.data);
            if (err != ESP_OK) result = err;
            acknowledged |= 1U << peer;
            int64_t elapsed_ms = (esp_timer_get_time() - peer_started) / 1000;
            /* Avoid five diagnostic lines per DATA chunk on a slow UART. */
            if (request->op != NODE_OTA_DATA || elapsed_ms >= 250 || err != ESP_OK)
                diagnostic("event=peer_ack op=%u session=%lu seq=%lu peer=%u elapsed_ms=%lld written=%lu error=0x%lx broadcast_ok=%u",
                           request->op, (unsigned long)request->session,
                           (unsigned long)request->sequence, peer, (long long)elapsed_ms,
                           (unsigned long)reply.offset, (unsigned long)err, (unsigned)broadcast_ok);
            /* The host tracks the whole node; only the SPI hop is directed. */
            reply.targets = request->targets;
            emit(&reply, context);
            received = true;
            break;
        }
        if (!received) {
            diagnostic("event=ack_timeout op=%u session=%lu seq=%lu offset=%lu peer=%u peer_ms=%lld total_ms=%lld ack_mask=0x%x ignored=%u drops=%u invalid=%u broadcast_ok=%u",
                       request->op, (unsigned long)request->session,
                       (unsigned long)request->sequence, (unsigned long)request->offset, peer,
                       (long long)((esp_timer_get_time() - peer_started) / 1000),
                       (long long)((esp_timer_get_time() - started) / 1000), acknowledged,
                       ignored, inbox_drops, invalid_packets, (unsigned)broadcast_ok);
            result = ESP_ERR_TIMEOUT;
            if (!visit_all) goto done;
        }
    }
    if (request->op == BOOT_RESUME_OP && result != ESP_OK) goto done;
    /* Receive peripheral ACKs before the center itself touches flash. */
    handle_request(request, &reply);
    emit(&reply, context);
    esp_err_t err = (esp_err_t)node_ota_get_u32(reply.data);
    if (err != ESP_OK) result = err;
done:
    if (result != ESP_OK || request->op != NODE_OTA_DATA || request->sequence % 128 == 0)
        diagnostic("event=exchange_end op=%u session=%lu seq=%lu elapsed_ms=%lld ack_mask=0x%x center_phase=%u center_written=%lu error=0x%lx drops=%u invalid=%u",
                   request->op, (unsigned long)request->session, (unsigned long)request->sequence,
                   (long long)((esp_timer_get_time() - started) / 1000), acknowledged,
                   receiver.phase, (unsigned long)receiver.received, (unsigned long)result,
                   inbox_drops, invalid_packets);
    xSemaphoreGive(exchange_lock);
    return result;
}

static void serial_reply(const node_ota_packet_t *reply, void *context)
{
    uint8_t wire[NODE_OTA_MAX_PACKET];
    size_t length = node_ota_encode(reply, wire);
    char line[SERIAL_LINE_SIZE];
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0;
    line[pos++] = '\n'; /* resync even after a partial printf from another task */
    memcpy(line + pos, NODE_OTA_PREFIX, strlen(NODE_OTA_PREFIX));
    pos += strlen(NODE_OTA_PREFIX);
    for (size_t i = 0; i < length; ++i) {
        line[pos++] = hex[wire[i] >> 4];
        line[pos++] = hex[wire[i] & 15];
    }
    line[pos++] = '\n';
    flockfile(stdout);
    fwrite(line, 1, pos, stdout);
    fflush(stdout);
    funlockfile(stdout);
}

static int unhex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void serial_line(char *line, size_t length)
{
    size_t prefix = strlen(NODE_OTA_PREFIX);
    if (length < prefix || memcmp(line, NODE_OTA_PREFIX, prefix)) return;
    size_t bytes = (length - prefix) / 2;
    if ((length - prefix) % 2 || bytes > NODE_OTA_MAX_PACKET) {
        diagnostic("event=serial_invalid_length length=%u", (unsigned)length);
        return;
    }
    uint8_t wire[NODE_OTA_MAX_PACKET];
    for (size_t i = 0; i < bytes; ++i) {
        int hi = unhex(line[prefix + 2 * i]), lo = unhex(line[prefix + 2 * i + 1]);
        if (hi < 0 || lo < 0) {
            diagnostic("event=serial_invalid_hex byte=%u", (unsigned)i);
            return;
        }
        wire[i] = (hi << 4) | lo;
    }
    node_ota_packet_t request;
    if (!node_ota_decode(wire, bytes, &request)) {
        diagnostic("event=serial_invalid_packet bytes=%u", (unsigned)bytes);
        return;
    }
    if (request.sender != NODE_OTA_HOST || request.op < NODE_OTA_INFO || request.op > NODE_OTA_REBOOT) {
        diagnostic("event=serial_invalid_request sender=%u op=%u", request.sender, request.op);
        return;
    }
    esp_err_t err = node_ota_exchange(&request, serial_reply, NULL);
    if (err == ESP_OK && request.op == NODE_OTA_REBOOT) {
        /* Peripherals remain up to carry ACKs around the ring. The center's
         * existing startup reset broadcast restarts them after it comes up. */
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000));
        vTaskDelay(pdMS_TO_TICKS(500));
        rm_restart_local();
    }
}

static void serial_task(void *arg)
{
    char line[SERIAL_LINE_SIZE];
    size_t length = 0;
    bool overflow = false;
    int64_t last_byte = 0;
    for (;;) {
        uint8_t byte;
        if (uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(100)) != 1) {
            if (esp_timer_get_time() - last_byte > 2000000) {
                length = 0;
                overflow = false;
            }
            if (xSemaphoreTake(exchange_lock, 0)) {
                expire_receiver();
                xSemaphoreGive(exchange_lock);
            }
            continue;
        }
        last_byte = esp_timer_get_time();
        if (byte == '\n') {
            if (!overflow) serial_line(line, length);
            else diagnostic("event=serial_line_overflow");
            length = 0;
            overflow = false;
        } else if (byte != '\r') {
            if (length < sizeof(line)) line[length++] = byte;
            else overflow = true;
        }
    }
}

esp_err_t node_ota_start_serial(void)
{
    if (!ring || application_ready || serial_started) return ESP_ERR_INVALID_STATE;
    if (role != NODE_OTA_CENTER) {
        application_ready = true;
        return ESP_OK;
    }
    /* USB-UART on DevKitC connects to UART0. Leave console TX configured by IDF. */
    esp_err_t err = uart_driver_install(UART_NUM_0, 4096, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    application_ready = true;
    if (xTaskCreate(serial_task, "ota_serial", 8192, NULL, 2, NULL) != pdPASS) {
        application_ready = false;
        uart_driver_delete(UART_NUM_0);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(boot_coordinator, "ota_boot_ring", 8192, NULL, 2, NULL) != pdPASS) {
        application_ready = false;
        diagnostic("event=boot_coordinator_create_failed");
        rm_restart_local();
        return ESP_ERR_NO_MEM;
    }
    serial_started = true;
    ESP_LOGI(TAG, "USB firmware update ready on UART0");
    return ESP_OK;
}

typedef struct { unsigned ready_mask; } boot_status_t;

static void collect_boot_status(const node_ota_packet_t *reply, void *context) {
    boot_status_t *status = context;
    if (reply->sender <= NODE_OTA_CENTER && reply->length >= 53 &&
        node_ota_get_u32(reply->data) == ESP_OK && reply->data[4] == NODE_OTA_IDLE &&
        !reply->data[49] && !reply->data[50] && reply->data[51] &&
        !memcmp(reply->data + 13, esp_app_get_description()->app_elf_sha256, 32))
        status->ready_mask |= 1U << reply->sender;
}

static void ignore_boot_reply(const node_ota_packet_t *reply, void *context) {
    (void)reply; (void)context;
}

/* No host connection is required. Fail closed if a board is absent, pending
 * verification or recovering: startup/reset/provisioning and OTA remain alive. */
static bool boot_probe(void) {
    if (boot_recovery_pending || !rs_boot_quiet(ring)) return false;
    boot_status_t status = {0};
    node_ota_packet_t request = { .op = NODE_OTA_INFO, .sender = NODE_OTA_HOST,
        .targets = NODE_OTA_ROLES, .session = 0, .sequence = ++boot_sequence };
    esp_err_t err = node_ota_exchange(&request, collect_boot_status, &status);
    diagnostic("event=boot_ring_probe ready_mask=0x%x error=0x%lx", status.ready_mask, (unsigned long)err);
    if (err != ESP_OK || status.ready_mask != NODE_OTA_ROLES) return false;
    request.op = BOOT_RESUME_OP;
    request.sender = NODE_OTA_CENTER;
    request.sequence = ++boot_sequence;
    return node_ota_exchange(&request, ignore_boot_reply, NULL) == ESP_OK;
}

static void boot_coordinator(void *arg) {
    (void)arg;
    while (rs_boot_quiet(ring)) {
        if (boot_probe()) break;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    vTaskDelete(NULL);
}

static bool boot_pending(void)
{
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

void node_ota_boot_trace(const char *stage)
{
    /* Share stdout's lock with serial_reply, so diagnostics cannot split an ACK.
     * Before node_ota_init the GPIO role is not known yet (host/255 sentinel). */
    flockfile(stdout);
    printf("\nI4ABOOT t_ms=%lld role=%u stage=%s\n",
           (long long)(esp_timer_get_time() / 1000),
           (unsigned)(ring ? role : NODE_OTA_HOST), stage);
    fflush(stdout);
    funlockfile(stdout);
}

static void boot_watchdog(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(BOOT_CONFIRM_TIMEOUT_MS));
    if (boot_pending()) {
        node_ota_boot_trace("watchdog.pending_restart");
        rm_restart_local(); /* bootloader rolls back unconfirmed image */
        return;
    }
    /* Confirmation writes otadata while SPI is active. A stalled ring cannot
     * deliver RM reset broadcasts. Each newly updated board therefore restarts
     * locally once, after the normal confirmation/rollback window. On that
     * clean boot the center's existing RM startup synchronizes the node again.
     * Do not restart immediately on confirmation: other boards may still be
     * pending verification and a center reset would roll them back. */
    vTaskDelay(pdMS_TO_TICKS(BOOT_RECOVERY_RESTART_MS - BOOT_CONFIRM_TIMEOUT_MS));
    node_ota_boot_trace("recovery.local_restart");
    rm_restart_local();
    vTaskDelete(NULL);
}

void node_ota_watch_boot(void)
{
    diagnostic("event=boot reset_reason=%u", (unsigned)esp_reset_reason());
    bool pending = boot_pending();
    boot_recovery_pending = pending;
    node_ota_boot_trace(pending ? "boot.pending_verify" : "boot.not_pending");
    if (pending &&
        xTaskCreate(boot_watchdog, "ota_boot_check", 2048, NULL, 2, NULL) != pdPASS) {
        node_ota_boot_trace("watchdog.create_failed_restart");
        rm_restart_local();
    }
}

esp_err_t node_ota_confirm_boot(void)
{
    node_ota_boot_trace("confirm.check");
    if (!boot_pending()) {
        node_ota_boot_trace("confirm.not_pending");
        return ESP_OK;
    }
    node_ota_boot_trace("confirm.write_begin");
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    node_ota_boot_trace(err == ESP_OK ? "confirm.write_ok" : "confirm.write_failed");
    return err;
}
