#pragma once

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/stats.h"
#include "lwip/inet_chksum.h"

#include "esp_netif.h"
#include "netif/etharp.h"
#include "lwip/esp_netif_net_stack.h"
#include "esp_netif_net_stack.h"
#include "lwip/esp_pbuf_ref.h"
#include "arpa/inet.h" // for ntohs, etc.

#include "ring_link_payload.h"
#include "ring_link_lowlevel.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    RING_LINK_EVENT_START, /**< ESP32 soft-AP start */
    RING_LINK_EVENT_STOP, /**< ESP32 soft-AP stop */
    RING_LINK_EVENT_NEW_ROUTE, /**< ESP32 soft-AP stop */
} network_ring_link_event_t;

struct ring_link_netif_driver
{
    esp_netif_driver_base_t base; /*!< base structure reserved as esp-netif driver */
    void *interface;    /*!< handle of driver implementation */
};

typedef struct ring_link_netif_driver *ring_link_netif_driver_t;

esp_netif_t* ring_link_netif_new(const esp_netif_config_t* config);

esp_err_t ring_link_netif_esp_netif_attach(esp_netif_t *esp_netif, esp_err_t (*post_attach_callback)(esp_netif_t *, void *));

/* Validate the IP packet against the physical frame before allocating/copying. */
bool ring_link_netif_ip_length(const ring_link_payload_t *payload, uint16_t *length);

/* The physical driver consumes the frame synchronously; no MTU-sized frame is
 * placed on lwIP's TCP/IP task stack. */
esp_err_t ring_link_netif_transmit(const void *buffer, size_t len, ring_link_payload_id_t id);

#ifdef __cplusplus
}
#endif
