#include "device.h"
#include "config.h"
#include "ring_link.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "callbacks.h"
#include "internal_messages.h"
#include "channel_manager/channel_manager.h"
#include "reset_manager/reset_manager.h"
#include "info_manager/info_manager.h"
#include "traffic.h"
#include "remote_control.h"
#include "task_config.h"
#include "node.h"
#include "esp_random.h"
#include "discovery_manager/discovery_manager.h"
#include "neighbor_manager/neighbor_manager.h"
#include "config_portal/config_portal.h"
#include "portal_settings/portal_settings.h"
#include "wifi_credentials/wifi_credentials.h"
#include "offer_policy.h"
#include "freertos/queue.h"
#include "node_ota/node_ota.h"
#include "client.h"
#include <stdarg.h>
#include <stdio.h>

/* Keep node's dependency on antenna_orientation_mode one-way. */
bool antenna_orientation_mode_pin_is_active(void);

#define MAX_DEVICES_PER_HOUSE 5

#define NODE_NAME_PREFIX "I4A"
#define NODE_LINK_PASSWORD "zWfAc2wXq5"

#define NAT_NETWORK_NAME "Internet4All_Root"
#define NAT_NETWORK_PASSWORD "I4A123456"

#ifndef I4A_HOUSE_SSID
#define I4A_HOUSE_SSID "ComNetAR - Ramona"
#endif
#define HOUSE_NETWORK_NAME I4A_HOUSE_SSID
#define HOUSE_NETWORK_PASSWORD ""

#define CALIBRATION_DELAY_SECONDS 2
#define AP_STA_DELAY_SECONDS 1

#define MAX_RETRIES 5
#define RETRY_DELAY_MS 500

#define DEFAULT_SUBNET 0x00000000
#define DEFAULT_MASK 0xFFFFFFFF

#include "sdkconfig.h"
#ifdef CONFIG_I4A_FAST_TEST_TIMING
#define SEARCH_WAIT_MIN_SECONDS 5
#define SEARCH_WAIT_SPREAD_SECONDS 6
#else
#define SEARCH_WAIT_MIN_SECONDS 600
#define SEARCH_WAIT_SPREAD_SECONDS 101
#endif

static const char *TAG = "node";

typedef struct node {
  DevicePtr node_device_ptr;
  const char *node_device_uuid;
  const char *node_device_mac;
  node_device_orientation_t node_device_orientation;
  bool node_device_is_center_root;
  bool node_device_is_apsta;
  bool original_uplink;
  uint32_t node_device_subnet;
  uint32_t node_device_mask;
} node_t;

static node_t node = {
  .node_device_subnet = DEFAULT_SUBNET,
  .node_device_mask = DEFAULT_MASK,
};

static Device node_device = {
  .mode = NAN,
  .ap_lock = false,
  .sta_lock = false,
};

static node_t *node_ptr = &node;
static bool orientation_mode_requested;
static bool configuration_mode_requested;
static mutex_t search_lock;
static dm_token_t search_turn;
static bool search_turn_held;
static volatile bool peer_offer_accepted;
static volatile bool wifi_started;
typedef struct { bool station; uint32_t network, mask; bool radio_cut; uint32_t seconds; } node_mode_command_t;
static QueueHandle_t mode_commands;
static volatile bool radio_cut_active;
static volatile bool radio_cut_pending;
static int64_t last_turn_request_trace_us = -30000000;
static void node_apply_sta(void);
static void node_apply_ap(uint32_t network, uint32_t mask);

void node_link_trace(const char *format, ...) {
  va_list args;
  va_start(args, format);
  flockfile(stdout);
  printf("\nI4ALINK t_ms=%lld role=%u ", (long long)(esp_timer_get_time() / 1000),
         (unsigned)node_ptr->node_device_orientation);
  vprintf(format, args);
  putchar('\n'); fflush(stdout); funlockfile(stdout);
  va_end(args);
}

void node_link_trace_ips(const char *event) {
  esp_netif_ip_info_t wifi = {0}, spi = {0};
  esp_netif_t *netif = node_get_wifi_netif();
  if (netif) (void)esp_netif_get_ip_info(netif, &wifi);
  netif = node_get_spi_netif();
  if (netif) (void)esp_netif_get_ip_info(netif, &spi);
  node_link_trace("event=%s wifi=" IPSTR " mask=" IPSTR " gw=" IPSTR " spi=" IPSTR,
                 event, IP2STR(&wifi.ip), IP2STR(&wifi.netmask), IP2STR(&wifi.gw), IP2STR(&spi.ip));
}

void node_set_orientation_mode_requested(bool enabled) { orientation_mode_requested = enabled; }
bool node_is_orientation_mode_enabled(void) { return orientation_mode_requested; }
void node_set_configuration_mode_requested(bool enabled) { configuration_mode_requested = enabled; }
bool node_is_configuration_mode_enabled(void) { return configuration_mode_requested; }

bool node_has_active_wireless_peer(void) {
  return peer_offer_accepted && node_is_network_provided() &&
         nm_is_radio_active(node_ptr->node_device_orientation);
}

void node_set_peer_offer_accepted(bool accepted) { peer_offer_accepted = accepted; }

bool node_acquire_search_turn(void) {
  if (radio_cut_active) return false;
  bool acquired = false;
  WITH_LOCK(&search_lock, {
    if (search_turn_held && dm_owns(&search_turn)) acquired = true;
    else {
      search_turn_held = false;
      int64_t now = esp_timer_get_time();
      bool trace_request = now - last_turn_request_trace_us >= 30000000;
      if (trace_request) {
        last_turn_request_trace_us = now;
        node_link_trace("event=turn_request provisioned=%u", (unsigned)node_is_network_provided());
      }
      if (!node_has_active_wireless_peer() && dm_acquire(&search_turn, node_is_network_provided())) {
        search_turn_held = true;
        acquired = true;
        node_link_trace("event=turn_granted session=%lu sequence=%lu deadline_ms=%llu",
                        (unsigned long)search_turn.session, (unsigned long)search_turn.request,
                        (unsigned long long)search_turn.deadline_ms);
      } else if (trace_request) {
        node_link_trace("event=turn_not_granted");
      }
    }
  });
  return acquired;
}

bool node_search_turn_is_valid(void) {
  if (radio_cut_active) return false;
  bool valid;
  WITH_LOCK(&search_lock, { valid = search_turn_held && dm_owns(&search_turn); });
  return valid;
}

void node_release_search_turn(void) {
  dm_token_t released;
  bool held;
  WITH_LOCK(&search_lock, {
    held = search_turn_held;
    released = search_turn;
    search_turn_held = false;
  });
  if (held) {
    node_link_trace("event=turn_release session=%lu sequence=%lu",
                    (unsigned long)released.session, (unsigned long)released.request);
    dm_release(&released);
  }
}

bool node_wait_wifi_started(uint32_t timeout_ms) {
  int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
  while (!wifi_started && esp_timer_get_time() < deadline)
    vTaskDelay(pdMS_TO_TICKS(10));
  return wifi_started;
}

bool node_accept_peer_offer(uint32_t offered_subnet, uint32_t offered_mask) {
  const char *reason = node_offer_rejection(node_ptr->node_device_subnet, node_ptr->node_device_mask,
                            node_is_network_provided(), node_ptr->original_uplink,
                            offered_subnet, offered_mask);
  bool allowed = reason == NULL;
  node_link_trace("event=offer_decision accepted=%u original_uplink=%u offered=%08lx/%08lx local=%08lx/%08lx reason=%s",
                 (unsigned)allowed, (unsigned)node_ptr->original_uplink,
                 (unsigned long)offered_subnet, (unsigned long)offered_mask,
                 (unsigned long)node_ptr->node_device_subnet, (unsigned long)node_ptr->node_device_mask,
                 allowed ? "allowed" : reason);
  return allowed;
}

static node_device_orientation_t node_get_config_orientation(void){
  config_id_t config_bits = config_get_id();
    if ((config_bits >> 2) == 0) {
      return config_bits;
  } else {
      return NODE_DEVICE_ORIENTATION_CENTER;
  }
}

/* All driver teardown and discovery RPCs run outside the SPI callback.
 * A provision callback only enqueues its AP configuration. */
static void node_control_task(void *arg) {
  (void)arg;
  bool searching = false;
  int64_t next_search_us = 0;
  int64_t radio_restore_us = 0;
  for (;;) {
    node_mode_command_t command;
    if (xQueueReceive(mode_commands, &command, pdMS_TO_TICKS(100))) {
      if (command.radio_cut) {
        if (command.seconds && !radio_cut_active &&
            node_ptr->node_device_ptr->mode == STATION && node_has_active_wireless_peer()) {
          /* Stop reconnect/scan before disconnecting. Keep the SPI and logical block. */
          radio_cut_active = true;
          node_link_trace_ips("cut_begin");
          node_ptr->node_device_ptr->sta_lock = true;
          device_disconnect_station(node_ptr->node_device_ptr);
          client_close();
          client_wait_stopped();
          device_stop_station(node_ptr->node_device_ptr);
          node_ptr->node_device_ptr->state = d_inactive;
          node_release_search_turn();
          radio_restore_us = esp_timer_get_time() + (int64_t)command.seconds * 1000000;
          node_link_trace("event=cut_applied seconds=%lu restore_at_ms=%lld",
                          (unsigned long)command.seconds, (long long)(radio_restore_us / 1000));
          node_link_trace_ips("cut_transport");
        } else if (!command.seconds && radio_cut_active) {
          radio_restore_us = esp_timer_get_time();
        } else {
          node_link_trace("event=cut_not_applied");
        }
        radio_cut_pending = false;
      } else if (!radio_cut_active) {
        if (command.station) node_apply_sta();
        else node_apply_ap(command.network, command.mask);
      }
      searching = false;
      next_search_us = 0;
    }
    if (radio_cut_active) {
      if (esp_timer_get_time() < radio_restore_us) continue;
      /* AP reset clears the temporary lock; restart discovery without the 10-minute wait. */
      node_apply_ap(node_ptr->node_device_subnet, node_ptr->node_device_mask);
      radio_cut_active = false;
      next_search_us = esp_timer_get_time();
      node_link_trace("event=radio_restored next_search=immediate");
      node_link_trace_ips("restored_transport");
    }
    if (!node_ptr->node_device_is_apsta || node_ptr->node_device_is_center_root ||
        node_is_orientation_mode_enabled() || node_is_configuration_mode_enabled()) continue;
    if (node_has_active_wireless_peer()) {
      node_release_search_turn();
      searching = false;
      next_search_us = 0;
      continue;
    }
    if (searching) {
      if (!node_search_turn_is_valid()) {
        node_link_trace("event=turn_expired_or_revoked");
        node_apply_ap(node_ptr->node_device_subnet, node_ptr->node_device_mask);
        searching = false;
        next_search_us = 0;
      }
      continue;
    }
    DevicePtr device = node_ptr->node_device_ptr;
    if (device->mode == STATION) {
      /* An admitted redundant link was lost; restore its AP before retrying. */
      node_apply_ap(node_ptr->node_device_subnet, node_ptr->node_device_mask);
      next_search_us = 0;
    }
    if (device->mode != AP || device->access_point_ptr->server_is_up ||
        nm_is_radio_busy(node_ptr->node_device_orientation) ||
        node_is_sta_locked() || node_is_ap_locked()) continue;
    int64_t now = esp_timer_get_time();
    if (!next_search_us) {
      uint32_t seconds = SEARCH_WAIT_MIN_SECONDS + esp_random() % SEARCH_WAIT_SPREAD_SECONDS;
      next_search_us = now + (int64_t)seconds * 1000000;
      node_link_trace("event=search_scheduled wait_seconds=%lu", (unsigned long)seconds);
    }
    if (now < next_search_us) continue;
    if (!node_acquire_search_turn()) {
      next_search_us = esp_timer_get_time() + 1000000 + esp_random() % 1000000;
      continue;
    }
    /* An incoming association/reservation may have arrived during the RPC. */
    if (nm_is_radio_busy(node_ptr->node_device_orientation) ||
        device->access_point_ptr->server_is_up) {
      node_release_search_turn();
      next_search_us = 0;
      continue;
    }
    node_apply_sta();
    searching = true;
  }
}

static esp_err_t node_queue_radio_cut(uint32_t seconds) {
  if (seconds > 300 || node_ptr->node_device_orientation == NODE_DEVICE_ORIENTATION_CENTER ||
      node_ptr->node_device_is_center_root || node_is_configuration_mode_enabled() ||
      node_is_orientation_mode_enabled() || !mode_commands || !wifi_started ||
      !node_is_network_provided()) return ESP_ERR_INVALID_STATE;
  if (seconds && (radio_cut_active || radio_cut_pending || node_ptr->node_device_ptr->mode != STATION ||
                  !node_has_active_wireless_peer())) return ESP_ERR_INVALID_STATE;
  node_mode_command_t command = { .radio_cut = true, .seconds = seconds };
  if (seconds) radio_cut_pending = true;
  if (xQueueSend(mode_commands, &command, 0) == pdPASS) return ESP_OK;
  if (seconds) radio_cut_pending = false;
  return ESP_ERR_NO_MEM;
}

void node_set_as_sta(void) {
  node_mode_command_t command = { .station = true };
  ESP_ERROR_CHECK(xQueueSend(mode_commands, &command, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

void node_set_as_ap(uint32_t network, uint32_t mask) {
  node_mode_command_t command = { .network = network, .mask = mask };
  ESP_ERROR_CHECK(xQueueSend(mode_commands, &command, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

void node_setup(void){
  node_ota_register_radio_cut(node_queue_radio_cut);
  ESP_ERROR_CHECK(node_init_event_queues());
  ESP_ERROR_CHECK(node_start_event_tasks());

  config_setup();
  config_print();
  ESP_ERROR_CHECK(mutex_create(&search_lock) ? ESP_OK : ESP_ERR_NO_MEM);

  node_ptr->node_device_ptr = &node_device;
  node_ptr->node_device_orientation = node_get_config_orientation();
  node_setup_internal_messages(node_ptr->node_device_orientation);

  // Wait in sequence to avoid current peaks while node calibrates
  vTaskDelay(pdMS_TO_TICKS(node_ptr->node_device_orientation * CALIBRATION_DELAY_SECONDS * 1000));

  ESP_ERROR_CHECK(device_wifi_init());
  ESP_ERROR_CHECK(wifi_credentials_init());
  ESP_ERROR_CHECK(ring_link_init());

  if(node_ptr->node_device_orientation == NODE_DEVICE_ORIENTATION_CENTER) {
    ESP_ERROR_CHECK(portal_settings_init());
    bool is_root = config_mode_is(CONFIG_MODE_ROOT);
    config_portal_boot_mode_t requested = config_portal_take_boot_mode_request();
    bool orientation_mode = antenna_orientation_mode_pin_is_active() ||
        requested == CONFIG_PORTAL_BOOT_MODE_ORIENTATION;
    bool configuration_mode = !orientation_mode && !is_root &&
        requested == CONFIG_PORTAL_BOOT_MODE_CONFIGURATION;
    node_set_orientation_mode_requested(orientation_mode);
    node_set_configuration_mode_requested(configuration_mode);
    portal_antenna_power_config_t powers = portal_settings_get_antenna_powers();
    portal_antenna_rssi_config_t thresholds = portal_settings_get_antenna_rssi_thresholds();
    while (!rm_broadcast_reset()) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds so all the devices come back up in case this was an actual node reset

    while (!rm_broadcast_startup_info(is_root, orientation_mode, configuration_mode,
                                      powers.quarter_dbm, thresholds.dbm)) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }

  } else {
    while (!rm_is_device_up()){
      if(rm_should_device_reset()){
        while (!rm_broadcast_reset()) {
          vTaskDelay(pdMS_TO_TICKS(100));
        }
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  node_ptr->node_device_mac = rm_get_mac();
  node_ptr->node_device_uuid = rm_get_uuid();
  node_ptr->node_device_is_center_root = rm_is_root();
  node_ptr->node_device_is_apsta = false;
  mode_commands = xQueueCreate(8, sizeof(node_mode_command_t));
  ESP_ERROR_CHECK(mode_commands ? ESP_OK : ESP_ERR_NO_MEM);
  ESP_ERROR_CHECK(xTaskCreatePinnedToCore(node_control_task, "node_control",
      TASK_NODE_AP_STA_STACK, NULL, TASK_NODE_AP_STA_PRIORITY, NULL,
      TASK_NODE_AP_STA_CORE) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
  if (!node_is_orientation_mode_enabled() && !node_is_configuration_mode_enabled()) {
    node_traffic_init();
    im_scheduler_start();
    if (node_ptr->node_device_orientation == NODE_DEVICE_ORIENTATION_CENTER &&
        !node_ptr->node_device_is_center_root)
      ESP_ERROR_CHECK(config_portal_start_button_monitor());
  }
}

static void node_apply_sta(void){
  node_set_peer_offer_accepted(false);
  if(node_ptr->node_device_ptr->mode != NAN){
    device_reset(node_ptr->node_device_ptr);
  }

  char *wifi_network_prefix = NODE_NAME_PREFIX;
  char *wifi_network_password = NODE_LINK_PASSWORD;

  device_init(node_ptr->node_device_ptr, node_ptr->node_device_uuid, node_ptr->node_device_orientation, wifi_network_prefix, wifi_network_password, 6, 4, (uint8_t)node_ptr->node_device_is_center_root, (uint8_t)node_ptr->node_device_is_apsta, STATION);

  // Wait in sequence to avoid current peaks while STA starts up
  vTaskDelay(pdMS_TO_TICKS(node_ptr->node_device_orientation * AP_STA_DELAY_SECONDS * 1000));
  device_start_station(node_ptr->node_device_ptr);
  device_set_max_tx_power(node_ptr->node_device_ptr, rm_get_local_antenna_power());
  device_set_rssi_threshold(node_ptr->node_device_ptr, rm_get_local_rssi_threshold());
  device_connect_station(node_ptr->node_device_ptr);
  wifi_started = true;
}

static void node_apply_ap(uint32_t network, uint32_t mask){
  bool was_station = node_ptr->node_device_ptr->mode == STATION;
  if(node_ptr->node_device_ptr->mode != NAN){
    device_reset(node_ptr->node_device_ptr);
  }
  node_set_peer_offer_accepted(false);

  node_set_network_settings(network, mask);
  uint32_t last_net30 = ((network | ~mask) - 3) & 0xFFFFFFFC; // Get last possible /30 address from given subnet

  uint32_t node_gateway;
  uint8_t ap_channel_to_emit = cm_get_suggested_channel();
  uint8_t ap_max_sta_connections;
  char *wifi_network_prefix;
  char *wifi_network_password;

  if (node_ptr->node_device_orientation == NODE_DEVICE_ORIENTATION_CENTER) {
    if(node_ptr->node_device_is_center_root) {
      network = last_net30;
      mask = 0xFFFFFFFC; // /30
      node_gateway = network + 2;
      wifi_network_prefix = NAT_NETWORK_NAME;
      wifi_network_password = NAT_NETWORK_PASSWORD;
      ap_max_sta_connections = 1;
    } else {
      node_gateway = network + 1;
      wifi_network_prefix = HOUSE_NETWORK_NAME;
      wifi_network_password = (char *)wifi_credentials_get_house_password();
      ap_max_sta_connections = MAX_DEVICES_PER_HOUSE;
    }
  } else {
    mask = 0xFFFFFFFC; // /30
    node_gateway = network + 2;
    wifi_network_prefix = NODE_NAME_PREFIX;
    wifi_network_password = NODE_LINK_PASSWORD;
    ap_max_sta_connections = 1;
  }

  ip4_addr_t net_addr, gateway_addr, mask_addr;
  net_addr.addr = htonl(network + 1);
  gateway_addr.addr = htonl(node_gateway);
  mask_addr.addr = htonl(mask);

  char network_cidr[IP4ADDR_STRLEN_MAX];
  char network_gateway[IP4ADDR_STRLEN_MAX];
  char network_mask[IP4ADDR_STRLEN_MAX];

  ip4addr_ntoa_r(&net_addr, network_cidr, sizeof(network_cidr));
  ip4addr_ntoa_r(&gateway_addr, network_gateway, sizeof(network_gateway));
  ip4addr_ntoa_r(&mask_addr, network_mask, sizeof(network_mask));

  // Wait in sequence to avoid current peaks while AP starts up
  vTaskDelay(pdMS_TO_TICKS(node_ptr->node_device_orientation * AP_STA_DELAY_SECONDS * 1000));

  // Root always AP only, non-root launches AP+STA mode
  if (node_ptr->node_device_orientation == NODE_DEVICE_ORIENTATION_CENTER || node_ptr->node_device_is_center_root){
    device_init(node_ptr->node_device_ptr, node_ptr->node_device_uuid, node_ptr->node_device_orientation, wifi_network_prefix, wifi_network_password, ap_channel_to_emit, ap_max_sta_connections, (uint8_t)node_ptr->node_device_is_center_root, (uint8_t)node_ptr->node_device_is_apsta, AP);
    device_set_network_ap(node_ptr->node_device_ptr, network_cidr, network_gateway, network_mask);
    device_start_ap(node_ptr->node_device_ptr);
    device_set_max_tx_power(node_ptr->node_device_ptr, rm_get_local_antenna_power());
  } else {
    // Start the loop task the first time, otherwise simply switch back to AP mode on subsequent calls
    if(!node_ptr->node_device_is_apsta) {
      node_ptr->node_device_is_apsta = true;
    }
    device_init(node_ptr->node_device_ptr, node_ptr->node_device_uuid, node_ptr->node_device_orientation, wifi_network_prefix, wifi_network_password, ap_channel_to_emit, ap_max_sta_connections, (uint8_t)node_ptr->node_device_is_center_root, (uint8_t)node_ptr->node_device_is_apsta, AP);
    device_set_network_ap(node_ptr->node_device_ptr, network_cidr, network_gateway, network_mask);
    device_start_ap(node_ptr->node_device_ptr);
    device_set_max_tx_power(node_ptr->node_device_ptr, rm_get_local_antenna_power());
  }

  wifi_started = true;
  /* Teardown is complete and this interface is offering its AP again. */
  if (was_station) node_release_search_turn();

  if(node_ptr->node_device_orientation == NODE_DEVICE_ORIENTATION_CENTER) {
    if(node_ptr->node_device_is_center_root) {
      im_http_client_start();
    } else {
      remote_command_server_create();
    }
  }
}

node_device_orientation_t node_get_device_orientation(void){
  return node_ptr->node_device_orientation;
}

bool node_is_device_center_root(void){
  return node_ptr->node_device_is_center_root;
}

bool node_broadcast_to_siblings(const uint8_t *msg, uint16_t len) {
  for (int i = 0; i < MAX_RETRIES; i++) {
    if (broadcast_to_siblings(msg, len)) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
  }
  return false;
}

bool node_send_wireless_message(const uint8_t *msg, uint16_t len) {
  for (int i = 0; i < MAX_RETRIES; i++) {
    if (device_send_wireless_message(node_ptr->node_device_ptr, msg, len)) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
  }
  return false;
}

bool node_is_point_to_point_message(uint32_t dst) {
  return device_is_point_to_point_message(node_ptr->node_device_ptr, dst);
}

bool node_is_packet_for_this_subnet(uint32_t dst) {
    return ((dst & node_ptr->node_device_mask) == node_ptr->node_device_subnet);
}

esp_netif_t *node_get_wifi_netif(void) {
  return device_get_netif(node_ptr->node_device_ptr);
}

esp_netif_t *node_get_spi_netif(void) {
  return get_ring_link_tx_netif();
}

int8_t node_get_device_rssi(void) {
  return device_get_rssi(node_ptr->node_device_ptr);
}

uint32_t node_get_device_subnet(void) {
    return node_ptr->node_device_subnet;
}

uint32_t node_get_device_mask(void) {
    return node_ptr->node_device_mask;
}

const char *node_get_uuid(void) {
    return node_ptr->node_device_uuid;
}

const char *node_get_device_mac(void) {
    return node_ptr->node_device_mac;
}

const char *node_get_link_name(void) {
  return device_get_link_name(node_ptr->node_device_ptr);
}

uint8_t node_get_device_channel(void) {
  return device_get_channel(node_ptr->node_device_ptr);
}

void node_set_network_settings(uint32_t network, uint32_t mask) {
    if (!node_is_network_provided() && node_ptr->node_device_ptr->mode == STATION)
      node_ptr->original_uplink = true;
    node_ptr->node_device_subnet = network;
    node_ptr->node_device_mask = mask;
}

int64_t node_get_device_uptime_minutes(void) {
    int64_t uptime_us = esp_timer_get_time() - rm_get_last_reset_time();
    return uptime_us / 60000000;
}

void node_disable_sta(void) {
    device_disable_station(node_ptr->node_device_ptr);
}

void node_reject_wireless_peer(void) {
    if (node_ptr->node_device_ptr->mode == STATION)
        station_disconnect(node_ptr->node_device_ptr->station_ptr);
    else if (node_ptr->node_device_ptr->mode == AP)
        ap_disconnect_all_stations(node_ptr->node_device_ptr->access_point_ptr);
}

void node_enable_sta(void) {
    device_enable_station(node_ptr->node_device_ptr);
}

void node_disable_ap(void) {
    device_disable_ap(node_ptr->node_device_ptr);
}

void node_enable_ap(void) {
    device_enable_ap(node_ptr->node_device_ptr);
}

bool node_is_ap_locked(void) {
  return device_is_ap_locked(node_ptr->node_device_ptr);
}

bool node_is_sta_locked(void) {
  return device_is_sta_locked(node_ptr->node_device_ptr);
}

bool node_is_device_apsta(void) {
  return device_is_apsta(node_ptr->node_device_ptr);
}

bool node_is_network_provided(void){
  return (node_ptr->node_device_subnet != DEFAULT_SUBNET);
}
