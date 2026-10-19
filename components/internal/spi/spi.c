#include "spi.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "task_config.h"
#include "esp_intr_alloc.h"

#include "ring_link_payload.h"


static spi_device_handle_t s_spi_device_handle = {0};
static const char* TAG = "==> SPI";

#define NUM_BUFFERS  8

DMA_ATTR static ring_link_payload_t buffer_pool[NUM_BUFFERS] __attribute__((aligned(4)));  // Buffers preasignados
/* Application consumers must never own a DMA receive buffer: retaining one
 * used to prevent rearming that transaction until the consumer returned it. */
static ring_link_payload_t delivery_pool[NUM_BUFFERS];
static portMUX_TYPE diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
static spi_diagnostics_t diagnostics;

void spi_get_diagnostics(spi_diagnostics_t *out) {
    portENTER_CRITICAL(&diagnostics_lock);
    *out = diagnostics;
    portEXIT_CRITICAL(&diagnostics_lock);
}
static QueueHandle_t free_buf_queue = NULL;       // Buffers libres
static QueueHandle_t spi_rx_queue = NULL;         // Mensajes recibidos

/* Descriptors outlive the task iteration and stay valid while the driver owns
 * them. All eight DMA buffers are queued, instead of rearming one transaction
 * only after the polling task runs again. No application callback runs in ISR. */
static spi_slave_transaction_t rx_transactions[NUM_BUFFERS];

static void queue_rx(spi_slave_transaction_t *transaction, ring_link_payload_t *payload)
{
    memset(transaction, 0, sizeof(*transaction));
    transaction->length = SPI_BUFFER_SIZE * 8;
    transaction->rx_buffer = payload;
    ESP_ERROR_CHECK(spi_slave_queue_trans(SPI_RECEIVER_HOST, transaction, portMAX_DELAY));
}

static void spi_polling_task(void *pvParameters) {
    (void)pvParameters;
    for (unsigned i = 0; i < NUM_BUFFERS; ++i) queue_rx(&rx_transactions[i], &buffer_pool[i]);
    printf("\nI4ASPI event=rx_ready queued=%u iram=1\n", NUM_BUFFERS);
    for (;;) {
        spi_slave_transaction_t *completed;
        ESP_ERROR_CHECK(spi_slave_get_trans_result(SPI_RECEIVER_HOST, &completed, portMAX_DELAY));
        ring_link_payload_t *payload = completed->rx_buffer;
        ring_link_payload_t *delivery = NULL;
        bool short_frame = completed->trans_len != sizeof(*payload) * 8;
        bool no_buffer = false, queue_full = false;
#if CONFIG_I4A_CAPTURE_DIAGNOSTICS
        bool crc_checked = false, crc_error = false;
#endif
        /* Never deliver a truncated/stale DMA buffer as a complete frame. */
        if (!short_frame) {
            if (xQueueReceive(free_buf_queue, &delivery, 0) == pdTRUE) {
                memcpy(delivery, payload, sizeof(*delivery));
#if CONFIG_I4A_CAPTURE_DIAGNOSTICS
                /* Inspect the application copy before rearming DMA, while no
                 * consumer can touch it. This observation never changes the
                 * existing CRC rejection performed by ring ingress. */
                crc_checked = true;
                crc_error = ring_link_compute_crc32(delivery) != delivery->crc32;
#endif
            } else no_buffer = true;
        }
        /* Rearm the same DMA buffer before exposing the separate copy. A full
         * application pool drops one delivery, never waits to receive again. */
        queue_rx(completed, payload);
        if (delivery && xQueueSend(spi_rx_queue, &delivery, 0) != pdTRUE) {
            queue_full = true;
            xQueueSend(free_buf_queue, &delivery, 0);
        }
        portENTER_CRITICAL(&diagnostics_lock);
        ++diagnostics.rx_complete;
        diagnostics.rx_short += short_frame;
        diagnostics.rx_no_buffer += no_buffer;
        diagnostics.rx_queue_full += queue_full;
#if CONFIG_I4A_CAPTURE_DIAGNOSTICS
        diagnostics.rx_crc_checked += crc_checked;
        diagnostics.rx_crc_error += crc_error;
#endif
        portEXIT_CRITICAL(&diagnostics_lock);
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
    free_buf_queue = xQueueCreate(NUM_BUFFERS, sizeof(ring_link_payload_t *));
    spi_rx_queue = xQueueCreate(NUM_BUFFERS, sizeof(ring_link_payload_t *));
    if (free_buf_queue == NULL || spi_rx_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_FAIL;
    }
    *rx_queue = &spi_rx_queue;

    // Inicializar pool de buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
        ring_link_payload_t *ptr = &delivery_pool[i];
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
    esp_err_t result = spi_device_transmit(s_spi_device_handle, &t);
    portENTER_CRITICAL(&diagnostics_lock);
    ++diagnostics.tx;
    diagnostics.tx_fail += result != ESP_OK;
    portEXIT_CRITICAL(&diagnostics_lock);
    return result;
}

esp_err_t spi_free_rx_buffer(void *p)
{
    xQueueSend(free_buf_queue, &p, 0);
    return ESP_OK;
}
