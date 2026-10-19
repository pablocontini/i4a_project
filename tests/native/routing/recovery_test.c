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
static unsigned mutex_errors, token_requests, routed_callbacks, observed_tick_ms;
static unsigned deferred_count;
static unsigned network_change_count, ap_enable_count;
static bool publish_success;
static mutex_t *created_mutexes[32];
static unsigned created_mutex_count;
static wireless_callbacks_t wireless_callbacks;
static void *wireless_context;
static ring_callback_t routing_callback;
static sync_cs_callback_fn_t critical_callback;
static void *critical_context;

bool mutex_create(mutex_t *lock) {
    if (created_mutex_count >= 32) return false;
    created_mutexes[created_mutex_count++] = lock;
    *lock = (void *)(uintptr_t)(0x1000 + created_mutex_count);
    return true;
}
static bool valid_mutex(mutex_t *lock) {
    for (unsigned i = 0; i < created_mutex_count; ++i)
        if (created_mutexes[i] == lock) return true;
    ++mutex_errors;
    return false;
}
bool mutex_lock(mutex_t *lock) { return valid_mutex(lock); }
bool mutex_unlock(mutex_t *lock) { return valid_mutex(lock); }
void mutex_destroy(mutex_t *lock) { if (valid_mutex(lock)) *lock = NULL; }
void os_log(os_log_level_t level, const char *tag, const char *fmt, ...) {
    (void)level; (void)tag; (void)fmt;
}
void os_panic(const char *fmt, ...) { (void)fmt; }
void node_set_network_settings(uint32_t network, uint32_t mask) {
    (void)network; (void)mask; ++network_change_count;
}
void wl_enable_ap_mode(wireless_t *wl, uint32_t network, uint32_t mask) {
    (void)wl; (void)network; (void)mask; ++ap_enable_count;
}
bool ss_refresh(shared_state_t *ss, component_id_t component) {
    (void)ss; (void)component; ++refresh_count;
    return publish_success;
}
void ss_watch(shared_state_t *ss, component_id_t component, shared_data_t data) {
    (void)ss; (void)component; (void)data;
}
void wl_register_peer_callbacks(wireless_t *wl, wireless_callbacks_t callbacks, void *ctx) {
    (void)wl; wireless_callbacks = callbacks; wireless_context = ctx;
}
void sync_register_critical_section(sync_t *sync, component_id_t component,
                                    sync_cs_callback_fn_t callback, void *ctx) {
    (void)sync; (void)component; critical_callback = callback; critical_context = ctx;
}
void sync_request_critical_section(sync_t *sync, component_id_t component) {
    (void)sync; (void)component; ++token_requests;
}
void sync_on_tick(sync_t *sync, uint32_t dt_ms) { (void)sync; (void)dt_ms; }
void sync_defer_critical_section(sync_t *sync, component_id_t component) {
    (void)sync; (void)component; ++deferred_count;
}
void rs_register_component(ring_share_t *rs, component_id_t component, ring_callback_t callback) {
    (void)rs; if (component == RS_ROUTING) routing_callback = callback;
}
void os_delay_ms(uint32_t dt_ms) { (void)dt_ms; }
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
    mutex_errors = token_requests = routed_callbacks = observed_tick_ms = 0;
    deferred_count = 0;
    network_change_count = ap_enable_count = 0;
    created_mutex_count = 0;
    publish_success = true;
    memset(&routing_callback, 0, sizeof(routing_callback));
}

static bool forwarder(routing_t *self, orientation_t orientation, uint32_t dtr) {
    memset(self, 0, sizeof(*self));
    self->orientation = orientation;
    if (!mutex_create(&self->node_state.m_lock) || !mutex_create(&self->queue_lock)) return false;
    routing_table_init(&self->node_state.routing_table, ORIENTATION_CENTER);
    if (!create_forwarder_core(self)) return false;
    if (dtr) {
        self->role.state.forwarder.global_state = GLOBAL_STATE_WITH_NETWORK;
        self->role.state.forwarder.local_state = LOCAL_STATE_CONNECTED;
        self->role.state.forwarder.node_network = dtr == 1 ?
            NETWORK(0x0A000000, 0xFF000000) : NETWORK(0x0A400000, 0xFFE00000);
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

EXPORT int test_unprovisioned_peer_handshake_preserves_provider_gateway(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_WEST, 1));
    rt_peer_handshake_t hello = {0};
    self.role.impl.on_peer_handshake(&self, &hello);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_CENTER);
    CHECK(self.node_state.routing_table.count == 0 && !self.pending_publish);
    CHECK(self.role.state.forwarder.dtr == 1);
    return 0;
}

EXPORT int test_reconnected_provisioned_node_restores_gateway_without_renumbering(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_SOUTH, 2));
    network_t original = self.role.state.forwarder.node_network;
    self.role.state.forwarder.is_local_root = true;
    self.node_state.routing_table.default_gateway = ORIENTATION_SOUTH;
    self.role.impl.on_peer_lost(&self, NULL);
    CHECK(self.node_state.routing_table.default_gateway == 0);
    self.role.impl.on_peer_connected(&self, NULL);
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A000000, 0xFF000000),
        .provided_network = NETWORK(0x0A800000, 0xFFE00000), .dtr = 1,
    };
    self.role.impl.on_peer_handshake(&self, &hello);
    CHECK(self.role.state.forwarder.dtr == 2 && self.role.state.forwarder.is_local_root);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_SOUTH);
    CHECK(self.role.state.forwarder.node_network.addr == original.addr);
    CHECK(self.role.state.forwarder.node_network.mask == original.mask);
    CHECK(sibling_event.event_id == SIBL_UPDATE_DTR);
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

EXPORT int test_lost_original_uplink_recovers_through_existing_root_link(void) {
    reset_capture();
    routing_t original, alternate, root_radio, root_center;
    CHECK(forwarder(&original, ORIENTATION_SOUTH, 2));
    CHECK(forwarder(&alternate, ORIENTATION_WEST, 2));
    CHECK(forwarder(&root_radio, ORIENTATION_WEST, 1));
    memset(&root_center, 0, sizeof(root_center));
    root_center.orientation = ORIENTATION_CENTER;
    CHECK(mutex_create(&root_center.node_state.m_lock));
    CHECK(mutex_create(&root_center.queue_lock));
    CHECK(create_root_core(&root_center, 0x0A000000, 0xFF000000));
    original.role.state.forwarder.is_local_root = true;
    original.node_state.routing_table.default_gateway = ORIENTATION_SOUTH;
    routing_table_add(&original.node_state.routing_table, 0x0A000000, 0xFF000000, ORIENTATION_WEST);
    alternate.node_state.routing_table = original.node_state.routing_table;

    /* A redundant 10/8 route works while internet still uses the lost S uplink. */
    original.role.impl.on_peer_lost(&original, NULL);
    CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    rt_sibl_send_new_gateway_request_t sibling_request = sibling_event.payload.send_new_gateway_request;
    alternate.node_state.routing_table = original.node_state.routing_table;
    CHECK(routing_table_route(&alternate.node_state.routing_table, 0x0AFFFFFE) == ORIENTATION_WEST);
    CHECK(routing_table_route(&alternate.node_state.routing_table, 0x01010101) == 0);

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

EXPORT int test_route_and_destroy_use_mutex_addresses(void) {
    reset_capture();
    routing_t self;
    CHECK(rt_create(&self, NULL, NULL, NULL, NULL, ORIENTATION_SOUTH));
    self.node_state.routing_table.default_gateway = ORIENTATION_SOUTH;
    CHECK(rt_do_route(&self, 0, 0x01010101) == ROUTE_WIFI);
    self.node_state.routing_table.default_gateway = ORIENTATION_WEST;
    CHECK(rt_do_route(&self, 0, 0x01010101) == ROUTE_SPI);
    self.node_state.routing_table.default_gateway = 0;
    CHECK(rt_do_route(&self, 0, 0x01010101) == ROUTE_NONE);
    rt_destroy(&self);
    CHECK(mutex_errors == 0);
    CHECK(self.node_state.m_lock == NULL && self.queue_lock == NULL);
    return 0;
}

static void count_sibling(routing_t *self, const rt_sibl_update_dtr_t *event) {
    (void)self; (void)event; ++routed_callbacks;
}
static void enqueue_sibling_from_peer(routing_t *self, const network_t *connection) {
    (void)self; (void)connection;
    rt_sibl_event_t event = { .event_id = SIBL_UPDATE_DTR, .payload.update_dtr.dtr = 2 };
    routing_callback.callback(routing_callback.context, (const uint8_t *)&event, sizeof(event));
}

EXPORT int test_sibling_arrival_during_peer_batch_is_not_erased(void) {
    reset_capture();
    routing_t self;
    CHECK(rt_create(&self, NULL, NULL, NULL, NULL, ORIENTATION_SOUTH));
    self.role.impl.on_peer_connected = enqueue_sibling_from_peer;
    self.role.impl.on_sibl_update_dtr = count_sibling;
    rt_on_start(&self);
    critical_callback(critical_context);
    wireless_callbacks.on_peer_connected(wireless_context, 0x0A400000, 0xFFE00000);
    critical_callback(critical_context);
    CHECK(self.internal_queue.count == 1);
    CHECK(routed_callbacks == 0);
    critical_callback(critical_context);
    CHECK(routed_callbacks == 1 && self.internal_queue.count == 0);
    CHECK(mutex_errors == 0);
    return 0;
}

static void count_tick(routing_t *self, uint32_t dt_ms) { (void)self; observed_tick_ms += dt_ms; }
EXPORT int test_ticks_run_inside_routing_transaction(void) {
    reset_capture();
    routing_t self;
    CHECK(rt_create(&self, NULL, NULL, NULL, NULL, ORIENTATION_CENTER));
    self.role.impl.on_tick = count_tick;
    rt_on_tick(&self, 1000);
    rt_on_tick(&self, 500);
    CHECK(observed_tick_ms == 0);
    CHECK(token_requests == 2);
    critical_callback(critical_context);
    CHECK(observed_tick_ms == 1500 && self.pending_tick_ms == 0);
    critical_callback(critical_context);
    CHECK(observed_tick_ms == 1500 && mutex_errors == 0);
    return 0;
}

static void change_route(routing_t *self, const network_t *connection) {
    (void)connection; ++routed_callbacks;
    add_global_route(self, &NETWORK(0, 0), ORIENTATION_SOUTH);
}
EXPORT int test_failed_table_publication_blocks_next_batch_until_retry(void) {
    reset_capture();
    routing_t self;
    CHECK(rt_create(&self, NULL, NULL, NULL, NULL, ORIENTATION_CENTER));
    self.role.impl.on_peer_connected = change_route;
    publish_success = false;
    wireless_callbacks.on_peer_connected(wireless_context, 0, 0);
    critical_callback(critical_context);
    CHECK(routed_callbacks == 1 && refresh_count == 1 && self.pending_publish);
    CHECK(deferred_count == 1);
    wireless_callbacks.on_peer_connected(wireless_context, 0, 0);
    critical_callback(critical_context);
    CHECK(routed_callbacks == 1 && self.external_queue.count == 1 && refresh_count == 2);
    CHECK(deferred_count == 2);
    rt_on_tick(&self, 1000);
    CHECK(token_requests == 3);
    publish_success = true;
    critical_callback(critical_context);
    CHECK(routed_callbacks == 1 && self.external_queue.count == 1 && refresh_count == 3);
    CHECK(!self.pending_publish && deferred_count == 2);
    critical_callback(critical_context);
    CHECK(routed_callbacks == 2 && self.external_queue.count == 0);
    CHECK(refresh_count == 4 && !self.pending_publish && mutex_errors == 0);
    return 0;
}

EXPORT int test_repeated_requests_do_not_postpone_root_response(void) {
    reset_capture();
    routing_t self = {0};
    CHECK(create_root_core(&self, 0x0A000000, 0xFF000000));
    rt_sibl_send_new_gateway_request_t request = {0};
    self.role.impl.on_sibl_send_new_gateway_request(&self, &request);
    self.role.impl.on_tick(&self, 9000);
    self.role.impl.on_sibl_send_new_gateway_request(&self, &request);
    self.role.impl.on_tick(&self, 1000);
    CHECK(sibling_count == 1 && sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER);
    return 0;
}

EXPORT int test_gateway_request_retries_after_loss_and_stops_on_winner(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_WEST, 2));
    rt_sibl_send_new_gateway_request_t request = {0};
    self.role.impl.on_sibl_send_new_gateway_request(&self, &request);
    CHECK(peer_count == 1);
    self.role.impl.on_tick(&self, 29999);
    CHECK(peer_count == 1);
    self.role.impl.on_tick(&self, 1);
    CHECK(peer_count == 2 && peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    CHECK(peer_event.payload.new_gateway_request.hag_networks[0].addr == 0x0A400000);
    rt_peer_new_gateway_response_t winner = { .dtr = 1 };
    self.role.impl.on_peer_new_gateway_response(&self, &winner);
    self.role.impl.on_tick(&self, 60000);
    CHECK(peer_count == 2 && self.role.state.forwarder.dtr == 2);
    return 0;
}

EXPORT int test_lost_non_gateway_link_reannounces_return_path(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_SOUTH, 3));
    self.node_state.routing_table.default_gateway = ORIENTATION_WEST;
    self.role.impl.on_peer_lost(&self, NULL);
    CHECK(sibling_count == 1 && sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_WEST);
    CHECK(self.role.state.forwarder.dtr == 0);
    reset_capture();
    CHECK(forwarder(&self, ORIENTATION_SOUTH, 1));
    self.role.impl.on_peer_lost(&self, NULL);
    CHECK(sibling_count == 0);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_CENTER);
    return 0;
}

EXPORT int test_hag_cycle_does_not_route_own_network_through_peer(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_WEST, 2));
    rt_peer_new_gateway_request_t request = {0};
    request.hag_networks[0] = NETWORK(0x0A400000, 0xFFE00000);
    self.role.impl.on_peer_new_gateway_request(&self, &request);
    CHECK(self.node_state.routing_table.count == 0 && sibling_count == 0);
    CHECK(self.role.state.forwarder.dtr == 2);
    return 0;
}

EXPORT int test_invalid_distance_cannot_overflow_or_provision(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_SOUTH, 0));
    rt_peer_handshake_t hello = { .dtr = UINT32_MAX };
    self.role.impl.on_peer_handshake(&self, &hello);
    CHECK(self.role.state.forwarder.dtr == 0 && sibling_count == 0);
    rt_peer_update_dtr_t update = { .dtr = UINT32_MAX };
    self.role.impl.on_peer_update_dtr(&self, &update);
    CHECK(self.role.state.forwarder.dtr == 0 && sibling_count == 0);
    return 0;
}

EXPORT int test_link_loss_survives_saturated_peer_queue(void) {
    /* Exercise both a queue of wire messages and an all-lifecycle queue. */
    for (unsigned lifecycle = 0; lifecycle < 2; ++lifecycle) {
        reset_capture();
        routing_t self;
        CHECK(rt_create(&self, NULL, NULL, NULL, NULL, ORIENTATION_SOUTH));
        CHECK(rt_init_forwarder(&self));
        self.role.state.forwarder.global_state = GLOBAL_STATE_WITH_NETWORK;
        self.role.state.forwarder.local_state = LOCAL_STATE_CONNECTED;
        self.role.state.forwarder.is_local_root = true;
        self.role.state.forwarder.dtr = 2;
        self.node_state.routing_table.default_gateway = ORIENTATION_SOUTH;
        rt_peer_event_t event = { .event_id = PEER_UPDATE_DTR, .payload.update_dtr.dtr = 3 };
        for (unsigned i = 0; i < MAX_PEER_EVENT_QUEUED; ++i) {
            if (lifecycle)
                wireless_callbacks.on_peer_connected(wireless_context, 0, 0);
            else
                wireless_callbacks.on_peer_message(wireless_context, (const uint8_t *)&event, sizeof(event));
        }
        CHECK(self.external_queue.count == MAX_PEER_EVENT_QUEUED);
        wireless_callbacks.on_peer_lost(wireless_context, 0, 0);
        CHECK(self.external_queue.count == MAX_PEER_EVENT_QUEUED);
        CHECK(self.external_queue.queue[MAX_PEER_EVENT_QUEUED - 1].event_id == PEER_LOST);
        critical_callback(critical_context);
        CHECK(self.external_queue.count == 0);
        CHECK(self.role.state.forwarder.local_state == LOCAL_STATE_NOT_CONNECTED);
        CHECK(self.node_state.routing_table.default_gateway == 0);
        CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
        CHECK(mutex_errors == 0);
    }
    return 0;
}

EXPORT int test_subnet_validation_rejects_invalid_provisions_before_mutation(void) {
    const network_t invalid[] = {
        {0, 0}, {0, 0xFF000000}, {0x0A000000, 0}, {0x0A000000, 0xFF00FF00},
        {0x0A000001, 0xFF000000},
        {0x0A400000, 0xFFFFFFF0}, /* /28 would leave only /31 sectors. */
        {0x0A400000, 0xFFFFFFF8}, /* /29 previously shifted by 32. */
        {0x0A400000, 0xFFFFFFFF}, /* /32 previously used a negative shift. */
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reset_capture();
        routing_t self;
        CHECK(forwarder(&self, ORIENTATION_SOUTH, 0));
        rt_forwarder_state_t before = self.role.state.forwarder;
        rt_routing_table_t before_table = self.node_state.routing_table;
        rt_peer_handshake_t hello = {
            .external_network = NETWORK(0x0A000000, 0xFF000000),
            .provided_network = invalid[i], .dtr = 1,
        };
        self.role.impl.on_peer_handshake(&self, &hello);
        CHECK(memcmp(&before, &self.role.state.forwarder, sizeof(before)) == 0);
        CHECK(memcmp(&before_table, &self.node_state.routing_table, sizeof(before_table)) == 0);
        rt_sibl_provision_t provision = { .provider_id = ORIENTATION_WEST, .dtr = 2, .network = invalid[i] };
        self.role.impl.on_sibl_provision(&self, &provision);
        CHECK(memcmp(&before, &self.role.state.forwarder, sizeof(before)) == 0);
        CHECK(network_change_count == 0 && ap_enable_count == 0 && sibling_count == 0);
        CHECK(!self.pending_publish);
        self.orientation = ORIENTATION_CENTER;
        CHECK(create_home_core(&self));
        self.role.impl.on_sibl_provision(&self, &provision);
        CHECK(!self.role.state.home.is_provisioned);
        CHECK(memcmp(&before_table, &self.node_state.routing_table, sizeof(before_table)) == 0);
        CHECK(ap_enable_count == 0 && !self.pending_publish);
        CHECK(!create_root_core(&self, invalid[i].addr, invalid[i].mask));
    }

    /* Boundary /27 keeps /30 sectors and the established orientation mapping. */
    const network_t smallest = {0x0A400000, 0xFFFFFFE0};
    for (unsigned orientation = ORIENTATION_NORTH; orientation <= ORIENTATION_CENTER; ++orientation) {
        network_t subnet = get_node_subnet(&smallest, (orientation_t)orientation);
        CHECK(subnet.mask == 0xFFFFFFFC);
        CHECK(subnet.addr == (smallest.addr | (orientation << 2)));
    }
    network_t subnet = get_node_subnet(&smallest, (orientation_t)0);
    CHECK(subnet.addr == 0 && subnet.mask == 0);
    subnet = get_node_subnet(&smallest, (orientation_t)(ORIENTATION_CENTER + 1));
    CHECK(subnet.addr == 0 && subnet.mask == 0);
    subnet = get_node_subnet(NULL, ORIENTATION_SOUTH);
    CHECK(subnet.addr == 0 && subnet.mask == 0);

    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_SOUTH, 0));
    rt_peer_handshake_t valid = {
        .external_network = NETWORK(0x0A000000, 0xFF000000),
        .provided_network = smallest, .dtr = 1,
    };
    self.role.impl.on_peer_handshake(&self, &valid);
    CHECK(self.role.state.forwarder.node_network.mask == smallest.mask);
    CHECK(self.role.state.forwarder.device_network.mask == 0xFFFFFFFC);
    CHECK(self.role.state.forwarder.device_network.addr == (smallest.addr | (ORIENTATION_SOUTH << 2)));
    CHECK(network_change_count == 1 && self.role.state.forwarder.dtr == 2);

    /* Reconnection retains the original block; a new offered block is unused. */
    self.role.impl.on_peer_lost(&self, NULL);
    self.role.impl.on_peer_connected(&self, NULL);
    valid.provided_network = NETWORK(0x0A800000, UINT32_MAX);
    self.role.impl.on_peer_handshake(&self, &valid);
    CHECK(self.role.state.forwarder.node_network.addr == smallest.addr);
    CHECK(self.role.state.forwarder.node_network.mask == smallest.mask);
    CHECK(network_change_count == 1 && self.role.state.forwarder.dtr == 2);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_SOUTH);
    return 0;
}
