#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "task_config.h"
#include "ring_link.h"
#include "sdkconfig.h"

static const char *TAG = "==> ring_link";

static QueueHandle_t *lowlevel_queue;
static QueueHandle_t *internal_queue;
static QueueHandle_t *esp_netif_queue;
static portMUX_TYPE diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
static ring_link_diagnostics_t diagnostics;

static void count_event(uint32_t *counter) {
    portENTER_CRITICAL(&diagnostics_lock);
    ++*counter;
    portEXIT_CRITICAL(&diagnostics_lock);
}

void ring_link_get_diagnostics(ring_link_diagnostics_t *out) {
    portENTER_CRITICAL(&diagnostics_lock);
    *out = diagnostics;
    portEXIT_CRITICAL(&diagnostics_lock);
}

static esp_err_t process_payload(ring_link_payload_t *p)
{
    QueueHandle_t *specific_queue;
    esp_err_t route_result = ESP_OK;

    uint32_t expected = ring_link_compute_crc32(p);
    if (expected != p->crc32) {
        portENTER_CRITICAL(&diagnostics_lock);
        ++diagnostics.crc_error;
#if CONFIG_I4A_CAPTURE_DIAGNOSTICS
        /* A bad CRC makes the header untrusted. These are observations of
         * its raw type byte, not evidence of the original packet's type. */
        if (ring_link_payload_is_internal(p)) ++diagnostics.crc_internal;
        else if (ring_link_payload_is_esp_netif(p)) ++diagnostics.crc_ip;
        else ++diagnostics.crc_other;
#endif
        portEXIT_CRITICAL(&diagnostics_lock);
        ESP_LOGD(TAG, "CRC32 mismatch, dropping.");
        ring_link_lowlevel_free_rx_buffer(p);
        return ESP_FAIL;
    }

    if (ring_link_payload_is_internal(p))
    {
        /* Application callbacks can wait for a broadcast or for queue space.
         * Keep both transit traffic and returning ACKs out of that wait chain,
         * including when the internal delivery queue is already full. */
        bool deliver_local;
        route_result = ring_link_internal_route(p, &deliver_local);
        if (route_result != ESP_OK) count_event(&diagnostics.forward_fail);
        if (!deliver_local) {
            ring_link_lowlevel_free_rx_buffer(p);
            return route_result;
        }
        ESP_LOGD(TAG, "Processing as internal payload");
        specific_queue = internal_queue;
    }
    else if (ring_link_payload_is_esp_netif(p))
    {
        ESP_LOGD(TAG, "Processing as esp_netif payload");
        specific_queue = esp_netif_queue;
    }
    else
    {
        count_event(&diagnostics.unknown_type);
        ESP_LOGD(TAG, "Unknown payload type: '0x%02x'", p->buffer_type);
        ring_link_lowlevel_free_rx_buffer(p);
        return ESP_FAIL;
    }
    if (xQueueSend(*specific_queue, &p, pdMS_TO_TICKS(100)) != pdTRUE) {
        count_event(&diagnostics.local_queue_full);
        ESP_LOGW(TAG, "Queue full, payload dropped");
        ring_link_lowlevel_free_rx_buffer(p);
        return ESP_FAIL;
    }
    return route_result;
}

static void ring_link_process_task(void *pvParameters)
{
    ring_link_payload_t *payload;
    esp_err_t rc;
    
    while (true) {
        if (xQueueReceive(*lowlevel_queue, &payload, portMAX_DELAY) == pdTRUE) {
            rc = process_payload(payload);
            // ESP_ERROR_CHECK_WITHOUT_ABORT(rc);
            taskYIELD();
        }
    }
}


esp_err_t ring_link_init(void)
{
    #ifdef CONFIG_RING_LINK_LOWLEVEL_IMPL_SPI
    printf("CONFIG_RING_LINK_LOWLEVEL_IMPL_SPI\n");
    #endif

    #ifdef CONFIG_RING_LINK_LOWLEVEL_IMPL_UART
    printf("CONFIG_RING_LINK_LOWLEVEL_IMPL_UART\n");
    #endif


    ESP_ERROR_CHECK(ring_link_lowlevel_init(&lowlevel_queue));
    ESP_ERROR_CHECK(ring_link_internal_init(&internal_queue));
    ESP_ERROR_CHECK(ring_link_netif_init(&esp_netif_queue));

    BaseType_t ret = xTaskCreatePinnedToCore(
        ring_link_process_task,
        "ring_link_process",
        TASK_RING_LINK_STACK,
        NULL,
        TASK_RING_LINK_PRIORITY,
        NULL,
        TASK_RING_LINK_CORE
    );
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create process task");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

