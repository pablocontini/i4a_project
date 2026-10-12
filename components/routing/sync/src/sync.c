#include "sync/sync.h"

#include <string.h>

#include "os/os.h"

#include "follower.h"
#include "leader.h"

#define TAG "sync"

static void on_sibling_message(void *ctx, const uint8_t *raw_msg, uint16_t len) {
    if (len != sizeof(sync_msg_t)) {
        os_diag("sync", "event=invalid_frame length=%u", len);
        return;
    }

    sync_t *self = ctx;
    const sync_msg_t *msg = (const sync_msg_t *)raw_msg;
    if (msg->version != 2) return;
    uint8_t cs_id = msg->kind == SYNC_MSG_TOKEN_GRANT ? msg->token_grant.cs_id : msg->token_request.cs_id;
    if (cs_id >= RS_LAST_COMPONENT_ID) return;
    if (msg->kind == SYNC_MSG_TOKEN_GRANT &&
        (!msg->token_grant.session || !msg->token_grant.sequence ||
         msg->token_grant.destination < 1 || msg->token_grant.destination > N_DEVICES)) return;

    switch (msg->kind) {
        case SYNC_MSG_TOKEN_GRANT:
            self->impl->on_token_grant(self, &msg->token_grant);
            break;
        case SYNC_MSG_TOKEN_REQUEST:
            if (self->impl->on_token_request)
                self->impl->on_token_request(self, &msg->token_request);
            break;
        default:
            log_warn(TAG, "Unknown message id: %u", msg->kind);
            break;
    }
}

bool sync_init(sync_t *self, ring_share_t *rs, orientation_t orientation) {
    memset(self, 0, sizeof(sync_t));
    self->rs = rs;
    self->orientation = orientation;
    self->is_leader = (orientation == ORIENTATION_CENTER);
    if (!mutex_create(&self->state_lock)) return false;
    if (self->is_leader) { self->session = os_random_u32(); if (!self->session) self->session = 1; }

    rs_register_component(self->rs, RS_SYNC, (ring_callback_t){ .callback = on_sibling_message, .context = self });

    if (self->is_leader) {
        self->impl = sync_leader_init(self);
    } else {
        self->impl = sync_follower_init(self);
    }
    return true;
}

void sync_register_critical_section(sync_t *self, component_id_t cs_id, sync_cs_callback_fn_t callback, void *ctx) {
    self->critical_sections[cs_id] = (sync_cs_callback_t){
        .callback = callback,
        .context = ctx,
        .is_inside = false,
    };
}

void sync_request_critical_section(sync_t *self, component_id_t cs_id) {
    if (cs_id >= RS_LAST_COMPONENT_ID) return;
    self->impl->request_critical_section(self, cs_id);
}

bool sync_is_inside_critical_section(sync_t *sync, component_id_t cs_id) {
    return sync->critical_sections[cs_id].is_inside;
}
void sync_defer_completion(sync_t *self, component_id_t cs_id) {
    if (cs_id < RS_LAST_COMPONENT_ID && self->critical_sections[cs_id].is_inside)
        self->deferred[cs_id] = true;
}

void sync_destroy(sync_t *self) {
    mutex_destroy(&self->state_lock);
    // Un-register sibling messages
    rs_register_component(self->rs, RS_SYNC, (ring_callback_t){ .callback = NULL, .context = NULL });
}
