/* Run the real coordinator with a simulated ring and real peripheral receivers.
 * Include the implementation to reset its singleton between independent tests. */
#ifdef __TINYC__
/* TinyCC 0.9.27 predates C11 assertions. Keep the compile-time check. */
#define _Static_assert(condition, message) typedef char test_static_assert[(condition) ? 1 : -1]
#endif
#include "node_ota.c"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

void mock_advance_time(int64_t delta);
void mock_boot(esp_ota_img_states_t state, esp_err_t error);
unsigned mock_confirmations(void);
static ring_share_t test_ring;
static bool test_maintenance, test_boot_quiet;
static unsigned peer_ready_mask = NODE_OTA_ROLES;
static int bad_hash_peer;
static ring_callback_t callback;
static ota_receiver_t peers[NODE_OTA_CENTER];
static node_ota_packet_t queued[16], emitted[5];
static unsigned queued_count, emitted_count, broadcasts;
static int pending_peer, dropped_peer, reject_peer, ordering_error;
static unsigned unreachable_peers;
static int64_t broadcast_delay_us, queue_wait_us;
static bool locked, inject_stale;
static void (*boot_task)(void *);
static unsigned local_restarts;
static int create_result = pdPASS;

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}
QueueHandle_t xQueueCreate(unsigned n, size_t size) { return queued; }
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    if (queued_count == 16) return 0;
    queued[queued_count++] = *(const node_ota_packet_t *)item;
    return 1;
}
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    if (!queued_count) {
        queue_wait_us += (int64_t)wait * 1000;
        mock_advance_time((int64_t)wait * 1000);
        return 0;
    }
    *(node_ota_packet_t *)item = queued[0];
    for (unsigned i = 1; i < queued_count; ++i) queued[i - 1] = queued[i];
    --queued_count;
    if (!queued_count) pending_peer = -1;
    return 1;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &locked; }
int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
    if (locked) return 0;
    locked = true;
    return 1;
}
int xSemaphoreGive(SemaphoreHandle_t mutex) { locked = false; return 1; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle) {
    if (fn == boot_watchdog && create_result == pdPASS) boot_task = fn;
    return create_result;
}
void vTaskDelay(TickType_t ticks) { mock_advance_time((int64_t)ticks * 1000); }
void vTaskDelete(void *task) { }
void esp_restart(void) { }
int esp_reset_reason(void) { return 1; }
void rm_restart_local(void) { ++local_restarts; }
esp_err_t uart_wait_tx_done(int uart, TickType_t ticks) { return ESP_OK; }
int uart_read_bytes(int uart, void *buffer, unsigned size, TickType_t ticks) { return 0; }
esp_err_t uart_driver_install(int a, int b, int c, int d, void *e, int f) { return ESP_OK; }
esp_err_t uart_driver_delete(int uart) { return ESP_OK; }
void rs_set_boot_quiet(ring_share_t *rs, bool active) { test_boot_quiet = active; }
bool rs_boot_quiet(ring_share_t *rs) { return test_boot_quiet; }
void rs_set_maintenance(ring_share_t *rs, bool active) { test_maintenance = active; }
bool rs_maintenance(ring_share_t *rs) { return test_maintenance; }
uint64_t rs_lease_time_ms(ring_share_t *rs) { return esp_timer_get_time() / 1000; }
void rs_register_component(ring_share_t *rs, int component, ring_callback_t cb) { callback = cb; }

static void deliver(const node_ota_packet_t *packet) {
    uint8_t wire[NODE_OTA_MAX_PACKET];
    size_t length = node_ota_encode(packet, wire);
    callback.callback(callback.context, wire, length);
}

bool rs_broadcast(ring_share_t *rs, int component, const void *wire, uint16_t length) {
    node_ota_packet_t request, reply;
    ++broadcasts;
    mock_advance_time(broadcast_delay_us);
    if (!node_ota_decode(wire, length, &request)) return false;
    if (request.op == NODE_OTA_BEGIN && !test_maintenance) ordering_error = 1;
    uint8_t targets = request.targets & ~(1U << NODE_OTA_CENTER);
    /* Model the original failure: multiple flash writers lose the ring ACKs. */
    if (!targets || (targets & (targets - 1)) || pending_peer >= 0) {
        ordering_error = 1;
        return false;
    }
    int peer = 0;
    while (!(targets & (1U << peer))) ++peer;
    /* The physical send may succeed while the destination never receives it. */
    if (unreachable_peers & (1U << peer)) return true;
    if (request.op == NODE_OTA_BEGIN && receiver.phase != NODE_OTA_IDLE)
        ordering_error = 1;
    ota_receiver_handle(&peers[peer], &request, &reply);
    if (request.op == NODE_OTA_INFO) {
        reply.length = 53;
        reply.data[51] = (peer_ready_mask & (1U << peer)) != 0;
        reply.data[52] = test_boot_quiet;
        if (peer == bad_hash_peer) reply.data[13] ^= 1;
    }
    if (request.op == BOOT_RESUME_OP) {
        node_ota_put_u32(reply.data, ESP_OK);
        reply.data[4] = NODE_OTA_IDLE;
    }
    if (peer == reject_peer) node_ota_put_u32(reply.data, ESP_FAIL);
    if (peer == dropped_peer) return true;
    pending_peer = peer;
    if (inject_stale) {
        node_ota_packet_t stale = reply;
        --stale.sequence;
        deliver(&stale);
        stale = reply;
        stale.targets = NODE_OTA_ROLES;
        deliver(&stale);
    }
    deliver(&reply);
    return true;
}

static void capture(const node_ota_packet_t *reply, void *context) {
    if (emitted_count >= 5 || reply->targets != NODE_OTA_ROLES || pending_peer >= 0) {
        ordering_error = 1;
        return;
    }
    emitted[emitted_count++] = *reply;
}

static int setup(node_ota_packet_t *request) {
    ring = NULL;
    serial_started = locked = inject_stale = false;
    boot_recovery_pending = false;
    test_boot_quiet = false;
    test_maintenance = false;
    peer_ready_mask = NODE_OTA_ROLES;
    bad_hash_peer = -1;
    queued_count = emitted_count = broadcasts = ordering_error = 0;
    pending_peer = dropped_peer = reject_peer = -1;
    unreachable_peers = 0;
    broadcast_delay_us = queue_wait_us = 0;
    for (uint8_t peer = 0; peer < NODE_OTA_CENTER; ++peer) ota_receiver_init(&peers[peer], peer);
    memset(request, 0, sizeof(*request));
    request->op = NODE_OTA_BEGIN;
    request->sender = NODE_OTA_HOST;
    request->targets = NODE_OTA_ROLES;
    request->session = 123;
    request->sequence = 1;
    request->length = 36;
    node_ota_put_u32(request->data, 4);
    memset(request->data + 4, 0x42, 32);
    esp_err_t err = node_ota_init(&test_ring, NODE_OTA_CENTER);
    if (err != ESP_OK) return err;
    return node_ota_start_serial();
}

EXPORT int test_begin_waits_for_application_ready(void) {
    node_ota_packet_t request, reply;
    CHECK(setup(&request) == ESP_OK);
    /* Peripheral OTA is registered early to receive INFO during setup. */
    ring = NULL;
    serial_started = false;
    CHECK(node_ota_init(&test_ring, 2) == ESP_OK);
    request.targets = 0x14;
    handle_request(&request, &reply);
    CHECK(node_ota_get_u32(reply.data) == ESP_ERR_INVALID_STATE);
    CHECK(receiver.phase == NODE_OTA_IDLE && !receiver.handle_open);
    request.op = NODE_OTA_INFO;
    request.length = 0;
    handle_request(&request, &reply);
    CHECK(node_ota_get_u32(reply.data) == ESP_OK && reply.length == 53 && !reply.data[51]);
    CHECK(node_ota_start_serial() == ESP_OK && !serial_started);
    request.op = NODE_OTA_BEGIN;
    request.length = 36;
    handle_request(&request, &reply);
    CHECK(node_ota_get_u32(reply.data) == ESP_OK && receiver.handle_open);
    CHECK(receiver.phase == NODE_OTA_RECEIVING);
    /* Future transports cannot bypass the center's startup guard either. */
    CHECK(setup(&request) == ESP_OK);
    application_ready = false;
    emitted_count = 0;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_ERR_INVALID_STATE);
    CHECK(broadcasts == 0 && emitted_count == 1 && receiver.phase == NODE_OTA_IDLE);
    CHECK(node_ota_get_u32(emitted[0].data) == ESP_ERR_INVALID_STATE);
    /* A failed UART task creation must not accidentally enable BEGIN. */
    serial_started = false;
    create_result = 0;
    esp_err_t start_error = node_ota_start_serial();
    create_result = pdPASS;
    CHECK(start_error == ESP_ERR_NO_MEM && !application_ready && !serial_started);
    CHECK(node_ota_start_serial() == ESP_OK && application_ready && serial_started);
    return 0;
}

EXPORT int test_begin_special_mode_is_read_only_and_mode_switch_waits_for_ota(void) {
    node_ota_packet_t request, reply;
    CHECK(setup(&request) == ESP_OK);
    CHECK(node_ota_can_switch_mode());
    test_boot_quiet = true;
    CHECK(!node_ota_can_switch_mode());
    test_boot_quiet = false;
    set_maintenance(true, request.session);
    CHECK(!node_ota_can_switch_mode());
    set_maintenance(false, 0);
    node_ota_set_updates_allowed(false);
    CHECK(node_ota_can_switch_mode());
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_ERR_INVALID_STATE);
    CHECK(broadcasts == 0 && emitted_count == 1 && receiver.phase == NODE_OTA_IDLE);
    CHECK(node_ota_get_u32(emitted[0].data) == ESP_ERR_INVALID_STATE);
    request.op = NODE_OTA_INFO;
    request.length = 0;
    handle_request(&request, &reply);
    CHECK(node_ota_get_u32(reply.data) == ESP_OK && reply.length == 53 && reply.data[51]);
    node_ota_set_updates_allowed(true);
    return 0;
}

EXPORT int test_exchange_serializes_flash_and_maps_acks(void) {
    node_ota_packet_t request;
    CHECK(setup(&request) == ESP_OK);
    inject_stale = true;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
    CHECK(!ordering_error && emitted_count == 5 && broadcasts == 4);
    for (unsigned i = 0; i < 5; ++i) CHECK(emitted[i].sender == i);
    CHECK(receiver.phase == NODE_OTA_RECEIVING && receiver.targets == NODE_OTA_ROLES);
    for (unsigned i = 0; i < 4; ++i)
        CHECK(peers[i].phase == NODE_OTA_RECEIVING && peers[i].targets == (0x10 | (1U << i)));

    request.op = NODE_OTA_DATA;
    request.length = 4;
    ++request.sequence;
    emitted_count = 0;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
    CHECK(!ordering_error && emitted_count == 5 && receiver.received == 4);
    /* Exact retransmission must not write twice on any board. */
    emitted_count = 0;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
    for (unsigned i = 0; i < 4; ++i) CHECK(peers[i].received == 4);

    request.op = NODE_OTA_ABORT;
    request.length = 0;
    ++request.sequence;
    emitted_count = 0;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
    CHECK(receiver.phase == NODE_OTA_IDLE && !test_maintenance);
    for (unsigned i = 0; i < 4; ++i) CHECK(peers[i].phase == NODE_OTA_IDLE);
    return 0;
}

EXPORT int test_exchange_timeout_then_retry(void) {
    node_ota_packet_t request;
    CHECK(setup(&request) == ESP_OK);
    dropped_peer = 0;
    int64_t start = esp_timer_get_time();
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_ERR_TIMEOUT);
    CHECK(esp_timer_get_time() - start == ACK_TIMEOUT_MS * 1000LL);
    CHECK(!locked && receiver.phase == NODE_OTA_IDLE && broadcasts == 1);
    CHECK(peers[0].phase == NODE_OTA_RECEIVING && peers[1].phase == NODE_OTA_IDLE);
    dropped_peer = -1;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
    CHECK(!ordering_error && emitted_count == 5 && !locked);
    return 0;
}

EXPORT int test_exchange_info_reaches_healthy_roles_after_missing_south(void) {
    node_ota_packet_t request;
    CHECK(setup(&request) == ESP_OK);
    request.op = NODE_OTA_INFO;
    request.length = 0;
    unreachable_peers = 1U << 1;
    int64_t start = esp_timer_get_time();
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_ERR_TIMEOUT);
    CHECK(broadcasts == 4 && emitted_count == 4 && !locked && !ordering_error);
    const unsigned senders[] = {0, 2, 3, NODE_OTA_CENTER};
    for (unsigned i = 0; i < 4; ++i) CHECK(emitted[i].sender == senders[i]);
    CHECK(esp_timer_get_time() - start == ACK_TIMEOUT_MS * 1000LL / 4);
    return 0;
}

EXPORT int test_exchange_abort_cleans_healthy_roles_after_missing_south(void) {
    node_ota_packet_t request;
    CHECK(setup(&request) == ESP_OK);
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
    CHECK(receiver.phase == NODE_OTA_RECEIVING && test_maintenance);
    request.op = NODE_OTA_ABORT;
    request.length = 0;
    ++request.sequence;
    emitted_count = broadcasts = 0;
    unreachable_peers = 1U << 1;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_ERR_TIMEOUT);
    CHECK(broadcasts == 4 && emitted_count == 4 && !locked && !ordering_error);
    CHECK(receiver.phase == NODE_OTA_IDLE && !test_maintenance && !maintenance);
    for (unsigned peer = 0; peer < NODE_OTA_CENTER; ++peer)
        CHECK(peers[peer].phase == (peer == 1 ? NODE_OTA_RECEIVING : NODE_OTA_IDLE));
    CHECK(emitted[3].sender == NODE_OTA_CENTER && node_ota_get_u32(emitted[3].data) == ESP_OK);
    return 0;
}

EXPORT int test_exchange_info_bounds_ack_wait_with_immediate_sends(void) {
    node_ota_packet_t request;
    CHECK(setup(&request) == ESP_OK);
    request.op = NODE_OTA_INFO;
    request.length = 0;
    unreachable_peers = 0x0f;
    int64_t start = esp_timer_get_time();
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_ERR_TIMEOUT);
    /* Mock sends return immediately. This bounds ACK waiting, not transport. */
    CHECK(esp_timer_get_time() - start == ACK_TIMEOUT_MS * 1000LL);
    CHECK(broadcasts == 4 && emitted_count == 1 && emitted[0].sender == NODE_OTA_CENTER);
    CHECK(!locked && !ordering_error);
    return 0;
}

EXPORT int test_exchange_status_and_abort_skip_wait_after_blocking_broadcasts(void) {
    const uint8_t operations[] = {NODE_OTA_INFO, NODE_OTA_ABORT};
    for (unsigned i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i) {
        node_ota_packet_t request;
        CHECK(setup(&request) == ESP_OK);
        CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
        CHECK(receiver.phase == NODE_OTA_RECEIVING && test_maintenance);
        request.op = operations[i];
        request.length = 0;
        ++request.sequence;
        emitted_count = broadcasts = 0;
        unreachable_peers = 0x0f;
        broadcast_delay_us = 5600000LL;
        queue_wait_us = 0;
        int64_t start = esp_timer_get_time();
        CHECK(node_ota_exchange(&request, capture, NULL) == ESP_ERR_TIMEOUT);
        CHECK(broadcasts == 4 && emitted_count == 1 && emitted[0].sender == NODE_OTA_CENTER);
        CHECK(queue_wait_us == 0);
        /* Four blocking sends exceed 15 s, while expired ACK deadlines add no
         * queue wait and still cannot bypass the local cancellation. */
        CHECK(esp_timer_get_time() - start == 4 * broadcast_delay_us);
        CHECK(!locked && !ordering_error);
        if (request.op == NODE_OTA_ABORT) {
            CHECK(receiver.phase == NODE_OTA_IDLE && !maintenance && !test_maintenance);
        } else {
            CHECK(receiver.phase == NODE_OTA_RECEIVING && maintenance && test_maintenance);
        }
    }
    return 0;
}

EXPORT int test_exchange_slow_broadcasts_accept_queued_acks_and_cancel_locally(void) {
    /* Both a complete slow exchange and a missing south preserve queued ACKs. */
    for (unsigned missing = 0; missing < 2; ++missing) {
        node_ota_packet_t request;
        CHECK(setup(&request) == ESP_OK);
        CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
        request.op = NODE_OTA_ABORT;
        request.length = 0;
        ++request.sequence;
        emitted_count = broadcasts = 0;
        unreachable_peers = missing ? 1U << 1 : 0;
        broadcast_delay_us = 6000000LL;
        queue_wait_us = 0;
        inject_stale = true;
        int64_t start = esp_timer_get_time();
        CHECK(node_ota_exchange(&request, capture, NULL) == (missing ? ESP_ERR_TIMEOUT : ESP_OK));
        CHECK(broadcasts == 4 && emitted_count == (missing ? 4 : 5));
        CHECK(emitted[0].sender == 0 && node_ota_get_u32(emitted[0].data) == ESP_OK);
        CHECK(emitted[emitted_count - 1].sender == NODE_OTA_CENTER);
        CHECK(queue_wait_us == 0 && esp_timer_get_time() - start == 4 * broadcast_delay_us);
        CHECK(receiver.phase == NODE_OTA_IDLE && !maintenance && !test_maintenance);
        for (unsigned peer = 0; peer < NODE_OTA_CENTER; ++peer)
            CHECK(peers[peer].phase == (missing && peer == 1 ? NODE_OTA_RECEIVING : NODE_OTA_IDLE));
        CHECK(!locked && !ordering_error);
    }
    return 0;
}

EXPORT int test_exchange_data_remains_fail_fast_after_missing_south(void) {
    node_ota_packet_t request;
    CHECK(setup(&request) == ESP_OK);
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
    request.op = NODE_OTA_DATA;
    request.length = 4;
    ++request.sequence;
    emitted_count = broadcasts = 0;
    unreachable_peers = 1U << 1;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_ERR_TIMEOUT);
    CHECK(broadcasts == 2 && emitted_count == 1 && emitted[0].sender == 0);
    CHECK(peers[0].received == 4 && receiver.received == 0);
    for (unsigned peer = 1; peer < NODE_OTA_CENTER; ++peer) CHECK(peers[peer].received == 0);
    CHECK(!locked && !ordering_error);
    return 0;
}

EXPORT int test_exchange_reboot_requires_every_successful_ack(void) {
    node_ota_packet_t request;
    CHECK(setup(&request) == ESP_OK);
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK);
    receiver.phase = NODE_OTA_SELECTED;
    for (unsigned i = 0; i < 4; ++i) peers[i].phase = NODE_OTA_SELECTED;
    request.op = NODE_OTA_REBOOT;
    request.length = 0;
    request.offset = 4;
    ++request.sequence;
    reject_peer = 2;
    emitted_count = 0;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_FAIL);
    CHECK(emitted_count == 5 && node_ota_get_u32(emitted[2].data) == (uint32_t)ESP_FAIL);
    CHECK(!ordering_error && !locked);
    return 0;
}

EXPORT int test_boot_recovery_without_ring_and_no_restart_loop(void) {
    node_ota_packet_t request, reply;
    CHECK(setup(&request) == ESP_OK);
    mock_boot(ESP_OTA_IMG_PENDING_VERIFY, ESP_OK);
    boot_task = NULL;
    local_restarts = 0;
    node_ota_watch_boot();
    CHECK(boot_task != NULL && boot_recovery_pending);
    /* In production watch_boot precedes node_setup/node_ota_init. */
    ring = NULL;
    CHECK(node_ota_init(&test_ring, NODE_OTA_CENTER) == ESP_OK);
    CHECK(receiver.boot_recovery_pending);
    CHECK(node_ota_confirm_boot() == ESP_OK && mock_confirmations() == 1);
    CHECK(local_restarts == 0); /* Never reset another board before its deadline. */
    request.op = NODE_OTA_INFO;
    request.length = 0;
    ota_receiver_handle(&receiver, &request, &reply);
    CHECK(reply.length == 51 && reply.data[49] == 0 && reply.data[50] == 1);
    request.op = NODE_OTA_BEGIN;
    request.length = 36;
    ota_receiver_handle(&receiver, &request, &reply);
    CHECK(node_ota_get_u32(reply.data) == ESP_ERR_INVALID_STATE);
    CHECK(receiver.phase == NODE_OTA_IDLE);
    /* No ring response is delivered during the recovery. */
    boot_task(NULL);
    CHECK(local_restarts == 1 && broadcasts == 0);
    CHECK(esp_timer_get_time() == 120000000LL);
    /* A clean boot of the confirmed image must not schedule another reset. */
    boot_task = NULL;
    node_ota_watch_boot();
    CHECK(!boot_task && !boot_recovery_pending);
    ring = NULL;
    CHECK(node_ota_init(&test_ring, NODE_OTA_CENTER) == ESP_OK);
    CHECK(!receiver.boot_recovery_pending);
    CHECK(node_ota_confirm_boot() == ESP_OK && mock_confirmations() == 1);
    return 0;
}

EXPORT int test_boot_failure_keeps_rollback_deadline(void) {
    mock_boot(ESP_OTA_IMG_PENDING_VERIFY, ESP_FAIL);
    local_restarts = 0;
    boot_task = NULL;
    node_ota_watch_boot();
    CHECK(boot_task != NULL);
    CHECK(node_ota_confirm_boot() == ESP_FAIL);
    CHECK(boot_pending());
    boot_task(NULL);
    CHECK(local_restarts == 1 && esp_timer_get_time() == 90000000LL);
    /* Failure to start the watchdog must also restart, never mark valid. */
    mock_boot(ESP_OTA_IMG_PENDING_VERIFY, ESP_OK);
    create_result = 0;
    node_ota_watch_boot();
    create_result = pdPASS;
    CHECK(local_restarts == 2 && mock_confirmations() == 0);
    mock_boot(ESP_OTA_IMG_VALID, ESP_OK);
    return 0;
}

EXPORT int test_exchange_maintenance_timeout_and_failed_begin(void) {
    node_ota_packet_t request, reply;
    CHECK(setup(&request) == ESP_OK);
    dropped_peer = 0;
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_ERR_TIMEOUT);
    CHECK(maintenance && test_maintenance && receiver.phase == NODE_OTA_IDLE);
    mock_advance_time(NODE_OTA_IDLE_TIMEOUT_US + 1);
    expire_receiver();
    CHECK(!maintenance && !test_maintenance);
    CHECK(setup(&request) == ESP_OK);
    request.length = 0;
    handle_request(&request, &reply);
    CHECK(node_ota_get_u32(reply.data) != ESP_OK && !test_maintenance);
    CHECK(setup(&request) == ESP_OK);
    CHECK(node_ota_exchange(&request, capture, NULL) == ESP_OK && test_maintenance);
    mock_advance_time(NODE_OTA_IDLE_TIMEOUT_US + 1);
    expire_receiver();
    CHECK(receiver.phase == NODE_OTA_IDLE && !test_maintenance);
    return 0;
}

EXPORT int test_boot_barrier_waits_for_every_confirmed_board(void) {
    node_ota_packet_t request, reply;
    CHECK(setup(&request) == ESP_OK);
    mock_boot(ESP_OTA_IMG_VALID, ESP_OK);
    test_boot_quiet = true;
    peer_ready_mask &= ~(1U << 3);
    CHECK(!boot_probe() && !boot_release_at && test_boot_quiet);
    peer_ready_mask = NODE_OTA_ROLES;
    bad_hash_peer = 2;
    CHECK(!boot_probe() && !boot_release_at);
    bad_hash_peer = -1;
    peers[2].boot_recovery_pending = true;
    CHECK(!boot_probe() && !boot_release_at);
    peers[2].boot_recovery_pending = false;
    boot_recovery_pending = true;
    unsigned before = broadcasts;
    CHECK(!boot_probe() && broadcasts == before);
    boot_recovery_pending = false;
    dropped_peer = 2;
    CHECK(!boot_probe() && !boot_release_at);
    dropped_peer = -1;
    CHECK(boot_probe() && boot_release_at && test_boot_quiet);
    mock_advance_time(BOOT_RESUME_DELAY_US + 1);
    expire_receiver();
    CHECK(!test_boot_quiet && !boot_release_at);
    /* UART cannot forge the internal release opcode, and pending images
     * cannot release the boot gate even if a release packet arrives. */
    request.op = BOOT_RESUME_OP;
    request.length = 0;
    CHECK(!valid_request(&request));
    request.sender = NODE_OTA_CENTER;
    CHECK(valid_request(&request));
    test_boot_quiet = true;
    application_ready = false;
    handle_request(&request, &reply);
    CHECK(node_ota_get_u32(reply.data) == ESP_ERR_INVALID_STATE && !boot_release_at);
    return 0;
}

EXPORT int test_boot_resume_cannot_be_injected_from_uart(void) {
    node_ota_packet_t request;
    CHECK(setup(&request) == ESP_OK);
    mock_boot(ESP_OTA_IMG_VALID, ESP_OK);
    request.op = BOOT_RESUME_OP;
    request.sender = NODE_OTA_CENTER;
    request.length = 0;
    test_boot_quiet = true;
    uint8_t wire[NODE_OTA_MAX_PACKET];
    char line[SERIAL_LINE_SIZE];
    size_t len = node_ota_encode(&request, wire);
    size_t pos = strlen(NODE_OTA_PREFIX);
    memcpy(line, NODE_OTA_PREFIX, pos);
    const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        line[pos++] = hex[wire[i] >> 4];
        line[pos++] = hex[wire[i] & 15];
    }
    serial_line(line, pos);
    CHECK(!boot_release_at && broadcasts == 0 && test_boot_quiet);
    return 0;
}
