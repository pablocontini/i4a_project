#ifndef _ROUTING_H_
#define _ROUTING_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "ring_share/ring_share.h"
#include "shared_state/shared_state.h"
#include "sync/sync.h"
#include "wireless/wireless.h"

#include "routing/impl.h"
#include "routing/routing_table.h"
#include "routing/types.h"

#define RT_GW_TRACE_CAPACITY 32
/* Local observation IDs, not wire transaction IDs. No protocol change. */
typedef struct rt_gw_trace {
    uint32_t time_ms, sequence, dtr, timeout_ms, value, path_hash;
    uint16_t event, reason;
    uint8_t hops, state;
} rt_gw_trace_t;

typedef struct routing {
    /**
     * Module dependencies
     */
    rt_dependencies_t deps;

    /**
     * Current device's orientation.
     */
    orientation_t orientation;

    /**
     * Shared and synchronized node global
     * state.
     */
    rt_node_state_t node_state;

    /**
     * External events queue
     *
     * Holds peer events until the device gets its turn
     * in the critical section.
     */
    rt_external_queue_t external_queue;

    /**
     * Internal events queue
     *
     * Holds sibling messages until the device gets its
     * turn in the critical section.
     */
    rt_internal_queue_t internal_queue;

    /* Producers run in the Wi-Fi and SPI tasks. Never hold this mutex while
     * calling a role callback, sending a message, or requesting a token. */
    mutex_t queue_lock;
    uint32_t pending_tick_ms;
    /* Written only in the routing critical section. */
    bool pending_publish;

    /**
     * Private internal device state and role definition.
     */
    rt_role_t role;
    /* Optional bounded recorder; producers never print or allocate. */
    uint32_t (*gw_clock_ms)(void);
    rt_gw_trace_t gw_trace[RT_GW_TRACE_CAPACITY];
    uint32_t gw_sequence, gw_dropped;
    unsigned gw_head, gw_count;
} routing_t;

enum { RT_GW_RX_PEER=1, RT_GW_RX_SIBLING, RT_GW_REQUEST_TX,
       RT_GW_RETRY_TX, RT_GW_RESPONSE_TX, RT_GW_ROOT_WAIT,
       RT_GW_ROOT_RESPONSE, RT_GW_DECISION, RT_GW_HANDSHAKE_TX };
enum { RT_GW_OK=0, RT_GW_NO_NETWORK, RT_GW_BAD_NETWORK,
       RT_GW_UNREACHABLE, RT_GW_PENDING, RT_GW_LOOP,
       RT_GW_PATH_FULL, RT_GW_NOT_CONNECTED, RT_GW_NOT_BETTER,
       RT_GW_ADOPT, RT_GW_FORWARD, RT_GW_ALREADY_PENDING };
void rt_gw_trace(routing_t *self, unsigned event, unsigned reason,
                 uint32_t value, const network_t *path, size_t count);
size_t rt_gw_trace_drain(routing_t *self, rt_gw_trace_t *out, size_t capacity,
                         uint32_t *dropped);

/**
 * Initializes the routing module and binds required dependencies.
 */
bool rt_create(
    routing_t *self, ring_share_t *rs, wireless_t *wl, sync_t *sync, shared_state_t *ss, orientation_t orientation
);

/**
 * Initialization functions. Only one of these should be used depending
 * on the kind of device that's starting up.
 *
 * rt_init_root:
 *  root_network/root_mask: Root's node network. This network will be spreaded
 *  over the rest of the nodes.
 * rt_init_home:
 *  Nothing special, the home device does very little and needs no extra info.
 * rt_init_forwarder:
 *  Nothing else needed.
 *
 * PRECONDITION: rt_create must have been called before.
 */
bool rt_init_root(routing_t *self, uint32_t root_network, uint32_t root_mask);
bool rt_init_home(routing_t *self);
bool rt_init_forwarder(routing_t *self);

/**
 * on_start event.
 *
 * Must be called after initialization but before calling any other routing methods.
 */
void rt_on_start(routing_t *self);

/**
 * Timer update callback.
 *
 * This function should be called periodically in intervals of 1 second
 * or less. It's used to refresh internal timers.
 *
 * dt_ms: Number of milliseconds that have passed since the last call
 *        to this function. E.g. if this function is called every second,
 *        this parameter will have a constant value of 1000 in each invocation.
 */
void rt_on_tick(routing_t *self, uint32_t dt_ms);

/**
 * Routing module output.
 *
 * The routing module will keep an internal routing table based on the information
 * exchanged with thre previous events that can be consulted using this function.
 *
 * This is intended to be used as a hook for lwIP routing function, although a thin
 * wrapper may be needed over it to handle special cases such as loopbacks.
 *
 * This method will return three possible outcomes:
 *  - NO_ROUTE: We can't route this packet, usually because a routing loop was detected.
 *  - ROUTE_SPI: Packet should be sent through SPI interface. Note that packets may
 *               bounce in the SPI interface if the need to be forwarder to a sibling.
 *  - ROUTE_WIFI: Packet should be sent through WLAN interface.
 */
rt_routing_result_t rt_do_route(routing_t *self, uint32_t src_ip, uint32_t dst_ip);

/**
 * Free up resources used by routing module.
 */
void rt_destroy(routing_t *self);

#ifdef __cplusplus
}
#endif

#endif  // _ROUTING_H_
