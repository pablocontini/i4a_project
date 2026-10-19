/* Simulate eight completed DMA receives while the polling task was paused. */
#include <setjmp.h>
#include "../../../components/internal/spi/spi.c"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static struct { void *items[NUM_BUFFERS]; unsigned count; } available, received;
static spi_slave_transaction_t *driver[NUM_BUFFERS];
static unsigned driver_count, results, max_queued;
static bool error, iram;
static bool hold_received, blocked_rearm, reject_delivery, delivered_before_rearm;
static bool crc_after_rearm;
static unsigned crc_calls;
static unsigned result_limit;
static void (*poll_task)(void *);
static jmp_buf finished;
uint32_t ring_link_compute_crc32(const ring_link_payload_t *p) {
    /* A deterministic checksum makes frame corruption observable without an
     * ESP ROM dependency. Inspect only the bytes preceding the wire CRC. */
    for (unsigned i = 0; i < NUM_BUFFERS; ++i) {
        if (p == &delivery_pool[i]) {
            ++crc_calls;
            if (driver_count == NUM_BUFFERS) crc_after_rearm = true;
        }
    }
    uint32_t crc = 2166136261U;
    const uint8_t *bytes = (const uint8_t *)p;
    for (unsigned i = 0; i < sizeof(*p) - sizeof(p->crc32); ++i)
        crc = (crc ^ bytes[i]) * 16777619U;
    return crc;
}
QueueHandle_t xQueueCreate(unsigned n, size_t size) {
    (void)n; (void)size; return free_buf_queue ? (void *)&received : (void *)&available;
}
int xQueueSend(QueueHandle_t queue, const void *p, TickType_t wait) {
    (void)wait;
    if (queue == &available) available.items[available.count++] = *(void *const *)p;
    else {
        if (driver_count != NUM_BUFFERS) delivered_before_rearm = true;
        if (reject_delivery) return 0;
        received.items[received.count++] = *(void *const *)p;
        /* Model the consumer returning the buffer after using it. */
        if (!hold_received) available.items[available.count++] = *(void *const *)p;
    }
    return 1;
}
int xQueueReceive(QueueHandle_t queue, void *p, TickType_t wait) {
    (void)queue;
    if (!available.count) {
        if (!wait) return 0;
        blocked_rearm = error = true;
        longjmp(finished, 1);
    }
    *(void **)p = available.items[--available.count]; return 1;
}
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                           unsigned priority, void *handle, unsigned core) {
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle; (void)core;
    poll_task = fn; return pdPASS;
}
esp_err_t spi_slave_initialize(int h, const spi_bus_config_t *b, const spi_slave_interface_config_t *c, int d) {
    (void)h; (void)d; iram = b->intr_flags == ESP_INTR_FLAG_IRAM && !c->post_trans_cb; return ESP_OK;
}
esp_err_t spi_bus_initialize(int h, const spi_bus_config_t *b, int d) { (void)h; (void)b; (void)d; return ESP_OK; }
esp_err_t spi_bus_add_device(int h, const spi_device_interface_config_t *c, spi_device_handle_t *d) { (void)h; (void)c; (void)d; return ESP_OK; }
esp_err_t spi_device_transmit(spi_device_handle_t h, spi_transaction_t *t) { (void)h; (void)t; return ESP_OK; }
esp_err_t spi_slave_queue_trans(int h, spi_slave_transaction_t *t, TickType_t wait) {
    (void)h; (void)wait;
    for (unsigned i = 0; i < driver_count; ++i)
        if (driver[i] == t || driver[i]->rx_buffer == t->rx_buffer) error = true;
    if (driver_count == NUM_BUFFERS) { error = true; longjmp(finished, 1); }
    driver[driver_count++] = t;
    if (driver_count > max_queued) max_queued = driver_count;
    return ESP_OK;
}
esp_err_t spi_slave_get_trans_result(int h, spi_slave_transaction_t **t, TickType_t wait) {
    (void)h; (void)wait;
    if (results == result_limit) longjmp(finished, 1);
    if (driver_count != NUM_BUFFERS) error = true;
    if (!results) {
        /* The ISR completed all eight transfers while polling could not run. */
        for (unsigned i = 0; i < driver_count; ++i)
            driver[i]->trans_len = i == 1 ? 8 : sizeof(ring_link_payload_t) * 8;
    }
    *t = driver[0];
    memset((*t)->rx_buffer, (int)(results + 1), sizeof(ring_link_payload_t));
    ring_link_payload_t *frame = (*t)->rx_buffer;
    frame->crc32 = ring_link_compute_crc32(frame);
    if (results == 2) frame->data[0] ^= 1; /* Full-length corrupted frame. */
    (*t)->trans_len = results == 1 ? 8 : sizeof(ring_link_payload_t) * 8;
    --driver_count;
    for (unsigned i = 0; i < driver_count; ++i) driver[i] = driver[i + 1];
    ++results;
    return ESP_OK;
}
EXPORT int test_spi_keeps_dma_receives_queued_and_drops_truncated_frames(void) {
    memset(&diagnostics, 0, sizeof(diagnostics));
    free_buf_queue = spi_rx_queue = NULL;
    available.count = received.count = driver_count = results = max_queued = 0;
    error = iram = hold_received = blocked_rearm = false;
    reject_delivery = delivered_before_rearm = false;
    crc_after_rearm = false; crc_calls = 0;
    result_limit = NUM_BUFFERS;
    QueueHandle_t *queue;
    CHECK(spi_init(&queue) == ESP_OK && poll_task);
    if (!setjmp(finished)) poll_task(NULL);
    CHECK(iram && !error && max_queued == NUM_BUFFERS && driver_count == NUM_BUFFERS);
    CHECK(results == NUM_BUFFERS && received.count == NUM_BUFFERS - 1);
    spi_diagnostics_t stats;
    spi_get_diagnostics(&stats);
    CHECK(stats.rx_crc_checked == NUM_BUFFERS - 1 && stats.rx_crc_error == 1);
    CHECK(crc_calls == NUM_BUFFERS - 1 && !crc_after_rearm);
    /* Bad CRC is still delivered: the existing ring layer owns rejection. */
    return 0;
}

EXPORT int test_spi_rearms_when_application_retains_all_receive_buffers(void) {
    memset(&diagnostics, 0, sizeof(diagnostics));
    free_buf_queue = spi_rx_queue = NULL;
    available.count = received.count = driver_count = results = max_queued = 0;
    error = iram = blocked_rearm = false;
    reject_delivery = delivered_before_rearm = false;
    crc_after_rearm = false; crc_calls = 0;
    hold_received = true;
    result_limit = NUM_BUFFERS + 3;
    QueueHandle_t *queue;
    CHECK(spi_init(&queue) == ESP_OK && poll_task);
    if (!setjmp(finished)) poll_task(NULL);
    CHECK(!blocked_rearm && !error && !delivered_before_rearm);
    CHECK(results == result_limit && driver_count == NUM_BUFFERS);
    CHECK(received.count == NUM_BUFFERS && available.count == 0);
    for (unsigned i = 0; i < received.count; ++i) {
        for (unsigned j = 0; j < driver_count; ++j)
            CHECK(received.items[i] != driver[j]->rx_buffer);
        CHECK(((uint8_t *)received.items[i])[0] == (i ? i + 2 : 1));
        for (unsigned j = i + 1; j < received.count; ++j)
            CHECK(received.items[i] != received.items[j]);
    }
    spi_diagnostics_t stats;
    spi_get_diagnostics(&stats);
    CHECK(stats.rx_complete == result_limit && stats.rx_short == 1);
    CHECK(stats.rx_no_buffer == 2 && !stats.rx_queue_full);
    CHECK(stats.rx_crc_checked == NUM_BUFFERS && stats.rx_crc_error == 1);
    CHECK(crc_calls == NUM_BUFFERS && !crc_after_rearm);
    return 0;
}

EXPORT int test_spi_full_delivery_queue_drops_without_leaking_or_delaying_rearm(void) {
    memset(&diagnostics, 0, sizeof(diagnostics));
    free_buf_queue = spi_rx_queue = NULL;
    available.count = received.count = driver_count = results = max_queued = 0;
    error = iram = blocked_rearm = delivered_before_rearm = false;
    hold_received = reject_delivery = true;
    crc_after_rearm = false; crc_calls = 0;
    result_limit = 3;
    QueueHandle_t *queue;
    CHECK(spi_init(&queue) == ESP_OK && poll_task);
    if (!setjmp(finished)) poll_task(NULL);
    CHECK(!blocked_rearm && !delivered_before_rearm && !error);
    CHECK(driver_count == NUM_BUFFERS && available.count == NUM_BUFFERS && !received.count);
    spi_diagnostics_t stats;
    spi_get_diagnostics(&stats);
    CHECK(stats.rx_complete == 3 && stats.rx_short == 1 && stats.rx_queue_full == 2);
    CHECK(stats.rx_no_buffer == 0);
    CHECK(stats.rx_crc_checked == 2 && stats.rx_crc_error == 1);
    CHECK(crc_calls == 2 && !crc_after_rearm);
    return 0;
}
