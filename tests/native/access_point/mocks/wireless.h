#ifndef TEST_ACCESS_POINT_WIRELESS_H
#define TEST_ACCESS_POINT_WIRELESS_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_WIFI_NOT_STARTED -2
#define ESP_ERROR_CHECK(value) assert((value)==ESP_OK)
const char *esp_err_to_name(esp_err_t value);
void wireless_log(const char *tag,const char *fmt,...);
#define ESP_LOGI(...) wireless_log(__VA_ARGS__)
#define ESP_LOGW(...) wireless_log(__VA_ARGS__)
#define ESP_LOGE(...) wireless_log(__VA_ARGS__)
typedef struct { int unused; } esp_netif_t;
typedef struct { uint32_t addr; } ip4_addr_t;
typedef struct { ip4_addr_t ip,gw,netmask; } esp_netif_ip_info_t;
typedef struct { struct { union { ip4_addr_t ip4; } u_addr; int type; } ip; } esp_netif_dns_info_t;
typedef union {
  struct {
    uint8_t ssid[32],ssid_len,password[64],channel,max_connection;
    int authmode;
    struct { bool required,capable; } pmf_cfg;
  } ap;
} wifi_config_t;
typedef struct { uint8_t mac[6],aid; bool is_mesh_child; } wifi_event_ap_staconnected_t;
typedef struct { uint8_t mac[6],aid; bool is_mesh_child; uint16_t reason; } wifi_event_ap_stadisconnected_t;
#define WIFI_IF_AP 1
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 1
typedef int esp_event_base_t;
typedef void (*esp_event_handler_t)(void *,esp_event_base_t,int32_t,void *);
#define WIFI_EVENT 1
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_AP_STACONNECTED 2
#define WIFI_EVENT_AP_STADISCONNECTED 3
#define ERR_OK 0
#define ESP_NETIF_OP_SET 1
#define ESP_NETIF_DOMAIN_NAME_SERVER 2
#define ESP_NETIF_DNS_MAIN 3
#define ESP_IPADDR_TYPE_V4 0
#define OFFER_DNS 1
typedef int dhcps_offer_t;
static inline uint32_t mock_ntohl(uint32_t v) { return ((v&255)<<24)|((v&65280)<<8)|((v>>8)&65280)|(v>>24); }
#define ntohl mock_ntohl
uint32_t ipaddr_addr(const char *value);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_err_t esp_wifi_set_config(int iface,const wifi_config_t *value);
esp_err_t esp_event_handler_register(esp_event_base_t base,int32_t id,esp_event_handler_t cb,void *arg);
esp_err_t esp_event_handler_unregister(esp_event_base_t base,int32_t id,esp_event_handler_t cb);
void node_traffic_start(esp_netif_t *value);
void node_traffic_stop(esp_netif_t *value);
esp_err_t esp_netif_dhcps_stop(esp_netif_t *value);
esp_err_t esp_netif_dhcps_start(esp_netif_t *value);
esp_err_t esp_netif_set_ip_info(esp_netif_t *value,const esp_netif_ip_info_t *info);
esp_err_t esp_netif_dhcps_option(esp_netif_t *value,int op,int option,void *data,uint32_t len);
esp_err_t esp_netif_set_dns_info(esp_netif_t *value,int type,const esp_netif_dns_info_t *dns);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_set_inactive_time(int iface,uint16_t seconds);
int tcpip_callback_wait(void (*cb)(void *),void *arg);
void esp_netif_destroy_default_wifi(esp_netif_t *value);
bool node_is_ap_locked(void);
esp_err_t esp_wifi_deauth_sta(uint16_t aid);
#endif
