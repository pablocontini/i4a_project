#include <string.h>

#include "forwarder.h"
#include "impl_priv.h"
#include "utils.h"
#include "neighbor_manager/blocks.h"

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
static bool offer_override;
static nm_network_t reserved_offer;
static bool reject_peer_claim;
static bool publish_success;
static unsigned provision_failures_remaining;
static unsigned gateway_response_failures_remaining, panic_count;
static unsigned peer_send_failures_remaining;
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
void os_panic(const char *fmt, ...) { (void)fmt; ++panic_count; }
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
    if (sibling_event.event_id == SIBL_PROVISION && provision_failures_remaining) {
        --provision_failures_remaining;
        return false;
    }
    if (sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER && gateway_response_failures_remaining) {
        --gateway_response_failures_remaining;
        return false;
    }
    return true;
}
bool wl_send_peer_message(wireless_t *wl, const void *data, uint16_t len) {
    (void)wl;
    if (len != sizeof(peer_event)) return false;
    memcpy(&peer_event, data, len);
    ++peer_count;
    if (peer_send_failures_remaining) {
        --peer_send_failures_remaining;
        return false;
    }
    return true;
}

bool wl_get_provision_offer(wireless_t *wl, uint32_t *network, uint32_t *mask) {
    (void)wl;
    if (!offer_override) return false;
    *network = reserved_offer.addr; *mask = reserved_offer.mask;
    return true;
}
bool wl_check_peer_network(wireless_t *wl, uint32_t network, uint32_t mask) {
    (void)wl; (void)network; (void)mask;
    return !reject_peer_claim;
}

static void reset_capture(void) {
    memset(&sibling_event, 0, sizeof(sibling_event));
    memset(&peer_event, 0, sizeof(peer_event));
    sibling_count = peer_count = refresh_count = 0;
    mutex_errors = token_requests = routed_callbacks = observed_tick_ms = 0;
    deferred_count = 0;
    network_change_count = ap_enable_count = 0;
    offer_override = false;
    reject_peer_claim = false;
    reserved_offer = (nm_network_t){0};
    created_mutex_count = 0;
    publish_success = true;
    provision_failures_remaining = 0;
    gateway_response_failures_remaining = panic_count = 0;
    peer_send_failures_remaining = 0;
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

EXPORT int test_moved_block_is_not_redelegated_to_a_new_node(void) {
    reset_capture();
    nm_blocks_t blocks;
    nm_blocks_init(&blocks, "000000000000");
    nm_network_t parent = {0x0A000000, 0xFF000000}, empty = {0};
    routing_t root_e, root_s, grillo_e, grillo_n, ramona_e;
    CHECK(forwarder(&root_e, ORIENTATION_EAST, 1));
    CHECK(forwarder(&root_s, ORIENTATION_SOUTH, 1));
    CHECK(forwarder(&grillo_e, ORIENTATION_EAST, 0));
    CHECK(forwarder(&grillo_n, ORIENTATION_NORTH, 2));
    CHECK(forwarder(&ramona_e, ORIENTATION_EAST, 0));
    root_e.role.state.forwarder.device_network = NETWORK(0x0A600000, 0xFFE00000);
    CHECK(nm_blocks_offer(&blocks, "142B2FC05E7D", parent, empty, 3, &reserved_offer));
    offer_override = true;
    root_e.role.impl.on_peer_connected(&root_e, NULL);
    rt_peer_handshake_t hello = peer_event.payload.handshake;
    grillo_e.role.impl.on_peer_handshake(&grillo_e, &hello);
    CHECK(grillo_e.role.state.forwarder.node_network.addr == 0x0A600000);
    grillo_n.role.state.forwarder.node_network = grillo_e.role.state.forwarder.node_network;
    grillo_n.node_state.routing_table.default_gateway = 0;
    hello = (rt_peer_handshake_t){.external_network=NETWORK(0x0A000000,0xFF000000),
        .provided_network=NETWORK(0x0A400000,0xFFE00000),.dtr=1};
    grillo_n.role.impl.on_peer_handshake(&grillo_n, &hello);
    CHECK(grillo_n.role.state.forwarder.node_network.addr == 0x0A600000);
    hello = (rt_peer_handshake_t){.external_network=grillo_n.role.state.forwarder.node_network,.dtr=2};
    root_s.role.impl.on_peer_handshake(&root_s, &hello);
    root_e.node_state.routing_table = root_s.node_state.routing_table;
    CHECK(nm_blocks_offer(&blocks, "20E7C85AA245", parent, empty, 3, &reserved_offer));
    CHECK(reserved_offer.addr != 0x0A600000);
    root_e.role.impl.on_peer_connected(&root_e, NULL);
    hello = peer_event.payload.handshake;
    CHECK(hello.provided_network.addr == reserved_offer.addr);
    ramona_e.role.impl.on_peer_handshake(&ramona_e, &hello);
    CHECK(ramona_e.role.state.forwarder.node_network.addr != grillo_n.role.state.forwarder.node_network.addr);
    hello = (rt_peer_handshake_t){.external_network=ramona_e.role.state.forwarder.node_network,.dtr=2};
    root_e.role.impl.on_peer_handshake(&root_e, &hello);
    CHECK(routing_table_route(&root_e.node_state.routing_table, 0x0A740002) == ORIENTATION_SOUTH);
    network_t ramona_home = get_node_subnet(&ramona_e.role.state.forwarder.node_network, ORIENTATION_CENTER);
    CHECK(routing_table_route(&root_e.node_state.routing_table, ramona_home.addr + 2) == ORIENTATION_EAST);
    CHECK(!mutex_errors);
    return 0;
}

EXPORT int test_changed_peer_claim_cannot_steal_a_return_route(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 1));
    routing_table_add(&self.node_state.routing_table, 0x0A600000, 0xFFE00000, ORIENTATION_SOUTH);
    reject_peer_claim = true;
    rt_peer_handshake_t hello = {.external_network=NETWORK(0x0A600000,0xFFE00000),.dtr=2};
    self.role.impl.on_peer_handshake(&self, &hello);
    CHECK(routing_table_route(&self.node_state.routing_table,0x0A740002)==ORIENTATION_SOUTH);
    rt_peer_new_gateway_response_t response = {.external_network=NETWORK(0x0A600000,0xFFE00000),.dtr=1};
    self.role.impl.on_peer_new_gateway_response(&self,&response);
    CHECK(self.node_state.routing_table.default_gateway==ORIENTATION_CENTER && !sibling_count);
    return 0;
}

EXPORT int test_gateway_messages_cannot_replace_initial_network_provision(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self,ORIENTATION_EAST,0));
    rt_peer_new_gateway_request_t early = {0};
    early.hag_networks[0] = NETWORK(0x0A600000, 0xFFE00000);
    self.role.impl.on_peer_new_gateway_request(&self, &early);
    CHECK(self.role.state.forwarder.global_state == GLOBAL_STATE_WITHOUT_NETWORK);
    CHECK(self.node_state.routing_table.count == 0 && !sibling_count);
    rt_peer_update_dtr_t dtr={.dtr=1};
    self.role.impl.on_peer_update_dtr(&self,&dtr);
    CHECK(self.role.state.forwarder.global_state==GLOBAL_STATE_WITHOUT_NETWORK);
    rt_peer_new_gateway_response_t response={.external_network=NETWORK(0x0A000000,0xFF000000),.dtr=1};
    self.role.impl.on_peer_new_gateway_response(&self,&response);
    CHECK(self.role.state.forwarder.global_state==GLOBAL_STATE_WITHOUT_NETWORK);
    rt_sibl_update_dtr_t sibling_dtr={.dtr=2};
    self.role.impl.on_sibl_update_dtr(&self,&sibling_dtr);
    CHECK(self.role.state.forwarder.global_state==GLOBAL_STATE_WITHOUT_NETWORK);
    rt_sibl_new_gateway_winner_t winner={.network=NETWORK(0x0A000000,0xFF000000),.dtr=2};
    self.role.impl.on_sibl_new_gateway_winner(&self,&winner);
    rt_sibl_send_new_gateway_request_t request={0};
    self.role.impl.on_sibl_send_new_gateway_request(&self,&request);
    CHECK(self.role.state.forwarder.global_state==GLOBAL_STATE_WITHOUT_NETWORK && !peer_count);
    rt_sibl_provision_t provision={.network=NETWORK(0x0A600000,0xFFE00000),.provider_id=ORIENTATION_NORTH,.dtr=2};
    self.role.impl.on_sibl_provision(&self,&provision);
    CHECK(self.role.state.forwarder.node_network.addr==0x0A600000 && ap_enable_count==1);
    return 0;
}

EXPORT int test_delayed_provision_cannot_renumber_a_node_during_gateway_recovery(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self,ORIENTATION_EAST,2));
    self.role.state.forwarder.global_state=GLOBAL_STATE_ON_GW_REQUEST;
    network_t before=self.role.state.forwarder.node_network;
    rt_sibl_provision_t replay={.network=NETWORK(0x0A600000,0xFFE00000),.provider_id=ORIENTATION_NORTH,.dtr=2};
    self.role.impl.on_sibl_provision(&self,&replay);
    CHECK(self.role.state.forwarder.node_network.addr==before.addr);
    CHECK(self.role.state.forwarder.global_state==GLOBAL_STATE_ON_GW_REQUEST && !ap_enable_count);
    return 0;
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

EXPORT int test_home_restores_missing_client_route_without_restarting_ap(void) {
    reset_capture();
    routing_t home;
    CHECK(forwarder(&home, ORIENTATION_CENTER, 0));
    CHECK(create_home_core(&home));
    rt_sibl_provision_t provision = {
        .network = NETWORK(0x0A400000, 0xFFE00000),
        .provider_id = ORIENTATION_SOUTH, .dtr = 2,
    };
    home.role.impl.on_sibl_provision(&home, &provision);
    CHECK(ap_enable_count == 1);
    CHECK(routing_table_route(&home.node_state.routing_table, 0x0A540003) == ORIENTATION_CENTER);
    /* Replay the observed Ramona table: WAN and lateral route, no client /14. */
    routing_table_init(&home.node_state.routing_table, ORIENTATION_SOUTH);
    routing_table_add(&home.node_state.routing_table, 0x0A600000, 0xFFE00000, ORIENTATION_EAST);
    routing_table_add(&home.node_state.routing_table, 0x0A000000, 0xFF000000, ORIENTATION_SOUTH);
    CHECK(routing_table_route(&home.node_state.routing_table, 0x0A540003) == ORIENTATION_SOUTH);
    CHECK(home.role.impl.on_tick != NULL);
    home.pending_publish = false;
    home.role.impl.on_tick(&home, 1000);
    CHECK(routing_table_route(&home.node_state.routing_table, 0x0A540003) == ORIENTATION_CENTER);
    CHECK(routing_table_route(&home.node_state.routing_table, 0x01010101) == ORIENTATION_SOUTH);
    CHECK(routing_table_route(&home.node_state.routing_table, 0x0A740003) == ORIENTATION_EAST);
    CHECK(home.pending_publish && ap_enable_count == 1 && panic_count == 0);
    home.pending_publish = false;
    home.role.impl.on_tick(&home, 1000);
    CHECK(!home.pending_publish && home.node_state.routing_table.count == 3);
    CHECK(ap_enable_count == 1 && mutex_errors == 0);
    routing_table_add(&home.node_state.routing_table, 0x0A540000, 0xFFFC0000, ORIENTATION_SOUTH);
    home.role.impl.on_tick(&home, 1000);
    CHECK(routing_table_route(&home.node_state.routing_table, 0x0A540003) == ORIENTATION_CENTER);
    CHECK(home.node_state.routing_table.count == 3 && home.pending_publish);
    CHECK(ap_enable_count == 1 && panic_count == 0);
    return 0;
}

EXPORT int test_home_does_not_invent_client_route_before_provision(void) {
    reset_capture();
    routing_t home;
    CHECK(forwarder(&home, ORIENTATION_CENTER, 0));
    CHECK(create_home_core(&home));
    CHECK(home.role.impl.on_tick != NULL);
    home.role.impl.on_tick(&home, 1000);
    CHECK(home.node_state.routing_table.count == 0 && !home.pending_publish);
    CHECK(ap_enable_count == 0 && panic_count == 0);
    return 0;
}

EXPORT int test_initial_provision_retries_loss_without_renumbering_or_duplicate_ap(void) {
    reset_capture();
    routing_t provider, follower;
    CHECK(forwarder(&provider, ORIENTATION_EAST, 0));
    CHECK(forwarder(&follower, ORIENTATION_NORTH, 0));
    provider.role.impl.on_peer_connected(&provider, NULL);
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A000000, 0xFF000000),
        .provided_network = NETWORK(0x0A400000, 0xFFE00000), .dtr = 1,
    };
    provision_failures_remaining = 3;
    provider.role.impl.on_peer_handshake(&provider, &hello);
    CHECK(sibling_count == 1 && sibling_event.event_id == SIBL_PROVISION);
    /* A failed full-ring ACK can still mean an earlier sibling received it. */
    follower.role.impl.on_sibl_provision(&follower, &sibling_event.payload.provision);
    CHECK(ap_enable_count == 1);
    provider.role.impl.on_tick(&provider, 4999);
    CHECK(sibling_count == 1);
    provider.role.impl.on_tick(&provider, 1);
    CHECK(sibling_count == 2 && sibling_event.event_id == SIBL_PROVISION);
    provider.role.impl.on_tick(&provider, 5000);
    CHECK(sibling_count == 3);
    provider.role.impl.on_tick(&provider, 120000);
    CHECK(sibling_count == 4 && provision_failures_remaining == 0);
    follower.role.impl.on_sibl_provision(&follower, &sibling_event.payload.provision);
    CHECK(ap_enable_count == 1 && network_change_count == 1);
    CHECK(provider.role.state.forwarder.node_network.addr == hello.provided_network.addr);
    CHECK(follower.role.state.forwarder.node_network.addr == hello.provided_network.addr);
    CHECK(provider.node_state.routing_table.default_gateway == ORIENTATION_EAST);
    provider.role.impl.on_tick(&provider, 120000);
    CHECK(sibling_count == 4); /* No more retries once acknowledged. */
    return 0;
}

EXPORT int test_pending_provision_waits_for_uplink_and_resumes_after_reconnect(void) {
    reset_capture();
    routing_t provider;
    CHECK(forwarder(&provider, ORIENTATION_EAST, 0));
    provider.role.impl.on_peer_connected(&provider, NULL);
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A000000, 0xFF000000),
        .provided_network = NETWORK(0x0A400000, 0xFFE00000), .dtr = 1,
    };
    provision_failures_remaining = 1;
    provider.role.impl.on_peer_handshake(&provider, &hello);
    provider.role.impl.on_peer_lost(&provider, NULL);
    unsigned before = sibling_count;
    provider.role.impl.on_tick(&provider, 120000);
    CHECK(sibling_count == before); /* Do not advertise a disconnected provider. */
    provider.role.impl.on_peer_connected(&provider, NULL);
    provider.role.impl.on_peer_handshake(&provider, &hello);
    before = sibling_count;
    provider.role.impl.on_tick(&provider, 5000);
    CHECK(sibling_count == before + 1 && sibling_event.event_id == SIBL_PROVISION);
    CHECK(sibling_event.payload.provision.network.addr == hello.provided_network.addr);
    CHECK(sibling_event.payload.provision.dtr == 2 && network_change_count == 1);
    provider.role.impl.on_tick(&provider, 5000);
    CHECK(sibling_count == before + 1);
    return 0;
}

EXPORT int test_root_provision_loss_retries_without_panic_or_gateway_change(void) {
    reset_capture();
    routing_t root;
    CHECK(forwarder(&root, ORIENTATION_CENTER, 0));
    CHECK(create_root_core(&root, 0x0A000000, 0xFF000000));
    provision_failures_remaining = 2;
    root.role.impl.on_start(&root);
    CHECK(panic_count == 0 && sibling_count == 1);
    CHECK(root.node_state.routing_table.default_gateway == ORIENTATION_CENTER);
    root.role.impl.on_tick(&root, 4999);
    CHECK(sibling_count == 1);
    root.role.impl.on_tick(&root, 1);
    CHECK(sibling_count == 2 && panic_count == 0);
    root.role.impl.on_tick(&root, 5000);
    CHECK(sibling_count == 3 && sibling_event.event_id == SIBL_PROVISION);
    CHECK(sibling_event.payload.provision.network.addr == 0x0A000000);
    CHECK(sibling_event.payload.provision.dtr == 1);
    root.role.impl.on_tick(&root, 120000);
    CHECK(sibling_count == 3 && panic_count == 0);
    CHECK(root.node_state.routing_table.default_gateway == ORIENTATION_CENTER);
    return 0;
}

EXPORT int test_root_gateway_response_retries_loss_without_deadline_starvation(void) {
    reset_capture();
    routing_t root;
    CHECK(forwarder(&root, ORIENTATION_CENTER, 0));
    CHECK(create_root_core(&root, 0x0A000000, 0xFF000000));
    root.role.impl.on_start(&root);
    gateway_response_failures_remaining = 1;
    rt_sibl_send_new_gateway_request_t request = {0};
    root.role.impl.on_sibl_send_new_gateway_request(&root, &request);
    root.role.impl.on_tick(&root, 10000);
    CHECK(sibling_count == 2 && sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER);
    root.role.impl.on_sibl_send_new_gateway_request(&root, &request);
    root.role.impl.on_tick(&root, 4999);
    CHECK(sibling_count == 2);
    root.role.impl.on_tick(&root, 1);
    CHECK(sibling_count == 3 && sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER);
    CHECK(sibling_event.payload.new_gateway_winner.dtr == 1);
    CHECK(root.node_state.routing_table.default_gateway == ORIENTATION_CENTER);
    root.role.impl.on_tick(&root, 120000);
    CHECK(sibling_count == 3 && panic_count == 0);
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
    CHECK(forwarder(&self, ORIENTATION_NORTH, 2));
    /* The gateway is unknown; the provisioned node block is retained. */
    self.role.state.forwarder.dtr = 0;
    self.role.state.forwarder.global_state = GLOBAL_STATE_ON_GW_REQUEST;
    self.role.state.forwarder.local_state = LOCAL_STATE_CONNECTED;
    self.role.state.forwarder.gateway_relay_pending = true;
    self.role.state.forwarder.gateway_relay_path[0] = NETWORK(0x0A440000, 0xFFFC0000);
    rt_sibl_new_gateway_winner_t winner = {
        .network = NETWORK(0x0A000000, 0xFF000000), .dtr = 2,
    };
    winner.confirmed_path[0] = self.role.state.forwarder.gateway_relay_path[0];
    self.role.impl.on_sibl_new_gateway_winner(&self, &winner);
    CHECK(self.role.state.forwarder.dtr == 2);
    CHECK(!self.role.state.forwarder.is_local_root);
    CHECK(peer_count == 1 && peer_event.event_id == PEER_NEW_GATEWAY_RESPONSE);
    CHECK(peer_event.payload.new_gateway_response.dtr == 2);
    return 0;
}

EXPORT int test_new_radio_restores_missing_default_at_unchanged_distance(void) {
    for (unsigned delivered = 0; delivered <= 1; ++delivered) {
        reset_capture();
        routing_t north;
        CHECK(forwarder(&north, ORIENTATION_NORTH, 2));
        north.role.state.forwarder.local_state = LOCAL_STATE_NOT_CONNECTED;
        network_t original = north.role.state.forwarder.node_network;
        north.node_state.routing_table.default_gateway = ORIENTATION_EAST;
        /* Replicate removal of East's default through the shared SPI table. */
        remove_routes_by_output(&north, ORIENTATION_EAST);
        if (delivered) {
            rt_sibl_send_new_gateway_request_t request = {0};
            north.role.impl.on_sibl_send_new_gateway_request(&north, &request);
        }
        CHECK(north.role.state.forwarder.dtr == 2);
        north.role.impl.on_peer_connected(&north, NULL);
        rt_peer_handshake_t hello = {
            .external_network = NETWORK(0x0A000000, 0xFF000000),
            .provided_network = NETWORK(0x0A800000, 0xFFE00000), .dtr = 1,
        };
        north.role.impl.on_peer_handshake(&north, &hello);
        CHECK(routing_table_route(&north.node_state.routing_table, 0x0AFFFFFE) == ORIENTATION_NORTH);
        CHECK(routing_table_route(&north.node_state.routing_table, 0x01010101) == ORIENTATION_NORTH);
        CHECK(north.role.state.forwarder.is_local_root);
        CHECK(north.role.state.forwarder.node_network.addr == original.addr);
        CHECK(north.role.state.forwarder.node_network.mask == original.mask);
        CHECK(network_change_count == 0 && ap_enable_count == 0);
        CHECK(sibling_event.event_id == SIBL_UPDATE_DTR);
        CHECK(sibling_event.payload.update_dtr.dtr == 2 && mutex_errors == 0);
    }
    return 0;
}

EXPORT int test_equal_distance_update_preserves_a_working_gateway(void) {
    reset_capture();
    routing_t north;
    CHECK(forwarder(&north, ORIENTATION_NORTH, 2));
    north.node_state.routing_table.default_gateway = ORIENTATION_EAST;
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A000000, 0xFF000000), .dtr = 1,
    };
    north.role.impl.on_peer_handshake(&north, &hello);
    CHECK(north.node_state.routing_table.default_gateway == ORIENTATION_EAST);
    CHECK(!north.role.state.forwarder.is_local_root && sibling_count == 0);
    rt_peer_update_dtr_t update = { .dtr = 2 };
    north.role.impl.on_peer_update_dtr(&north, &update);
    CHECK(north.node_state.routing_table.default_gateway == ORIENTATION_EAST);
    CHECK(north.role.state.forwarder.dtr == 2 && sibling_count == 0);
    return 0;
}

EXPORT int test_equal_gateway_response_restores_only_a_missing_default(void) {
    reset_capture();
    routing_t north;
    CHECK(forwarder(&north, ORIENTATION_NORTH, 2));
    north.node_state.routing_table.default_gateway = 0;
    rt_peer_new_gateway_response_t reply = {
        .external_network = NETWORK(0x0A000000, 0xFF000000), .dtr = 1,
    };
    const uint32_t rejected[] = {0, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        reply.dtr = rejected[i];
        north.role.impl.on_peer_new_gateway_response(&north, &reply);
        rt_peer_update_dtr_t update = { .dtr = rejected[i] };
        north.role.impl.on_peer_update_dtr(&north, &update);
        CHECK(north.node_state.routing_table.default_gateway == 0);
        CHECK(north.role.state.forwarder.dtr == 2 && sibling_count == 0);
    }
    /* A longer unsolicited response still cannot replace a retained DTR.
     * A bare DTR update starts a separate path request (tested below). */
    reply.dtr = 2;
    north.role.impl.on_peer_new_gateway_response(&north, &reply);
    CHECK(north.node_state.routing_table.default_gateway == 0 && sibling_count == 0);
    reply.dtr = 1;
    north.role.impl.on_peer_new_gateway_response(&north, &reply);
    CHECK(north.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(north.role.state.forwarder.is_local_root && north.role.state.forwarder.dtr == 2);
    CHECK(sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER && sibling_count == 1);
    /* Replayed equal responses must not repeatedly publish the same winner. */
    north.role.impl.on_peer_new_gateway_response(&north, &reply);
    CHECK(sibling_count == 1 && mutex_errors == 0);
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

EXPORT int test_late_longer_uplink_confirms_path_and_recovers(void) {
    reset_capture();
    routing_t grillo, ramona_e, ramona_n, root_s, root_c;
    CHECK(forwarder(&grillo, ORIENTATION_EAST, 2));
    CHECK(forwarder(&ramona_e, ORIENTATION_EAST, 2));
    CHECK(forwarder(&ramona_n, ORIENTATION_NORTH, 2));
    CHECK(forwarder(&root_s, ORIENTATION_SOUTH, 1));
    CHECK(forwarder(&root_c, ORIENTATION_CENTER, 0));
    CHECK(create_root_core(&root_c, 0x0A000000, 0xFF000000));
    grillo.role.state.forwarder.node_network = NETWORK(0x0A200000, 0xFFE00000);
    network_t original = grillo.role.state.forwarder.node_network;
    grillo.role.state.forwarder.local_state = LOCAL_STATE_NOT_CONNECTED;
    grillo.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    routing_table_add(&grillo.node_state.routing_table, 0x0A340000, 0xFFFC0000, ORIENTATION_CENTER);
    /* The former gateway disappears while East has no peer. Its SPI request
     * is lost, leaving East with DTR 2 but no default in the shared table. */
    remove_routes_by_output(&grillo, ORIENTATION_NORTH);
    grillo.role.impl.on_peer_connected(&grillo, NULL);
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A400000, 0xFFE00000),
        .provided_network = NETWORK(0x0A480000, 0xFFFC0000), .dtr = 2,
    };
    grillo.role.impl.on_peer_handshake(&grillo, &hello);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    CHECK(grillo.node_state.routing_table.default_gateway == 0);
    CHECK(grillo.role.state.forwarder.dtr == 0);
    CHECK(peer_event.payload.new_gateway_request.hag_networks[0].addr == original.addr);
    unsigned requests = peer_count;
    rt_peer_update_dtr_t update = { .dtr = 2 };
    grillo.role.impl.on_peer_update_dtr(&grillo, &update);
    CHECK(grillo.node_state.routing_table.default_gateway == 0 && peer_count == requests);
    /* Lose the first request; a repeated handshake must not restart its timer. */
    grillo.role.impl.on_tick(&grillo, 29999);
    grillo.role.impl.on_peer_handshake(&grillo, &hello);
    grillo.role.impl.on_tick(&grillo, 1);
    CHECK(peer_count == requests + 1 && peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    rt_peer_new_gateway_request_t path = peer_event.payload.new_gateway_request;
    ramona_e.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    ramona_n.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    ramona_e.role.impl.on_peer_new_gateway_request(&ramona_e, &path);
    CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    rt_sibl_send_new_gateway_request_t relay = sibling_event.payload.send_new_gateway_request;
    ramona_n.role.impl.on_sibl_send_new_gateway_request(&ramona_n, &relay);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    path = peer_event.payload.new_gateway_request;
    CHECK(path.hag_networks[0].addr == original.addr);
    CHECK(path.hag_networks[1].addr == ramona_n.role.state.forwarder.node_network.addr);
    root_s.role.impl.on_peer_new_gateway_request(&root_s, &path);
    relay = sibling_event.payload.send_new_gateway_request;
    root_c.role.impl.on_sibl_send_new_gateway_request(&root_c, &relay);
    root_c.role.impl.on_tick(&root_c, 10000);
    CHECK(sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER);
    rt_sibl_new_gateway_winner_t winner = sibling_event.payload.new_gateway_winner;
    root_s.role.impl.on_sibl_new_gateway_winner(&root_s, &winner);
    rt_peer_new_gateway_response_t reply = peer_event.payload.new_gateway_response;
    ramona_n.role.impl.on_peer_new_gateway_response(&ramona_n, &reply);
    CHECK(sibling_event.event_id == SIBL_GATEWAY_CONFIRMATION);
    winner = sibling_event.payload.new_gateway_winner;
    ramona_e.role.impl.on_sibl_gateway_confirmation(&ramona_e, &winner);
    reply = peer_event.payload.new_gateway_response;
    CHECK(reply.dtr == 2);
    grillo.role.impl.on_peer_new_gateway_response(&grillo, &reply);
    CHECK(grillo.role.state.forwarder.dtr == 3 && grillo.role.state.forwarder.is_local_root);
    CHECK(routing_table_route(&grillo.node_state.routing_table, 0x01010101) == ORIENTATION_EAST);
    CHECK(routing_table_route(&grillo.node_state.routing_table, 0x0AFFFFFE) == ORIENTATION_EAST);
    CHECK(routing_table_route(&grillo.node_state.routing_table, 0x0A340002) == ORIENTATION_CENTER);
    CHECK(routing_table_route(&root_s.node_state.routing_table, 0x0A340002) == ORIENTATION_SOUTH);
    CHECK(routing_table_route(&ramona_e.node_state.routing_table, 0x0A340002) == ORIENTATION_EAST);
    CHECK(grillo.role.state.forwarder.node_network.addr == original.addr);
    CHECK(grillo.role.state.forwarder.gateway_request_timeout == 0);
    CHECK(network_change_count == 0 && ap_enable_count == 0 && panic_count == 0 && mutex_errors == 0);
    return 0;
}

EXPORT int test_missing_default_tick_recovers_a_lost_sibling_request(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 2));
    self.role.impl.on_tick(&self, 1000);
    CHECK(peer_count == 0); /* Existing configured gateway is preserved. */
    self.node_state.routing_table.default_gateway = 0;
    self.role.impl.on_tick(&self, 1000);
    CHECK(peer_count == 1 && peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    CHECK(self.role.state.forwarder.gateway_request_timeout == 30000);
    CHECK(self.role.state.forwarder.dtr == 0);
    self.role.impl.on_tick(&self, 29999);
    CHECK(peer_count == 1);
    self.role.impl.on_tick(&self, 1);
    CHECK(peer_count == 2);
    rt_peer_new_gateway_response_t reply = { .dtr = 2 };
    self.role.impl.on_peer_new_gateway_response(&self, &reply);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_EAST);
    CHECK(self.role.state.forwarder.dtr == 3);
    self.role.impl.on_tick(&self, 60000);
    CHECK(peer_count == 2 && mutex_errors == 0);
    return 0;
}

EXPORT int test_late_recovery_path_returning_to_origin_is_rejected(void) {
    reset_capture();
    routing_t requester, origin_radio;
    CHECK(forwarder(&requester, ORIENTATION_EAST, 2));
    CHECK(forwarder(&origin_radio, ORIENTATION_NORTH, 2));
    requester.node_state.routing_table.default_gateway = 0;
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A800000, 0xFFE00000), .dtr = 2,
    };
    requester.role.impl.on_peer_handshake(&requester, &hello);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    rt_peer_new_gateway_request_t path = peer_event.payload.new_gateway_request;
    unsigned before = sibling_count;
    origin_radio.role.impl.on_peer_new_gateway_request(&origin_radio, &path);
    CHECK(sibling_count == before && origin_radio.node_state.routing_table.count == 0);
    CHECK(requester.node_state.routing_table.default_gateway == 0);
    CHECK(requester.role.state.forwarder.gateway_request_timeout != 0);
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
    self.node_state.routing_table.default_gateway = 0;
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
    CHECK(self.role.state.forwarder.dtr == 3);
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
    CHECK(!self.role.state.forwarder.gateway_relay_pending);
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

static uint32_t diagnostic_clock(void) { return 123456; }
EXPORT int test_gateway_trace_is_bounded_and_preserves_event_order(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 2));
    network_t path[2]={NETWORK(0x0A800000,0xFFE00000),NETWORK(0,0)};
    rt_gw_trace(&self, RT_GW_REQUEST_TX, RT_GW_OK, 1, path, 2);
    CHECK(self.gw_count==0);
    self.gw_clock_ms=diagnostic_clock;
    for (unsigned i=0; i<RT_GW_TRACE_CAPACITY+3; ++i)
        rt_gw_trace(&self, RT_GW_REQUEST_TX, RT_GW_OK, i, path, 2);
    CHECK(self.gw_count==RT_GW_TRACE_CAPACITY && self.gw_dropped==3);
    rt_gw_trace_t records[4]; uint32_t dropped=0;
    CHECK(rt_gw_trace_drain(&self,records,4,&dropped)==4);
    CHECK(dropped==3 && records[0].time_ms==123456 && records[0].hops==1);
    CHECK(records[0].sequence==1 && records[3].sequence==4);
    CHECK(records[0].value==0 && records[3].value==3 && records[0].dtr==2);
    CHECK(records[0].path_hash!=0 && records[0].path_hash==records[3].path_hash);
    rt_gw_trace(&self,RT_GW_RETRY_TX,RT_GW_OK,99,NULL,0);
    while (self.gw_count>1) {
        size_t cap=self.gw_count>4?4:self.gw_count-1;
        CHECK(rt_gw_trace_drain(&self,records,cap,&dropped)==cap);
    }
    CHECK(rt_gw_trace_drain(&self,records,4,&dropped)==1);
    CHECK(records[0].sequence==36 && records[0].event==RT_GW_RETRY_TX);
    CHECK(records[0].path_hash==0 && self.gw_count==0 && mutex_errors==0);
    return 0;
}

EXPORT int test_gateway_trace_records_request_retry_and_winner_without_changing_deadlines(void) {
    reset_capture(); routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 2));
    self.node_state.routing_table.default_gateway = 0;
    self.gw_clock_ms=diagnostic_clock;
    rt_sibl_send_new_gateway_request_t request={0};
    self.role.impl.on_sibl_send_new_gateway_request(&self,&request);
    CHECK(self.role.state.forwarder.gateway_request_timeout==30000);
    self.role.impl.on_tick(&self,30000);
    rt_peer_new_gateway_response_t winner={.dtr=1};
    self.role.impl.on_peer_new_gateway_response(&self,&winner);
    rt_gw_trace_t r[4];uint32_t drops;
    CHECK(rt_gw_trace_drain(&self,r,4,&drops)==3 && drops==0);
    CHECK(r[0].event==RT_GW_REQUEST_TX && r[0].value==1 && r[0].timeout_ms==30000);
    CHECK(r[1].event==RT_GW_RETRY_TX && r[1].path_hash==r[0].path_hash);
    CHECK(r[2].event==RT_GW_DECISION && r[2].reason==RT_GW_ADOPT);
    CHECK(r[2].timeout_ms==0 && self.role.state.forwarder.dtr==2 && peer_count==2);
    return 0;
}

EXPORT int test_transit_request_preserves_working_parent_and_distance(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 2));
    self.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    network_t original = self.role.state.forwarder.node_network;
    rt_peer_new_gateway_request_t request = {0};
    request.hag_networks[0] = NETWORK(0x0A440000, 0xFFFC0000);
    self.role.impl.on_peer_new_gateway_request(&self, &request);
    CHECK(self.role.state.forwarder.dtr == 2);
    CHECK(self.role.state.forwarder.gateway_relay_pending);
    CHECK(self.role.state.forwarder.global_state == GLOBAL_STATE_WITH_NETWORK);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    /* The descendant's old path may still depend on this node. Its delayed
     * response must not become a new, longer principal or generate an echo. */
    rt_peer_new_gateway_response_t delayed = {
        .external_network = NETWORK(0x0A440000, 0xFFFC0000), .dtr = 3,
    };
    self.role.impl.on_peer_new_gateway_response(&self, &delayed);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(self.role.state.forwarder.dtr == 2 && sibling_count == 1 && !peer_count);
    CHECK(self.role.state.forwarder.node_network.addr == original.addr);
    CHECK(!network_change_count && !ap_enable_count && !mutex_errors);
    return 0;
}

EXPORT int test_transit_retry_preserves_live_uplink_and_original_deadline(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_NORTH, 2));
    self.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    self.role.state.forwarder.is_local_root = true;
    rt_sibl_send_new_gateway_request_t request = {0};
    request.hag_networks[0] = NETWORK(0x0A440000, 0xFFFC0000);
    self.role.impl.on_sibl_send_new_gateway_request(&self, &request);
    CHECK(self.role.state.forwarder.dtr == 2 && self.role.state.forwarder.is_local_root);
    CHECK(self.role.state.forwarder.global_state == GLOBAL_STATE_WITH_NETWORK);
    CHECK(self.role.state.forwarder.gateway_request_timeout == 30000 && peer_count == 1);
    self.role.impl.on_tick(&self, 29999);
    self.role.impl.on_sibl_send_new_gateway_request(&self, &request);
    CHECK(self.role.state.forwarder.gateway_request_timeout == 1 && peer_count == 1);
    self.role.impl.on_tick(&self, 1);
    CHECK(peer_count == 2 && peer_event.event_id == PEER_NEW_GATEWAY_REQUEST);
    rt_peer_new_gateway_response_t confirmed = {
        .external_network = NETWORK(0x0A000000, 0xFF000000), .dtr = 1,
    };
    self.role.impl.on_peer_new_gateway_response(&self, &confirmed);
    CHECK(sibling_count == 1 && sibling_event.event_id == SIBL_GATEWAY_CONFIRMATION);
    CHECK(sibling_event.payload.new_gateway_winner.dtr == 2);
    CHECK(self.role.state.forwarder.gateway_request_timeout == 0);
    CHECK(self.role.state.forwarder.dtr == 2 && self.role.state.forwarder.is_local_root);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    self.role.impl.on_tick(&self, 60000);
    self.role.impl.on_peer_new_gateway_response(&self, &confirmed);
    CHECK(peer_count == 2 && sibling_count == 1); /* no unsolicited confirmation */
    return 0;
}

EXPORT int test_confirmation_answers_pending_child_once_without_reelection(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 2));
    self.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    rt_peer_new_gateway_request_t request = {0};
    request.hag_networks[0] = NETWORK(0x0A440000, 0xFFFC0000);
    self.role.impl.on_peer_new_gateway_request(&self, &request);
    rt_sibl_new_gateway_winner_t confirmed = {
        .network = self.role.state.forwarder.node_network, .dtr = 2,
    };
    memcpy(confirmed.confirmed_path, request.hag_networks, sizeof(confirmed.confirmed_path));
    CHECK(self.role.impl.on_sibl_gateway_confirmation);
    self.role.impl.on_sibl_gateway_confirmation(&self, &confirmed);
    CHECK(peer_count == 1 && peer_event.event_id == PEER_NEW_GATEWAY_RESPONSE);
    CHECK(peer_event.payload.new_gateway_response.dtr == 2);
    CHECK(!self.role.state.forwarder.gateway_relay_pending);
    self.role.impl.on_sibl_gateway_confirmation(&self, &confirmed);
    CHECK(peer_count == 1 && sibling_count == 1);
    CHECK(self.role.state.forwarder.dtr == 2 && !self.role.state.forwarder.is_local_root);
    CHECK(self.role.state.forwarder.global_state == GLOBAL_STATE_WITH_NETWORK);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(!network_change_count && !ap_enable_count);
    return 0;
}

EXPORT int test_pending_confirmation_survives_failed_peer_send(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 2));
    self.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    rt_peer_new_gateway_request_t request = {0};
    request.hag_networks[0] = NETWORK(0x0A440000, 0xFFFC0000);
    self.role.impl.on_peer_new_gateway_request(&self, &request);
    rt_sibl_new_gateway_winner_t confirmed = {
        .network = self.role.state.forwarder.node_network, .dtr = 2,
    };
    memcpy(confirmed.confirmed_path, request.hag_networks, sizeof(confirmed.confirmed_path));
    CHECK(self.role.impl.on_sibl_gateway_confirmation);
    peer_send_failures_remaining = 1;
    self.role.impl.on_sibl_gateway_confirmation(&self, &confirmed);
    CHECK(peer_count == 1 && self.role.state.forwarder.gateway_relay_pending);
    /* The child retries after losing its response; this must reopen the
     * upstream path request rather than remain permanently suppressed. */
    self.role.impl.on_peer_new_gateway_request(&self, &request);
    CHECK(sibling_count == 2);
    self.role.impl.on_sibl_gateway_confirmation(&self, &confirmed);
    CHECK(peer_count == 2 && !self.role.state.forwarder.gateway_relay_pending);
    self.role.impl.on_sibl_gateway_confirmation(&self, &confirmed);
    CHECK(peer_count == 2 && self.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    return 0;
}

EXPORT int test_unsolicited_gateway_winner_does_not_reply_to_parent(void) {
    reset_capture();
    routing_t sibling, root_radio;
    CHECK(forwarder(&sibling, ORIENTATION_EAST, 2));
    CHECK(forwarder(&root_radio, ORIENTATION_WEST, 1));
    rt_sibl_new_gateway_winner_t winner = {
        .network = NETWORK(0x0A000000, 0xFF000000), .dtr = 2,
    };
    sibling.role.impl.on_sibl_new_gateway_winner(&sibling, &winner);
    root_radio.role.impl.on_sibl_new_gateway_winner(&root_radio, &winner);
    CHECK(!peer_count && !sibling_count);
    return 0;
}

EXPORT int test_early_peer_gateway_request_cannot_block_initial_provision(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_NORTH, 0));
    rt_peer_new_gateway_request_t early = {0};
    early.hag_networks[0] = NETWORK(0x0A600000, 0xFFE00000);
    self.role.impl.on_peer_new_gateway_request(&self, &early);
    CHECK(self.role.state.forwarder.global_state == GLOBAL_STATE_WITHOUT_NETWORK);
    CHECK(!self.node_state.routing_table.count && !sibling_count);
    CHECK(!self.role.state.forwarder.gateway_relay_pending);
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A400000, 0xFFE00000),
        .provided_network = NETWORK(0x0A440000, 0xFFFC0000), .dtr = 2,
    };
    self.role.impl.on_peer_handshake(&self, &hello);
    CHECK(self.role.state.forwarder.node_network.addr == 0x0A440000);
    CHECK(self.role.state.forwarder.dtr == 3 && network_change_count == 1);
    CHECK(sibling_count == 1 && sibling_event.event_id == SIBL_PROVISION);
    return 0;
}

EXPORT int test_better_gateway_still_replaces_live_parent_after_transit_request(void) {
    reset_capture();
    routing_t self;
    CHECK(forwarder(&self, ORIENTATION_EAST, 4));
    self.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    rt_peer_new_gateway_request_t request = {0};
    request.hag_networks[0] = NETWORK(0x0A440000, 0xFFFC0000);
    self.role.impl.on_peer_new_gateway_request(&self, &request);
    rt_peer_new_gateway_response_t better = {
        .external_network = NETWORK(0x0A440000, 0xFFFC0000), .dtr = 2,
    };
    self.role.impl.on_peer_new_gateway_response(&self, &better);
    CHECK(self.role.state.forwarder.dtr == 3 && self.role.state.forwarder.is_local_root);
    CHECK(self.node_state.routing_table.default_gateway == ORIENTATION_EAST);
    CHECK(sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER && sibling_count == 2);
    CHECK(!self.role.state.forwarder.gateway_relay_pending && !peer_count);
    return 0;
}

EXPORT int test_three_node_transit_confirmation_finishes_without_echo_or_reselection(void) {
    reset_capture();
    routing_t origin, middle_in, middle_out, root_in, root_other, root_center, origin_other;
    CHECK(forwarder(&origin, ORIENTATION_NORTH, 3));
    CHECK(forwarder(&middle_in, ORIENTATION_EAST, 2));
    CHECK(forwarder(&middle_out, ORIENTATION_NORTH, 2));
    CHECK(forwarder(&root_in, ORIENTATION_SOUTH, 1));
    CHECK(forwarder(&root_other, ORIENTATION_WEST, 1));
    CHECK(forwarder(&root_center, ORIENTATION_CENTER, 0));
    CHECK(create_root_core(&root_center, 0x0A000000, 0xFF000000));
    CHECK(forwarder(&origin_other, ORIENTATION_EAST, 3));
    origin.role.state.forwarder.node_network = NETWORK(0x0A440000, 0xFFFC0000);
    origin_other.role.state.forwarder.node_network = origin.role.state.forwarder.node_network;
    origin.node_state.routing_table.default_gateway = 0;
    middle_in.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    middle_out.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    middle_out.role.state.forwarder.is_local_root = true;
    rt_sibl_send_new_gateway_request_t own = {0};
    origin.role.impl.on_sibl_send_new_gateway_request(&origin, &own);
    rt_peer_new_gateway_request_t path = peer_event.payload.new_gateway_request;
    middle_in.role.impl.on_peer_new_gateway_request(&middle_in, &path);
    rt_sibl_send_new_gateway_request_t relay = sibling_event.payload.send_new_gateway_request;
    middle_out.role.impl.on_sibl_send_new_gateway_request(&middle_out, &relay);
    path = peer_event.payload.new_gateway_request;
    root_in.role.impl.on_peer_new_gateway_request(&root_in, &path);
    relay = sibling_event.payload.send_new_gateway_request;
    root_center.role.impl.on_sibl_send_new_gateway_request(&root_center, &relay);
    root_center.role.impl.on_tick(&root_center, 10000);
    rt_sibl_new_gateway_winner_t winner = sibling_event.payload.new_gateway_winner;
    root_other.role.impl.on_sibl_new_gateway_winner(&root_other, &winner);
    CHECK(peer_count == 2); /* root radio without a requester stays silent */
    root_in.role.impl.on_sibl_new_gateway_winner(&root_in, &winner);
    rt_peer_new_gateway_response_t reply = peer_event.payload.new_gateway_response;
    middle_out.role.impl.on_peer_new_gateway_response(&middle_out, &reply);
    CHECK(sibling_event.event_id == SIBL_GATEWAY_CONFIRMATION);
    rt_sibl_new_gateway_winner_t confirmed = sibling_event.payload.new_gateway_winner;
    CHECK(middle_in.role.impl.on_sibl_gateway_confirmation);
    middle_in.role.impl.on_sibl_gateway_confirmation(&middle_in, &confirmed);
    reply = peer_event.payload.new_gateway_response;
    origin.role.impl.on_peer_new_gateway_response(&origin, &reply);
    CHECK(origin.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    origin_other.node_state.routing_table = origin.node_state.routing_table;
    winner = sibling_event.payload.new_gateway_winner;
    origin_other.role.impl.on_sibl_new_gateway_winner(&origin_other, &winner);
    CHECK(peer_count == 4 && sibling_count == 5);
    CHECK(middle_out.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(middle_out.role.state.forwarder.dtr == 2 && middle_out.role.state.forwarder.is_local_root);
    CHECK(middle_in.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(middle_in.role.state.forwarder.dtr == 2);
    CHECK(routing_table_route(&root_in.node_state.routing_table, 0x0A468002) == ORIENTATION_SOUTH);
    CHECK(routing_table_route(&middle_in.node_state.routing_table, 0x0A468002) == ORIENTATION_EAST);
    /* Replays and the descendant's delayed reply cannot start an echo chain. */
    middle_in.role.impl.on_sibl_gateway_confirmation(&middle_in, &confirmed);
    root_in.role.impl.on_sibl_new_gateway_winner(&root_in, &winner);
    reply = (rt_peer_new_gateway_response_t){
        .external_network = origin.role.state.forwarder.node_network, .dtr = 3,
    };
    middle_in.role.impl.on_peer_new_gateway_response(&middle_in, &reply);
    CHECK(peer_count == 4 && sibling_count == 5);
    CHECK(!network_change_count && !ap_enable_count && !panic_count && !mutex_errors);
    return 0;
}

EXPORT int test_child_restart_cannot_replace_a_live_parent(void) {
    reset_capture();
    routing_t downstream, uplink;
    CHECK(forwarder(&downstream, ORIENTATION_EAST, 2));
    CHECK(forwarder(&uplink, ORIENTATION_NORTH, 2));
    downstream.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    uplink.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    uplink.role.state.forwarder.is_local_root = true;
    routing_table_add(&downstream.node_state.routing_table, 0x0A440000, 0xFFFC0000,
                      ORIENTATION_EAST);
    downstream.role.impl.on_peer_lost(&downstream, NULL);
    CHECK(downstream.role.state.forwarder.dtr == 2);
    CHECK(downstream.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(sibling_event.event_id == SIBL_SEND_NEW_GATEWAY_REQUEST);
    rt_sibl_send_new_gateway_request_t relay = sibling_event.payload.send_new_gateway_request;
    uplink.role.impl.on_sibl_send_new_gateway_request(&uplink, &relay);
    CHECK(uplink.role.state.forwarder.dtr == 2 && uplink.role.state.forwarder.is_local_root);
    downstream.role.impl.on_peer_connected(&downstream, NULL);
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A440000, 0xFFFC0000),
        .provided_network = NETWORK(0x0A448000, 0xFFFF8000), .dtr = 3,
    };
    downstream.role.impl.on_peer_handshake(&downstream, &hello);
    CHECK(downstream.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(downstream.role.state.forwarder.dtr == 2);
    rt_peer_new_gateway_response_t confirmed = {
        .external_network = NETWORK(0x0A000000, 0xFF000000), .dtr = 1,
    };
    uplink.role.impl.on_peer_new_gateway_response(&uplink, &confirmed);
    CHECK(sibling_event.event_id == SIBL_GATEWAY_CONFIRMATION);
    rt_sibl_new_gateway_winner_t node_confirmation = sibling_event.payload.new_gateway_winner;
    CHECK(downstream.role.impl.on_sibl_gateway_confirmation);
    downstream.role.impl.on_sibl_gateway_confirmation(&downstream, &node_confirmation);
    CHECK(downstream.role.state.forwarder.dtr == 2);
    CHECK(downstream.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(peer_count == 2); /* uplink reannouncement and provider handshake */
    CHECK(!network_change_count && !ap_enable_count && !panic_count);
    return 0;
}

EXPORT int test_unknown_sibling_distance_cannot_steal_a_live_parent(void) {
    reset_capture();
    routing_t idle;
    CHECK(forwarder(&idle, ORIENTATION_EAST, 2));
    idle.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    idle.role.state.forwarder.dtr = 0;
    idle.role.state.forwarder.global_state = GLOBAL_STATE_ON_GW_REQUEST;
    rt_peer_handshake_t hello = {
        .external_network = NETWORK(0x0A440000, 0xFFFC0000), .dtr = 3,
    };
    idle.role.impl.on_peer_handshake(&idle, &hello);
    CHECK(idle.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(idle.role.state.forwarder.dtr == 0 && !sibling_count);
    rt_sibl_new_gateway_winner_t confirmed = {
        .network = idle.role.state.forwarder.node_network, .dtr = 2,
    };
    CHECK(idle.role.impl.on_sibl_gateway_confirmation);
    idle.role.impl.on_sibl_gateway_confirmation(&idle, &confirmed);
    CHECK(idle.role.state.forwarder.dtr == 2);
    CHECK(idle.role.state.forwarder.global_state == GLOBAL_STATE_WITH_NETWORK);
    CHECK(idle.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(!peer_count && !sibling_count);
    return 0;
}

EXPORT int test_confirmation_dispatch_is_safe_for_root_center_and_home(void) {
    for (unsigned root = 0; root < 2; ++root) {
        reset_capture();
        routing_t center;
        CHECK(rt_create(&center, NULL, NULL, NULL, NULL, ORIENTATION_CENTER));
        if (root) {
            CHECK(rt_init_root(&center, 0x0A000000, 0xFF000000));
        } else CHECK(rt_init_home(&center));
        rt_on_start(&center);
        critical_callback(critical_context);
        rt_sibl_event_t event = {
            .event_id = SIBL_GATEWAY_CONFIRMATION,
            .payload.new_gateway_winner = {
                .network = NETWORK(0x0A400000, 0xFFE00000), .dtr = 2,
            },
        };
        CHECK(sizeof(event) == 144); /* new SPI path scope, coherent siblings */
        CHECK(sizeof(rt_peer_event_t) == 132); /* Wi-Fi format stays unchanged */
        routing_callback.callback(routing_callback.context, (const uint8_t *)&event, sizeof(event));
        critical_callback(critical_context);
        CHECK(!panic_count && !peer_count && !network_change_count && !ap_enable_count);
    }
    return 0;
}

EXPORT int test_selected_parent_distance_increase_requires_pending_confirmation(void) {
    reset_capture();
    routing_t uplink, downstream;
    CHECK(forwarder(&uplink, ORIENTATION_NORTH, 2));
    CHECK(forwarder(&downstream, ORIENTATION_EAST, 2));
    uplink.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    downstream.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    uplink.role.state.forwarder.is_local_root = true;
    rt_peer_new_gateway_response_t longer = {
        .external_network = NETWORK(0x0A000000, 0xFF000000), .dtr = 3,
    };
    uplink.role.impl.on_peer_new_gateway_response(&uplink, &longer);
    CHECK(uplink.role.state.forwarder.dtr == 2 && !sibling_count);
    rt_peer_new_gateway_request_t child_request = {0};
    child_request.hag_networks[0] = NETWORK(0x0A440000, 0xFFFC0000);
    downstream.role.impl.on_peer_new_gateway_request(&downstream, &child_request);
    rt_sibl_send_new_gateway_request_t relay = sibling_event.payload.send_new_gateway_request;
    uplink.role.impl.on_sibl_send_new_gateway_request(&uplink, &relay);
    /* Its existing parent is still selected but now confirms a longer path. */
    uplink.role.impl.on_peer_new_gateway_response(&uplink, &longer);
    CHECK(sibling_event.event_id == SIBL_GATEWAY_CONFIRMATION && sibling_count == 2);
    CHECK(uplink.role.state.forwarder.dtr == 4 && uplink.role.state.forwarder.is_local_root);
    CHECK(uplink.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    rt_sibl_new_gateway_winner_t confirmation = sibling_event.payload.new_gateway_winner;
    CHECK(downstream.role.impl.on_sibl_gateway_confirmation);
    downstream.role.impl.on_sibl_gateway_confirmation(&downstream, &confirmation);
    CHECK(downstream.role.state.forwarder.dtr == 4);
    CHECK(downstream.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(peer_event.event_id == PEER_NEW_GATEWAY_RESPONSE);
    CHECK(peer_event.payload.new_gateway_response.dtr == 4);
    CHECK(peer_count == 2 && !downstream.role.state.forwarder.gateway_relay_pending);
    return 0;
}

EXPORT int test_four_nodes_confirm_only_forwarded_child_then_retry_other_return_path(void) {
    reset_capture();
    routing_t radio_a, radio_b, uplink, root_radio, root_center;
    CHECK(forwarder(&radio_a, ORIENTATION_EAST, 2));
    CHECK(forwarder(&radio_b, ORIENTATION_WEST, 2));
    CHECK(forwarder(&uplink, ORIENTATION_NORTH, 2));
    CHECK(forwarder(&root_radio, ORIENTATION_SOUTH, 1));
    CHECK(forwarder(&root_center, ORIENTATION_CENTER, 0));
    CHECK(create_root_core(&root_center, 0x0A000000, 0xFF000000));
    radio_a.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    radio_b.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    uplink.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    uplink.role.state.forwarder.is_local_root = true;
    /* Both children retained blocks outside their current parent's aggregate.
     * A route to that parent therefore cannot stand in for either child. */
    rt_peer_new_gateway_request_t request_a = {0}, request_b = {0};
    request_a.hag_networks[0] = NETWORK(0x0A600000, 0xFFE00000);
    request_b.hag_networks[0] = NETWORK(0x0A800000, 0xFFE00000);
    radio_a.role.impl.on_peer_new_gateway_request(&radio_a, &request_a);
    rt_sibl_send_new_gateway_request_t relay_a = sibling_event.payload.send_new_gateway_request;
    radio_b.role.impl.on_peer_new_gateway_request(&radio_b, &request_b);
    rt_sibl_send_new_gateway_request_t relay_b = sibling_event.payload.send_new_gateway_request;
    uplink.role.impl.on_sibl_send_new_gateway_request(&uplink, &relay_a);
    rt_peer_new_gateway_request_t forwarded_a = peer_event.payload.new_gateway_request;
    /* An A replay and B's concurrent request must not replace A's pending
     * path or restart its deadline. Only A has crossed the Wi-Fi uplink. */
    uplink.role.impl.on_tick(&uplink, 1000);
    radio_a.role.impl.on_peer_new_gateway_request(&radio_a, &request_a);
    uplink.role.impl.on_sibl_send_new_gateway_request(&uplink, &relay_a);
    uplink.role.impl.on_sibl_send_new_gateway_request(&uplink, &relay_b);
    CHECK(peer_count == 1 && uplink.role.state.forwarder.gateway_request_timeout == 29000);
    root_radio.role.impl.on_peer_new_gateway_request(&root_radio, &forwarded_a);
    rt_sibl_send_new_gateway_request_t root_request = sibling_event.payload.send_new_gateway_request;
    root_center.role.impl.on_sibl_send_new_gateway_request(&root_center, &root_request);
    root_center.role.impl.on_tick(&root_center, 10000);
    rt_sibl_new_gateway_winner_t root_winner = sibling_event.payload.new_gateway_winner;
    root_radio.role.impl.on_sibl_new_gateway_winner(&root_radio, &root_winner);
    rt_peer_new_gateway_response_t reply = peer_event.payload.new_gateway_response;
    uplink.role.impl.on_peer_new_gateway_response(&uplink, &reply);
    CHECK(sibling_event.event_id == SIBL_GATEWAY_CONFIRMATION);
    rt_sibl_new_gateway_winner_t confirmed_a = sibling_event.payload.new_gateway_winner;
    CHECK(radio_a.role.impl.on_sibl_gateway_confirmation);
    radio_a.role.impl.on_sibl_gateway_confirmation(&radio_a, &confirmed_a);
    radio_b.role.impl.on_sibl_gateway_confirmation(&radio_b, &confirmed_a);
    CHECK(peer_count == 3 && !radio_a.role.state.forwarder.gateway_relay_pending);
    CHECK(radio_b.role.state.forwarder.gateway_relay_pending);
    CHECK(routing_table_route(&root_radio.node_state.routing_table, 0x0A740002) == ORIENTATION_SOUTH);
    CHECK(routing_table_route(&root_radio.node_state.routing_table, 0x0A940002) == ORIENTATION_CENTER);
    radio_a.role.impl.on_sibl_gateway_confirmation(&radio_a, &confirmed_a);
    radio_b.role.impl.on_sibl_gateway_confirmation(&radio_b, &confirmed_a);
    root_radio.role.impl.on_sibl_new_gateway_winner(&root_radio, &root_winner);
    CHECK(peer_count == 3); /* no duplicate response or false B confirmation */

    /* B's normal retry can now travel upstream and install its return path. */
    radio_b.role.impl.on_peer_new_gateway_request(&radio_b, &request_b);
    relay_b = sibling_event.payload.send_new_gateway_request;
    uplink.role.impl.on_sibl_send_new_gateway_request(&uplink, &relay_b);
    CHECK(peer_count == 4);
    rt_peer_new_gateway_request_t forwarded_b = peer_event.payload.new_gateway_request;
    CHECK(forwarded_b.hag_networks[0].addr == request_b.hag_networks[0].addr);
    root_radio.role.impl.on_peer_new_gateway_request(&root_radio, &forwarded_b);
    root_request = sibling_event.payload.send_new_gateway_request;
    root_center.role.impl.on_sibl_send_new_gateway_request(&root_center, &root_request);
    root_center.role.impl.on_tick(&root_center, 10000);
    root_winner = sibling_event.payload.new_gateway_winner;
    root_radio.role.impl.on_sibl_new_gateway_winner(&root_radio, &root_winner);
    reply = peer_event.payload.new_gateway_response;
    uplink.role.impl.on_peer_new_gateway_response(&uplink, &reply);
    rt_sibl_new_gateway_winner_t confirmed_b = sibling_event.payload.new_gateway_winner;
    radio_a.role.impl.on_sibl_gateway_confirmation(&radio_a, &confirmed_b);
    radio_b.role.impl.on_sibl_gateway_confirmation(&radio_b, &confirmed_b);
    CHECK(peer_count == 6 && sibling_count == 10);
    CHECK(!radio_b.role.state.forwarder.gateway_relay_pending);
    CHECK(routing_table_route(&root_radio.node_state.routing_table, 0x0A940002) == ORIENTATION_SOUTH);
    CHECK(uplink.role.state.forwarder.dtr == 2 && uplink.role.state.forwarder.is_local_root);
    CHECK(uplink.node_state.routing_table.default_gateway == ORIENTATION_NORTH);
    CHECK(!network_change_count && !ap_enable_count && !panic_count && !mutex_errors);
    return 0;
}

EXPORT int test_confirmation_preserves_an_unrelated_outbound_request(void) {
    reset_capture();
    routing_t other_radio;
    CHECK(forwarder(&other_radio, ORIENTATION_WEST, 2));
    other_radio.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    rt_sibl_send_new_gateway_request_t request_b = {0};
    request_b.hag_networks[0] = NETWORK(0x0A800000, 0xFFE00000);
    other_radio.role.impl.on_sibl_send_new_gateway_request(&other_radio, &request_b);
    rt_sibl_new_gateway_winner_t confirmed_a = {
        .network = other_radio.role.state.forwarder.node_network, .dtr = 2,
    };
    confirmed_a.confirmed_path[0] = NETWORK(0x0A600000, 0xFFE00000);
    confirmed_a.confirmed_path[1] = other_radio.role.state.forwarder.node_network;
    CHECK(other_radio.role.impl.on_sibl_gateway_confirmation);
    other_radio.role.impl.on_sibl_gateway_confirmation(&other_radio, &confirmed_a);
    CHECK(other_radio.role.state.forwarder.gateway_request_timeout == 30000 && peer_count == 1);
    rt_sibl_new_gateway_winner_t confirmed_b = confirmed_a;
    confirmed_b.confirmed_path[0] = request_b.hag_networks[0];
    other_radio.role.impl.on_sibl_gateway_confirmation(&other_radio, &confirmed_b);
    CHECK(other_radio.role.state.forwarder.gateway_request_timeout == 0 && peer_count == 1);
    return 0;
}

EXPORT int test_unsolicited_improvement_does_not_confirm_an_unforwarded_child_path(void) {
    reset_capture();
    routing_t candidate, downstream;
    CHECK(forwarder(&candidate, ORIENTATION_EAST, 4));
    CHECK(forwarder(&downstream, ORIENTATION_WEST, 4));
    candidate.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    downstream.node_state.routing_table.default_gateway = ORIENTATION_NORTH;
    rt_peer_new_gateway_request_t child_request = {0};
    child_request.hag_networks[0] = NETWORK(0x0A800000, 0xFFE00000);
    downstream.role.impl.on_peer_new_gateway_request(&downstream, &child_request);
    /* This alternate advertises a shorter route without a corresponding
     * outbound NGR; it may win, but cannot confirm the child's missing path. */
    rt_peer_new_gateway_response_t improved = {
        .external_network = NETWORK(0x0A600000, 0xFFE00000), .dtr = 2,
    };
    candidate.role.impl.on_peer_new_gateway_response(&candidate, &improved);
    CHECK(candidate.node_state.routing_table.default_gateway == ORIENTATION_EAST);
    CHECK(candidate.role.state.forwarder.dtr == 3);
    CHECK(sibling_event.event_id == SIBL_NEW_GATEWAY_WINNER);
    rt_sibl_new_gateway_winner_t winner = sibling_event.payload.new_gateway_winner;
    CHECK(!winner.confirmed_path[0].addr);
    downstream.node_state.routing_table = candidate.node_state.routing_table;
    downstream.role.impl.on_sibl_new_gateway_winner(&downstream, &winner);
    CHECK(!peer_count && downstream.role.state.forwarder.gateway_relay_pending);
    CHECK(downstream.role.state.forwarder.dtr == 3);
    return 0;
}
