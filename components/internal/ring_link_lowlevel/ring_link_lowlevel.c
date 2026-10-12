#include "ring_link_lowlevel.h"
#include "ip_diagnostics.h"

static const char* TAG = "==> ring_link_lowlevel";

static SemaphoreHandle_t s_tx_semaphore_handle = NULL;
static bool s_maintenance;

esp_err_t ring_link_lowlevel_init(QueueHandle_t **rx_queue) {
    ESP_ERROR_CHECK(RING_LINK_LOWLEVEL_IMPL_INIT(rx_queue));

    s_tx_semaphore_handle = xSemaphoreCreateMutex();
    if (!s_tx_semaphore_handle) return ESP_ERR_NO_MEM;

    return ESP_OK;
}

void ring_link_lowlevel_set_maintenance(bool active)
{
    /* Drain the active physical transfer and serialize all IP sends/forwards
     * with the gate. Internal frames must still complete the closed ring. */
    if (!s_tx_semaphore_handle) {
        s_maintenance = active; /* Startup gate is configured before SPI init. */
        return;
    }
    if (xSemaphoreTake(s_tx_semaphore_handle, portMAX_DELAY) == pdTRUE) {
        s_maintenance = active;
        xSemaphoreGive(s_tx_semaphore_handle);
    }
}

esp_err_t ring_link_lowlevel_transmit_payload(ring_link_payload_t *p)
{
    esp_err_t rc;
    p->crc32 = ring_link_compute_crc32(p);
    if( xSemaphoreTake( s_tx_semaphore_handle, ( TickType_t ) 10 ) == pdTRUE )
    {
        /* Drop IP frames during OTA, including forwarded frames and queued
         * netif sends. Report a deliberate drop as success to avoid aborting
         * forwarding tasks; TCP can retransmit after maintenance ends. */
        bool dropped = s_maintenance && p->buffer_type != RING_LINK_PAYLOAD_TYPE_INTERNAL;
        rc = dropped
            ? ESP_OK : RING_LINK_LOWLEVEL_IMPL_TRANSMIT(p, sizeof(ring_link_payload_t));
        xSemaphoreGive( s_tx_semaphore_handle );
        if (p->buffer_type == RING_LINK_PAYLOAD_TYPE_ESP_NETIF) {
            ip_diag_packet(p->buffer, p->len < sizeof(p->buffer) ? p->len : sizeof(p->buffer),
                           dropped ? IP_DIAG_SPI_GATE_DROP :
                           (rc == ESP_OK ? IP_DIAG_SPI_WIRE_TX_OK : IP_DIAG_SPI_WIRE_TX_FAIL), rc);
        }
        return rc;
    }
    ESP_LOGE(TAG, "Could not adquire Mutex...");
    if (p->buffer_type == RING_LINK_PAYLOAD_TYPE_ESP_NETIF)
        ip_diag_packet(p->buffer, p->len < sizeof(p->buffer) ? p->len : sizeof(p->buffer),
                       IP_DIAG_SPI_WIRE_TX_FAIL, ESP_FAIL);
    return ESP_FAIL;
}

esp_err_t ring_link_lowlevel_free_rx_buffer(void *p)
{
    return RING_LINK_LOWLEVEL_IMPL_FREE_RX_BUFFER(p);
}

esp_err_t ring_link_lowlevel_forward_payload(ring_link_payload_t *p)
{
    if (p->ttl <= 0) {
        ESP_LOGW(TAG, "Discarding packet (src=%i,dest=%i,id=%i,ttl=%i) due to TTL. Won't forward.",p->src_id, p->dst_id, p->id, p->ttl);
        return ESP_OK;
    }
    p->ttl--;
    ESP_LOGD(TAG, "Forwarding packet (src=%i,dest=%i,id=%i,ttl=%i).", p->src_id, p->dst_id, p->id, p->ttl);
    return ring_link_lowlevel_transmit_payload(p);
}


