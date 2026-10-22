/* Production station scanning, including orientation and RSSI admission. */
#include <stdio.h>
#include <string.h>
#include "../../../components/wireless/station/station.c"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static wifi_ap_record_t records[12];
static unsigned record_count, cursor;
static esp_netif_t netif;
static const char *blocked_uuid;
static unsigned wifi_connects;
static unsigned client_closes, blocked_full_aps;
static bool accepted_offer;

void node_set_peer_offer_accepted(bool accepted) { accepted_offer = accepted; }

esp_err_t esp_wifi_scan_start(const void *config, bool blocking) {
    (void)config; (void)blocking; cursor = 0; return ESP_OK;
}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number) {
    *number = (uint16_t)record_count; return ESP_OK;
}
esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *record) {
    if (cursor == record_count) return ESP_FAIL;
    *record = records[cursor++]; return ESP_OK;
}
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *cfg) {
    (void)iface; (void)cfg; return ESP_OK;
}
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_stop(void) { return ESP_OK; }
esp_err_t esp_wifi_connect(void) { ++wifi_connects; return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &netif; }
esp_err_t esp_netif_dhcpc_start(esp_netif_t *iface) { (void)iface; return ESP_OK; }
esp_err_t esp_netif_dhcpc_stop(esp_netif_t *iface) { (void)iface; return ESP_OK; }
esp_err_t esp_netif_set_ip_info(esp_netif_t *iface, const esp_netif_ip_info_t *info) {
    iface->ip = *info; return ESP_OK;
}
void esp_netif_destroy_default_wifi(esp_netif_t *iface) { (void)iface; }
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t event,
                                    esp_event_handler_t fn, void *context) {
    (void)base; (void)event; (void)fn; (void)context; return ESP_OK;
}
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t event,
                                      esp_event_handler_t fn) {
    (void)base; (void)event; (void)fn; return ESP_OK;
}
int tcpip_callback_wait(void (*callback)(void *), void *context) {
    callback(context); return ERR_OK;
}
void client_open(void) { }
void client_close(void) { ++client_closes; }
void im_http_client_start(void) { }
void node_traffic_start(esp_netif_t *iface) { (void)iface; }
void node_traffic_stop(esp_netif_t *iface) { (void)iface; }
bool cm_is_blocked_uuid(const char *ssid) {
    return blocked_uuid != NULL && strstr(ssid, blocked_uuid) != NULL;
}
void cm_block_full_ap(const char *ssid) { (void)ssid; ++blocked_full_aps; }

static void clear_scan(void) {
    memset(records, 0, sizeof(records));
    record_count = cursor = wifi_connects = 0;
    client_closes = blocked_full_aps = 0;
    accepted_offer = false;
    blocked_uuid = NULL;
}
static void add_ap(const char *ssid, int8_t rssi, uint8_t channel) {
    wifi_ap_record_t *record = &records[record_count++];
    strcpy((char *)record->ssid, ssid);
    record->rssi = rssi;
    record->primary = channel;
    record->bssid[5] = (uint8_t)record_count;
}
static Station fresh_station(uint8_t id, int8_t threshold) {
    Station station = {0};
    station_init(&station, "I4A", id, "A00000000000", "link-password", true);
    station_set_rssi_threshold(&station, threshold);
    return station;
}
static int threshold_boundary_and_repeated_scans(void) {
    clear_scan();
    Station station = fresh_station(CONFIG_ID_EAST, -65);
    add_ap("I4A_W_B00000000000", -66, 6);
    station_find_ap(&station);
    CHECK(!station.ap_found);
    CHECK(station_scan_best_rssi(&station) == -128);
    CHECK(station.rssi_threshold_dbm == -65);
    records[0].rssi = -65;
    station_find_ap(&station);
    CHECK(station.ap_found);
    CHECK(station_scan_best_rssi(&station) == -65);
    CHECK(station.rssi_threshold_dbm == -65);
    CHECK(station.wifi_config.sta.bssid_set);
    CHECK(station.wifi_config.sta.bssid[5] == 1);
    records[0].rssi = -66;
    station_find_ap(&station);
    CHECK(!station.ap_found);
    CHECK(station.rssi_threshold_dbm == -65);
    CHECK(wifi_connects == 0);
    return 0;
}
static int strongest_candidate_must_also_pass_identity_and_axis(void) {
    clear_scan();
    Station station = fresh_station(CONFIG_ID_NORTH, -80);
    blocked_uuid = "B00000000000";
    add_ap("I4A_N_A00000000000", -20, 1);
    add_ap("I4A_S_B00000000000", -21, 2);
    add_ap("I4A_E_C00000000000", -22, 3);
    add_ap("OTHER_N_D00000000000", -23, 4);
    add_ap("I4A_S_E00000000000", -81, 5);
    add_ap("I4A_N_F00000000000", -79, 6);
    add_ap("I4A_S_100000000000", -70, 7);
    station_find_ap(&station);
    CHECK(station.ap_found);
    CHECK(strcmp((const char *)station.wifi_ap_found.ssid, "I4A_S_100000000000") == 0);
    CHECK(station_scan_best_rssi(&station) == -70);
    CHECK(station.rssi_threshold_dbm == -80);
    return 0;
}
static int thresholds_belong_to_each_radio_and_invalid_values_are_ignored(void) {
    clear_scan();
    Station east = fresh_station(CONFIG_ID_EAST, -60);
    Station west = fresh_station(CONFIG_ID_WEST, -75);
    add_ap("I4A_E_B00000000000", -70, 6);
    station_find_ap(&east);
    station_find_ap(&west);
    CHECK(!east.ap_found && west.ap_found);
    station_set_rssi_threshold(&west, 0);
    station_set_rssi_threshold(&west, 1);
    station_set_rssi_threshold(NULL, -80);
    CHECK(west.rssi_threshold_dbm == -75 && east.rssi_threshold_dbm == -60);
    station_set_rssi_threshold(&east, -128);
    records[0].rssi = -127;
    station_find_ap(&east);
    CHECK(east.ap_found && east.rssi_threshold_dbm == -128);
    CHECK(west.rssi_threshold_dbm == -75);
    return 0;
}
static int disconnect_callback_preserves_stability_branch_bounded_retries(void) {
    clear_scan();
    Station station = fresh_station(CONFIG_ID_EAST, -65);
    add_ap("I4A_W_B00000000000", -60, 6);
    station_find_ap(&station);
    CHECK(station.ap_found);
    station_connect(&station);
    CHECK(wifi_connects == 1);
    station.is_fully_connected = true;
    station.sta_subnet = 0x0a600000;
    station.sta_mask = 0xfffffffcu;
    accepted_offer = true;
    s_retry_num = 3;
    wifi_event_sta_disconnected_t disconnected = { .reason = 0 };
    event_handler(&station, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnected);
    CHECK(wifi_connects == 2 && client_closes == 1);
    CHECK(!station.is_fully_connected && s_retry_num == 4);
    CHECK(station.sta_subnet == 0x0a600000 && station.sta_mask == 0xfffffffcu);
    CHECK(station.rssi_threshold_dbm == -65);
    /* Preserve the existing bounded reconnection policy of this branch. */
    event_handler(&station, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnected);
    CHECK(wifi_connects == 3 && client_closes == 1);
    disconnected.reason = WIFI_REASON_ASSOC_TOOMANY;
    event_handler(&station, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnected);
    CHECK(wifi_connects == 3 && blocked_full_aps == 1);
    CHECK(station.state == s_inactive);
    return 0;
}
int main(void) {
    int (*cases[])(void) = {
        threshold_boundary_and_repeated_scans,
        strongest_candidate_must_also_pass_identity_and_axis,
        thresholds_belong_to_each_radio_and_invalid_values_are_ignored,
        disconnect_callback_preserves_stability_branch_bounded_retries
    };
    const char *names[] = {
        "threshold_boundary_and_repeated_scans",
        "strongest_candidate_must_also_pass_identity_and_axis",
        "thresholds_belong_to_each_radio_and_invalid_values_are_ignored",
        "disconnect_callback_preserves_stability_branch_bounded_retries"
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int line = cases[i]();
        if (line) { printf("%s failed at station_test.c:%d\n", names[i], line); return 1; }
        printf("%s: OK\n", names[i]);
    }
    return 0;
}
