#include "routing/routing.h"

#include <stdio.h>
#include <string.h>

#include "forwarder/forwarder.h"
#include "impl_priv.h"

#include "ring_share/ring_share.h"
#include "routing/impl.h"
#include "shared_state/shared_state.h"
#include "sync/sync.h"
#include "wireless/wireless.h"

#define NODE_STARTUP_DELAY_SECONDS 10
#define TICK_TOKEN_RETRY_MS 3000u

static void trace_table(void *ctx) {
    routing_t *self=ctx;
    WITH_LOCK(&self->node_state.m_lock, {
        os_diag("routing","event=table_state orientation=%u default=%u rpi_out=%u internet_out=%u client_10_52_0_3_out=%u",
            self->orientation,self->node_state.routing_table.default_gateway,
            routing_table_route(&self->node_state.routing_table,0x0AFFFFFE),
            routing_table_route(&self->node_state.routing_table,0x01010101),
            routing_table_route(&self->node_state.routing_table,0x0A340003));
    });
}

static void on_sibling_message(void *ctx, const uint8_t *raw_event, uint16_t len) {
    routing_t *self = ctx;
    if (sizeof(rt_sibl_event_t)!=len) return;
    bool queued=false;
    WITH_LOCK(&self->queue_lock, {
        rt_internal_queue_t *queue=&self->internal_queue;
        if (queue->count<MAX_SIBL_EVENT_QUEUED) {
            memcpy(&queue->queue[queue->count++],raw_event,len);
            self->token_waiting=true; queued=true;
        }
    });
    if (!queued) { os_panic("Routing sibling queue full"); return; }
    sync_request_critical_section(self->deps.sync,RS_ROUTING);

}

static void on_critical_section(void *ctx) {
    routing_t *self = ctx;

    if (!ss_flush(self->deps.ss,RS_ROUTING)) {
        sync_defer_completion(self->deps.sync,RS_ROUTING);
        return;
    }
    uint32_t tick_ms;
    WITH_LOCK(&self->queue_lock, {
        self->dispatch_internal=self->internal_queue;
        self->dispatch_external=self->external_queue;
        self->internal_queue.count=0; self->external_queue.count=0;
        self->token_waiting=false;
        tick_ms=self->pending_tick_ms;
        self->pending_tick_ms=0; self->pending_tick_request_ms=0;
    });
    os_diag("routing","event=dispatch orientation=%u peer_events=%u sibling_events=%u tick_ms=%lu",
            self->orientation,(unsigned)self->dispatch_external.count,(unsigned)self->dispatch_internal.count,(unsigned long)tick_ms);
    // Dispatch sibling messages first
    rt_internal_queue_t *internal_queue = &self->dispatch_internal;
    for (size_t i = 0; i < internal_queue->count; i++) {
        const rt_sibl_event_t *ev = &internal_queue->queue[i];
        os_diag("routing","event=sibling_dispatch orientation=%u id=%u",self->orientation,ev->event_id);
        switch (ev->event_id) {
            case SIBL_ON_START:
                if (self->role.impl.on_start)
                    self->role.impl.on_start(self);
                break;
            case SIBL_UPDATE_DTR:
                if (self->role.impl.on_sibl_update_dtr)
                    self->role.impl.on_sibl_update_dtr(self, &ev->payload.update_dtr);
                break;
            case SIBL_PROVISION:
                if (self->role.impl.on_sibl_provision)
                    self->role.impl.on_sibl_provision(self, &ev->payload.provision);
                break;
            case SIBL_SEND_NEW_GATEWAY_REQUEST:
                if (self->role.impl.on_sibl_send_new_gateway_request)
                    self->role.impl.on_sibl_send_new_gateway_request(self, &ev->payload.send_new_gateway_request);
                break;
            case SIBL_NEW_GATEWAY_WINNER:
                if (self->role.impl.on_sibl_new_gateway_winner)
                    self->role.impl.on_sibl_new_gateway_winner(self, &ev->payload.new_gateway_winner);
                break;
            default:
                os_panic("Unknown sibling message (id = %u) -- aborting", ev->event_id);
                return;
        }
    }

    // Dispatch peer messages after sibling messages
    rt_external_queue_t *external_queue = &self->dispatch_external;
    for (size_t i = 0; i < external_queue->count; i++) {
        const rt_peer_event_t *ev = &external_queue->queue[i];
        os_diag("routing","event=peer_dispatch orientation=%u id=%u",self->orientation,ev->event_id);
        switch (ev->event_id) {
            case PEER_HANDSHAKE:
                if (self->role.impl.on_peer_handshake)
                    self->role.impl.on_peer_handshake(self, &ev->payload.handshake);
                break;
            case PEER_UPDATE_DTR:
                if (self->role.impl.on_peer_update_dtr)
                    self->role.impl.on_peer_update_dtr(self, &ev->payload.update_dtr);
                break;
            case PEER_NEW_GATEWAY_REQUEST:
                if (self->role.impl.on_peer_new_gateway_request)
                    self->role.impl.on_peer_new_gateway_request(self, &ev->payload.new_gateway_request);
                break;
            case PEER_NEW_GATEWAY_RESPONSE:
                if (self->role.impl.on_peer_new_gateway_response)
                    self->role.impl.on_peer_new_gateway_response(self, &ev->payload.new_gateway_response);
                break;
            case PEER_CONNECTED:
                if (self->role.impl.on_peer_connected)
                    self->role.impl.on_peer_connected(self, &ev->payload.connection);
                break;
            case PEER_LOST:
                if (self->role.impl.on_peer_lost)
                    self->role.impl.on_peer_lost(self, &ev->payload.connection);
                break;
            default:
                os_panic("Unknown peer message (id = %u) -- aborting", ev->event_id);
                return;
        }
    }

    if (tick_ms && self->role.impl.on_tick) self->role.impl.on_tick(self,tick_ms);
    if (!ss_flush(self->deps.ss,RS_ROUTING)) sync_defer_completion(self->deps.sync,RS_ROUTING);
    trace_table(self);

}

static void queue_peer_message(routing_t *self, const rt_peer_event_t *event) {
    bool queued=false;
    WITH_LOCK(&self->queue_lock, {
        rt_external_queue_t *queue=&self->external_queue;
        if (queue->count<MAX_PEER_EVENT_QUEUED) {
            memcpy(&queue->queue[queue->count++],event,sizeof(*event));
            self->token_waiting=true; queued=true;
        }
    });
    if (!queued) { os_panic("Routing peer queue full"); return; }
    os_diag("routing","event=peer_queued orientation=%u id=%u",self->orientation,event->event_id);
    sync_request_critical_section(self->deps.sync,RS_ROUTING);

}

static void on_peer_connected(void *ctx, uint32_t network, uint32_t mask) {
    routing_t *self = ctx;

    queue_peer_message(
        self,
        &(rt_peer_event_t){
            .event_id = PEER_CONNECTED,
            .payload.connection = { .addr = network, .mask = mask },
        }
    );
}

static void on_peer_message(void *ctx, const uint8_t *raw_event, uint16_t len) {
    routing_t *self = ctx;

    if (sizeof(rt_peer_event_t) != len) {
        os_panic("Invalid peer message received (len = %u, expected = %zu)\n", len, sizeof(rt_peer_event_t));
        return;
    }

    queue_peer_message(self, (const rt_peer_event_t *)raw_event);
}

static void on_peer_lost(void *ctx, uint32_t network, uint32_t mask) {
    routing_t *self = ctx;

    queue_peer_message(self, &(rt_peer_event_t) {
        .event_id = PEER_LOST,
        .payload.connection = {
            .addr = network,
            .mask = mask,
        },
    });
}

bool rt_create(
    routing_t *self, ring_share_t *rs, wireless_t *wl, sync_t *sync, shared_state_t *ss, orientation_t orientation
) {
    memset(self, 0, sizeof(routing_t));

    if (!mutex_create(&self->node_state.m_lock))
        return false;

    if (!mutex_create(&self->queue_lock)) return false;

    self->deps = (rt_dependencies_t){
        .rs = rs,
        .wl = wl,
        .sync = sync,
        .ss = ss,
    };
    self->orientation = orientation;

    routing_table_init(&self->node_state.routing_table, ROUTING_OUTPUT_NONE);

    wl_register_peer_callbacks(
        self->deps.wl,
        (wireless_callbacks_t){
            .on_peer_connected = on_peer_connected,
            .on_peer_message = on_peer_message,
            .on_peer_lost = on_peer_lost,
        },
        self
    );
    sync_register_critical_section(self->deps.sync, RS_ROUTING, on_critical_section, self);
    ss_watch(
        self->deps.ss, RS_ROUTING,
        (shared_data_t){
            .lock = self->node_state.m_lock,
            .length = sizeof(rt_routing_table_t),
            .ptr = &self->node_state.routing_table,
            .on_updated = trace_table,
            .context = self,
        }
    );
    return true;
}

bool rt_init_root(routing_t *self, uint32_t root_network, uint32_t root_mask) {
    // Root / Center device always starts inside a critical section
    return create_root_core(self, root_network, root_mask);
}

bool rt_init_home(routing_t *self) {
    // Root / Center device always starts inside a critical section
    return create_home_core(self);
}

bool rt_init_forwarder(routing_t *self) {
    return create_forwarder_core(self);
}

void rt_on_start(routing_t *self) {
    // Put the first message in the internal queue
    rt_internal_queue_t *queue = &self->internal_queue;
    rt_sibl_event_t start = { .event_id = SIBL_ON_START };
    memcpy(&queue->queue[0], &start, sizeof(rt_sibl_event_t));
    queue->count = 1;

    // Register the component after putting the message to avoid race conditions
    rs_register_component(
        self->deps.rs, RS_ROUTING, (ring_callback_t){ .callback = on_sibling_message, .context = self }
    );

    // Wait for all devices of the node to setup
    os_delay_ms(NODE_STARTUP_DELAY_SECONDS * 1000);

    // Request the first critical section
    sync_request_critical_section(self->deps.sync, RS_ROUTING);
}

rt_routing_result_t rt_do_route(routing_t *self, uint32_t src_ip, uint32_t dst_ip) {
    (void)src_ip;

    uint8_t output;
    WITH_LOCK(&self->node_state.m_lock, { output = routing_table_route(&self->node_state.routing_table, dst_ip); });

    if (output == ROUTING_OUTPUT_NONE)
        return ROUTE_NONE;

    if (output == self->orientation)
        return ROUTE_WIFI;

    return ROUTE_SPI;
}

void rt_on_tick(routing_t *self, uint32_t dt_ms) {
    if (!dt_ms || (self->deps.rs && rs_maintenance(self->deps.rs))) return;
    bool timer_needed=self->role.impl.on_tick &&
        (!self->role.impl.tick_needed || self->role.impl.tick_needed(self));
    bool request=false;
    WITH_LOCK(&self->queue_lock, {
        bool work=self->internal_queue.count || self->external_queue.count || self->pending_tick_ms || timer_needed;
        if (work) {
            if (timer_needed) self->pending_tick_ms=dt_ms>UINT32_MAX-self->pending_tick_ms ? UINT32_MAX : self->pending_tick_ms+dt_ms;
            self->pending_tick_request_ms=dt_ms>UINT32_MAX-self->pending_tick_request_ms ? UINT32_MAX : self->pending_tick_request_ms+dt_ms;
            if (!self->token_waiting || self->pending_tick_request_ms>=TICK_TOKEN_RETRY_MS) {
                self->token_waiting=true; self->pending_tick_request_ms=0; request=true;
            }
        }
    });
    if (request) sync_request_critical_section(self->deps.sync,RS_ROUTING);
}

void rt_destroy(routing_t *self) {
    mutex_destroy(&self->queue_lock);
    mutex_destroy(self->node_state.m_lock);
}


