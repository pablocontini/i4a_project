#include <stdio.h>
#include <string.h>
#include "wireless.h"
#include "../../../components/wireless/access_point/access_point.c"

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static unsigned server_creates, server_closes, deauths;
static uint16_t deauth_aid;
static bool locked;
static esp_err_t deauth_result;
static esp_netif_t mock_netif;
static const uint8_t original[6] = {1,2,3,4,5,6};
static const uint8_t newcomer[6] = {6,5,4,3,2,1};

void wireless_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
const char *esp_err_to_name(esp_err_t result) { (void)result; return "mock error"; }
void server_create(void) { ++server_creates; }
void server_close(void) { ++server_closes; }
bool node_is_ap_locked(void) { return locked; }
esp_err_t esp_wifi_deauth_sta(uint16_t aid) { ++deauths; deauth_aid=aid; return deauth_result; }
esp_netif_t *esp_netif_create_default_wifi_ap(void) { return &mock_netif; }
esp_err_t esp_wifi_set_config(int iface,const wifi_config_t *value) { (void)iface;(void)value;return ESP_OK; }
esp_err_t esp_event_handler_register(esp_event_base_t base,int32_t id,esp_event_handler_t cb,void *arg) {
  (void)base;(void)id;(void)cb;(void)arg;return ESP_OK;
}
esp_err_t esp_event_handler_unregister(esp_event_base_t base,int32_t id,esp_event_handler_t cb) {
  (void)base;(void)id;(void)cb;return ESP_OK;
}
void node_traffic_start(esp_netif_t *value) { (void)value; }
void node_traffic_stop(esp_netif_t *value) { (void)value; }
uint32_t ipaddr_addr(const char *value) { (void)value; return 0; }
esp_err_t esp_netif_dhcps_stop(esp_netif_t *value) { (void)value;return ESP_OK; }
esp_err_t esp_netif_dhcps_start(esp_netif_t *value) { (void)value;return ESP_OK; }
esp_err_t esp_netif_set_ip_info(esp_netif_t *value,const esp_netif_ip_info_t *info) { (void)value;(void)info;return ESP_OK; }
esp_err_t esp_netif_dhcps_option(esp_netif_t *value,int op,int option,void *data,uint32_t len) {
  (void)value;(void)op;(void)option;(void)data;(void)len;return ESP_OK;
}
esp_err_t esp_netif_set_dns_info(esp_netif_t *value,int type,const esp_netif_dns_info_t *dns) {
  (void)value;(void)type;(void)dns;return ESP_OK;
}
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_stop(void) { return ESP_OK; }
esp_err_t esp_wifi_set_inactive_time(int iface,uint16_t seconds) { (void)iface;(void)seconds;return ESP_OK; }
int tcpip_callback_wait(void (*cb)(void *),void *arg) { cb(arg);return ERR_OK; }
void esp_netif_destroy_default_wifi(esp_netif_t *value) { (void)value; }

static void connected(AccessPoint *ap,const uint8_t *mac,uint8_t aid) {
  wifi_event_ap_staconnected_t event={.aid=aid};
  memcpy(event.mac,mac,sizeof(event.mac));
  ap_event_handler(ap,WIFI_EVENT,WIFI_EVENT_AP_STACONNECTED,&event);
}
static void disconnected(AccessPoint *ap,const uint8_t *mac,uint8_t aid) {
  wifi_event_ap_stadisconnected_t event={.aid=aid};
  memcpy(event.mac,mac,sizeof(event.mac));
  ap_event_handler(ap,WIFI_EVENT,WIFI_EVENT_AP_STADISCONNECTED,&event);
}
static AccessPoint reset(bool center) {
  AccessPoint ap={0};
  ap_init(&ap,1,"test","",1,center,false);
  ap.state=active;
  server_creates=server_closes=deauths=deauth_aid=0;
  locked=false;deauth_result=ESP_OK;
  return ap;
}

static int test_unrelated_disconnect_preserves_peer(void) {
  AccessPoint ap=reset(false);
  connected(&ap,original,1);
  CHECK(ap.server_is_up && server_creates==1);
  disconnected(&ap,newcomer,2);
  CHECK(ap.server_is_up && server_closes==0);
  /* A reused AID must still be matched against the station MAC. */
  disconnected(&ap,newcomer,1);
  CHECK(ap.server_is_up && server_closes==0);
  disconnected(&ap,original,1);
  CHECK(!ap.server_is_up && server_closes==1);
  return 0;
}
static int test_locked_ap_rejects_only_new_association(void) {
  AccessPoint ap=reset(false);
  connected(&ap,original,1);
  locked=true;
  connected(&ap,newcomer,2);
  CHECK(deauths==1 && deauth_aid==2 && ap.server_is_up && server_closes==0);
  disconnected(&ap,newcomer,2);
  CHECK(ap.server_is_up && server_closes==0);
  return 0;
}
static int test_busy_lateral_preserves_existing_session(void) {
  AccessPoint ap=reset(false);
  connected(&ap,original,1);
  connected(&ap,newcomer,2);
  CHECK(deauths==1 && deauth_aid==2 && server_creates==1 && server_closes==0);
  disconnected(&ap,newcomer,2);
  CHECK(ap.server_is_up && server_closes==0);
  return 0;
}
static int test_reject_race_does_not_abort_or_deauth_everyone(void) {
  AccessPoint ap=reset(false);
  connected(&ap,original,1);
  locked=true;deauth_result=ESP_ERR_WIFI_NOT_STARTED;
  connected(&ap,newcomer,2);
  CHECK(ap.server_is_up && deauths==1 && deauth_aid==2);
  connected(&ap,newcomer,0);
  CHECK(deauths==1);
  ap_event_handler(&ap,WIFI_EVENT,WIFI_EVENT_AP_STACONNECTED,NULL);
  ap_event_handler(&ap,WIFI_EVENT,WIFI_EVENT_AP_STADISCONNECTED,NULL);
  CHECK(ap.server_is_up && !server_closes && deauths==1);
  return 0;
}
static int test_center_clients_do_not_start_peer_server(void) {
  AccessPoint ap=reset(true);
  connected(&ap,original,1);
  connected(&ap,newcomer,2);
  disconnected(&ap,original,1);
  CHECK(!server_creates && !server_closes && !deauths);
  return 0;
}
static int test_matching_disconnect_allows_another_peer(void) {
  AccessPoint ap=reset(false);
  connected(&ap,original,1);
  disconnected(&ap,original,1);
  connected(&ap,newcomer,2);
  CHECK(ap.server_is_up && server_creates==2 && server_closes==1 && !deauths);
  disconnected(&ap,original,1);
  CHECK(ap.server_is_up && server_closes==1);
  disconnected(&ap,newcomer,2);
  CHECK(!ap.server_is_up && server_closes==2);
  return 0;
}
static int test_duplicate_connect_does_not_restart_server(void) {
  AccessPoint ap=reset(false);
  connected(&ap,original,1);
  connected(&ap,original,2);
  CHECK(server_creates==1 && !deauths && ap.server_is_up);
  disconnected(&ap,original,1);
  CHECK(!server_closes && ap.server_is_up);
  disconnected(&ap,original,2);
  CHECK(server_closes==1 && !ap.server_is_up);
  return 0;
}
int main(void) {
  int result;
#define RUN(test) do { result=test(); if(result) { fprintf(stderr,#test " failed: %d\n",result);return 1; } puts(#test ": OK"); } while(0)
  RUN(test_unrelated_disconnect_preserves_peer);
  RUN(test_locked_ap_rejects_only_new_association);
  RUN(test_busy_lateral_preserves_existing_session);
  RUN(test_reject_race_does_not_abort_or_deauth_everyone);
  RUN(test_center_clients_do_not_start_peer_server);
  RUN(test_matching_disconnect_allows_another_peer);
  RUN(test_duplicate_connect_does_not_restart_server);
  return 0;
}
