#ifndef NATIVE_PORTAL_ESP_MAC_H
#define NATIVE_PORTAL_ESP_MAC_H
#include <stdint.h>
#define ESP_MAC_WIFI_SOFTAP 1
void esp_read_mac(uint8_t *mac, int type);

#endif
