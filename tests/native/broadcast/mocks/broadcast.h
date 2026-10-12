#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int esp_err_t;
typedef unsigned TickType_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef unsigned ring_link_payload_id_t;
typedef struct { unsigned id,ttl,src_id,dst_id,buffer_type; uint16_t len; uint8_t buffer[64]; } ring_link_payload_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_SIZE 2
#define pdTRUE 1
#define RING_LINK_PAYLOAD_TTL 8
#define CONFIG_ID_ALL 11
#define RING_LINK_PAYLOAD_TYPE_INTERNAL 1
#define RING_LINK_NETIF_MTU 64
#define BROADCAST_TIMEOUT_MS 500
#define portTICK_PERIOD_MS 10
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
extern bool test_abort;
#define ESP_ERROR_CHECK(value) do { if ((value)!=ESP_OK) test_abort=true; } while(0)
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,TickType_t);
int xSemaphoreGive(SemaphoreHandle_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
unsigned ulTaskNotifyTake(int,unsigned);
void xTaskNotifyGive(TaskHandle_t);
unsigned config_get_id(void);
esp_err_t ring_link_lowlevel_transmit_payload(ring_link_payload_t *);
esp_err_t ring_link_lowlevel_forward_payload(ring_link_payload_t *);
esp_err_t ring_link_internal_process(ring_link_payload_t *);
bool ring_link_payload_is_from_device(ring_link_payload_t *);
