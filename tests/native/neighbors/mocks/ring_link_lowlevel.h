#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#define RING_LINK_PAYLOAD_TYPE_INTERNAL 1
#define RING_LINK_PAYLOAD_TYPE_ESP_NETIF 2
#define RING_LINK_LOWLEVEL_IMPL_INIT mock_init
#define RING_LINK_LOWLEVEL_IMPL_TRANSMIT mock_transmit
#define RING_LINK_LOWLEVEL_IMPL_FREE_RX_BUFFER mock_free
#define pdTRUE 1
typedef struct { uint32_t crc32; int ttl, src_id, dst_id, id, buffer_type; size_t len; char buffer[84]; } ring_link_payload_t;
esp_err_t mock_init(QueueHandle_t **queue);
esp_err_t mock_transmit(void *payload, size_t size);
esp_err_t mock_free(void *payload);
uint32_t ring_link_compute_crc32(ring_link_payload_t *p);
void ring_link_lowlevel_set_maintenance(bool active);
