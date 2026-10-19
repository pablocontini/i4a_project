#ifndef NATIVE_INGRESS_RING_LINK_H
#define NATIVE_INGRESS_RING_LINK_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef void *QueueHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef struct {
    uint32_t crc_error, unknown_type, local_queue_full, forward_fail;
    uint32_t crc_internal, crc_ip, crc_other;
} ring_link_diagnostics_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERROR_CHECK(rc) ((void)(rc))
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY ((TickType_t)-1)
#define RING_LINK_INTERNAL_QUEUE_SIZE 10
typedef struct { uint32_t crc32; uint8_t src_id, dst_id, buffer_type; uint16_t len; uint8_t buffer[512]; } ring_link_payload_t;
QueueHandle_t xQueueCreate(unsigned length, size_t size);
int xQueueSend(QueueHandle_t queue, const void *data, TickType_t wait);
int xQueueReceive(QueueHandle_t queue, void *data, TickType_t wait);
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *, unsigned, void *, unsigned, void *, unsigned);
void taskYIELD(void);
uint32_t ring_link_compute_crc32(const ring_link_payload_t *p);
bool ring_link_payload_is_internal(ring_link_payload_t *p);
bool ring_link_payload_is_esp_netif(ring_link_payload_t *p);
bool ring_link_payload_is_broadcast(ring_link_payload_t *p);
bool ring_link_payload_is_from_device(ring_link_payload_t *p);
bool ring_link_payload_is_for_device(ring_link_payload_t *p);
esp_err_t ring_link_lowlevel_init(QueueHandle_t **queue);
esp_err_t ring_link_internal_init(QueueHandle_t **queue);
esp_err_t ring_link_netif_init(QueueHandle_t **queue);
esp_err_t ring_link_lowlevel_free_rx_buffer(void *p);
esp_err_t ring_link_lowlevel_forward_payload(ring_link_payload_t *p);
esp_err_t ring_link_internal_process(ring_link_payload_t *p);
esp_err_t ring_link_internal_route(ring_link_payload_t *p, bool *deliver_local);
esp_err_t broadcast_init(void);
esp_err_t broadcast_handler(ring_link_payload_t *p);
void node_on_sibling_message(void *msg, uint16_t len);
#endif
