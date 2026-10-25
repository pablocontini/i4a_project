#include "neighbor_manager/neighbor_manager.h"

#include <string.h>
#include <stdio.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "task_config.h"
#include "os/os.h"

#define NM_CENTER NM_RADIOS
#define NM_RPC_MS 3000
#define NM_QUEUE_LENGTH 16

typedef enum { NM_RESERVE = 1, NM_COMMIT, NM_ACTIVATE, NM_RENEW, NM_RELEASE, NM_SNAPSHOT, NM_EXPIRE,
               NM_PROVIDE_BLOCK } nm_command_t;

typedef struct {
    uint32_t request;
    uint32_t revision;
    uint8_t command;
    uint8_t owner;
    uint8_t accepted;
    char peer_uuid[NM_UUID_LEN + 1];
    nm_link_id_t link;
    nm_registry_t registry;
    nm_network_t parent, peer_network, offer;
} nm_message_t;

_Static_assert(sizeof(nm_message_t) <= RS_MAX_BROADCAST_LEN, "Neighbor snapshot exceeds SPI MTU");

static struct {
    ring_share_t *rs;
    mutex_t lock;
    mutex_t rpc_lock;
    nm_registry_t registry;
    char uuid[NM_UUID_LEN + 1];
    uint8_t orientation;
    uint32_t next_request;
    uint32_t revision;
    uint32_t waiting_request;
    bool answered;
    bool accepted;
    nm_network_t offer;
    nm_blocks_t blocks;
    bool blocks_ready;
    QueueHandle_t inbox;
    volatile unsigned dropped_requests;
    volatile unsigned duplicate_requests;
    nm_message_t last_request[NM_RADIOS];
    uint64_t last_request_ms[NM_RADIOS];
} manager;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static uint64_t lease_ms(void) { return rs_lease_time_ms(manager.rs); }

/* Worker-only diagnostics share stdout's lock with OTA replies. */
static void diagnostic(const char *event, const nm_message_t *msg, unsigned value)
{
    flockfile(stdout);
    printf("\nI4ANM t_ms=%llu role=%u event=%s owner=%u request=%lu value=%u\n",
           (unsigned long long)now_ms(), manager.orientation, event,
           msg ? msg->owner : NM_CENTER, msg ? (unsigned long)msg->request : 0UL, value);
    fflush(stdout);
    funlockfile(stdout);
}

/* Caller holds manager.lock. Deadlines are sent as durations, not center uptime. */
static void snapshot(nm_message_t *msg, uint64_t now) {
    msg->revision = ++manager.revision;
    msg->command = NM_SNAPSHOT;
    msg->registry = manager.registry;
    for (unsigned i = 0; i < NM_RADIOS; ++i) {
        nm_claim_t *claim = &msg->registry.claims[i];
        claim->deadline_ms = claim->deadline_ms > now ? claim->deadline_ms - now : 0;
    }
}

static bool request_valid(const nm_message_t *msg) {
    if (msg->owner >= NM_RADIOS || !nm_uuid_valid(msg->peer_uuid) ||
        !memcmp(msg->peer_uuid, manager.uuid, NM_UUID_LEN + 1))
        return false;
    if (!memcmp(msg->link.initiator_uuid, manager.uuid, NM_UUID_LEN + 1))
        return msg->owner == msg->link.station_orientation;
    return !memcmp(msg->link.initiator_uuid, msg->peer_uuid, NM_UUID_LEN + 1) &&
           msg->owner == msg->link.ap_orientation;
}

static void on_message(void *ctx, const uint8_t *data, uint16_t len) {
    (void)ctx;
    if (len != sizeof(nm_message_t)) return;
    nm_message_t msg;
    memcpy(&msg, data, sizeof(msg));
    uint64_t now = lease_ms();
    if (msg.command == NM_SNAPSHOT) {
        if (manager.orientation == NM_CENTER) return;
        WITH_LOCK(&manager.lock, {
            bool fresh = (int32_t)(msg.revision - manager.revision) > 0;
            if (fresh) {
                manager.revision = msg.revision;
                manager.registry = msg.registry;
                for (unsigned i = 0; i < NM_RADIOS; ++i)
                    manager.registry.claims[i].deadline_ms += now;
            }
            if (fresh && msg.owner == manager.orientation && msg.request == manager.waiting_request) {
                manager.accepted = msg.accepted;
                manager.offer = msg.offer;
                manager.answered = true;
            }
        });
        return;
    }
    if (manager.orientation != NM_CENTER || msg.owner >= NM_RADIOS ||
        !((msg.command >= NM_RESERVE && msg.command <= NM_RELEASE) ||
          msg.command == NM_PROVIDE_BLOCK)) return;
    /* Never wait for a broadcast from the shared sibling callback. If that
     * callback stalls, its queue can fill and block ring forwarding itself.
     * An unanswered request fails the peripheral's bounded RPC safely. */
    WITH_LOCK(&manager.lock, {
        nm_message_t *previous = &manager.last_request[msg.owner];
        bool same = previous->request == msg.request && previous->command == msg.command &&
                    !memcmp(&previous->link, &msg.link, sizeof(msg.link)) &&
                    !memcmp(previous->peer_uuid, msg.peer_uuid, sizeof(msg.peer_uuid)) &&
                    !memcmp(&previous->parent, &msg.parent, sizeof(msg.parent)) &&
                    !memcmp(&previous->peer_network, &msg.peer_network, sizeof(msg.peer_network));
        if (same && now_ms() - manager.last_request_ms[msg.owner] < NM_ACTIVE_MS) {
            ++manager.duplicate_requests;
        } else if (xQueueSend(manager.inbox, &msg, 0)) {
            *previous = msg;
            manager.last_request_ms[msg.owner] = now_ms();
        } else ++manager.dropped_requests;
    });
}

static void process_pending(TickType_t wait)
{
    nm_message_t msg = { .command = NM_EXPIRE, .owner = NM_CENTER };
    xQueueReceive(manager.inbox, &msg, wait);
    bool block_request = msg.command == NM_PROVIDE_BLOCK;
    uint64_t now = lease_ms();
    bool send = true;

    WITH_LOCK(&manager.lock, {
        bool expired = nm_registry_expire(&manager.registry, now);
        switch (msg.command) {
            case NM_RESERVE:
                msg.accepted = request_valid(&msg) &&
                    nm_registry_reserve(&manager.registry, msg.owner, msg.peer_uuid, &msg.link, now);
                break;
            case NM_COMMIT:
                msg.accepted = nm_registry_advance(&manager.registry, msg.owner, &msg.link, NM_COMMITTED, now);
                break;
            case NM_ACTIVATE:
            case NM_RENEW:
                msg.accepted = nm_registry_advance(&manager.registry, msg.owner, &msg.link, NM_ACTIVE, now);
                break;
            case NM_RELEASE:
                msg.accepted = nm_registry_release(&manager.registry, msg.owner, &msg.link);
                break;
            case NM_PROVIDE_BLOCK: {
                /* Link leases may expire; address ownership must not. Serialize
                 * all radios here, and commit before returning any offer. */
                nm_claim_t *claim = msg.owner < NM_RADIOS ? &manager.registry.claims[msg.owner] : NULL;
                if (claim) memcpy(msg.peer_uuid, claim->peer_uuid, NM_UUID_LEN + 1);
                nm_blocks_t planned = manager.blocks;
                msg.accepted = manager.blocks_ready && claim && claim->phase >= NM_COMMITTED &&
                    nm_registry_owns(&manager.registry, msg.owner, &msg.link, now) &&
                    !memcmp(msg.link.initiator_uuid, claim->peer_uuid, NM_UUID_LEN + 1) &&
                    msg.owner == msg.link.ap_orientation &&
                    nm_blocks_offer(&planned, claim->peer_uuid, msg.parent, msg.peer_network,
                                    msg.owner + 1U, &msg.offer);
                if (msg.accepted && memcmp(&planned, &manager.blocks, sizeof(planned))) {
                    if (nm_block_store_save(&planned)) manager.blocks = planned;
                    else {
                        /* A failed commit may be ambiguous: never reuse its
                         * candidate in this boot. Existing links keep running. */
                        manager.blocks_ready = false;
                        msg.accepted = false;
                    }
                }
                if (!msg.accepted) msg.offer = (nm_network_t){0};
                break;
            }
            case NM_EXPIRE:
                send = expired;
                break;
            default:
                msg.accepted = false;
                break;
        }
        if (send) snapshot(&msg, now);
    });
    if (block_request) {
        flockfile(stdout);
        printf("\nI4ABLOCK t_ms=%llu role=%u event=offer accepted=%u durable_ready=%u uuid=%.12s parent=%08lx/%08lx network=%08lx/%08lx\n",
               (unsigned long long)now_ms(), manager.orientation, msg.accepted, manager.blocks_ready,
               msg.peer_uuid, (unsigned long)msg.parent.addr, (unsigned long)msg.parent.mask,
               (unsigned long)msg.offer.addr, (unsigned long)msg.offer.mask);
        fflush(stdout);
        funlockfile(stdout);
    }
    if (send) {
        uint64_t started = now_ms();
        bool sent = rs_broadcast(manager.rs, RS_NEIGHBOR_MANAGER, &msg, sizeof(msg));
        unsigned elapsed_ms = (unsigned)(now_ms() - started);
        if (!sent || elapsed_ms >= 250)
            diagnostic(sent ? "snapshot_slow" : "snapshot_failed", &msg, elapsed_ms);
    }
}

static void manager_task(void *arg)
{
    (void)arg;
    unsigned reported_drops = 0, reported_duplicates = 0;
    for (;;) {
        process_pending(pdMS_TO_TICKS(1000));
        if (manager.duplicate_requests != reported_duplicates) {
            reported_duplicates = manager.duplicate_requests;
            diagnostic("duplicates_suppressed", NULL, reported_duplicates);
        }
        unsigned drops = manager.dropped_requests;
        if (drops != reported_drops) {
            diagnostic("queue_full", NULL, drops);
            reported_drops = drops;
        }
    }
}

bool nm_init(ring_share_t *rs, uint8_t orientation, const char *node_uuid) {
    if (!rs || orientation > NM_CENTER || !nm_uuid_valid(node_uuid)) return false;
    memset(&manager, 0, sizeof(manager));
    if (!mutex_create(&manager.lock)) return false;
    if (!mutex_create(&manager.rpc_lock)) {
        mutex_destroy(&manager.lock);
        return false;
    }
    manager.rs = rs;
    manager.orientation = orientation;
    memcpy(manager.uuid, node_uuid, NM_UUID_LEN + 1);
    if (orientation == NM_CENTER) {
        manager.blocks_ready = nm_block_store_load(node_uuid, &manager.blocks);
        if (!manager.blocks_ready) diagnostic("block_store_unavailable", NULL, 0);
        manager.inbox = xQueueCreate(NM_QUEUE_LENGTH, sizeof(nm_message_t));
        if (!manager.inbox || xTaskCreatePinnedToCore(manager_task, "neighbor_manager",
                TASK_NEIGHBOR_STACK, NULL, TASK_NEIGHBOR_PRIORITY, NULL,
                TASK_NEIGHBOR_CORE) != pdPASS) {
            if (manager.inbox) vQueueDelete(manager.inbox);
            manager.rs = NULL;
            mutex_destroy(&manager.rpc_lock);
            mutex_destroy(&manager.lock);
            return false;
        }
    }
    rs_register_component(rs, RS_NEIGHBOR_MANAGER,
                          (ring_callback_t){ .callback = on_message, .context = NULL });
    return true;
}

/* Do not call from a sibling callback: that task delivers the RPC response. */
static bool rpc(nm_command_t command, const char *peer_uuid, const nm_link_id_t *link,
                const nm_network_t *parent, const nm_network_t *peer_network, nm_network_t *offer) {
    if (!manager.rs || manager.orientation >= NM_RADIOS) return false;
    if (!link || (command == NM_RESERVE && !nm_uuid_valid(peer_uuid))) return false;
    /* Existing leases are frozen; renewal is local while SPI is reserved.
     * New admissions are refused and retried by the connection lifecycle. */
    if (rs_maintenance(manager.rs)) return command == NM_RENEW && nm_owns(link);
    bool accepted = false;
    mutex_lock(&manager.rpc_lock);
    nm_message_t msg = { .command = command, .owner = manager.orientation, .link = *link };
    if (parent) msg.parent = *parent;
    if (peer_network) msg.peer_network = *peer_network;
    if (peer_uuid) memcpy(msg.peer_uuid, peer_uuid, NM_UUID_LEN + 1);
    WITH_LOCK(&manager.lock, {
        if (++manager.next_request == 0) ++manager.next_request;
        msg.request = manager.next_request;
        manager.waiting_request = msg.request;
        manager.answered = false;
        manager.offer = (nm_network_t){0};
    });
    if (rs_broadcast(manager.rs, RS_NEIGHBOR_MANAGER, &msg, sizeof(msg))) {
        uint64_t deadline = now_ms() + NM_RPC_MS;
        while (now_ms() < deadline) {
            bool answered;
            WITH_LOCK(&manager.lock, {
                answered = manager.answered;
                accepted = answered && manager.accepted;
                if (accepted && offer) *offer = manager.offer;
            });
            if (answered) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    WITH_LOCK(&manager.lock, { manager.waiting_request = 0; });
    mutex_unlock(&manager.rpc_lock);
    return accepted || (command == NM_RENEW && rs_maintenance(manager.rs) && nm_owns(link));
}

bool nm_reserve(const char *peer_uuid, const nm_link_id_t *link) { return rpc(NM_RESERVE, peer_uuid, link, NULL, NULL, NULL); }
bool nm_commit(const nm_link_id_t *link) { return rpc(NM_COMMIT, NULL, link, NULL, NULL, NULL); }
bool nm_activate(const nm_link_id_t *link) { return rpc(NM_ACTIVATE, NULL, link, NULL, NULL, NULL); }
bool nm_renew(const nm_link_id_t *link) { return rpc(NM_RENEW, NULL, link, NULL, NULL, NULL); }
void nm_release(const nm_link_id_t *link) { (void)rpc(NM_RELEASE, NULL, link, NULL, NULL, NULL); }

bool nm_provide_block(const nm_link_id_t *link, nm_network_t parent,
                      nm_network_t peer_network, nm_network_t *offer) {
    if (!offer) return false;
    *offer = (nm_network_t){0};
    return rpc(NM_PROVIDE_BLOCK, NULL, link, &parent, &peer_network, offer);
}

bool nm_owns(const nm_link_id_t *link) {
    bool owned;
    if (!manager.rs) return false;
    WITH_LOCK(&manager.lock, {
        owned = nm_registry_owns(&manager.registry, manager.orientation, link, lease_ms());
    });
    return owned;
}

bool nm_is_blocked_uuid(const char *uuid) {
    bool blocked;
    if (!manager.rs) return true;
    WITH_LOCK(&manager.lock, { blocked = nm_registry_blocks(&manager.registry, uuid, lease_ms()); });
    return blocked;
}

bool nm_copy_snapshot(nm_registry_t *registry) {
    if (!manager.rs || !registry) return false;
    WITH_LOCK(&manager.lock, {
        *registry = manager.registry;
        nm_registry_expire(registry, lease_ms());
    });
    return true;
}

void nm_tick(void) {
    if (!manager.rs || manager.orientation != NM_CENTER) return;
    if (rs_maintenance(manager.rs)) return;
    nm_message_t msg = { .command = NM_EXPIRE, .owner = NM_CENTER };
    /* The worker also expires leases on idle receive timeouts, so a full
     * queue can safely coalesce this tick without delaying the routing task. */
    xQueueSend(manager.inbox, &msg, 0);
}
