/* Test the actual SPI admission gate, not a mock of its behavior. */
#ifdef __TINYC__
#define _Static_assert(condition, message) typedef char native_assert[(condition) ? 1 : -1]
#endif
#include "../../../components/routing/ring_share/src/ring_share.c"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static int64_t clock_us;
static unsigned sends, callbacks;
static bool lock_error;
static bool lowlevel_paused;
void ring_link_lowlevel_set_maintenance(bool active) { lowlevel_paused = active; }
bool mutex_create(mutex_t *m) { *m = calloc(1, sizeof(bool)); return *m != NULL; }
bool mutex_lock(mutex_t *m) {
    if (!m || !*m || *(bool *)*m) { lock_error = true; return false; }
    *(bool *)*m = true;
    return true;
}
bool mutex_unlock(mutex_t *m) { *(bool *)*m = false; return true; }
void mutex_destroy(mutex_t *m) { free(*m); *m = NULL; }
void os_panic(const char *format, ...) { (void)format; lock_error = true; }
int64_t esp_timer_get_time(void) { return clock_us; }
void sb_register_callback(siblings_t *sb, siblings_callback_t callback, void *ctx) {
    sb->callback = callback; sb->context = ctx;
}
bool sb_broadcast_to_siblings(siblings_t *sb, const uint8_t *msg, uint16_t len) {
    (void)sb; (void)msg; (void)len; ++sends; return true;
}
static void delivered(void *ctx, const uint8_t *data, uint16_t len) {
    (void)ctx; (void)data; (void)len; ++callbacks;
}
EXPORT int test_ring_maintenance_admission_and_clock(void) {
    ring_share_t ring;
    siblings_t sb;
    clock_us = 1000000; sends = callbacks = 0; lock_error = false;
    CHECK(rs_init(&ring, &sb));
    CHECK(rs_boot_quiet(&ring) && lowlevel_paused);
    rs_set_boot_quiet(&ring, false);
    rs_register_component(&ring, RS_ROUTING, (ring_callback_t){ .callback = delivered });
    rs_register_component(&ring, RS_RESET_MANAGER, (ring_callback_t){ .callback = delivered });
    rs_register_component(&ring, RS_NODE_OTA, (ring_callback_t){ .callback = delivered });
    uint8_t normal[] = {RS_ROUTING, 42}, ota[] = {RS_NODE_OTA, 42};
    CHECK(rs_broadcast(&ring, RS_ROUTING, normal, sizeof(normal)) && sends == 1);
    rs_set_maintenance(&ring, true);
    CHECK(rs_maintenance(&ring) && lowlevel_paused);
    clock_us += 300000000;
    rs_set_maintenance(&ring, true); /* Retry must not reset frozen time. */
    CHECK(rs_lease_time_ms(&ring) == 1000);
    *(bool *)ring.broadcast_lock = true; /* OTA currently owns the send lock. */
    CHECK(!rs_broadcast(&ring, RS_ROUTING, normal, sizeof(normal)) && sends == 1);
    *(bool *)ring.broadcast_lock = false;
    sb.callback(sb.context, normal, sizeof(normal));
    CHECK(callbacks == 0);
    CHECK(rs_broadcast(&ring, RS_NODE_OTA, ota, sizeof(ota)) && sends == 2);
    sb.callback(sb.context, ota, sizeof(ota));
    CHECK(callbacks == 1);
    /* After the center reboots, peripherals remain paused. Its RESET and
     * STARTUP broadcasts must reach the registered callback in that state. */
    uint8_t reset[] = {RS_RESET_MANAGER, 0xA5};
    uint8_t startup[] = {RS_RESET_MANAGER, 0xB6};
    CHECK(rs_broadcast(&ring, RS_RESET_MANAGER, reset + 1, 1) && sends == 3);
    sb.callback(sb.context, reset, sizeof(reset));
    sb.callback(sb.context, startup, sizeof(startup));
    CHECK(callbacks == 3 && rs_maintenance(&ring));
    rs_set_maintenance(&ring, false);
    CHECK(!rs_maintenance(&ring) && !lowlevel_paused && rs_lease_time_ms(&ring) == 1000);
    clock_us += 5000000;
    CHECK(rs_lease_time_ms(&ring) == 6000);
    CHECK(rs_broadcast(&ring, RS_ROUTING, normal, sizeof(normal)) && sends == 4);
    sb.callback(sb.context, normal, sizeof(normal));
    CHECK(callbacks == 4 && !lock_error);
    rs_set_maintenance(&ring, true);
    rs_shutdown(&ring);
    CHECK(!lowlevel_paused);
    CHECK(!ring.broadcast_lock && !ring.maintenance_lock);
    return 0;
}

EXPORT int test_boot_gate_preserves_provisioning_and_ota_lease_pause(void) {
    ring_share_t ring;
    siblings_t sb;
    clock_us = 1000000; sends = callbacks = 0; lock_error = false;
    CHECK(rs_init(&ring, &sb) && rs_boot_quiet(&ring));
    uint8_t data = 42;
    CHECK(rs_broadcast(&ring, RS_ROUTING, &data, 1));
    CHECK(rs_broadcast(&ring, RS_RESET_MANAGER, &data, 1));
    CHECK(!rs_broadcast(&ring, RS_NEIGHBOR_MANAGER, &data, 1));
    clock_us += 10000000;
    CHECK(rs_lease_time_ms(&ring) == 1000);
    rs_set_maintenance(&ring, true);
    rs_set_boot_quiet(&ring, false);
    CHECK(lowlevel_paused && rs_maintenance(&ring));
    CHECK(!rs_broadcast(&ring, RS_ROUTING, &data, 1));
    rs_set_maintenance(&ring, false);
    CHECK(!lowlevel_paused && rs_lease_time_ms(&ring) == 1000);
    CHECK(rs_broadcast(&ring, RS_NEIGHBOR_MANAGER, &data, 1));
    CHECK(!lock_error);
    rs_shutdown(&ring);
    return 0;
}
