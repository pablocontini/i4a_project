#ifndef NATIVE_PORTAL_ESP_WIFI_H
#define NATIVE_PORTAL_ESP_WIFI_H
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include "esp_err.h"
typedef struct {
    uint8_t ssid[33], bssid[6];
    int8_t rssi;
    uint8_t primary;
} wifi_ap_record_t;
typedef struct {
    struct {
        uint8_t ssid[32], bssid[6], password[64];
        bool bssid_set;
    } sta;
} wifi_config_t;
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
#define WIFI_REASON_ASSOC_TOOMANY 5
#define WIFI_IF_STA 0
esp_err_t esp_wifi_scan_start(const void *, bool);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *);
esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *);
esp_err_t esp_wifi_set_config(int, const wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);

#endif
