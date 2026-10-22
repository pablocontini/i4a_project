/* The production reset manager serializes and applies startup radio settings. */
#include <stdio.h>
#include <string.h>
#include "../../../components/integration/reset_manager/src/reset_manager.c"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static ring_share_t ring;
static ring_callback_t registered;
static rm_startup_packet_t last_packet;
static config_id_t orientation;
static bool requested_orientation, requested_configuration, broadcast_ok;
static unsigned broadcasts, restarts;
static int64_t now_us;

config_id_t config_get_id(void) { return orientation; }
int64_t esp_timer_get_time(void) { return now_us; }
void esp_read_mac(uint8_t *mac, int type) {
    (void)type;
    const uint8_t identity[6] = {0xa0, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(mac, identity, sizeof(identity));
}
void esp_restart(void) { ++restarts; }
void vTaskDelay(TickType_t ticks) { now_us += (int64_t)ticks * 1000; }
void node_set_orientation_mode_requested(bool on) { requested_orientation = on; }
void node_set_configuration_mode_requested(bool on) { requested_configuration = on; }
void rs_register_component(ring_share_t *rs, int component, ring_callback_t cb) {
    (void)rs; (void)component; registered = cb;
}
bool rs_broadcast(ring_share_t *rs, int component, const void *data, uint16_t length) {
    (void)rs; (void)component;
    ++broadcasts;
    if (length == sizeof(last_packet)) memcpy(&last_packet, data, length);
    return broadcast_ok;
}

static void reset(config_id_t id) {
    memset(&reset_manager, 0, sizeof(reset_manager));
    orientation = id;
    requested_orientation = requested_configuration = false;
    broadcast_ok = true;
    broadcasts = restarts = 0;
    now_us = 1000;
    rm_init(&ring);
}
static void deliver(const void *data, uint16_t length) {
    registered.callback(registered.context, data, length);
}
static int startup_propagates_every_radio(void) {
    const uint8_t powers[4] = {8, 20, 60, 80};
    const int8_t thresholds[4] = {-88, -77, -66, -55};
    reset(CONFIG_ID_CENTER);
    CHECK(rm_broadcast_startup_info(false, true, false, powers, thresholds));
    CHECK(rm_is_device_up());
    CHECK(last_packet.version == 2);
    CHECK(memcmp(last_packet.tx_power_qdbm, powers, sizeof(powers)) == 0);
    CHECK(memcmp(last_packet.rssi_threshold_dbm, thresholds, sizeof(thresholds)) == 0);
    CHECK(strcmp(last_packet.uuid, "A01122334455") == 0);
    rm_startup_packet_t packet = last_packet;
    for (int id = 0; id < 4; ++id) {
        reset((config_id_t)id);
        deliver(&packet, sizeof(packet));
        CHECK(rm_is_device_up());
        CHECK(!rm_is_root());
        CHECK(strcmp(rm_get_uuid(), packet.uuid) == 0);
        CHECK(requested_orientation && !requested_configuration);
        CHECK(rm_get_local_antenna_power() == powers[id]);
        CHECK(rm_get_local_rssi_threshold() == thresholds[id]);
        CHECK(restarts == 0);
    }
    return 0;
}
static int startup_rejects_bad_configuration_without_side_effects(void) {
    const uint8_t powers[4] = {8, 20, 60, 80};
    const int8_t thresholds[4] = {-88, -77, -66, -55};
    reset(CONFIG_ID_CENTER);
    CHECK(rm_broadcast_startup_info(false, false, true, powers, thresholds));
    rm_startup_packet_t packet = last_packet;
    reset(CONFIG_ID_EAST);
    deliver(&packet, sizeof(packet) - 1);
    CHECK(!rm_is_device_up() && !requested_configuration);
    packet.version = 1;
    deliver(&packet, sizeof(packet));
    CHECK(!rm_is_device_up() && rm_get_local_rssi_threshold() == -128);
    packet.version = 2;
    packet.tx_power_qdbm[3] = 79;
    deliver(&packet, sizeof(packet));
    CHECK(!rm_is_device_up() && !requested_configuration);
    packet.tx_power_qdbm[3] = 80;
    packet.rssi_threshold_dbm[0] = 0;
    deliver(&packet, sizeof(packet));
    CHECK(!rm_is_device_up() && !requested_configuration);
    packet.rssi_threshold_dbm[0] = -88;
    deliver(&packet, sizeof(packet));
    CHECK(rm_is_device_up() && requested_configuration && !requested_orientation);
    CHECK(rm_get_local_rssi_threshold() == -66);
    CHECK(rm_get_local_antenna_power() == 60);
    CHECK(restarts == 0);
    return 0;
}
static int startup_failed_broadcast_never_releases_boot_barrier(void) {
    const uint8_t powers[4] = {8, 20, 60, 80};
    const int8_t thresholds[4] = {-88, -77, -66, -55};
    reset(CONFIG_ID_CENTER);
    broadcast_ok = false;
    CHECK(!rm_broadcast_startup_info(true, false, false, powers, thresholds));
    CHECK(!rm_is_device_up());
    CHECK(strcmp(last_packet.uuid, "000000000000") == 0);
    CHECK(last_packet.is_root == 1);
    CHECK(!rm_broadcast_startup_info(true, false, false, NULL, thresholds));
    CHECK(broadcasts == 1);
    uint8_t invalid_power[4] = {8, 20, 60, 79};
    CHECK(!rm_broadcast_startup_info(true, false, false, invalid_power, thresholds));
    CHECK(broadcasts == 1);
    broadcast_ok = true;
    CHECK(rm_broadcast_startup_info(true, false, false, powers, thresholds));
    CHECK(rm_is_device_up() && rm_is_root());
    CHECK(restarts == 0);
    return 0;
}
static int reset_during_startup_does_not_restart_special_mode(void) {
    reset(CONFIG_ID_SOUTH);
    uint8_t opcode = RM_OPCODE_RESET;
    now_us += 1000000;
    deliver(&opcode, 1);
    CHECK(!rm_is_device_up() && restarts == 0);
    CHECK(rm_get_last_reset_time() == now_us);
    const uint8_t powers[4] = {8, 20, 60, 80};
    const int8_t thresholds[4] = {-88, -77, -66, -55};
    CHECK(rm_broadcast_startup_info(false, false, true, powers, thresholds));
    deliver(&opcode, 1);
    CHECK(restarts == 1);
    return 0;
}
int main(void) {
    int (*cases[])(void) = {
        startup_propagates_every_radio,
        startup_rejects_bad_configuration_without_side_effects,
        startup_failed_broadcast_never_releases_boot_barrier,
        reset_during_startup_does_not_restart_special_mode
    };
    const char *names[] = {
        "startup_propagates_every_radio",
        "startup_rejects_bad_configuration_without_side_effects",
        "startup_failed_broadcast_never_releases_boot_barrier",
        "reset_during_startup_does_not_restart_special_mode"
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int line = cases[i]();
        if (line) { printf("%s failed at reset_test.c:%d\n", names[i], line); return 1; }
        printf("%s: OK\n", names[i]);
    }
    return 0;
}
