#ifndef NATIVE_PORTAL_ESP_EVENT_H
#define NATIVE_PORTAL_ESP_EVENT_H
#include <stdint.h>
#include "esp_err.h"
typedef int esp_event_base_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_DISCONNECTED 3
#define IP_EVENT_STA_GOT_IP 4
#define ESP_EVENT_ANY_ID (-1)
esp_err_t esp_event_handler_register(esp_event_base_t, int32_t,
                                    esp_event_handler_t, void *);
esp_err_t esp_event_handler_unregister(esp_event_base_t, int32_t,
                                      esp_event_handler_t);

#endif
