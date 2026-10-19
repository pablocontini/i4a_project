#include <stdio.h>
#include <string.h>
#include "wireless.h"

/* Exercise the real private task and event handler with deterministic callbacks. */
#define LOGGING_TAG station_log_tag
#include "../../../components/wireless/station/station.c"
#undef LOGGING_TAG
#define LOGGING_TAG device_log_tag
#include "../../../components/wireless/device/device.c"
#undef LOGGING_TAG

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static esp_netif_t netif;
static esp_err_t dhcp_result, scan_result, disconnect_result;
static unsigned connects, disconnects, scans, records, client_closes;
static unsigned provisions, deleted_self, deleted_other, lock_depth;
static unsigned cancel_scan, lock_scan;
static bool finish_on_wait, cancel_on_delay;
static unsigned ap_associations;
static esp_err_t ap_list_result;

const char *esp_err_to_name(esp_err_t result) { (void)result; return "mock error"; }
void wireless_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
void vTaskDelay(TickType_t ticks) {
  (void)ticks;
  if (finish_on_wait) { station_task_handle = NULL; finish_on_wait = false; }
  if (cancel_on_delay) station_task_stop_requested = true;
}
void vTaskDelete(TaskHandle_t task) {
  assert(!lock_depth);
  if (task) ++deleted_other; else ++deleted_self;
}
int xTaskCreatePinnedToCore(void (*task)(void *), const char *name, unsigned stack,
                           void *arg, unsigned priority, TaskHandle_t *handle, unsigned core) {
  (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)core;
  *handle = (TaskHandle_t)1;
  return pdPASS;
}
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg) {
  (void)base; (void)id; (void)handler; (void)arg; return ESP_OK;
}
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id, esp_event_handler_t handler) {
  (void)base; (void)id; (void)handler; return ESP_OK;
}
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &netif; }
void esp_netif_destroy_default_wifi(esp_netif_t *value) { (void)value; }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_netif_dhcpc_start(esp_netif_t *value) { (void)value; return dhcp_result; }
esp_err_t esp_netif_dhcpc_stop(esp_netif_t *value) { (void)value; return ESP_OK; }
esp_err_t esp_netif_set_ip_info(esp_netif_t *value, const esp_netif_ip_info_t *info) {
  (void)value; (void)info; return ESP_OK;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *config) { (void)config; return ESP_OK; }
esp_err_t esp_wifi_set_ps(int mode) { (void)mode; return ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) { (void)mode; return ESP_OK; }
esp_err_t esp_wifi_scan_start(void *config, bool block) {
  (void)config; (void)block; ++scans; records = 0; return scan_result;
}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count) { *count = 1; return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *record) {
  if (records++) return ESP_FAIL;
  memset(record, 0, sizeof(*record));
  memcpy(record->ssid, "I4A_S_B00000000000", 18);
  record->rssi = -40;
  return ESP_OK;
}
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *config) {
  (void)iface; (void)config; return ESP_OK;
}
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_stop(void) { return ESP_OK; }
esp_err_t esp_wifi_connect(void) { ++connects; return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { ++disconnects; return disconnect_result; }
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *list) {
  list->num = ap_associations; return ap_list_result;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *record) { (void)record; return ESP_FAIL; }
esp_err_t esp_wifi_set_max_tx_power(int8_t power) { (void)power; return ESP_OK; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
int tcpip_callback_wait(void (*callback)(void *), void *arg) { callback(arg); return ERR_OK; }
void node_traffic_start(esp_netif_t *value) { (void)value; }
void node_traffic_stop(esp_netif_t *value) { (void)value; }
bool cm_is_blocked_uuid(const char *ssid) {
  (void)ssid;
  ++lock_depth;
  if (scans == cancel_scan) station_task_stop_requested = true;
  if (scans == lock_scan) {
    /* A sibling can lock STA while the blocking scan is in progress. */
    extern Device fixture_device;
    fixture_device.sta_lock = true;
    cancel_on_delay = true;
  }
  --lock_depth;
  return false;
}
void cm_block_full_ap(const char *ssid) { (void)ssid; }
void im_http_client_start(void) {}
void pm_reset_priority_list(void) {}
void pm_provide_to_siblings(int8_t rssi) { (void)rssi; ++provisions; }
uint8_t pm_get_suggested_priority(void) { return 0; }
void client_open(void) {}
void client_close(void) { ++client_closes; }
void client_wait_stopped(void) {}
bool client_send_message(const uint8_t *data, uint16_t len) { (void)data; (void)len; return false; }
void server_close(void) {}
void server_wait_stopped(void) {}
bool server_send_message(const uint8_t *data, uint16_t len) { (void)data; (void)len; return false; }
void ap_init(AccessPointPtr ap, uint8_t channel, const char *ssid, const char *password,
             uint8_t maximum, bool center, bool apsta) {
  (void)ap; (void)channel; (void)ssid; (void)password; (void)maximum; (void)center; (void)apsta;
}
bool ap_is_initialized(AccessPointPtr ap) { return ap->initialized; }
void ap_set_network(AccessPointPtr ap, const char *ip, const char *gateway, const char *mask) {
  (void)ap; (void)ip; (void)gateway; (void)mask;
}
void ap_start(AccessPointPtr ap) { (void)ap; }
void ap_stop(AccessPointPtr ap) { (void)ap; }
void ap_restart(AccessPointPtr ap) { (void)ap; }
void ap_destroy_netif(AccessPointPtr ap) { (void)ap; }
void ap_disconnect_all_stations(AccessPointPtr ap) { (void)ap; }

Device fixture_device;
static Station *reset(void) {
  memset(&fixture_device, 0, sizeof(fixture_device));
  fixture_device.station_ptr = &fixture_device.station;
  fixture_device.access_point_ptr = &fixture_device.access_point;
  station_init(fixture_device.station_ptr, "I4A", 0, "A00000000000", "password", false);
  dhcp_result = scan_result = disconnect_result = ESP_OK;
  connects = disconnects = scans = records = client_closes = 0;
  provisions = deleted_self = deleted_other = lock_depth = 0;
  cancel_scan = lock_scan = 0;
  ap_associations = 0;
  ap_list_result = ESP_OK;
  station_task_handle = (TaskHandle_t)1;
  station_task_stop_requested = finish_on_wait = cancel_on_delay = false;
  s_retry_num = 0;
  return fixture_device.station_ptr;
}

static int test_dhcp_retry_and_disconnect_without_event(void) {
  Station *station = reset();
  station_connect(station);
  wifi_event_sta_disconnected_t event = {0};
  for (unsigned retry = 0; retry <= MAX_RETRIES; ++retry)
    event_handler(station, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
  CHECK(!station_is_active(station));
  dhcp_result = ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED;
  station_connect(station);
  CHECK(station_is_active(station) && connects == MAX_RETRIES + 2);
  station->is_fully_connected = true;
  disconnect_result = ESP_ERR_WIFI_NOT_STARTED;
  station_disconnect(station);
  CHECK(!station_is_active(station) && !station->is_fully_connected && client_closes == 1);
  station_disconnect(station);
  CHECK(disconnects == 1);
  dhcp_result = ESP_FAIL;
  station_connect(station);
  CHECK(!station_is_active(station) && connects == MAX_RETRIES + 2);
  return 0;
}

static int test_failed_scan_does_not_reuse_old_result(void) {
  Station *station = reset();
  station->ap_found = true;
  scan_result = ESP_FAIL;
  station_find_ap(station);
  CHECK(!station_found_ap(station) && records == 0);
  CHECK(station_scan_best_rssi(station) == -128 && records == 0);
  return 0;
}

static int test_cancel_during_scans_leaves_locks_and_does_not_connect(void) {
  reset();
  cancel_scan = 1;
  device_connect_station_task(&fixture_device);
  CHECK(!station_task_handle && deleted_self == 1 && deleted_other == 0);
  CHECK(provisions == 0 && connects == 0 && lock_depth == 0);
  reset();
  cancel_scan = 2;
  device_connect_station_task(&fixture_device);
  CHECK(scans == 2 && provisions == 1 && connects == 0);
  CHECK(!station_task_handle && deleted_self == 1 && deleted_other == 0);
  reset();
  lock_scan = 2;
  device_connect_station_task(&fixture_device);
  CHECK(fixture_device.sta_lock && connects == 0);
  CHECK(!station_task_handle && deleted_self == 1 && deleted_other == 0);
  return 0;
}

static int test_stop_waits_for_worker_instead_of_deleting_it(void) {
  reset();
  finish_on_wait = true;
  device_disconnect_station(&fixture_device);
  CHECK(station_task_stop_requested && !station_task_handle && !finish_on_wait);
  CHECK(deleted_other == 0);
  return 0;
}

static int test_unconfigured_or_stopped_wifi_cannot_capture_internet(void) {
  Station *station = reset();
  fixture_device.mode = STATION;
  fixture_device.state = d_active;
  station->state = s_active;
  station->is_fully_connected = true;
  CHECK(!device_is_point_to_point_message(&fixture_device, 0x08080808));
  station->sta_subnet = 0x0A000000;
  station->sta_mask = 0xFFFFFFFC;
  CHECK(device_is_point_to_point_message(&fixture_device, 0x0A000001));
  CHECK(!device_is_point_to_point_message(&fixture_device, 0x08080808));
  station->sta_mask = 0xFF00FF00; /* A malformed mask must never match. */
  CHECK(!device_is_point_to_point_message(&fixture_device, 0x0A000001));
  station->sta_mask = 0xFFFFFFFC;
  station_disconnect(station);
  CHECK(!device_is_point_to_point_message(&fixture_device, 0x0A000001));
  fixture_device.mode = AP;
  AccessPointPtr ap = fixture_device.access_point_ptr;
  ap->initialized = true;
  ap->state = active;
  ap->netif = &netif;
  CHECK(!device_is_point_to_point_message(&fixture_device, 0x08080808));
  ap->ap_subnet = 0x0A000000;
  ap->ap_mask = 0xFFFFFFFC;
  CHECK(device_is_point_to_point_message(&fixture_device, 0x0A000001));
  ap->stopping = true;
  CHECK(!device_is_point_to_point_message(&fixture_device, 0x0A000001));
  ap->stopping = false;
  ap->netif = NULL;
  CHECK(!device_is_point_to_point_message(&fixture_device, 0x0A000001));
  CHECK(!device_is_point_to_point_message(NULL, 0x08080808));
  return 0;
}

static int test_ap_search_preserves_provisional_associations(void) {
  reset();
  CHECK(!device_ap_can_search(NULL));
  fixture_device.mode = AP;
  fixture_device.state = d_active;
  AccessPointPtr ap = fixture_device.access_point_ptr;
  ap->initialized = true;
  ap->state = active;
  ap->netif = &netif;
  CHECK(device_ap_can_search(&fixture_device));
  ap_associations = 1; /* DHCP/TCP admission is still pending, server is down. */
  CHECK(!device_ap_can_search(&fixture_device));
  ap_associations = 0;
  ap_list_result = ESP_FAIL;
  CHECK(!device_ap_can_search(&fixture_device));
  ap_list_result = ESP_OK;
  ap->server_is_up = true;
  CHECK(!device_ap_can_search(&fixture_device));
  ap->server_is_up = false;
  ap->stopping = true;
  CHECK(!device_ap_can_search(&fixture_device));
  ap->stopping = false;
  fixture_device.mode = STATION;
  CHECK(!device_ap_can_search(&fixture_device));
  return 0;
}

int main(void) {
  int result;
#define RUN(test) do { result = test(); if (result) { \
  fprintf(stderr, #test ": failed at line %d\n", result); return 1; } \
  puts(#test ": OK"); } while (0)
  RUN(test_dhcp_retry_and_disconnect_without_event);
  RUN(test_failed_scan_does_not_reuse_old_result);
  RUN(test_cancel_during_scans_leaves_locks_and_does_not_connect);
  RUN(test_stop_waits_for_worker_instead_of_deleting_it);
  RUN(test_unconfigured_or_stopped_wifi_cannot_capture_internet);
  RUN(test_ap_search_preserves_provisional_associations);
  return 0;
}
