#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_netif.h"

void node_set_network_settings(uint32_t network, uint32_t mask);
bool node_is_point_to_point_message(uint32_t dst);
bool node_is_packet_for_this_subnet(uint32_t dst);
esp_netif_t *node_get_wifi_netif(void);
esp_netif_t *node_get_spi_netif(void);
