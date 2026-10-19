#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define pdTRUE 1
#define portMAX_DELAY ((TickType_t)-1)
#define pdMS_TO_TICKS(ms) (ms)
#define portTICK_PERIOD_MS 1
#define BROADCAST_TIMEOUT_MS 500
#define RING_LINK_NETIF_MTU 1588
#define RING_LINK_PAYLOAD_TTL 4
#define CONFIG_ID_ALL 255
#define RING_LINK_PAYLOAD_TYPE_INTERNAL 0x11
typedef int esp_err_t;
typedef unsigned TickType_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef uint8_t ring_link_payload_id_t;
typedef struct {
    uint8_t id, buffer_type;
    uint16_t len;
    uint8_t ttl, src_id, dst_id;
    char buffer[RING_LINK_NETIF_MTU];
    uint32_t crc32;
} ring_link_payload_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait);
int xSemaphoreGive(SemaphoreHandle_t mutex);
void vSemaphoreDelete(SemaphoreHandle_t mutex);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
unsigned ulTaskNotifyTake(int clear, TickType_t wait);
void xTaskNotifyGive(TaskHandle_t task);
uint8_t config_get_id(void);
bool ring_link_payload_is_from_device(ring_link_payload_t *payload);
esp_err_t ring_link_lowlevel_transmit_payload(ring_link_payload_t *payload);
esp_err_t ring_link_lowlevel_forward_payload(ring_link_payload_t *payload);
esp_err_t ring_link_internal_process(ring_link_payload_t *payload);
