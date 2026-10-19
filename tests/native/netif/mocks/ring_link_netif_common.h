#ifndef NATIVE_RING_LINK_NETIF_COMMON_H
#define NATIVE_RING_LINK_NETIF_COMMON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define RING_LINK_NETIF_MTU 1588
#define RING_LINK_PAYLOAD_TTL 4
#define CONFIG_ID_ANY 255
#define RING_LINK_PAYLOAD_TYPE_ESP_NETIF 0x80
typedef int esp_err_t;
typedef uint8_t ring_link_payload_id_t;
typedef struct {
    uint8_t id, buffer_type;
    uint16_t len;
    uint8_t ttl, src_id, dst_id;
    char buffer[RING_LINK_NETIF_MTU];
    uint32_t crc32;
} ring_link_payload_t;
uint8_t config_get_id(void);
esp_err_t ring_link_lowlevel_transmit_payload(ring_link_payload_t *payload);
#endif
