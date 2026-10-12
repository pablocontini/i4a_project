#include <string.h>

#include "forwarder.h"
#include "impl_priv.h"
#include "utils.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

static rt_sibl_event_t sibling_event;
static rt_peer_event_t peer_event;
static unsigned sibling_count, peer_count, refresh_count;
static unsigned network_settings_count, ap_settings_count, token_request_count;
static sync_cs_callback_fn_t token_callback;
static void *token_context;
static bool maintenance;
static wireless_callbacks_t captured_wireless;
static void *captured_wireless_context;

bool mutex_create(mutex_t *lock) { *lock = (void *)1; return true; }
bool mutex_lock(mutex_t *lock) { (void)lock; return true; }
bool mutex_unlock(mutex_t *lock) { (void)lock; return true; }
void mutex_destroy(mutex_t *lock) { (void)lock; }
void os_log(os_log_level_t level, const char *tag, const char *fmt, ...) {
    (void)level; (void)tag; (void)fmt;
}
void os_panic(const char *fmt, ...) { (void)fmt; }
void node_set_network_settings(uint32_t network, uint32_t mask) {
    (void)network; (void)mask;
    ++network_settings_count;
}
void wl_enable_ap_mode(wireless_t *wl, uint32_t network, uint32_t mask) {
    (void)wl; (void)network; (void)mask;
    ++ap_settings_count;
}
void os_delay_ms(uint32_t ms) { (void)ms; }
void os_diag(const char *tag,const char *fmt,...) { (void)tag; (void)fmt; }
bool ss_flush(shared_state_t *ss,component_id_t id) { (void)ss; (void)id; return true; }
void sync_defer_completion(sync_t *s,component_id_t id) { (void)s; (void)id; }
void wl_register_peer_callbacks(wireless_t *wl, wireless_callbacks_t callbacks, void *ctx) {
    (void)wl; captured_wireless=callbacks; captured_wireless_context=ctx;
}
void rs_register_component(ring_share_t *rs, component_id_t id, ring_callback_t callback) {
    (void)rs; (void)id; (void)callback;
}
void sync_register_critical_section(sync_t *sync, component_id_t id,
                                   sync_cs_callback_fn_t callback, void *ctx) {
    (void)sync; (void)id; token_callback = callback; token_context = ctx;
}
void sync_request_critical_section(sync_t *sync, component_id_t id) {
    (void)sync; (void)id; ++token_request_count;
}
void ss_watch(shared_state_t *ss, component_id_t id, shared_data_t data) {
    (void)ss; (void)id; (void)data;
}
bool rs_maintenance(ring_share_t *rs) { (void)rs; return maintenance; }
void ss_refresh(shared_state_t *ss, component_id_t component) {
    (void)ss; (void)component; ++refresh_count;
}
bool rs_broadcast(ring_share_t *rs, component_id_t component, const void *data, uint16_t len) {
    (void)rs;
    if (component != RS_ROUTING || len != sizeof(sibling_event)) return false;
    memcpy(&sibling_event, data, len);
    ++sibling_count;
    return true;
}
bool wl_send_peer_message(wireless_t *wl, const void *data, uint16_t len) {
    (void)wl;
    if (len != sizeof(peer_event)) return false;
    memcpy(&peer_event, data, len);
    ++peer_count;
    return true;
}

static void reset_capture(void) {
    memset(&sibling_event, 0, sizeof(sibling_event));
    memset(&peer_event, 0, sizeof(peer_event));
    sibling_count = peer_count = refresh_count = 0;
    network_settings_count = ap_settings_count = token_request_count = 0;
    token_callback = NULL; token_context = NULL;
    maintenance = false;
}

static bool forwarder(routing_t *self, orientation_t orientation, uint32_t dtr) {
    memset(self, 0, sizeof(*self));
    self->orientation = orientation;
    routing_table_init(&self->node_state.routing_table, ORIENTATION_CENTER);
    if (!create_forwarder_core(self)) return false;
    if (dtr) {
        self->role.state.forwarder.global_state = GLOBAL_STATE_WITH_NETWORK;
        self->role.state.forwarder.local_state = LOCAL_STATE_CONNECTED;
        self->role.state.forwarder.node_network = NETWORK(0x0A400000, 0xFFE00000);
        self->role.state.forwarder.device_network = get_node_subnet(
            &self->role.state.forwarder.node_network, orientation);
        self->role.state.forwarder.dtr = dtr;
    }
    return true;
}

EXPORT int test_initial_uplink_counts_the_node_once(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_SOUTH, 0));
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A000000, 0xFF000000),
        .provided_network = NETWORK(0x0A400000, 0xFFE00000),
        .dtr = 1,
    };
    self.role.impl.on_peer_handshake(&self, &hello);
    CHECK(self.role.state.forwarder.dtr == 2);
    CHECK(self.role.state.forwarder.is_local_root);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_SOUTH);
    CHECK(sibling_count == 1 && sibling_event.event_id == SIBL_PROVISION);
    CHECK(sibling_event.payload.provision.dtr == 2);
    CHECK(sibling_event.payload.provision.network.addr == 0x0A400000);
    return 0;
}

EXPORT int test_gateway_request_keeps_the_root_default_route(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_WEST, 1));
    self.role.state.forwarder.node_network = NETWORK(0x0A000000, 0xFF000000);
    rt_peer_new_gateway_request_t request = {0};
    request.hag_networks[0] = NETWORK(0x0A400000, 0xFFE00000);
    self.role.impl.on_peer_new_gateway_request(&self, &request);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_CENTER);
    CHECK(self.node_state.routing_table.count == 1);
    CHECK(routing_table_route(&self.node_state.routing_table, 0x0A540005) == ORIENTATION_WEST);
    CHECK(routing_table_route(&self.node_state.routing_table, 0x01010101) == ORIENTATION_CENTER);
    CHECK(sibling_count == 1 && sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    return 0;
}

EXPORT int test_sibling_gateway_winner_preserves_node_distance(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_NORTH, 0));
    self.role.state.forwarder.global_state = GLOBAL_STATE_ON_GW_REQUEST;
    self.role.state.forwarder.local_state = LOCAL_STATE_CONNECTED;
    rt_sibl_new_gateway_winner_t winner = {
        .network = NETWORK(0x0A000000, 0xFF000000), .dtr = 2,
    };
    self.role.impl.on_sibl_new_gateway_winner(&self, &winner);
    CHECK(self.role.state.forwarder.dtr == 2);
    CHECK(!self.role.state.forwarder.is_local_root);
    CHECK(peer_count == 1 && peer_event.event_id == PEER_NEW_GATEWAY_RESPONSE);
    CHECK(peer_event.payload.new_gateway_response.dtr == 2);
    return 0;
}

EXPORT int test_equal_or_unreachable_gateway_does_not_replace_uplink(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_WEST, 2));
    self.node_state.routing_table.default_gateway = ORIENTATION_SOUTH;
    const uint32_t distances[] = {1, 2, 0, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(distances) / sizeof(distances[0]); ++i) {
        rt_peer_new_gateway_response_t reply = {
            .external_network = NETWORK(0x0A000000, 0xFF000000), .dtr = distances[i],
        };
        self.role.impl.on_peer_new_gateway_response(&self, &reply);
        CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_SOUTH);
        CHECK(self.role.state.forwarder.dtr == 2);
        CHECK(!self.role.state.forwarder.is_local_root);
        CHECK(sibling_count == 0 && refresh_count == 0);
    }
    return 0;
}

static int recover_existing_root_link(bool active_original) {
    reset_capture();
    routing_t original, alternate, root_radio, root_center;
    CHECK(forwarder(&original, ORIENTATION_SOUTH, 2));
    CHECK(forwarder(&alternate, ORIENTATION_WEST, 2));
    CHECK(forwarder(&root_radio, ORIENTATION_WEST, 1));
    root_radio.role.state.forwarder.node_network = NETWORK(0x0A000000, 0xFF000000);
    memset(&root_center, 0, sizeof(root_center));
    root_center.orientation = ORIENTATION_CENTER;
    CHECK(create_root_core(&root_center, 0x0A000000, 0xFF000000));
    original.role.state.forwarder.is_local_root = active_original;
    original.node_state.routing_table.default_gateway = ORIENTATION_SOUTH;
    routing_table_add(&original.node_state.routing_table, 0x0A000000, 0xFF000000, ORIENTATION_WEST);
    alternate.node_state.routing_table = original.node_state.routing_table;

    /* A redundant 10/8 route works while internet still uses the lost S uplink. */
    original.role.impl.on_peer_lost(&original, NULL);
    CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    rt_sibl_send_new_gateway_request_t sibling_request = sibling_event.payload.send_new_gateway_request;
    alternate.node_state.routing_table = original.node_state.routing_table;
    CHECK(routing_table_route(&alternate.node_state.routing_table, 0x0AFFFFFE) == ORIENTATION_WEST);
    CHECK(routing_table_route(&alternate.node_state.routing_table, 0x01010101) == ROUTING_OUTPUT_NONE);

    alternate.role.impl.on_sibl_send_new_gateway_request(&alternate, &sibling_request);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    rt_peer_new_gateway_request_t peer_request = peer_event.payload.new_gateway_request;
    root_radio.role.impl.on_peer_new_gateway_request(&root_radio, &peer_request);
    CHECK(root_radio.node_state.routing_table.default_gateway == ORIENTATION_CENTER);
    CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    sibling_request = sibling_event.payload.send_new_gateway_request;
    root_center.role.impl.on_sibl_send_new_gateway_request(&root_center, &sibling_request);
    root_center.role.impl.on_tick(&root_center, 10000);
    CHECK(sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER);
    rt_sibl_new_gateway_winner_t winner = sibling_event.payload.new_gateway_winner;
    root_radio.role.impl.on_sibl_new_gateway_winner(&root_radio, &winner);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_RESPONSE);
    rt_peer_new_gateway_response_t reply = peer_event.payload.new_gateway_response;
    alternate.role.impl.on_peer_new_gateway_response(&alternate, &reply);
    CHECK(alternate.role.state.forwarder.dtr == 2);
    CHECK(alternate.role.state.forwarder.is_local_root);
    CHECK(sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER);
    winner = sibling_event.payload.new_gateway_winner;
    original.role.impl.on_sibl_new_gateway_winner(&original, &winner);
    CHECK(original.role.state.forwarder.dtr == 2);
    CHECK(!original.role.state.forwarder.is_local_root);

    /* SPI shared-state replication publishes the alternate's routing table. */
    original.node_state.routing_table = alternate.node_state.routing_table;
    CHECK(routing_table_route(&original.node_state.routing_table, 0x0AFFFFFE) == ORIENTATION_WEST);
    CHECK(routing_table_route(&original.node_state.routing_table, 0x01010101) == ORIENTATION_WEST);
    CHECK(routing_table_route(&root_radio.node_state.routing_table, 0x0A540005) == ORIENTATION_WEST);
    CHECK(routing_table_route(&root_radio.node_state.routing_table, 0x01010101) == ORIENTATION_CENTER);
    return 0;
}

EXPORT int test_lost_original_uplink_recovers_through_existing_root_link(void) {
    return recover_existing_root_link(true);
}

EXPORT int test_lost_nondefault_link_rebuilds_return_route(void) {
    return recover_existing_root_link(false);
}

static void copy_table(routing_t *source, routing_t modules[], size_t count) {
    for (size_t i = 0; i < count; ++i)
        modules[i].node_state.routing_table = source->node_state.routing_table;
}

EXPORT int test_lost_uplink_recovers_via_forwarder_without_renumbering(void) {
    reset_capture();
    routing_t c[5], b[5], root_radio, root_center;
    const network_t c_network = NETWORK(0x0A200000, 0xFFE00000);
    const network_t b_network = NETWORK(0x0A600000, 0xFFE00000);
    network_t c_devices[4], b_devices[4];
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(forwarder(&c[i], (orientation_t)(i + 1), 2));
        CHECK(forwarder(&b[i], (orientation_t)(i + 1), 2));
        c[i].role.state.forwarder.node_network = c_network;
        b[i].role.state.forwarder.node_network = b_network;
        c_devices[i] = get_node_subnet(&c_network, (orientation_t)(i + 1));
        b_devices[i] = get_node_subnet(&b_network, (orientation_t)(i + 1));
        c[i].role.state.forwarder.device_network = c_devices[i];
        b[i].role.state.forwarder.device_network = b_devices[i];
        c[i].node_state.routing_table.default_gateway = ORIENTATION_NORTH;
        b[i].node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    }
    /* C's original N uplink is lost; E is already connected to B's W AP. */
    c[0].role.state.forwarder.is_local_root = true;
    b[0].role.state.forwarder.is_local_root = true;
    c[1].role.state.forwarder.local_state = LOCAL_STATE_NOT_CONNECTED;
    c[3].role.state.forwarder.local_state = LOCAL_STATE_NOT_CONNECTED;
    b[1].role.state.forwarder.local_state = LOCAL_STATE_NOT_CONNECTED;
    b[2].role.state.forwarder.local_state = LOCAL_STATE_NOT_CONNECTED;
    memset(&c[4], 0, sizeof(c[4]));
    memset(&b[4], 0, sizeof(b[4]));
    c[4].orientation = b[4].orientation = ORIENTATION_CENTER;
    CHECK(create_home_core(&c[4]));
    CHECK(create_home_core(&b[4]));
    rt_sibl_provision_t c_provision = {
        .network = c_network, .dtr = 2, .provider_id = ORIENTATION_NORTH,
    };
    rt_sibl_provision_t b_provision = {
        .network = b_network, .dtr = 2, .provider_id = ORIENTATION_NORTH,
    };
    c[4].role.impl.on_sibl_provision(&c[4], &c_provision);
    b[4].role.impl.on_sibl_provision(&b[4], &b_provision);
    const network_t c_home = get_node_subnet(&c_network, ORIENTATION_CENTER);
    const network_t b_home = get_node_subnet(&b_network, ORIENTATION_CENTER);
    const uint32_t c_client = c_home.addr + 10;
    const uint32_t b_client = b_home.addr + 10;
    routing_table_add(&c[0].node_state.routing_table, c_home.addr, c_home.mask, ORIENTATION_CENTER);
    routing_table_add(&c[0].node_state.routing_table, b_network.addr, b_network.mask, ORIENTATION_EAST);
    routing_table_add(&b[0].node_state.routing_table, b_home.addr, b_home.mask, ORIENTATION_CENTER);
    routing_table_add(&b[0].node_state.routing_table, c_network.addr, c_network.mask, ORIENTATION_WEST);
    copy_table(&c[0], c, 5);
    copy_table(&b[0], b, 5);
    CHECK(forwarder(&root_radio, ORIENTATION_SOUTH, 1));
    root_radio.role.state.forwarder.node_network = NETWORK(0x0A000000, 0xFF000000);
    memset(&root_center, 0, sizeof(root_center));
    root_center.orientation = ORIENTATION_CENTER;
    routing_table_init(&root_center.node_state.routing_table, ORIENTATION_CENTER);
    CHECK(create_root_core(&root_center, 0x0A000000, 0xFF000000));
    const unsigned old_ap_settings = ap_settings_count;
    const unsigned old_network_settings = network_settings_count;

    c[0].role.impl.on_peer_lost(&c[0], NULL);
    CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    rt_sibl_send_new_gateway_request_t request = sibling_event.payload.send_new_gateway_request;
    copy_table(&c[0], c, 5);
    CHECK(rt_do_route(&c[2], c_client, 0x01010101) == ROUTE_NONE);
    CHECK(rt_do_route(&c[2], c_client, b_client) == ROUTE_WIFI);
    for (unsigned i = 1; i < 4; ++i)
        c[i].role.impl.on_sibl_send_new_gateway_request(&c[i], &request);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    rt_peer_new_gateway_request_t peer_request = peer_event.payload.new_gateway_request;
    b[3].role.impl.on_peer_new_gateway_request(&b[3], &peer_request);
    CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    request = sibling_event.payload.send_new_gateway_request;
    copy_table(&b[3], b, 5);
    for (unsigned i = 0; i < 3; ++i)
        b[i].role.impl.on_sibl_send_new_gateway_request(&b[i], &request);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    peer_request = peer_event.payload.new_gateway_request;
    CHECK(peer_request.hag_networks[0].addr == c_network.addr);
    CHECK(peer_request.hag_networks[1].addr == b_network.addr);
    root_radio.role.impl.on_peer_new_gateway_request(&root_radio, &peer_request);
    CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    request = sibling_event.payload.send_new_gateway_request;
    root_center.role.impl.on_sibl_send_new_gateway_request(&root_center, &request);
    root_center.role.impl.on_tick(&root_center, 10000);
    CHECK(sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER);
    rt_sibl_new_gateway_winner_t winner = sibling_event.payload.new_gateway_winner;
    root_radio.role.impl.on_sibl_new_gateway_winner(&root_radio, &winner);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_RESPONSE);
    rt_peer_new_gateway_response_t response = peer_event.payload.new_gateway_response;
    b[0].role.impl.on_peer_new_gateway_response(&b[0], &response);
    CHECK(b[0].role.state.forwarder.dtr == 2);
    CHECK(b[0].role.state.forwarder.is_local_root);
    copy_table(&b[0], b, 5);
    winner = sibling_event.payload.new_gateway_winner;
    for (unsigned i = 1; i < 4; ++i)
        b[i].role.impl.on_sibl_new_gateway_winner(&b[i], &winner);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_RESPONSE);
    response = peer_event.payload.new_gateway_response;
    c[2].role.impl.on_peer_new_gateway_response(&c[2], &response);
    CHECK(c[2].role.state.forwarder.dtr == 3);
    CHECK(c[2].role.state.forwarder.is_local_root);
    copy_table(&c[2], c, 5);
    winner = sibling_event.payload.new_gateway_winner;
    for (unsigned i = 0; i < 4; ++i) {
        if (i != 2)
            c[i].role.impl.on_sibl_new_gateway_winner(&c[i], &winner);
        CHECK(c[i].role.state.forwarder.dtr == 3);
        CHECK(c[i].role.state.forwarder.node_network.addr == c_network.addr);
        CHECK(c[i].role.state.forwarder.node_network.mask == c_network.mask);
        CHECK(memcmp(&c[i].role.state.forwarder.device_network, &c_devices[i], sizeof(network_t)) == 0);
        CHECK(b[i].role.state.forwarder.dtr == 2);
        CHECK(b[i].role.state.forwarder.node_network.addr == b_network.addr);
        CHECK(memcmp(&b[i].role.state.forwarder.device_network, &b_devices[i], sizeof(network_t)) == 0);
    }
    CHECK(ap_settings_count == old_ap_settings);
    CHECK(network_settings_count == old_network_settings);
    CHECK(c[4].role.state.home.is_provisioned && b[4].role.state.home.is_provisioned);

    /* Public internet and the RPi take C E -> B W/SPI/N -> root S/center. */
    const uint32_t remote_destinations[] = {0x01010101, 0x0AFFFFFE};
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(rt_do_route(&c[0], c_client, remote_destinations[i]) == ROUTE_SPI);
        CHECK(rt_do_route(&c[2], c_client, remote_destinations[i]) == ROUTE_WIFI);
        CHECK(rt_do_route(&b[3], c_client, remote_destinations[i]) == ROUTE_SPI);
        CHECK(rt_do_route(&b[0], c_client, remote_destinations[i]) == ROUTE_WIFI);
        CHECK(rt_do_route(&root_radio, c_client, remote_destinations[i]) == ROUTE_SPI);
    }
    /* Replies have specific routes to the original C subnet; client IP stays. */
    CHECK(rt_do_route(&root_radio, 0x01010101, c_client) == ROUTE_WIFI);
    CHECK(rt_do_route(&b[0], 0x01010101, c_client) == ROUTE_SPI);
    CHECK(rt_do_route(&b[3], 0x01010101, c_client) == ROUTE_WIFI);
    CHECK(rt_do_route(&c[2], 0x01010101, c_client) == ROUTE_SPI);
    CHECK(rt_do_route(&c[4], 0x01010101, c_client) == ROUTE_WIFI);
    CHECK(rt_do_route(&c[2], c_client, b_client) == ROUTE_WIFI);
    CHECK(rt_do_route(&b[3], c_client, b_client) == ROUTE_SPI);
    CHECK(rt_do_route(&b[4], c_client, b_client) == ROUTE_WIFI);
    CHECK(rt_do_route(&b[3], b_client, c_client) == ROUTE_WIFI);
    CHECK(rt_do_route(&c[4], b_client, c_client) == ROUTE_WIFI);
    return 0;
}

EXPORT int test_gateway_retry_timeout_and_reply_cancel(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 2));
    rt_sibl_send_new_gateway_request_t request = {0};
    self.role.impl.on_sibl_send_new_gateway_request(&self, &request);
    CHECK(peer_count == 1);
    CHECK(self.role.state.forwarder.dtr == 0);
    CHECK(self.node_state.routing_table.default_gateway == ROUTING_OUTPUT_NONE);
    CHECK(peer_event.payload.new_gateway_request.hag_networks[0].addr == self.role.state.forwarder.node_network.addr);
    self.role.impl.on_tick(&self, GATEWAY_RETRY_TIMEOUT_MS - 1);
    CHECK(peer_count == 1);
    self.role.impl.on_tick(&self, 1);
    CHECK(peer_count == 2);
    self.role.impl.on_tick(&self, GATEWAY_RETRY_TIMEOUT_MS * 5);
    CHECK(peer_count == 3);  // One bounded retry per tick, even after a long pause.
    rt_peer_new_gateway_response_t response = {
        .external_network = NETWORK(0x0A600000, 0xFFE00000), .dtr = 2,
    };
    self.role.impl.on_peer_new_gateway_response(&self, &response);
    CHECK(self.role.state.forwarder.dtr == 3);
    CHECK(self.role.state.forwarder.gateway_retry_timeout == 0);
    self.role.impl.on_tick(&self, GATEWAY_RETRY_TIMEOUT_MS);
    CHECK(peer_count == 3);
    CHECK(network_settings_count == 0 && ap_settings_count == 0);
    return 0;
}

EXPORT int test_recovery_does_not_reprovision_or_accept_looped_requests(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 2));
    network_t old_node = self.role.state.forwarder.node_network;
    network_t old_device = self.role.state.forwarder.device_network;
    rt_sibl_send_new_gateway_request_t request = {0};
    self.role.impl.on_sibl_send_new_gateway_request(&self, &request);
    rt_sibl_provision_t reprovision = {
        .network = NETWORK(0x0A600000, 0xFFE00000), .dtr = 2,
    };
    self.role.impl.on_sibl_provision(&self, &reprovision);
    CHECK(memcmp(&self.role.state.forwarder.node_network, &old_node, sizeof(old_node)) == 0);
    CHECK(memcmp(&self.role.state.forwarder.device_network, &old_device, sizeof(old_device)) == 0);
    CHECK(ap_settings_count == 0 && network_settings_count == 0);
    unsigned old_refreshes = refresh_count;
    rt_peer_new_gateway_request_t loop = {0};
    loop.hag_networks[0] = old_node;
    self.role.impl.on_peer_new_gateway_request(&self, &loop);
    CHECK(refresh_count == old_refreshes);
    CHECK(self.node_state.routing_table.count == 0);
    rt_peer_update_dtr_t overflow = {.dtr = UINT32_MAX};
    self.role.impl.on_peer_update_dtr(&self, &overflow);
    CHECK(self.role.state.forwarder.dtr == 0);
    self.role.state.forwarder.local_state = LOCAL_STATE_NOT_CONNECTED;
    rt_peer_new_gateway_response_t stale = {.dtr = 1};
    self.role.impl.on_peer_new_gateway_response(&self, &stale);
    CHECK(self.role.state.forwarder.dtr == 0);
    return 0;
}

EXPORT int test_root_requests_do_not_postpone_response(void) {
    reset_capture();
    routing_t root;
    memset(&root, 0, sizeof(root));
    CHECK(create_root_core(&root, 0x0A000000, 0xFF000000));
    rt_sibl_send_new_gateway_request_t request = {0};
    root.role.impl.on_sibl_send_new_gateway_request(&root, &request);
    for (unsigned i = 0; i < 10; ++i) {
        root.role.impl.on_tick(&root, 1000);
        if (i < 9)
            root.role.impl.on_sibl_send_new_gateway_request(&root, &request);
    }
    CHECK(sibling_count == 1 && sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER);
    CHECK(sibling_event.payload.new_gateway_winner.dtr == 1);
    return 0;
}

EXPORT int test_tick_waits_for_routing_token_and_no_route_is_explicit(void) {
    reset_capture();
    routing_t self;
    CHECK(rt_create(&self, NULL, NULL, NULL, NULL, ORIENTATION_EAST));
    CHECK(rt_init_forwarder(&self));
    self.role.state.forwarder.local_state = LOCAL_STATE_CONNECTED;
    self.role.state.forwarder.global_state = GLOBAL_STATE_WITH_NETWORK;
    self.role.state.forwarder.dtr = 2;
    self.role.state.forwarder.node_network = NETWORK(0x0A400000, 0xFFE00000);
    rt_sibl_send_new_gateway_request_t request = {0};
    self.role.impl.on_sibl_send_new_gateway_request(&self, &request);
    CHECK(peer_count == 1);
    rt_on_tick(&self, GATEWAY_RETRY_TIMEOUT_MS - 1);
    rt_on_tick(&self, 1);
    CHECK(token_request_count == 1);
    CHECK(peer_count == 1);
    CHECK(token_callback != NULL);
    token_callback(token_context);
    CHECK(peer_count == 2);
    CHECK(self.pending_tick_ms == 0);
    routing_table_add(&self.node_state.routing_table, 0, 0, ORIENTATION_EAST);
    routing_table_add(&self.node_state.routing_table, 0x0A000000, 0xFF000000, ORIENTATION_WEST);
    routing_table_remove_by_output(&self.node_state.routing_table, ORIENTATION_EAST);
    CHECK(rt_do_route(&self, 0, 0x01010101) == ROUTE_NONE);
    CHECK(rt_do_route(&self, 0, 0x0AFFFFFE) == ROUTE_SPI);
    return 0;
}

EXPORT int test_handshake_rejects_unreachable_or_invalid_initial_allocation(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 0));
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A000000, 0xFF000000),
        .provided_network = NETWORK(0x0A600000, 0xFFE00000), .dtr = 1,
    };
    const uint32_t bad_distances[] = {0, UINT32_MAX, UINT32_MAX - 1};
    for (unsigned i = 0; i < 3; ++i) {
        hello.dtr = bad_distances[i];
        self.role.impl.on_peer_handshake(&self, &hello);
        CHECK(self.role.state.forwarder.node_network.mask == 0);
    }
    hello.dtr = 1;
    hello.provided_network.mask = 0xFFFFFE00;
    hello.provided_network.addr = 0x0A600001;  // Non-canonical network.
    self.role.impl.on_peer_handshake(&self, &hello);
    CHECK(self.role.state.forwarder.node_network.mask == 0);
    hello.provided_network.addr = 0x0A600000;
    hello.provided_network.mask = 0xFFFFFFE0;  // /27 cannot allocate four /30 AP links and home.
    self.role.impl.on_peer_handshake(&self, &hello);
    CHECK(self.role.state.forwarder.node_network.mask == 0);
    CHECK(network_settings_count == 0 && sibling_count == 0);

    CHECK(forwarder(&self, ORIENTATION_EAST, 2));
    self.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    memset(&hello, 0, sizeof(hello));
    self.role.impl.on_peer_handshake(&self, &hello);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(self.node_state.routing_table.count == 0);
    return 0;
}

EXPORT int test_retry_recovers_lost_sibling_request_and_pauses_for_ota(void) {
    reset_capture();
    routing_t self;
    ring_share_t rs = {0};
    CHECK(rt_create(&self, &rs, NULL, NULL, NULL, ORIENTATION_EAST));
    CHECK(rt_init_forwarder(&self));
    self.role.state.forwarder.node_network = NETWORK(0x0A400000, 0xFFE00000);
    self.role.state.forwarder.local_state = LOCAL_STATE_CONNECTED;
    self.role.state.forwarder.global_state = GLOBAL_STATE_WITH_NETWORK;
    self.role.state.forwarder.is_local_root = true;
    self.role.state.forwarder.dtr = 2;
    self.role.impl.on_peer_lost(&self, NULL);
    CHECK(sibling_count == 1 && peer_count == 0);
    rt_on_tick(&self, 1000);
    CHECK(token_request_count == 1);
    /* Simulate a lost token request: later ticks ask for it again. */
    rt_on_tick(&self, 3000);
    CHECK(token_request_count == 2);
    uint32_t pending = self.pending_tick_ms;
    maintenance = true;
    rt_on_tick(&self, GATEWAY_RETRY_TIMEOUT_MS);
    CHECK(self.pending_tick_ms == pending && token_request_count == 2);
    maintenance = false;
    token_callback(token_context);
    CHECK(sibling_count == 1);
    rt_on_tick(&self, GATEWAY_RETRY_TIMEOUT_MS);
    token_callback(token_context);
    CHECK(sibling_count == 2 && peer_count == 0);
    CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    CHECK(sibling_event.payload.send_new_gateway_request.hag_networks[0].addr == 0);
    CHECK(self.role.state.forwarder.node_network.addr == 0x0A400000);
    return 0;
}

EXPORT int test_idle_nodes_do_not_circulate_routing_tokens(void) {
    reset_capture();
    routing_t radio, root;
    CHECK(forwarder(&radio, ORIENTATION_NORTH, 0));
    memset(&root, 0, sizeof(root));
    root.orientation = ORIENTATION_CENTER;
    CHECK(create_root_core(&root, 0x0A000000, 0xFF000000));
    CHECK(!radio.role.impl.tick_needed(&radio));
    CHECK(!root.role.impl.tick_needed(&root));
    for (unsigned i = 0; i < 120; ++i) {
        rt_on_tick(&radio, 1000);
        rt_on_tick(&root, 1000);
    }
    CHECK(radio.pending_tick_ms == 0 && root.pending_tick_ms == 0);
    radio.role.state.forwarder.global_state = GLOBAL_STATE_ON_GW_REQUEST;
    radio.role.state.forwarder.gateway_retry_timeout = 30000;
    CHECK(radio.role.impl.tick_needed(&radio));
    rt_on_tick(&radio, 1000);
    CHECK(radio.pending_tick_ms == 1000);
    root.role.state.root.gateway_requested = true;
    CHECK(root.role.impl.tick_needed(&root));
    rt_on_tick(&root, 1000);
    CHECK(root.pending_tick_ms == 1000);
    return 0;
}

EXPORT int test_lost_request_retries_pending_loss_without_timer(void) {
    reset_capture();
    routing_t self;
    CHECK(rt_create(&self,NULL,NULL,NULL,NULL,ORIENTATION_NORTH));
    CHECK(rt_init_forwarder(&self));
    self.role.state.forwarder.local_state=LOCAL_STATE_CONNECTED;
    self.role.state.forwarder.global_state=GLOBAL_STATE_WITH_NETWORK;
    self.role.state.forwarder.dtr=2;
    self.role.state.forwarder.is_local_root=true;
    self.role.state.forwarder.node_network=NETWORK(0x0A200000,0xFFE00000);
    routing_table_add(&self.node_state.routing_table,0,0,ORIENTATION_NORTH);
    captured_wireless.on_peer_lost(captured_wireless_context,0x0A200000,0xFFFFFFFC);
    CHECK(token_request_count==1 && self.external_queue.count==1);
    for (unsigned i=0;i<120;++i) rt_on_tick(&self,1000);
    CHECK(token_request_count>1 && self.external_queue.count==1);
    token_callback(token_context);
    CHECK(self.external_queue.count==0);
    CHECK(self.node_state.routing_table.default_gateway==ROUTING_OUTPUT_NONE);
    CHECK(self.role.state.forwarder.global_state==GLOBAL_STATE_ON_GW_REQUEST);
    return 0;
}
