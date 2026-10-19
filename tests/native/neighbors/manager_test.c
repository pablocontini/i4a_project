/* Exercise the real coordinator/RPC code with five independent ESP contexts. */
#ifdef __TINYC__
#define _Static_assert(condition, message) typedef char native_assert[(condition) ? 1 : -1]
#endif
#include "../../../components/integration/neighbor_manager/src/neighbor_manager.c"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

typedef __typeof__(manager) manager_state_t;
static manager_state_t states[5];
static ring_share_t rings[5];
static int current;
static uint64_t clock_ms;
static bool paused;
static uint64_t pause_started, paused_total;
static bool drop_requests, drop_snapshots;
static nm_message_t last_snapshot;
static struct { nm_message_t items[NM_QUEUE_LENGTH]; unsigned count; } pending;
static bool in_callback;
static unsigned callback_broadcasts, broadcasts;
static int task_result = pdPASS;

QueueHandle_t xQueueCreate(unsigned length, size_t size) {
    if (length != NM_QUEUE_LENGTH || size != sizeof(nm_message_t)) return NULL;
    pending.count = 0;
    return &pending;
}
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    if (queue != &pending || wait != 0 || pending.count == NM_QUEUE_LENGTH) return 0;
    pending.items[pending.count++] = *(const nm_message_t *)item;
    return 1;
}
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    (void)wait;
    if (queue != &pending || !pending.count) return 0;
    *(nm_message_t *)item = pending.items[0];
    --pending.count;
    for (unsigned i = 0; i < pending.count; ++i) pending.items[i] = pending.items[i + 1];
    return 1;
}
void vQueueDelete(QueueHandle_t queue) { (void)queue; pending.count = 0; }
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
                           void *arg, unsigned priority, void *handle, unsigned core) {
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle; (void)core;
    return fn == manager_task ? task_result : 0;
}

static void switch_to(int next) {
    states[current] = manager;
    manager = states[next];
    current = next;
}

bool mutex_create(mutex_t *m) { *m = (void *)1; return true; }
bool mutex_lock(mutex_t *m) { return m && *m == (void *)1; }
bool mutex_unlock(mutex_t *m) { return m && *m == (void *)1; }
void mutex_destroy(mutex_t *m) { *m = NULL; }
int64_t esp_timer_get_time(void) { return (clock_ms + current * 100000) * 1000; }
void vTaskDelay(TickType_t ticks) {
    clock_ms += ticks;
    int caller = current;
    switch_to(NM_CENTER);
    process_pending(0);
    switch_to(caller);
}
bool rs_maintenance(ring_share_t *rs) { (void)rs; return paused; }
uint64_t rs_lease_time_ms(ring_share_t *rs) {
    (void)rs;
    return (paused ? pause_started : clock_ms) - paused_total + current * 100000;
}
void rs_register_component(ring_share_t *rs, int component, ring_callback_t callback) {
    (void)rs; (void)component; (void)callback;
}

bool rs_broadcast(ring_share_t *rs, int component, const void *data, uint16_t len) {
    (void)rs; (void)component;
    ++broadcasts;
    if (in_callback) { ++callback_broadcasts; return false; }
    int caller = current;
    if (caller != NM_CENTER) {
        if (drop_requests) return false;
        switch_to(NM_CENTER);
        in_callback = true;
        on_message(NULL, data, len);
        in_callback = false;
    } else {
        memcpy(&last_snapshot, data, len);
        if (!drop_snapshots) {
            for (int radio = 0; radio < NM_RADIOS; ++radio) {
                switch_to(radio);
                on_message(NULL, data, len);
                switch_to(NM_CENTER);
            }
        }
    }
    switch_to(caller);
    return true;
}

static void reset(void) {
    memset(states, 0, sizeof(states));
    memset(&manager, 0, sizeof(manager));
    current = 0;
    clock_ms = 0;
    paused = false;
    paused_total = 0;
    drop_requests = drop_snapshots = false;
    in_callback = false;
    broadcasts = callback_broadcasts = 0;
    task_result = pdPASS;
    for (int radio = 0; radio <= NM_CENTER; ++radio) {
        nm_init(&rings[radio], radio, "A00000000000");
        states[radio] = manager;
    }
    current = NM_CENTER;
}

static nm_link_id_t new_link(uint8_t sta, uint8_t ap, uint32_t token) {
    nm_link_id_t link = { .initiator_uuid = "A00000000000", .station_orientation = sta,
                         .ap_orientation = ap, .session = token };
    return link;
}

EXPORT int test_spi_reservations_snapshots_and_release(void) {
    reset();
    nm_link_id_t first = new_link(0, 1, 1), other = new_link(2, 3, 2);
    switch_to(0);
    CHECK(nm_reserve("B00000000000", &first));
    CHECK(nm_commit(&first) && nm_activate(&first));
    nm_message_t stale = last_snapshot;
    for (int radio = 0; radio < NM_RADIOS; ++radio) {
        switch_to(radio);
        CHECK(nm_is_blocked_uuid("B00000000000"));
        CHECK(!nm_is_blocked_uuid("C00000000000"));
    }
    switch_to(2);
    CHECK(!nm_reserve("B00000000000", &other));
    CHECK(nm_reserve("C00000000000", &other));
    CHECK(nm_commit(&other) && nm_activate(&other));
    switch_to(0);
    nm_release(&first);
    CHECK(!nm_is_blocked_uuid("B00000000000"));
    on_message(NULL, (const uint8_t *)&stale, sizeof(stale));
    CHECK(!nm_is_blocked_uuid("B00000000000"));
    CHECK(nm_is_blocked_uuid("C00000000000"));
    CHECK(nm_reserve("B00000000000", &first));
    CHECK(nm_commit(&first) && nm_activate(&first));
    clock_ms += 5000;
    CHECK(nm_renew(&first));
    CHECK(nm_owns(&first));
    return 0;
}

EXPORT int test_spi_loss_expiry_and_stale_cleanup(void) {
    reset();
    nm_link_id_t old = new_link(0, 1, 1), next = new_link(0, 1, 2);
    switch_to(0);
    drop_snapshots = true;
    CHECK(!nm_reserve("B00000000000", &old));
    CHECK(clock_ms >= NM_RPC_MS);
    CHECK(!nm_owns(&old));
    drop_snapshots = false;
    clock_ms += NM_PENDING_MS;
    switch_to(NM_CENTER);
    nm_tick();
    process_pending(0);
    switch_to(0);
    CHECK(!nm_is_blocked_uuid("B00000000000"));
    CHECK(nm_reserve("B00000000000", &next));
    CHECK(nm_commit(&next) && nm_activate(&next));
    nm_release(&old);
    CHECK(nm_owns(&next));
    drop_requests = true;
    CHECK(!nm_renew(&next));
    drop_requests = false;
    clock_ms += NM_ACTIVE_MS;
    switch_to(NM_CENTER);
    nm_tick();
    process_pending(0);
    switch_to(0);
    CHECK(!nm_owns(&next) && !nm_is_blocked_uuid("B00000000000"));
    return 0;
}

EXPORT int test_spi_incoming_identity_and_invalid_owner(void) {
    reset();
    nm_link_id_t incoming = { .initiator_uuid = "B00000000000", .station_orientation = 0,
                             .ap_orientation = 1, .session = 1 };
    switch_to(1);
    CHECK(nm_reserve("B00000000000", &incoming));
    CHECK(nm_commit(&incoming) && nm_activate(&incoming));
    switch_to(0);
    nm_link_id_t wrong = new_link(3, 1, 2);
    CHECK(!nm_reserve("C00000000000", &wrong));
    wrong = new_link(0, 1, 3);
    CHECK(!nm_reserve("A00000000000", &wrong));
    CHECK(!nm_reserve("bad", &wrong));
    CHECK(nm_is_blocked_uuid("B00000000000"));
    return 0;
}

EXPORT int test_neighbor_callback_defers_broadcast_and_handles_full_queue(void) {
    reset();
    nm_message_t request = { .command = NM_RESERVE, .owner = 0, .request = 1,
                            .peer_uuid = "B00000000000", .link = new_link(0, 1, 1) };
    uint64_t started = clock_ms;
    in_callback = true;
    for (unsigned i = 0; i < NM_QUEUE_LENGTH + 1; ++i) {
        request.request = i + 1;
        on_message(NULL, (const uint8_t *)&request, sizeof(request));
    }
    in_callback = false;
    CHECK(clock_ms == started && broadcasts == 0 && callback_broadcasts == 0);
    CHECK(pending.count == NM_QUEUE_LENGTH && manager.dropped_requests == 1);
    CHECK(!nm_registry_blocks(&manager.registry, "B00000000000", now_ms()));
    process_pending(0);
    CHECK(pending.count == NM_QUEUE_LENGTH - 1 && broadcasts == 1);
    CHECK(last_snapshot.accepted && last_snapshot.request == 1);
    CHECK(nm_registry_blocks(&manager.registry, "B00000000000", now_ms()));
    /* Only the worker expires leases and emits the resulting snapshot. */
    while (pending.count) process_pending(0);
    clock_ms += NM_PENDING_MS;
    unsigned before = broadcasts;
    nm_tick();
    CHECK(broadcasts == before);
    process_pending(0);
    CHECK(broadcasts == before + 1 && callback_broadcasts == 0);
    CHECK(!nm_registry_blocks(&manager.registry, "B00000000000", now_ms()));
    return 0;
}

EXPORT int test_neighbor_worker_failure_and_idle_expiry(void) {
    reset();
    task_result = 0;
    CHECK(!nm_init(&rings[NM_CENTER], NM_CENTER, "A00000000000"));
    CHECK(!manager.rs && !manager.lock && !manager.rpc_lock && pending.count == 0);
    task_result = pdPASS;
    CHECK(nm_init(&rings[NM_CENTER], NM_CENTER, "A00000000000"));
    nm_link_id_t link = new_link(0, 1, 1);
    CHECK(nm_registry_reserve(&manager.registry, 0, "B00000000000", &link, now_ms()));
    clock_ms += NM_PENDING_MS;
    process_pending(0); /* No routing tick or incoming message required. */
    CHECK(!nm_registry_blocks(&manager.registry, "B00000000000", now_ms()));
    CHECK(broadcasts == 1 && callback_broadcasts == 0);
    return 0;
}

EXPORT int test_neighbor_duplicate_requests_and_frozen_leases(void) {
    reset();
    nm_message_t request = { .command = NM_RESERVE, .owner = 0, .request = 1,
                            .peer_uuid = "B00000000000", .link = new_link(0, 1, 1) };
    for (unsigned i = 0; i < 5; ++i)
        on_message(NULL, (const uint8_t *)&request, sizeof(request));
    CHECK(pending.count == 1 && manager.duplicate_requests == 4);
    process_pending(0);
    CHECK(broadcasts == 1);
    on_message(NULL, (const uint8_t *)&request, sizeof(request));
    CHECK(pending.count == 0 && manager.duplicate_requests == 5);
    switch_to(0);
    CHECK(nm_commit(&request.link) && nm_activate(&request.link));
    pause_started = clock_ms;
    paused = true;
    unsigned before = broadcasts;
    clock_ms += NM_ACTIVE_MS * 10;
    CHECK(nm_owns(&request.link) && nm_renew(&request.link));
    CHECK(!nm_reserve("C00000000000", &request.link));
    CHECK(broadcasts == before);
    switch_to(NM_CENTER);
    process_pending(0);
    CHECK(nm_registry_blocks(&manager.registry, "B00000000000", lease_ms()));
    paused_total += clock_ms - pause_started;
    paused = false;
    switch_to(0);
    CHECK(nm_owns(&request.link) && nm_renew(&request.link));
    clock_ms += NM_ACTIVE_MS + 1;
    CHECK(!nm_owns(&request.link));
    return 0;
}
