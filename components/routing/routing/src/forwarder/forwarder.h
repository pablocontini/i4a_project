#ifndef _ROUTING_FORWARDER_H_
#define _ROUTING_FORWARDER_H_

#include "routing/routing.h"

#define TAG "forwarder"
#define GET_STATE(self) (&((self)->role.state.forwarder))
#define GATEWAY_RETRY_TIMEOUT_MS 30000u

bool create_forwarder_core(routing_t *self);
void rt_fwd_peer_set_required_callbacks(rt_role_impl_t *impl);
void rt_fwd_sibl_set_required_callbacks(rt_role_impl_t *impl);
bool rt_fwd_prepare_gateway_request(routing_t *self, const network_t path[16]);
void rt_fwd_send_gateway_request(routing_t *self);

#endif  // _ROUTING_FORWARDER_H_
