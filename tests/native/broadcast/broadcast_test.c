#include <stdio.h>
#include "../../../components/internal/ring_link_internal/broadcast.c"
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static unsigned forwarded, processed, notified;
static bool callback_blocked, serial_locked, state_locked, lock_error;
static unsigned creates, notifications;
static int serial_token, state_token;
static TickType_t ticks;
static esp_err_t forward_result, process_result, transmit_result;
static ring_link_payload_t last_frame, old_frame;
static enum { NO_ACK, ACK, DUPLICATE_ACK, OLD_ACK, OLD_AND_CURRENT_ACK, SPURIOUS_NOTIFICATION } delivery;
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return creates++ % 2 ? (void *)&state_token : (void *)&serial_token;
}
int xSemaphoreTake(SemaphoreHandle_t m, TickType_t w) {
    (void)w;
    bool *locked = m == &state_token ? &state_locked : &serial_locked;
    if ((m != &state_token && m != &serial_token) || *locked) lock_error = true;
    *locked = true;
    return 1;
}
int xSemaphoreGive(SemaphoreHandle_t m) {
    bool *locked = m == &state_token ? &state_locked : &serial_locked;
    if (!*locked) lock_error = true;
    *locked = false;
    return 1;
}
void vSemaphoreDelete(SemaphoreHandle_t m) { (void)m; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)2; }
TickType_t xTaskGetTickCount(void) { return ticks; }
unsigned ulTaskNotifyTake(int clear, TickType_t w) {
    unsigned result = notifications;
    if (clear) notifications = 0;
    if (!result) ticks += w;
    return result;
}
void xTaskNotifyGive(TaskHandle_t t) {
    if (!state_locked || t != (void *)2) lock_error = true;
    ++notified;
    ++notifications;
}
uint8_t config_get_id(void) { return 1; }
bool ring_link_payload_is_from_device(ring_link_payload_t *p) { return p->src_id == 1; }
esp_err_t ring_link_lowlevel_transmit_payload(ring_link_payload_t *p) {
    if (!serial_locked || state_locked) lock_error = true;
    last_frame = *p;
    if (delivery == OLD_ACK || delivery == OLD_AND_CURRENT_ACK) broadcast_handler(&old_frame);
    if (delivery == ACK || delivery == DUPLICATE_ACK || delivery == OLD_AND_CURRENT_ACK)
        broadcast_handler(p);
    if (delivery == DUPLICATE_ACK) broadcast_handler(p);
    if (delivery == SPURIOUS_NOTIFICATION) ++notifications;
    return transmit_result;
}
esp_err_t ring_link_lowlevel_forward_payload(ring_link_payload_t *p) {
    (void)p; ++forwarded; return forward_result;
}
esp_err_t ring_link_internal_process(ring_link_payload_t *p) {
    (void)p;
    ++processed;
    /* Simulate a callback needing a broadcast lock held by a task awaiting
     * this frame's physical return. Before forwarding it cannot continue. */
    if (!forwarded) callback_blocked = true;
    return process_result;
}
static void reset(void) {
    forwarded = processed = notified = 0;
    callback_blocked = false;
    serial_locked = state_locked = lock_error = false;
    creates = notifications = ticks = 0;
    delivery = NO_ACK;
    forward_result = process_result = transmit_result = ESP_OK;
    s_broadcast_task = NULL;
    s_broadcast_pending = s_broadcast_acknowledged = false;
    s_id_counter = 0;
    broadcast_init();
}
int test_forward_precedes_callback_that_needs_broadcast_lock(void) {
    reset();
    ring_link_payload_t p = { .src_id = 2 };
    CHECK(broadcast_handler(&p) == ESP_OK);
    CHECK(forwarded == 1 && processed == 1 && !callback_blocked && !notified);
    return 0;
}
int test_callback_errors_do_not_abort_or_prevent_forwarding(void) {
    reset();
    ring_link_payload_t p = { .src_id = 2 };
    process_result = ESP_FAIL;
    CHECK(broadcast_handler(&p) == ESP_FAIL);
    CHECK(forwarded == 1 && processed == 1 && !callback_blocked);
    reset();
    forward_result = ESP_ERR_INVALID_SIZE;
    CHECK(broadcast_handler(&p) == ESP_ERR_INVALID_SIZE);
    CHECK(forwarded == 1 && processed == 1);
    return 0;
}
int test_returning_frame_only_notifies_origin(void) {
    reset();
    ring_link_payload_t p = { .src_id = 1, .id = 7 };
    s_broadcast_task = (void *)2;
    s_pending_id = 7;
    s_broadcast_pending = true;
    CHECK(broadcast_handler(&p) == ESP_OK);
    CHECK(notified == 1 && !forwarded && !processed);
    CHECK(!s_broadcast_pending && s_broadcast_acknowledged && !lock_error);
    CHECK(broadcast_handler(&p) == ESP_OK); /* A duplicate cannot notify twice. */
    CHECK(notified == 1);
    s_broadcast_task = NULL;
    CHECK(broadcast_handler(&p) == ESP_OK);
    CHECK(notified == 1 && !forwarded && !processed);
    return 0;
}
int test_old_return_during_new_broadcast_cannot_confirm_it(void) {
    reset();
    CHECK(!broadcast_to_siblings("a", 1));
    old_frame = last_frame;
    CHECK(!s_broadcast_task && !s_broadcast_pending && !notified);
    delivery = OLD_ACK;
    CHECK(!broadcast_to_siblings("b", 1));
    CHECK(last_frame.id != old_frame.id && notified == 0);
    CHECK(ticks == 2 * BROADCAST_TIMEOUT_MS);
    CHECK(!s_broadcast_task && !s_broadcast_pending && !lock_error);
    CHECK(broadcast_handler(&last_frame) == ESP_OK && !notified);
    delivery = OLD_AND_CURRENT_ACK;
    CHECK(broadcast_to_siblings("c", 1));
    CHECK(notified == 1 && !notifications && !s_broadcast_task && !s_broadcast_pending);
    CHECK(!serial_locked && !state_locked && !lock_error);
    return 0;
}
int test_residual_or_unrelated_notifications_do_not_confirm_broadcast(void) {
    reset();
    notifications = 3; /* Left by a previous wait before this send begins. */
    CHECK(!broadcast_to_siblings("a", 1));
    CHECK(ticks == BROADCAST_TIMEOUT_MS && !notifications);
    delivery = SPURIOUS_NOTIFICATION; /* Arrives while a send is pending. */
    CHECK(!broadcast_to_siblings("b", 1));
    CHECK(ticks == 2 * BROADCAST_TIMEOUT_MS && !notifications);
    delivery = DUPLICATE_ACK;
    CHECK(broadcast_to_siblings("c", 1));
    CHECK(notified == 1 && !notifications && !s_broadcast_task && !s_broadcast_pending);
    CHECK(!serial_locked && !state_locked && !lock_error);
    return 0;
}
int test_transmit_failure_clears_pending_task_before_return(void) {
    reset();
    transmit_result = ESP_FAIL;
    CHECK(!broadcast_to_siblings("a", 1));
    CHECK(!s_broadcast_task && !s_broadcast_pending && !ticks);
    CHECK(broadcast_handler(&last_frame) == ESP_OK);
    CHECK(!notified && !serial_locked && !state_locked && !lock_error);
    return 0;
}
#define RUN(test) do { int line = test(); if (line) { \
    printf("%s: FAILED broadcast_test.c:%d\n", #test, line); return 1; } \
    printf("%s: OK\n", #test); } while (0)
int main(void) {
    RUN(test_forward_precedes_callback_that_needs_broadcast_lock);
    RUN(test_callback_errors_do_not_abort_or_prevent_forwarding);
    RUN(test_returning_frame_only_notifies_origin);
    RUN(test_old_return_during_new_broadcast_cannot_confirm_it);
    RUN(test_residual_or_unrelated_notifications_do_not_confirm_broadcast);
    RUN(test_transmit_failure_clears_pending_task_before_return);
    return 0;
}
