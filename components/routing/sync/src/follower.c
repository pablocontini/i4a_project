#include "follower.h"

static void on_token_grant(sync_t *self, const sync_token_grant_t *grant) {
    if (grant->destination != self->orientation) return;
    sync_cs_callback_t callback = {0};
    component_id_t id = grant->cs_id;
    WITH_LOCK(&self->lock, {
        if (self->completed[id] != grant->sequence) {
            self->critical_sections[id].is_inside = true;
            self->critical_sections[id].deferred = false;
            callback = self->critical_sections[id];
        }
    });
    if (callback.callback) callback.callback(callback.context);
    bool deferred = false;
    WITH_LOCK(&self->lock, {
        self->critical_sections[id].is_inside = false;
        deferred = self->critical_sections[id].deferred;
        if (!deferred) self->completed[id] = grant->sequence;
    });
    if (deferred) return;
    sync_msg_t msg = {.version = 2, .kind = SYNC_MSG_TOKEN_GRANT, .token_grant = *grant};
    msg.token_grant.destination = (self->orientation % N_DEVICES) + 1;
    WITH_LOCK(&self->lock, { self->destination[id] = msg.token_grant.destination; });
    rs_broadcast(self->rs, RS_SYNC, &msg, sizeof(msg));
}

static void request_critical_section(sync_t *self, component_id_t id) {
    sync_msg_t msg = {.version = 2, .kind = SYNC_MSG_TOKEN_REQUEST,
                      .token_request = {.cs_id = id}};
    rs_broadcast(self->rs, RS_SYNC, &msg, sizeof(msg));
}

static sync_impl_t IMPL = {.on_token_grant = on_token_grant,
    .request_critical_section = request_critical_section};

sync_impl_t *sync_follower_init(sync_t *self) {
    (void)self;
    return &IMPL;
}
