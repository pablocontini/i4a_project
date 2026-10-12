#ifndef I4A_TEST_NETIF_OUTPUT_COMMON_H
#define I4A_TEST_NETIF_OUTPUT_COMMON_H
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PRIu32 "u"

typedef int err_t;
typedef int esp_err_t;
typedef int esp_netif_recv_ret_t;
typedef uint16_t u16_t;
typedef const char *esp_event_base_t;
typedef struct { uint32_t addr; } ip4_addr_t;
typedef ip4_addr_t esp_ip4_addr_t;
typedef struct { int handle; } esp_netif_t;
struct pbuf { void *payload; u16_t len, tot_len; struct pbuf *next; };
struct ip_hdr { uint8_t version; uint8_t unused; uint16_t length; };
struct netif {
    char name[2];
    u16_t mtu;
    unsigned hwaddr_len, flags;
    err_t (*output)(struct netif *, struct pbuf *, const ip4_addr_t *);
    err_t (*linkoutput)(struct netif *, struct pbuf *);
};
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_VAL -6
#define ERR_IF -12
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_NETIF_OPTIONAL_RETURN_CODE(code) (code)
#define IPH_V(header) ((header)->version >> 4)
#define IPH_LEN(header) ((header)->length)
#define IP_HLEN 20
#define lwip_ntohs(value) (value)
#define likely(value) (value)
#define LWIP_ASSERT(message, condition) assert(condition)
#define LWIP_DEBUGF(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_ERROR_CHECK(result) assert((result) == ESP_OK)
#define ESP_EVENT_DEFINE_BASE(name) const char *name = #name
#define ESP_NETIF_FLAG_AUTOUP 1
#define ESP_COMPILER_DESIGNATED_INIT_AGGREGATE_TYPE_EMPTY(name) .name = {0},
#define ETH_HWADDR_LEN 6
#define NETIF_FLAG_BROADCAST 1
#define NETIF_FLAG_LINK_UP 2
#define PBUF_RAW_TX 1
#define PBUF_TRANSPORT 2
#define PBUF_RAM 1
#define RING_LINK_NETIF_MTU 1500
#define RING_LINK_PAYLOAD_TTL 5
#define CONFIG_ID_ANY 0xff
#define RING_LINK_PAYLOAD_TYPE_ESP_NETIF 1
#define RING_LINK_EVENT_START 1
#define ESP_MAC_WIFI_SOFTAP 1
#define portMAX_DELAY 0xffffffff

typedef uint8_t ring_link_payload_id_t;
typedef struct {
    uint8_t id, ttl, src_id, dst_id, buffer_type;
    u16_t len;
    uint8_t buffer[RING_LINK_NETIF_MTU];
} ring_link_payload_t;
typedef struct { esp_ip4_addr_t ip; } esp_netif_ip_info_t;
typedef struct { esp_netif_t *netif; } esp_netif_driver_base_t;
struct ring_link_netif_driver { esp_netif_driver_base_t base; };
typedef struct ring_link_netif_driver *ring_link_netif_driver_t;
struct esp_netif_netstack_config {
    struct {
        err_t (*init_fn)(struct netif *);
        esp_netif_recv_ret_t (*input_fn)(void *, void *, size_t, void *);
    } lwip;
};
typedef struct {
    unsigned flags;
    uint8_t mac[6];
    esp_netif_ip_info_t ip_info;
    int get_ip_event, lost_ip_event;
    const char *if_key, *if_desc;
    int route_prio;
    void *bridge_info;
} esp_netif_inherent_config_t;
typedef struct {
    const esp_netif_inherent_config_t *base;
    void *driver;
    const struct esp_netif_netstack_config *stack;
} esp_netif_config_t;
typedef struct {
    void *handle;
    esp_err_t (*transmit)(void *, void *, size_t);
    esp_err_t (*transmit_wrap)(void *, void *, size_t, void *);
    void (*driver_free_rx_buffer)(void *, void *);
} esp_netif_driver_ifconfig_t;

struct pbuf *pbuf_alloc(int layer, u16_t length, int kind);
err_t pbuf_copy(struct pbuf *destination, const struct pbuf *source);
unsigned pbuf_free(struct pbuf *p);
esp_netif_t *esp_netif_get_handle_from_netif_impl(struct netif *netif);
esp_err_t esp_netif_transmit(esp_netif_t *netif, void *buffer, size_t len);
static inline void ip4_debug_print(struct pbuf *p) { (void)p; }
static inline uint8_t config_get_id(void) { return 4; }
static inline esp_err_t ring_link_lowlevel_transmit_payload(ring_link_payload_t *p) { (void)p; return ESP_OK; }
static inline esp_err_t esp_netif_receive(esp_netif_t *n, void *b, size_t s, void *l) { (void)n; (void)b; (void)s; (void)l; return ESP_OK; }
static inline esp_netif_t *ring_link_netif_new(const esp_netif_config_t *c) { (void)c; return NULL; }
static inline esp_err_t ring_link_netif_esp_netif_attach(esp_netif_t *n, esp_err_t (*f)(esp_netif_t *, void *)) { (void)n; (void)f; return ESP_OK; }
static inline esp_err_t esp_netif_set_driver_config(esp_netif_t *n, const esp_netif_driver_ifconfig_t *c) { (void)n; (void)c; return ESP_OK; }
static inline esp_err_t esp_read_mac(uint8_t *m, int type) { memset(m, 0, 6); (void)type; return ESP_OK; }
static inline esp_err_t esp_netif_set_mac(esp_netif_t *n, const uint8_t *m) { (void)n; (void)m; return ESP_OK; }
static inline esp_err_t esp_netif_set_ip_info(esp_netif_t *n, const esp_netif_ip_info_t *i) { (void)n; (void)i; return ESP_OK; }
static inline esp_err_t esp_netif_get_ip_info(esp_netif_t *n, esp_netif_ip_info_t *i) { (void)n; i->ip.addr = 0; return ESP_OK; }
static inline esp_err_t esp_netif_set_default_netif(esp_netif_t *n) { (void)n; return ESP_OK; }
static inline esp_netif_ip_info_t config_get_tx_ip_info(void) { esp_netif_ip_info_t i = {{0}}; return i; }
static inline esp_netif_ip_info_t config_get_rx_ip_info(void) { return config_get_tx_ip_info(); }
static inline void esp_netif_action_start(esp_netif_t *n, esp_event_base_t b, int32_t e, void *d) { (void)n; (void)b; (void)e; (void)d; }
static inline void esp_netif_action_got_ip(esp_netif_t *n, esp_event_base_t b, int32_t e, void *d) { (void)n; (void)b; (void)e; (void)d; }
static inline esp_err_t esp_event_handler_instance_register(esp_event_base_t b, int32_t e, void (*f)(void *, esp_event_base_t, int32_t, void *), void *a, void *i) { (void)b; (void)e; (void)f; (void)a; (void)i; return ESP_OK; }
static inline esp_err_t esp_event_post(esp_event_base_t b, int32_t e, void *d, size_t s, unsigned t) { (void)b; (void)e; (void)d; (void)s; (void)t; return ESP_OK; }
#endif
