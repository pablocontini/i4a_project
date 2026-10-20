#ifndef _ROUTING_FORWARDER_H_
#define _ROUTING_FORWARDER_H_

#include "routing/routing.h"

#define TAG "forwarder"
#define GET_STATE(self) (&((self)->role.state.forwarder))
#define GATEWAY_REQUEST_RETRY_MS 30000
#define PROVISION_RETRY_MS 5000

bool create_forwarder_core(routing_t *self);
void rt_fwd_peer_set_required_callbacks(rt_role_impl_t *impl);
void rt_fwd_sibl_set_required_callbacks(rt_role_impl_t *impl);
void rt_fwd_publish_provision(routing_t *self);

#endif  // _ROUTING_FORWARDER_H_
