#ifndef TEST_WIRELESS_MOCKS_H
#define TEST_WIRELESS_MOCKS_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED -3
#define ESP_ERR_WIFI_NOT_STARTED -4
#define ESP_ERR_NVS_NO_FREE_PAGES -5
#define ESP_ERR_NVS_NEW_VERSION_FOUND -6
#define ESP_ERROR_CHECK(result) assert((result) == ESP_OK)
const char *esp_err_to_name(esp_err_t result);
void wireless_log(const char *tag, const char *format, ...);
#define ESP_LOGI(...) wireless_log(__VA_ARGS__)
#define ESP_LOGW(...) wireless_log(__VA_ARGS__)
#define ESP_LOGE(...) wireless_log(__VA_ARGS__)

typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define TASK_DEVICE_STACK 4096
#define TASK_DEVICE_PRIORITY 2
#define TASK_DEVICE_CORE 0
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
int xTaskCreatePinnedToCore(void (*task)(void *), const char *name, unsigned stack,
                           void *arg, unsigned priority, TaskHandle_t *handle, unsigned core);

typedef struct { uint32_t addr; } ip4_addr_t;
typedef struct { ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
typedef struct { int unused; } esp_netif_t;
typedef struct {
  uint8_t ssid[33], bssid[6];
  int8_t rssi;
  uint8_t primary;
} wifi_ap_record_t;
typedef union {
  struct { uint8_t ssid[32], bssid[6], password[64]; bool bssid_set; } sta;
  struct { uint8_t ssid[32], password[64]; } ap;
} wifi_config_t;
typedef struct { unsigned num; struct { int8_t rssi; } sta[1]; } wifi_sta_list_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_IF_STA 0
#define WIFI_MODE_AP 1
#define WIFI_MODE_STA 2
#define WIFI_PS_NONE 0
#define CONFIG_ORIENTATION_NORTH 0
#define CONFIG_ORIENTATION_SOUTH 1
#define CONFIG_ID_CENTER 4
typedef int esp_event_base_t;
#define WIFI_EVENT 1
#define IP_EVENT 2
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_DISCONNECTED 3
#define IP_EVENT_STA_GOT_IP 4
#define WIFI_REASON_ASSOC_TOOMANY 5
typedef struct { int reason; } wifi_event_sta_disconnected_t;
typedef struct { esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg);
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id, esp_event_handler_t handler);
esp_err_t esp_event_loop_create_default(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
void esp_netif_destroy_default_wifi(esp_netif_t *netif);
esp_err_t esp_netif_init(void);
esp_err_t esp_netif_dhcpc_start(esp_netif_t *netif);
esp_err_t esp_netif_dhcpc_stop(esp_netif_t *netif);
esp_err_t esp_netif_set_ip_info(esp_netif_t *netif, const esp_netif_ip_info_t *info);
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_ps(int mode);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_scan_start(void *config, bool block);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count);
esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *record);
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *config);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *list);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *record);
esp_err_t esp_wifi_set_max_tx_power(int8_t power);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
#define ERR_OK 0
int tcpip_callback_wait(void (*callback)(void *), void *arg);
static inline uint32_t mock_ntohl(uint32_t v) {
  return ((v & 255) << 24) | ((v & 65280) << 8) | ((v >> 8) & 65280) | (v >> 24);
}
#define ntohl mock_ntohl
#define htonl mock_ntohl
void node_traffic_start(esp_netif_t *netif);
void node_traffic_stop(esp_netif_t *netif);
bool cm_is_blocked_uuid(const char *ssid);
void cm_block_full_ap(const char *ssid);
void im_http_client_start(void);
void pm_reset_priority_list(void);
void pm_provide_to_siblings(int8_t rssi);
uint8_t pm_get_suggested_priority(void);
#endif
