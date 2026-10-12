#include "spi.h"
#include "link_diagnostics.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "task_config.h"
#include "esp_intr_alloc.h"

#include "ring_link_payload.h"


static spi_device_handle_t s_spi_device_handle = {0};
static const char* TAG = "==> SPI";

#define NUM_BUFFERS  8  /* DMA descriptors; physical protocol unchanged. */
#ifndef CONFIG_SPI_RX_EXTRA_BUFFERS
#define CONFIG_SPI_RX_EXTRA_BUFFERS 0
#endif
#define NUM_RX_BUFFERS (NUM_BUFFERS + CONFIG_SPI_RX_EXTRA_BUFFERS)
_Static_assert(sizeof(ring_link_payload_t) == SPI_BUFFER_SIZE, "SPI frame size mismatch");
_Static_assert(sizeof(ring_link_payload_t) % 4 == 0, "SPI DMA stride must be word aligned");
static struct { uint32_t computed_crc, wire_crc; bool valid; } rx_snapshot[NUM_RX_BUFFERS];



DMA_ATTR static ring_link_payload_t buffer_pool[NUM_RX_BUFFERS] __attribute__((aligned(4)));  // Buffers preasignados
static int rx_buffer_index(const void *p) {
    uintptr_t addr=(uintptr_t)p, base=(uintptr_t)&buffer_pool[0];
    if(addr<base || addr>=base+sizeof(buffer_pool) ||
       (addr-base)%sizeof(ring_link_payload_t)) return -1;
    return (int)((addr-base)/sizeof(ring_link_payload_t));
}
static QueueHandle_t free_buf_queue = NULL;       // Buffers libres
static QueueHandle_t spi_rx_queue = NULL;         // Mensajes recibidos

/* Descriptors outlive the task iteration and stay valid while the driver owns
 * them. All eight DMA buffers are queued, instead of rearming one transaction
 * only after the polling task runs again. No application callback runs in ISR. */
static spi_slave_transaction_t rx_transactions[NUM_BUFFERS];

static void queue_rx(spi_slave_transaction_t *transaction)
{
    ring_link_payload_t *payload;
    uint32_t free_count=uxQueueMessagesWaiting(free_buf_queue);
    uint32_t rx_count=uxQueueMessagesWaiting(spi_rx_queue);
    int64_t before=esp_timer_get_time();
    xQueueReceive(free_buf_queue, &payload, portMAX_DELAY);
    int64_t waited=esp_timer_get_time()-before;
    ld_pressure(waited>UINT32_MAX?UINT32_MAX:(uint32_t)waited,free_count,rx_count);
    int index=rx_buffer_index(payload);
    if(index>=0)rx_snapshot[index].valid=false;
    memset(transaction, 0, sizeof(*transaction));
    transaction->length = SPI_BUFFER_SIZE * 8;
    transaction->rx_buffer = payload;
    ESP_ERROR_CHECK(spi_slave_queue_trans(SPI_RECEIVER_HOST, transaction, portMAX_DELAY));
}

static void spi_polling_task(void *pvParameters) {
    (void)pvParameters;
    for (unsigned i = 0; i < NUM_BUFFERS; ++i) queue_rx(&rx_transactions[i]);
    printf("\nI4ASPI event=rx_ready queued=%u iram=1\n", NUM_BUFFERS);
    for (;;) {
        spi_slave_transaction_t *completed;
        ESP_ERROR_CHECK(spi_slave_get_trans_result(SPI_RECEIVER_HOST, &completed, portMAX_DELAY));
        ring_link_payload_t *payload = completed->rx_buffer;
        /* Never deliver a truncated/stale DMA buffer as a complete frame. */
        ld_count(LD_RX);
        bool release=false;
        if (completed->trans_len != sizeof(*payload) * 8) {
            ld_count(LD_SHORT);release=true;
        } else {
            int index=rx_buffer_index(payload);
            uint32_t computed=ring_link_compute_crc32(payload);
            if(computed!=payload->crc32)ld_count(LD_RX_CRC_EARLY);
            if(index>=0) {
                rx_snapshot[index].computed_crc=computed;
                rx_snapshot[index].wire_crc=payload->crc32;
                rx_snapshot[index].valid=true;
            }
            if(xQueueSend(spi_rx_queue, &payload, 0)!=pdTRUE) {
                ld_count(LD_RX_FULL);release=true;
            }
        }
        if (release && xQueueSend(free_buf_queue, &payload, 0)!=pdTRUE)
            ld_count(LD_FREE_FAIL);
        queue_rx(completed);
    }
}

static esp_err_t spi_rx_init() {
    //Configuration for the SPI bus
    spi_bus_config_t buscfg={
        .mosi_io_num=SPI_RECEIVER_GPIO_MOSI,
        .miso_io_num=-1,
        .sclk_io_num=SPI_RECEIVER_GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .intr_flags = ESP_INTR_FLAG_IRAM,
    };

    //Configuration for the SPI slave interface
    spi_slave_interface_config_t slvcfg={
        .mode=0,
        .spics_io_num=SPI_RECEIVER_GPIO_CS,
        .queue_size=SPI_QUEUE_SIZE,
        .flags=0,
        .post_trans_cb=NULL
    };

    //Initialize SPI slave interface
    ESP_ERROR_CHECK(spi_slave_initialize(SPI_RECEIVER_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO));
    return ESP_OK;
}

static esp_err_t spi_tx_init() {
    //Configuration for the SPI bus
    spi_bus_config_t buscfg={
        .mosi_io_num=SPI_SENDER_GPIO_MOSI,
        .miso_io_num=-1,
        .sclk_io_num=SPI_SENDER_GPIO_SCLK,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1
    };

    //Configuration for the SPI device on the other side of the bus
    spi_device_interface_config_t devcfg={
        .command_bits=0,
        .address_bits=0,
        .dummy_bits=0,
        .clock_speed_hz=SPI_FREQ,
        .duty_cycle_pos=128,        //50% duty cycle
        .mode=0,
        .spics_io_num=SPI_SENDER_GPIO_CS,
        .cs_ena_pretrans = 3,   
        .cs_ena_posttrans = 10,        //Keep the CS low 3 cycles after transaction, to stop slave from missing the last bit when CS has less propagation delay than CLK
        .queue_size=SPI_QUEUE_SIZE
    };

    //Initialize the SPI bus and add the device we want to send stuff to.
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_SENDER_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_SENDER_HOST, &devcfg, &s_spi_device_handle));
    return ESP_OK;
}

esp_err_t spi_init(QueueHandle_t **rx_queue) {

    ESP_ERROR_CHECK(spi_rx_init());
    ESP_ERROR_CHECK(spi_tx_init());

    // inicializo punteros a queues
    free_buf_queue = xQueueCreate(NUM_RX_BUFFERS, sizeof(ring_link_payload_t *));
    spi_rx_queue = xQueueCreate(NUM_RX_BUFFERS, sizeof(ring_link_payload_t *));
    if (free_buf_queue == NULL || spi_rx_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_FAIL;
    }
    *rx_queue = &spi_rx_queue;

    // Inicializar pool de buffers
    for (int i = 0; i < NUM_RX_BUFFERS; i++) {
        ring_link_payload_t *ptr = &buffer_pool[i];
        xQueueSend(free_buf_queue, &ptr, 0);
    }
    // inicializo tarea de polling
    if (xTaskCreatePinnedToCore(spi_polling_task, "spi_polling_task", TASK_SPI_STACK, NULL,
                                TASK_SPI_PRIORITY, NULL, TASK_SPI_CORE) != pdPASS)
        return ESP_ERR_NO_MEM;

    return ESP_OK;
}


esp_err_t spi_transmit(void *p, size_t len) {
    ring_link_payload_t* payload = (ring_link_payload_t*)p;
    ESP_LOGD(TAG, "Pre-transmit payload - Type: 0x%02x, ID: %d, TTL: %d", 
             payload->buffer_type, payload->id, payload->ttl);
             
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = p,
    };
    ld_count(LD_TX);
    esp_err_t result=spi_device_transmit(s_spi_device_handle, &t);
    if(result!=ESP_OK)ld_count(LD_TX_FAIL);
    return result;
}

bool spi_rx_buffer_changed(const void *p, uint32_t computed_crc) {
    int index=rx_buffer_index(p);
    if(index<0 || !rx_snapshot[index].valid)return false;
    const ring_link_payload_t *payload=p;
    return computed_crc!=rx_snapshot[index].computed_crc ||
           payload->crc32!=rx_snapshot[index].wire_crc;
}

esp_err_t spi_free_rx_buffer(void *p)
{
    if(xQueueSend(free_buf_queue, &p, 0)!=pdTRUE)ld_count(LD_FREE_FAIL);
    return ESP_OK;
}
