#include "leader.h"
#include <string.h>

static void request_critical_section(sync_t *self, component_id_t id) {
    sync_msg_t msg = {.version = 2, .kind = SYNC_MSG_TOKEN_GRANT};
    bool send = false;
    WITH_LOCK(&self->lock, {
        sync_leader_t *leader = &self->_st.leader;
        if (leader->is_token_out[id]) {
            /* Coalesce repeated requests instead of queuing 255 circulations. */
            leader->requested_tokens[id] = 1;
        } else {
            leader->is_token_out[id] = true;
            leader->retry_ms[id] = 0;
            if (++self->sequence[id] == 0) ++self->sequence[id];
            self->destination[id] = leader->first_device_to_get_token;
            msg.token_grant = ((sync_token_grant_t){leader->first_device_to_get_token,
                id, self->session, self->sequence[id]});
            send = true;
        }
    });
    /* A failed send stays active and retries the same sequence on tick. */
    if (send) rs_broadcast(self->rs, RS_SYNC, &msg, sizeof(msg));
}

static void on_token_grant(sync_t *self, const sync_token_grant_t *grant) {
    if (grant->destination != self->orientation) return;
    component_id_t id = grant->cs_id;
    sync_cs_callback_t callback = {0};
    WITH_LOCK(&self->lock, {
        if (self->completed[id] != grant->sequence) {
            self->critical_sections[id].is_inside = true;
            self->critical_sections[id].deferred = false;
            callback = self->critical_sections[id];
        }
    });
    if (callback.callback) callback.callback(callback.context);
    bool again = false;
    bool deferred = false;
    WITH_LOCK(&self->lock, {
        self->critical_sections[id].is_inside = false;
        deferred = self->critical_sections[id].deferred;
        if (!deferred) {
            self->completed[id] = grant->sequence;
            self->_st.leader.is_token_out[id] = false;
            again = self->_st.leader.requested_tokens[id] != 0;
            self->_st.leader.requested_tokens[id] = 0;
        }
    });
    if (again) request_critical_section(self, id);
}

static void on_token_request(sync_t *self, const sync_token_request_t *request) {
    request_critical_section(self, request->cs_id);
}

static sync_impl_t IMPL = {on_token_grant, on_token_request, request_critical_section};

sync_impl_t *sync_leader_init(sync_t *self) {
    memset(&self->_st.leader, 0, sizeof(self->_st.leader));
    self->_st.leader.first_device_to_get_token = ORIENTATION_NORTH;
    return &IMPL;
}
