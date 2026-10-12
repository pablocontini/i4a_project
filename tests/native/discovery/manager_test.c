/* Exercise the real discovery arbiter and bounded RPC in five ESP contexts. */
#ifdef __TINYC__
#define _Static_assert(condition, message) typedef char native_assert[(condition) ? 1 : -1]
#endif
#include "../../../components/integration/discovery_manager/src/discovery_manager.c"

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
static uint64_t pause_started, paused_total;
static bool paused, drop_requests, drop_replies;
static bool radio_busy[4];
static bool in_callback;
static unsigned broadcasts, callback_broadcasts, nonce;
static unsigned send_delay_ms;
static int task_result;
static dm_message_t last_request, last_reply;
static struct { dm_message_t items[DM_QUEUE_LENGTH]; unsigned count; } pending;

QueueHandle_t xQueueCreate(unsigned length, size_t size) {
    if (length != DM_QUEUE_LENGTH || size != sizeof(dm_message_t)) return NULL;
    pending.count = 0;
    return &pending;
}
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    if (queue != &pending || wait != 0 || pending.count == DM_QUEUE_LENGTH) return 0;
    pending.items[pending.count++] = *(const dm_message_t *)item;
    return 1;
}
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    (void)wait;
    if (queue != &pending || !pending.count) return 0;
    *(dm_message_t *)item = pending.items[0];
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
uint32_t esp_random(void) { return ++nonce + current * 100; }
bool nm_is_radio_busy(uint8_t owner) { return owner >= 4 || radio_busy[owner]; }
void vTaskDelay(TickType_t ticks) {
    clock_ms += ticks;
    int caller = current;
    switch_to(DM_CENTER);
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
    (void)rs;
    if (component != RS_DISCOVERY_MANAGER || len != sizeof(dm_message_t)) return false;
    ++broadcasts;
    if (in_callback) { ++callback_broadcasts; return false; }
    int caller = current;
    if (caller != DM_CENTER) {
        if (drop_requests) return false;
        memcpy(&last_request, data, len);
        clock_ms += send_delay_ms;
        switch_to(DM_CENTER);
        in_callback = true;
        on_message(NULL, data, len);
        in_callback = false;
        if (send_delay_ms) process_pending(0);
    } else {
        memcpy(&last_reply, data, len);
        if (!drop_replies) {
            for (int radio = 0; radio < NM_RADIOS; ++radio) {
                switch_to(radio);
                on_message(NULL, data, len);
                switch_to(DM_CENTER);
            }
        }
    }
    switch_to(caller);
    return true;
}
static void reset(void) {
    memset(states, 0, sizeof(states));
    memset(&manager, 0, sizeof(manager));
    memset(radio_busy, 0, sizeof(radio_busy));
    memset(&last_request, 0, sizeof(last_request));
    memset(&last_reply, 0, sizeof(last_reply));
    current = 0;
    clock_ms = pause_started = paused_total = 0;
    paused = drop_requests = drop_replies = in_callback = false;
    broadcasts = callback_broadcasts = send_delay_ms = nonce = 0;
    task_result = pdPASS;
    for (int radio = 0; radio <= DM_CENTER; ++radio) {
        dm_init(&rings[radio], radio);
        states[radio] = manager;
    }
    current = DM_CENTER;
    clock_ms = DM_CENTER_MS; /* Ordinary tests start after central quarantine. */
}

EXPORT int test_discovery_exclusion_and_busy_radio(void) {
    reset();
    dm_token_t first = {0}, second = {0};
    switch_to(0);
    CHECK(dm_acquire(&first, true) && dm_owns(&first));
    CHECK(!dm_acquire(&second, true)); /* No renewal by reacquiring. */
    switch_to(1);
    CHECK(!dm_acquire(&second, true));
    switch_to(0);
    dm_release(&first);
    CHECK(!dm_owns(&first));
    switch_to(1);
    radio_busy[1] = true;
    CHECK(!dm_acquire(&second, true));
    radio_busy[1] = false;
    CHECK(dm_acquire(&second, true) && dm_owns(&second));
    switch_to(0);
    CHECK(!dm_owns(&second));
    switch_to(DM_CENTER);
    CHECK(!dm_acquire(&first, true));
    return 0;
}

EXPORT int test_discovery_finite_deadline_and_teardown_grace(void) {
    reset();
    dm_token_t first = {0}, second = {0};
    switch_to(0);
    uint64_t started = clock_ms;
    CHECK(dm_acquire(&first, true));
    CHECK(first.deadline_ms == started + DM_SEARCH_MS);
    clock_ms = started + DM_SEARCH_MS;
    CHECK(!dm_owns(&first));
    switch_to(1);
    CHECK(!dm_acquire(&second, true)); /* The center still reserves teardown grace. */
    clock_ms = started + DM_CENTER_MS + 100;
    CHECK(dm_acquire(&second, true) && dm_owns(&second));
    switch_to(0);
    dm_release(&first);
    switch_to(1);
    CHECK(dm_owns(&second));
    switch_to(DM_CENTER);
    CHECK(manager.center_active && manager.center.owner == 1);
    return 0;
}

EXPORT int test_discovery_spi_failure_and_late_reply(void) {
    reset();
    dm_token_t first = {0}, second = {0};
    switch_to(0);
    drop_requests = true;
    CHECK(!dm_acquire(&first, true));
    CHECK(!dm_owns(&first));
    drop_requests = false;
    drop_replies = true;
    uint64_t started = clock_ms;
    CHECK(!dm_acquire(&first, true) && clock_ms - started == DM_RPC_MS);
    dm_message_t late = last_reply;
    CHECK(late.accepted);
    on_message(NULL, (const uint8_t *)&late, sizeof(late));
    CHECK(!manager.local_active && !dm_owns(&first));
    drop_replies = false;
    switch_to(1);
    CHECK(!dm_acquire(&second, true));
    clock_ms += DM_CENTER_MS;
    CHECK(dm_acquire(&second, true));
    switch_to(0);
    on_message(NULL, (const uint8_t *)&late, sizeof(late));
    CHECK(!manager.local_active);
    return 0;
}

EXPORT int test_discovery_stale_sequence_and_old_release(void) {
    reset();
    dm_token_t old = {0}, fresh = {0};
    switch_to(0);
    CHECK(dm_acquire(&old, true));
    dm_message_t old_acquire = last_request;
    dm_release(&old);
    CHECK(dm_acquire(&fresh, true) && fresh.request != old.request);
    dm_release(&old);
    CHECK(dm_owns(&fresh));
    switch_to(DM_CENTER);
    uint64_t deadline = manager.center.deadline_ms;
    on_message(NULL, (const uint8_t *)&old_acquire, sizeof(old_acquire));
    process_pending(0);
    CHECK(manager.center_active && manager.center.request == fresh.request);
    CHECK(manager.center.deadline_ms == deadline);
    clock_ms += DM_CENTER_MS;
    process_pending(0);
    CHECK(!manager.center_active);
    on_message(NULL, (const uint8_t *)&old_acquire, sizeof(old_acquire));
    process_pending(0);
    CHECK(!manager.center_active); /* Replay never creates another turn. */
    return 0;
}

EXPORT int test_discovery_callback_queue_and_worker_failure(void) {
    reset();
    dm_message_t msg = { .command = DM_ACQUIRE, .owner = 0, .session = 1 };
    uint64_t started = clock_ms;
    in_callback = true;
    for (unsigned i = 0; i <= DM_QUEUE_LENGTH; ++i) {
        msg.request = msg.lease_request = i + 1;
        on_message(NULL, (const uint8_t *)&msg, sizeof(msg));
    }
    in_callback = false;
    CHECK(clock_ms == started && broadcasts == 0 && callback_broadcasts == 0);
    CHECK(!manager.center_active && pending.count == DM_QUEUE_LENGTH);
    CHECK(manager.dropped_requests == 1);
    process_pending(0);
    CHECK(manager.center_active && broadcasts == 1 && callback_broadcasts == 0);
    while (pending.count) process_pending(0);
    clock_ms += DM_CENTER_MS;
    process_pending(0);
    CHECK(!manager.center_active); /* Idle worker expires without routing tick. */
    task_result = 0;
    CHECK(!dm_init(&rings[DM_CENTER], DM_CENTER));
    CHECK(!manager.rs && !manager.lock && !manager.rpc_lock && pending.count == 0);
    return 0;
}

EXPORT int test_discovery_ota_expiry_and_radio_reboot(void) {
    reset();
    dm_token_t old = {0}, rebooted = {0};
    switch_to(0);
    CHECK(dm_acquire(&old, true));
    pause_started = clock_ms;
    paused = true;
    clock_ms += DM_CENTER_MS * 10;
    unsigned before = broadcasts;
    CHECK(!dm_owns(&old) && !dm_acquire(&rebooted, true));
    CHECK(broadcasts == before);
    switch_to(DM_CENTER);
    process_pending(0);
    CHECK(!manager.center_active); /* Real deadlines expire during OTA. */
    paused_total += clock_ms - pause_started;
    paused = false;
    switch_to(0);
    CHECK(!dm_owns(&old)); /* An old turn cannot come back after maintenance. */
    CHECK(dm_acquire(&rebooted, true));
    CHECK(dm_init(&rings[0], 0)); /* Radio reboot, center remains alive. */
    CHECK(manager.session != old.session && !dm_acquire(&rebooted, true));
    clock_ms += DM_CENTER_MS;
    CHECK(dm_acquire(&rebooted, true) && dm_owns(&rebooted));
    dm_release(&old);
    CHECK(dm_owns(&rebooted));
    return 0;
}

EXPORT int test_discovery_send_delay_cannot_extend_client_budget(void) {
    reset();
    dm_token_t token = {0};
    switch_to(0);
    send_delay_ms = DM_SEARCH_MS + 1000;
    CHECK(!dm_acquire(&token, true));
    CHECK(!manager.local_active && !dm_owns(&token));
    switch_to(DM_CENTER);
    CHECK(manager.center_active); /* Unanswered grant safely remains finite. */
    CHECK(manager.center.deadline_ms == now_ms() + DM_CENTER_MS);
    return 0;
}

EXPORT int test_discovery_center_reboot_quarantine(void) {
    reset();
    dm_token_t old = {0}, next = {0};
    switch_to(0);
    CHECK(dm_acquire(&old, true));
    switch_to(DM_CENTER);
    CHECK(dm_init(&rings[DM_CENTER], DM_CENTER));
    uint64_t booted = clock_ms;
    switch_to(1);
    CHECK(!dm_acquire(&next, true));
    clock_ms = booted + DM_SEARCH_MS;
    switch_to(0);
    CHECK(!dm_owns(&old));
    switch_to(1);
    CHECK(!dm_acquire(&next, true)); /* Includes the radio teardown margin. */
    clock_ms = booted + DM_CENTER_MS;
    CHECK(dm_acquire(&next, true) && dm_owns(&next));
    switch_to(0);
    dm_release(&old);
    switch_to(1);
    CHECK(dm_owns(&next));
    return 0;
}

EXPORT int test_discovery_reply_matches_owner_session_and_sequence(void) {
    reset();
    switch_to(0);
    manager.waiting_request = 42;
    dm_message_t reply = { .command = DM_REPLY, .accepted = true,
        .owner = 1, .session = manager.session, .request = 42 };
    on_message(NULL, (const uint8_t *)&reply, sizeof(reply));
    CHECK(!manager.answered);
    reply.owner = 0;
    ++reply.session;
    on_message(NULL, (const uint8_t *)&reply, sizeof(reply));
    CHECK(!manager.answered);
    reply.session = manager.session;
    --reply.request;
    on_message(NULL, (const uint8_t *)&reply, sizeof(reply));
    CHECK(!manager.answered);
    ++reply.request;
    on_message(NULL, (const uint8_t *)&reply, sizeof(reply));
    CHECK(manager.answered && manager.accepted);
    reply.accepted = false;
    on_message(NULL, (const uint8_t *)&reply, sizeof(reply));
    CHECK(manager.accepted); /* Replayed denial cannot overwrite the answer. */
    manager.waiting_request = 0;
    manager.answered = false;
    on_message(NULL, (const uint8_t *)&reply, sizeof(reply));
    CHECK(!manager.answered);
    return 0;
}

EXPORT int test_discovery_initial_admission_waits_for_other_radios(void) {
    reset();
    dm_token_t first = {0}, other = {0};
    switch_to(0);
    CHECK(dm_acquire(&first, false)); /* A first provider is allowed. */
    radio_busy[0] = true; /* Its NM reservation precedes routing provision. */
    dm_release(&first);
    switch_to(1);
    CHECK(!dm_acquire(&other, false)); /* Pending neighbor blocks initial admission. */
    clock_ms += 1000; /* Still busy after becoming an active neighbor. */
    CHECK(!dm_acquire(&other, false));
    switch_to(2);
    CHECK(dm_acquire(&other, true)); /* Provisioned discovery can use a third free radio. */
    dm_release(&other);
    switch_to(1);
    radio_busy[0] = false; /* Failed provisioning session has actually closed. */
    CHECK(dm_acquire(&other, false));
    return 0;
}

#ifdef DISCOVERY_NATIVE_MAIN
#include <stdio.h>
int main(void) {
    const struct { const char *name; int (*run)(void); } tests[] = {
        {"exclusion_and_busy_radio", test_discovery_exclusion_and_busy_radio},
        {"finite_deadline_and_teardown_grace", test_discovery_finite_deadline_and_teardown_grace},
        {"spi_failure_and_late_reply", test_discovery_spi_failure_and_late_reply},
        {"stale_sequence_and_old_release", test_discovery_stale_sequence_and_old_release},
        {"callback_queue_and_worker_failure", test_discovery_callback_queue_and_worker_failure},
        {"ota_expiry_and_radio_reboot", test_discovery_ota_expiry_and_radio_reboot},
        {"send_delay_cannot_extend_client_budget", test_discovery_send_delay_cannot_extend_client_budget},
        {"center_reboot_quarantine", test_discovery_center_reboot_quarantine},
        {"reply_matches_owner_session_and_sequence", test_discovery_reply_matches_owner_session_and_sequence},
        {"initial_admission_waits_for_other_radios", test_discovery_initial_admission_waits_for_other_radios},
    };
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        int line = tests[i].run();
        if (line) {
            printf("%s: FAIL at manager_test.c:%d\n", tests[i].name, line);
            return 1;
        }
        printf("%s: OK\n", tests[i].name);
    }
    return 0;
}
#endif
