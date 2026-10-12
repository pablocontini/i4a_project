#include <stdio.h>
#ifndef BROADCAST_UNDER_TEST
#define BROADCAST_UNDER_TEST "../../../components/internal/ring_link_internal/broadcast.c"
#endif
#include BROADCAST_UNDER_TEST
bool test_abort;
static bool queue_full,callback_released,local_seen,forward_fails;
static unsigned forwarded,notifications;
static uint8_t delivered[64];
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void*)1; }
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t t) { (void)s;(void)t;return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s;return 1; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void*)2; }
unsigned ulTaskNotifyTake(int c,unsigned t) { (void)c;(void)t;return 1; }
void xTaskNotifyGive(TaskHandle_t t) { (void)t;++notifications; }
unsigned config_get_id(void) { return 1; }
bool ring_link_payload_is_from_device(ring_link_payload_t *p) { return p->src_id==1; }
esp_err_t ring_link_lowlevel_transmit_payload(ring_link_payload_t *p) { (void)p;return ESP_OK; }
esp_err_t ring_link_lowlevel_forward_payload(ring_link_payload_t *p) {
    ++forwarded; --p->ttl;
    if (forward_fails) return ESP_FAIL;
    /* Completing the ring wakes the sender callback that drains this queue. */
    callback_released=true;return ESP_OK;
}
esp_err_t ring_link_internal_process(ring_link_payload_t *p) {
    if (queue_full && !callback_released) return ESP_FAIL;
    memcpy(delivered,p->buffer,p->len);local_seen=true;return ESP_OK;
}
#define CHECK(x) do { if(!(x)) { printf("ordering_test.c:%d\n",__LINE__);return 1; } } while(0)
int main(void) {
    ring_link_payload_t p={.src_id=2,.ttl=8,.len=3,.buffer={4,5,6}};
    queue_full=true;
    CHECK(broadcast_handler(&p)==ESP_OK);
    CHECK(!test_abort && callback_released && local_seen && forwarded==1);
    CHECK(delivered[0]==4 && delivered[1]==5 && delivered[2]==6);
    /* Returning to the origin notifies its waiter and is not enqueued again. */
    p.src_id=1;s_broadcast_task=(void*)2;local_seen=false;
    CHECK(broadcast_handler(&p)==ESP_OK);
    CHECK(notifications==1 && !local_seen && forwarded==1);
    /* Preserve local delivery and forwarding error when the queue is free. */
    p.src_id=2;queue_full=false;forward_fails=true;callback_released=false;
    CHECK(broadcast_handler(&p)==ESP_FAIL);
    CHECK(local_seen && !test_abort);
    puts("broadcast_queue_pressure_cannot_hold_the_sender_completion: OK");
    return 0;
}
