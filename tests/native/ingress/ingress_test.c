#include <stdio.h>
#include <setjmp.h>
#define TAG ingress_tag
#include "../../../components/internal/ring_link/ring_link.c"
#undef TAG
#define TAG internal_tag
#include "../../../components/internal/ring_link_internal/ring_link_internal.c"
#undef TAG
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static unsigned forwarded, acknowledged, enqueued, freed, delivered;
static bool full, order_error, forward_failed, read_once;
static ring_link_payload_t *saved;
static int queue_token;
static QueueHandle_t queue_handle = &queue_token;
static jmp_buf done;
QueueHandle_t xQueueCreate(unsigned n, size_t s) { (void)n; (void)s; return queue_handle; }
int xQueueSend(QueueHandle_t q, const void *data, TickType_t wait) {
    (void)q; (void)wait;
    ring_link_payload_t *p = *(ring_link_payload_t *const *)data;
    if (p->buffer_type == 1 && p->src_id != 5 && p->dst_id != 5 && !forwarded)
        order_error = true;
    ++enqueued;
    if (full) return 0; /* Application side cannot drain while awaiting a return. */
    saved = p;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *data, TickType_t wait) {
    (void)q; (void)wait;
    if (read_once) longjmp(done, 1);
    read_once = true;
    *(ring_link_payload_t **)data = saved;
    return pdTRUE;
}
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *n, unsigned s, void *a, unsigned p, void *h, unsigned c) {
    (void)fn; (void)n; (void)s; (void)a; (void)p; (void)h; (void)c; return pdPASS;
}
void taskYIELD(void) {}
uint32_t ring_link_compute_crc32(const ring_link_payload_t *p) { (void)p; return 42; }
bool ring_link_payload_is_internal(ring_link_payload_t *p) { return p->buffer_type == 1; }
bool ring_link_payload_is_esp_netif(ring_link_payload_t *p) { return p->buffer_type == 2; }
bool ring_link_payload_is_broadcast(ring_link_payload_t *p) { return p->dst_id == 255; }
bool ring_link_payload_is_from_device(ring_link_payload_t *p) { return p->src_id == 5; }
bool ring_link_payload_is_for_device(ring_link_payload_t *p) { return p->dst_id == 5; }
esp_err_t ring_link_lowlevel_init(QueueHandle_t **q) { *q = &queue_handle; return ESP_OK; }
esp_err_t ring_link_netif_init(QueueHandle_t **q) { *q = &queue_handle; return ESP_OK; }
esp_err_t ring_link_lowlevel_free_rx_buffer(void *p) { (void)p; ++freed; return ESP_OK; }
esp_err_t ring_link_lowlevel_forward_payload(ring_link_payload_t *p) { (void)p; ++forwarded; return forward_failed ? ESP_FAIL : ESP_OK; }
esp_err_t broadcast_init(void) { return ESP_OK; }
esp_err_t broadcast_handler(ring_link_payload_t *p) {
    if (p->src_id == 5) { ++acknowledged; return ESP_OK; }
    ring_link_lowlevel_forward_payload(p);
    return ring_link_internal_process(p);
}
void node_on_sibling_message(void *data, uint16_t len) { (void)data; (void)len; ++delivered; }
static void reset(void) {
    memset(&diagnostics, 0, sizeof(diagnostics));
    forwarded = acknowledged = enqueued = freed = delivered = 0;
    full = order_error = forward_failed = read_once = false;
    internal_queue = esp_netif_queue = &queue_handle;
}
static int transit_survives_blocked_application(void) {
    reset(); full = true;
    ring_link_payload_t p = { .crc32 = 42, .src_id = 1, .dst_id = 255, .buffer_type = 1 };
    CHECK(process_payload(&p) == ESP_FAIL);
    CHECK(forwarded == 1 && enqueued == 1 && freed == 1 && !order_error);
    ring_link_diagnostics_t counters;
    ring_link_get_diagnostics(&counters);
    CHECK(counters.local_queue_full == 1 && counters.crc_error == 0);
    return 0;
}
static int return_ack_bypasses_blocked_application(void) {
    reset(); full = true;
    ring_link_payload_t p = { .crc32 = 42, .src_id = 5, .dst_id = 255, .buffer_type = 1 };
    CHECK(process_payload(&p) == ESP_OK);
    CHECK(acknowledged == 1 && !enqueued && !forwarded && freed == 1 && !delivered);
    return 0;
}
static int local_delivery_is_once_after_forwarding(void) {
    reset();
    ring_link_payload_t p = { .crc32 = 42, .src_id = 1, .dst_id = 255, .buffer_type = 1 };
    CHECK(process_payload(&p) == ESP_OK);
    CHECK(forwarded == 1 && enqueued == 1 && !freed && !delivered && !order_error);
    if (!setjmp(done)) ring_link_internal_process_task(NULL);
    CHECK(delivered == 1 && forwarded == 1 && freed == 1);
    return 0;
}
static int directed_and_invalid_frames_keep_ownership(void) {
    reset(); full = true;
    ring_link_payload_t p = { .crc32 = 42, .src_id = 1, .dst_id = 2, .buffer_type = 1 };
    CHECK(process_payload(&p) == ESP_OK && forwarded == 1 && freed == 1 && !enqueued);
    reset(); p.dst_id = 5;
    CHECK(process_payload(&p) == ESP_OK && enqueued == 1 && !forwarded && !freed);
    reset(); p.crc32 = 0;
    CHECK(process_payload(&p) == ESP_FAIL && freed == 1 && !enqueued && !forwarded);
    CHECK(diagnostics.crc_error == 1);
    reset(); p.crc32 = 42; p.buffer_type = 2;
    CHECK(process_payload(&p) == ESP_OK && enqueued == 1 && !forwarded && !acknowledged);
    return 0;
}
static int forwarding_failure_keeps_local_delivery(void) {
    reset(); forward_failed = true;
    ring_link_payload_t p = { .crc32 = 42, .src_id = 1, .dst_id = 255, .buffer_type = 1 };
    CHECK(process_payload(&p) == ESP_FAIL && forwarded == 1 && enqueued == 1 && !freed);
    CHECK(diagnostics.forward_fail == 1);
    if (!setjmp(done)) ring_link_internal_process_task(NULL);
    CHECK(delivered == 1 && freed == 1 && forwarded == 1);
    return 0;
}
static int crc_diagnostics_classify_only_observed_type(void) {
    reset();
    ring_link_payload_t p = { .crc32 = 0, .src_id = 1, .dst_id = 255, .buffer_type = 1 };
    CHECK(process_payload(&p) == ESP_FAIL);
    p.buffer_type = 2;
    CHECK(process_payload(&p) == ESP_FAIL);
    p.buffer_type = 0xab;
    CHECK(process_payload(&p) == ESP_FAIL);
    ring_link_diagnostics_t counters;
    ring_link_get_diagnostics(&counters);
    CHECK(counters.crc_error == 3 && counters.crc_internal == 1);
    CHECK(counters.crc_ip == 1 && counters.crc_other == 1);
    CHECK(freed == 3 && !forwarded && !enqueued && !delivered && !acknowledged);
    return 0;
}
#define RUN(test) do { int line = test(); if (line) { printf("%s: FAIL line %d\n", #test, line); return 1; } printf("%s: OK\n", #test); } while (0)
int main(void) {
    RUN(transit_survives_blocked_application);
    RUN(return_ack_bypasses_blocked_application);
    RUN(local_delivery_is_once_after_forwarding);
    RUN(directed_and_invalid_frames_keep_ownership);
    RUN(forwarding_failure_keeps_local_delivery);
    RUN(crc_diagnostics_classify_only_observed_type);
    return 0;
}
