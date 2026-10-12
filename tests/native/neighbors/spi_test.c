/* Simulate eight completed DMA receives while the polling task was paused. */
#include <setjmp.h>
#include "../../../components/internal/spi/spi.c"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static struct { void *items[NUM_RX_BUFFERS]; unsigned count; } available, received;
static spi_slave_transaction_t *driver[NUM_BUFFERS];
static unsigned driver_count, results, max_queued;
static bool error, iram, hold_consumer, starved, corrupt_first;
static void (*poll_task)(void *);
static jmp_buf finished;
static unsigned counters[LD_COUNT];
uint32_t ring_link_compute_crc32(const ring_link_payload_t *p){
 const uint8_t *b=(const uint8_t*)p;uint32_t h=2166136261u;
 for(unsigned i=0;i<sizeof(*p)-4;i++)h=(h^b[i])*16777619u;
 return h;
}
void ld_count(ld_event_t e){counters[e]++;}
void ld_pressure(uint32_t us,uint32_t free_count,uint32_t rx_count){(void)us;(void)free_count;(void)rx_count;}
int64_t esp_timer_get_time(void){static int64_t t;return ++t;}
unsigned uxQueueMessagesWaiting(QueueHandle_t q){return q==&available?available.count:received.count;}
QueueHandle_t xQueueCreate(unsigned n, size_t size) {
    (void)n; (void)size; return free_buf_queue ? (void *)&received : (void *)&available;
}
int xQueueSend(QueueHandle_t queue, const void *p, TickType_t wait) {
    (void)wait;
    if (queue == &available) available.items[available.count++] = *(void *const *)p;
    else {
        received.items[received.count++] = *(void *const *)p;
        /* Model the consumer returning the buffer after using it. */
        if(!hold_consumer)available.items[available.count++] = *(void *const *)p;
    }
    return 1;
}
int xQueueReceive(QueueHandle_t queue, void *p, TickType_t wait) {
    (void)queue; (void)wait;
    if (!available.count) { starved = true; if(!hold_consumer)error=true; longjmp(finished, 1); }
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
    if(hold_consumer)for(unsigned i=0;i<received.count;i++)
        if(received.items[i]==t->rx_buffer)error=true;
    if (driver_count == NUM_BUFFERS) { error = true; longjmp(finished, 1); }
    driver[driver_count++] = t;
    if (driver_count > max_queued) max_queued = driver_count;
    return ESP_OK;
}
esp_err_t spi_slave_get_trans_result(int h, spi_slave_transaction_t **t, TickType_t wait) {
    (void)h; (void)wait;
    if (results == NUM_BUFFERS) longjmp(finished, 1);
    if (driver_count != NUM_BUFFERS) error = true;
    if (!results) {
        /* The ISR completed all eight transfers while polling could not run. */
        for (unsigned i = 0; i < driver_count; ++i) {
            ring_link_payload_t *p=driver[i]->rx_buffer;
            memset(p,0,sizeof(*p));p->data[0]=(uint8_t)(i+1);
            p->crc32=ring_link_compute_crc32(p);
            if(corrupt_first && i==0)p->crc32^=1;
            driver[i]->trans_len = i == 1 ? 8 : sizeof(ring_link_payload_t) * 8;
        }
    }
    *t = driver[0];
    --driver_count;
    for (unsigned i = 0; i < driver_count; ++i) driver[i] = driver[i + 1];
    ++results;
    return ESP_OK;
}
EXPORT int test_spi_keeps_dma_receives_queued_and_drops_truncated_frames(void) {
    free_buf_queue = spi_rx_queue = NULL;
    available.count = received.count = driver_count = results = max_queued = 0;
    error = iram = hold_consumer = starved = corrupt_first = false;
    memset(counters,0,sizeof(counters));
    QueueHandle_t *queue;
    CHECK(spi_init(&queue) == ESP_OK && poll_task);
    if (!setjmp(finished)) poll_task(NULL);
    CHECK(iram && !error && max_queued == NUM_BUFFERS && driver_count == NUM_BUFFERS);
    CHECK(results == NUM_BUFFERS && received.count == NUM_BUFFERS - 1);
    CHECK(available.count == CONFIG_SPI_RX_EXTRA_BUFFERS);
    CHECK(counters[LD_RX]==8 && counters[LD_SHORT]==1);
    CHECK(counters[LD_RX_FULL]==0 && counters[LD_FREE_FAIL]==0);
    CHECK(counters[LD_RX_CRC_EARLY]==0);
    return 0;
}

static int delayed_consumer(bool corrupt) {
    corrupt_first=corrupt;
    free_buf_queue=spi_rx_queue=NULL;
    available.count=received.count=driver_count=results=max_queued=0;
    error=iram=starved=false;hold_consumer=true;
    memset(counters,0,sizeof(counters));
    QueueHandle_t *queue;
    CHECK(spi_init(&queue)==ESP_OK && poll_task);
    if(!setjmp(finished))poll_task(NULL);
    CHECK(!error);
    if(CONFIG_SPI_RX_EXTRA_BUFFERS>=8) {
        CHECK(!starved && results==NUM_BUFFERS && driver_count==NUM_BUFFERS);
        CHECK(received.count==7 && available.count==CONFIG_SPI_RX_EXTRA_BUFFERS-7);
    } else if(CONFIG_SPI_RX_EXTRA_BUFFERS==0) {
        CHECK(starved && results==1 && received.count==1);
    }
    for(unsigned i=0;i<received.count;i++) {
        ring_link_payload_t *p=received.items[i];
        CHECK(!spi_rx_buffer_changed(p,ring_link_compute_crc32(p)));
        p->data[0]^=1;
        CHECK(spi_rx_buffer_changed(p,ring_link_compute_crc32(p)));
        p->data[0]^=1;
        p->crc32^=1;
        CHECK(spi_rx_buffer_changed(p,ring_link_compute_crc32(p)));
        p->crc32^=1;
    }
    CHECK(counters[LD_RX_CRC_EARLY]==(unsigned)corrupt);
    CHECK(!spi_rx_buffer_changed(NULL,0));
    CHECK(!spi_rx_buffer_changed((const uint8_t*)&buffer_pool[0]+1,0));
    return 0;
}

EXPORT int test_spi_delayed_consumer_retains_ownership(void) {return delayed_consumer(false);}
EXPORT int test_spi_crc_at_completion_is_distinct_from_buffer_mutation(void) {return delayed_consumer(true);}
