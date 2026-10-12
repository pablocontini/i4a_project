#include "discovery_manager/discovery_manager.h"

#include <string.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "neighbor_manager/neighbor_manager.h"
#include "task_config.h"
#include "os/os.h"

#define DM_CENTER NM_RADIOS
#define DM_RPC_MS 3000
#define DM_QUEUE_LENGTH 16

typedef enum { DM_ACQUIRE = 1, DM_RELEASE, DM_REPLY } dm_command_t;

typedef struct {
    uint32_t session;
    uint32_t request;
    uint32_t lease_request;
    uint8_t command;
    uint8_t owner;
    uint8_t accepted;
    uint8_t provisioned;
} dm_message_t;

_Static_assert(sizeof(dm_message_t) <= RS_MAX_BROADCAST_LEN, "Discovery exceeds SPI MTU");
_Static_assert(DM_CENTER_MS > DM_SEARCH_MS, "Discovery teardown needs central grace");

static struct {
    ring_share_t *rs;
    mutex_t lock;
    mutex_t rpc_lock;
    QueueHandle_t inbox;
    uint8_t orientation;
    uint32_t session;
    uint32_t next_request;
    uint32_t waiting_request;
    bool answered;
    bool accepted;
    bool local_active;
    dm_token_t local;
    bool center_active;
    dm_token_t center;
    uint64_t center_ready_ms;
    struct { uint32_t session, request; } history[NM_RADIOS];
    volatile unsigned dropped_requests;
} manager;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static bool same_turn(const dm_token_t *a, const dm_token_t *b)
{
    return a && b && a->session == b->session && a->request == b->request &&
           a->owner == b->owner;
}

static bool radios_ready(uint8_t owner, bool provisioned)
{
    if (nm_is_radio_busy(owner)) return false;
    if (!provisioned) {
        /* A first admitted neighbor may still be waiting for routing/SPI to
         * distribute its allocation. Do not admit a second initial provider
         * in that interval, even if the original search lease was released. */
        for (uint8_t radio = 0; radio < NM_RADIOS; ++radio)
            if (radio != owner && nm_is_radio_busy(radio)) return false;
    }
    return true;
}

static void on_message(void *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    if (len != sizeof(dm_message_t)) return;
    dm_message_t msg;
    memcpy(&msg, data, sizeof(msg));
    if (msg.owner >= NM_RADIOS || !msg.session || !msg.request || msg.provisioned > 1) return;
    if (msg.command == DM_REPLY) {
        if (manager.orientation == DM_CENTER) return;
        WITH_LOCK(&manager.lock, {
            if (msg.owner == manager.orientation && msg.session == manager.session &&
                msg.request == manager.waiting_request && !manager.answered) {
                manager.accepted = msg.accepted != 0;
                manager.answered = true;
            }
        });
        return;
    }
    if (manager.orientation != DM_CENTER ||
        (msg.command != DM_ACQUIRE && msg.command != DM_RELEASE)) return;
    /* Never send or wait for an RPC from the sibling callback. The sole
     * central worker serializes grants; overload means a safe client timeout. */
    if (!xQueueSend(manager.inbox, &msg, 0)) ++manager.dropped_requests;
}

static void process_pending(TickType_t wait)
{
    dm_message_t msg;
    if (!xQueueReceive(manager.inbox, &msg, wait)) {
        WITH_LOCK(&manager.lock, {
            if (manager.center_active && now_ms() >= manager.center.deadline_ms)
                manager.center_active = false;
        });
        return;
    }
    uint64_t now = now_ms();
    bool reply = true;
    WITH_LOCK(&manager.lock, {
        if (manager.center_active && now >= manager.center.deadline_ms)
            manager.center_active = false;
        unsigned owner = msg.owner;
        bool same_session = manager.history[owner].session == msg.session;
        bool other_boot_active = manager.center_active && manager.center.owner == owner &&
                                 manager.center.session != msg.session;
        /* Duplicate/late requests must never restart a lease after expiry.
         * Sequence numbers are scoped to a random boot session, so a radio
         * reboot can acquire again once its previous central lease expires. */
        if (other_boot_active || (same_session &&
            (int32_t)(msg.request - manager.history[owner].request) <= 0)) {
            reply = false;
        } else {
            manager.history[owner].session = msg.session;
            manager.history[owner].request = msg.request;
            msg.accepted = false;
            if (msg.command == DM_ACQUIRE) {
                if (!rs_maintenance(manager.rs) && now >= manager.center_ready_ms &&
                    !manager.center_active &&
                    msg.lease_request == msg.request && radios_ready(msg.owner, msg.provisioned)) {
                    manager.center = ((dm_token_t){ .deadline_ms = now + DM_CENTER_MS,
                        .session = msg.session, .request = msg.request, .owner = msg.owner });
                    manager.center_active = true;
                    msg.accepted = true;
                }
            } else if (manager.center_active && manager.center.owner == msg.owner &&
                       manager.center.session == msg.session &&
                       manager.center.request == msg.lease_request) {
                manager.center_active = false;
                msg.accepted = true;
            }
        }
    });
    if (reply) {
        msg.command = DM_REPLY;
        /* A failed reply leaves the lease reserved until its finite deadline.
         * No peripheral can use an unanswered acquisition. */
        (void)rs_broadcast(manager.rs, RS_DISCOVERY_MANAGER, &msg, sizeof(msg));
    }
}

static void manager_task(void *arg)
{
    (void)arg;
    for (;;) process_pending(pdMS_TO_TICKS(1000));
}

bool dm_init(ring_share_t *rs, uint8_t orientation)
{
    if (!rs || orientation > DM_CENTER) return false;
    memset(&manager, 0, sizeof(manager));
    if (!mutex_create(&manager.lock)) return false;
    if (!mutex_create(&manager.rpc_lock)) {
        mutex_destroy(&manager.lock);
        return false;
    }
    manager.rs = rs;
    manager.orientation = orientation;
    /* A center can reboot while a peripheral misses the ring reset. Do not
     * issue a new turn until any grant from the previous center has expired.
     * Real time keeps OTA from resurrecting one of those old turns. */
    manager.center_ready_ms = now_ms() + DM_CENTER_MS;
    manager.session = esp_random();
    if (!manager.session) manager.session = 1;
    if (orientation == DM_CENTER) {
        manager.inbox = xQueueCreate(DM_QUEUE_LENGTH, sizeof(dm_message_t));
        if (!manager.inbox || xTaskCreatePinnedToCore(manager_task, "discovery_manager",
                TASK_NEIGHBOR_STACK, NULL, TASK_NEIGHBOR_PRIORITY, NULL,
                TASK_NEIGHBOR_CORE) != pdPASS) {
            if (manager.inbox) vQueueDelete(manager.inbox);
            manager.rs = NULL;
            mutex_destroy(&manager.rpc_lock);
            mutex_destroy(&manager.lock);
            return false;
        }
    }
    rs_register_component(rs, RS_DISCOVERY_MANAGER,
        (ring_callback_t){ .callback = on_message, .context = NULL });
    return true;
}

/* Caller holds rpc_lock. Every response must match both the boot session and
 * the RPC sequence. The timeout starts before the SPI send, not after it. */
static bool rpc(dm_command_t command, uint32_t lease_request, uint32_t *request,
                bool provisioned)
{
    dm_message_t msg = { .command = command, .owner = manager.orientation,
                        .session = manager.session, .provisioned = provisioned };
    WITH_LOCK(&manager.lock, {
        if (++manager.next_request == 0) ++manager.next_request;
        msg.request = manager.next_request;
        msg.lease_request = command == DM_ACQUIRE ? msg.request : lease_request;
        manager.waiting_request = msg.request;
        manager.answered = false;
        manager.accepted = false;
    });
    *request = msg.request;
    bool accepted = false;
    uint64_t deadline = now_ms() + DM_RPC_MS;
    bool sent = rs_broadcast(manager.rs, RS_DISCOVERY_MANAGER, &msg, sizeof(msg));
    while (sent && now_ms() < deadline) {
        bool answered;
        WITH_LOCK(&manager.lock, {
            answered = manager.answered;
            accepted = answered && manager.accepted;
        });
        if (answered) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    WITH_LOCK(&manager.lock, { manager.waiting_request = 0; });
    return accepted;
}

bool dm_acquire(dm_token_t *token, bool provisioned)
{
    if (!token || !manager.rs || manager.orientation >= NM_RADIOS ||
        rs_maintenance(manager.rs)) return false;
    bool accepted = false;
    mutex_lock(&manager.rpc_lock);
    uint64_t started = now_ms();
    bool already_active;
    WITH_LOCK(&manager.lock, {
        already_active = manager.local_active && started < manager.local.deadline_ms;
    });
    uint32_t request = 0;
    if (!already_active && rpc(DM_ACQUIRE, 0, &request, provisioned) &&
        !rs_maintenance(manager.rs) && now_ms() < started + DM_SEARCH_MS) {
        dm_token_t acquired = { .session = manager.session, .request = request,
            .owner = manager.orientation, .deadline_ms = started + DM_SEARCH_MS };
        WITH_LOCK(&manager.lock, {
            manager.local = acquired;
            manager.local_active = true;
        });
        *token = acquired;
        accepted = true;
    }
    mutex_unlock(&manager.rpc_lock);
    return accepted;
}

bool dm_owns(const dm_token_t *token)
{
    if (!token || !manager.rs || manager.orientation >= NM_RADIOS ||
        rs_maintenance(manager.rs)) return false;
    bool owned;
    WITH_LOCK(&manager.lock, {
        owned = manager.local_active && same_turn(token, &manager.local) &&
                now_ms() < manager.local.deadline_ms;
    });
    return owned;
}

void dm_release(const dm_token_t *token)
{
    if (!token || !manager.rs || manager.orientation >= NM_RADIOS ||
        token->owner != manager.orientation || token->session != manager.session ||
        !token->request) return;
    mutex_lock(&manager.rpc_lock);
    WITH_LOCK(&manager.lock, {
        if (same_turn(token, &manager.local)) manager.local_active = false;
    });
    uint32_t request;
    if (!rs_maintenance(manager.rs)) (void)rpc(DM_RELEASE, token->request, &request, false);
    mutex_unlock(&manager.rpc_lock);
}
