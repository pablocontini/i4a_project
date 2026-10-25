#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "client.h"
#include "node.h"
#include "node_ota/node_ota.h"
#include "node_radio_cut.h"
#include "task_config.h"

static DevicePtr cut_device;
static QueueHandle_t cut_commands;
static volatile bool cut_active, cut_pending;

bool node_radio_cut_busy(void) { return cut_active || cut_pending; }

static bool can_cut(void) {
    return cut_device && cut_device->mode == STATION &&
        cut_device->state == d_active && cut_device->station_ptr &&
        cut_device->station_ptr->is_fully_connected && client_is_peer_admitted() &&
        node_is_network_provided() && !node_is_sta_locked() &&
        node_ota_can_switch_mode() && !node_is_configuration_mode_enabled() &&
        !node_is_orientation_mode_enabled();
}

static void trace(const char *event, uint32_t seconds, int64_t deadline) {
    flockfile(stdout);
    printf("\nI4ACUT t_ms=%lld board=%u event=%s seconds=%lu restore_at_ms=%lld\n",
        (long long)(esp_timer_get_time() / 1000),
        (unsigned)node_get_device_orientation() + 1, event,
        (unsigned long)seconds, (long long)(deadline / 1000));
    fflush(stdout);
    funlockfile(stdout);
}

static esp_err_t enqueue(uint32_t seconds) {
    if (!cut_commands || seconds > 300) return ESP_ERR_INVALID_ARG;
    if (!seconds && !node_radio_cut_busy()) return ESP_OK;
    if (seconds && (node_radio_cut_busy() || !can_cut())) return ESP_ERR_INVALID_STATE;
    if (seconds) cut_pending = true;
    if (xQueueSend(cut_commands, &seconds, 0) == pdPASS) return ESP_OK;
    if (seconds) cut_pending = false;
    return ESP_ERR_NO_MEM;
}

static void control_task(void *arg) {
    (void)arg;
    int64_t restore_at = 0;
    for (;;) {
        uint32_t seconds;
        if (xQueueReceive(cut_commands, &seconds, pdMS_TO_TICKS(100))) {
            if (seconds && !cut_active && can_cut()) {
                cut_active = true;
                cut_device->sta_lock = true;
                trace("cut_begin", seconds, 0);
                /* Stop workers through their normal teardown; keep SPI and netif alive. */
                device_disconnect_station(cut_device);
                client_close();
                client_wait_stopped();
                device_stop_station(cut_device);
                cut_device->state = d_inactive;
                restore_at = esp_timer_get_time() + (int64_t)seconds * 1000000;
                trace("cut_applied", seconds, restore_at);
            } else if (!seconds && cut_active) {
                restore_at = esp_timer_get_time();
            } else {
                trace("cut_not_applied", seconds, 0);
            }
            cut_pending = false;
        }
        if (cut_active && esp_timer_get_time() >= restore_at) {
            trace("restore_begin", 0, 0);
            device_start_station(cut_device);
            cut_device->sta_lock = false;
            device_connect_station(cut_device);
            cut_active = false;
            trace("radio_restored", 0, 0);
        }
    }
}

esp_err_t node_radio_cut_init(DevicePtr device) {
    if (node_get_device_orientation() == NODE_DEVICE_ORIENTATION_CENTER ||
        node_is_device_center_root()) return ESP_OK;
    if (!device || cut_commands) return ESP_ERR_INVALID_STATE;
    cut_device = device;
    cut_commands = xQueueCreate(2, sizeof(uint32_t));
    if (!cut_commands) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(control_task, "radio_cut", TASK_NODE_AP_STA_STACK,
            NULL, TASK_NODE_AP_STA_PRIORITY, NULL, TASK_NODE_AP_STA_CORE) != pdPASS)
        return ESP_ERR_NO_MEM;
    node_ota_register_radio_cut(enqueue);
    return ESP_OK;
}
