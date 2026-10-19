#include "broadcast.h"

static const char* TAG = "==> broadcast";
static SemaphoreHandle_t s_broadcast_mutex = NULL;
static SemaphoreHandle_t s_broadcast_state_mutex = NULL;
static TaskHandle_t s_broadcast_task = NULL;
static ring_link_payload_id_t s_id_counter = 0;
static ring_link_payload_id_t s_pending_id;
static bool s_broadcast_pending;
static bool s_broadcast_acknowledged;

esp_err_t broadcast_init( void )
{
    s_broadcast_mutex = xSemaphoreCreateMutex();
    if( s_broadcast_mutex == NULL )
    {
        ESP_LOGE(TAG, "an error ocurred creating mutex.");
        return ESP_FAIL;
    }

    s_broadcast_state_mutex = xSemaphoreCreateMutex();
    if (!s_broadcast_state_mutex) {
        vSemaphoreDelete(s_broadcast_mutex);
        s_broadcast_mutex = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t send_broadcast(const void *buffer, uint16_t len, ring_link_payload_id_t id){
    ring_link_payload_t p = {
        .id = id,
        .ttl = RING_LINK_PAYLOAD_TTL,
        .src_id = config_get_id(),
        .dst_id = CONFIG_ID_ALL,
        .buffer_type = RING_LINK_PAYLOAD_TYPE_INTERNAL,
        .len = len,
    };
    if (len > RING_LINK_NETIF_MTU) {
        ESP_LOGE(TAG, "Buffer length exceeds maximum allowed size.");
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(p.buffer, buffer, len);
    return ring_link_lowlevel_transmit_payload(&p);
}

bool broadcast_to_siblings(const void *msg, uint16_t len)
{
    if( xSemaphoreTake( s_broadcast_mutex, ( TickType_t ) 400 ) == pdTRUE )
    {
        /* Notifications belong to this task, so discard any residue before
         * publishing the next wait. State is protected by a task mutex: no
         * FreeRTOS API is called from inside a portMUX critical section. */
        ulTaskNotifyTake(pdTRUE, 0);
        xSemaphoreTake(s_broadcast_state_mutex, portMAX_DELAY);
        ring_link_payload_id_t id = s_id_counter++;
        s_pending_id = id;
        s_broadcast_task = xTaskGetCurrentTaskHandle();
        s_broadcast_pending = true;
        s_broadcast_acknowledged = false;
        xSemaphoreGive(s_broadcast_state_mutex);

        bool sent = send_broadcast(msg, len, id) == ESP_OK;
        if (sent) {
            TickType_t timeout = pdMS_TO_TICKS(BROADCAST_TIMEOUT_MS);
            TickType_t started = xTaskGetTickCount();
            TickType_t remaining = timeout;
            for (;;) {
                ulTaskNotifyTake(pdTRUE, remaining);
                xSemaphoreTake(s_broadcast_state_mutex, portMAX_DELAY);
                bool acknowledged = s_broadcast_acknowledged;
                xSemaphoreGive(s_broadcast_state_mutex);
                if (acknowledged) break;
                TickType_t elapsed = xTaskGetTickCount() - started;
                if (elapsed >= timeout) break;
                remaining = timeout - elapsed;
            }
        }

        xSemaphoreTake(s_broadcast_state_mutex, portMAX_DELAY);
        bool result = sent && s_broadcast_acknowledged;
        s_broadcast_pending = false;
        s_broadcast_task = NULL;
        xSemaphoreGive(s_broadcast_state_mutex);
        xSemaphoreGive( s_broadcast_mutex );
        return result;
    }
    ESP_LOGE(TAG, "Could not adquire Mutex...");
    return false;
}

esp_err_t broadcast_handler(ring_link_payload_t *p)
{
    // broadcast origin
    if (ring_link_payload_is_from_device(p))
    {
        bool matched = false;
        xSemaphoreTake(s_broadcast_state_mutex, portMAX_DELAY);
        if (s_broadcast_pending && s_broadcast_task && p->id == s_pending_id) {
            s_broadcast_pending = false;
            s_broadcast_acknowledged = true;
            /* Keep the handle protected until notification is delivered.
             * The sender clears it under this mutex before it can return
             * and its task can be stopped or deleted. */
            xTaskNotifyGive(s_broadcast_task);
            matched = true;
        }
        xSemaphoreGive(s_broadcast_state_mutex);
        if (!matched) {
            ESP_LOGW(
                TAG, "Broadcast (src=%i,dest=%i,id=%i,ttl=%i) is stale or already acknowledged -- dropping",
                p->src_id, p->dst_id, p->id, p->ttl
            );
        } else {
            ESP_LOGD(TAG, "Broadcast complete (src=%i,dest=%i,id=%i,ttl=%i).", p->src_id, p->dst_id, p->id, p->ttl);
        }
        return ESP_OK;
    }
    else
    {
        /* Complete the physical ring before invoking application callbacks.
         * A callback can wait for a broadcast mutex held by a sender which is
         * itself waiting for this frame to return to its origin. */
        esp_err_t forwarded = ring_link_lowlevel_forward_payload(p);
        esp_err_t processed = ring_link_internal_process(p);
        return forwarded != ESP_OK ? forwarded : processed;
    }
}

