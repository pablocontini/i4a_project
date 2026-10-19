#include "sync/sync.h"
#include <string.h>
#include "esp_random.h"
#include "follower.h"
#include "leader.h"

#define SYNC_VERSION 2
#define TOKEN_RETRY_MS 5000U

static bool valid_component(component_id_t id) { return (unsigned)id < RS_LAST_COMPONENT_ID; }

static void on_sibling_message(void *ctx, const uint8_t *raw, uint16_t len) {
    sync_t *self = ctx;
    sync_msg_t msg;
    if (!raw || len != sizeof(msg)) return;
    memcpy(&msg, raw, sizeof(msg));
    if (msg.version != SYNC_VERSION) return;
    if (msg.kind == SYNC_MSG_TOKEN_REQUEST) {
        if (valid_component(msg.token_request.cs_id) && self->impl->on_token_request)
            self->impl->on_token_request(self, &msg.token_request);
    } else if (msg.kind == SYNC_MSG_TOKEN_GRANT) {
        sync_token_grant_t *grant = &msg.token_grant;
        if (!valid_component(grant->cs_id) || grant->destination < 1 ||
            grant->destination > N_DEVICES || !grant->session || !grant->sequence) return;
        bool accept = false;
        WITH_LOCK(&self->lock, {
            /* STARTUP resets the physical node when the central restarts.
             * Binding once per boot prevents old sessions from rebinding it. */
            if (!self->session && !self->is_leader) self->session = grant->session;
            if (self->session == grant->session &&
                grant->sequence >= self->sequence[grant->cs_id]) {
                if (!self->is_leader ||
                    (self->_st.leader.is_token_out[grant->cs_id] &&
                     grant->sequence == self->sequence[grant->cs_id])) {
                    self->sequence[grant->cs_id] = grant->sequence;
                    self->destination[grant->cs_id] = grant->destination;
                    accept = true;
                }
            }
        });
        if (accept) self->impl->on_token_grant(self, grant);
    }
}

bool sync_init(sync_t *self, ring_share_t *rs, orientation_t orientation) {
    memset(self, 0, sizeof(*self));
    if (!rs || orientation < 1 || orientation > N_DEVICES || !mutex_create(&self->lock)) return false;
    self->rs = rs;
    self->orientation = orientation;
    self->is_leader = orientation == ORIENTATION_CENTER;
    if (self->is_leader) {
        self->session = esp_random();
        if (!self->session) self->session = 1;
        self->impl = sync_leader_init(self);
    } else self->impl = sync_follower_init(self);
    rs_register_component(rs, RS_SYNC, (ring_callback_t){on_sibling_message, self});
    return true;
}

void sync_register_critical_section(sync_t *self, component_id_t id, sync_cs_callback_fn_t callback, void *ctx) {
    if (!valid_component(id)) return;
    WITH_LOCK(&self->lock, {
        self->critical_sections[id] = ((sync_cs_callback_t){.callback = callback, .context = ctx});
    });
}

void sync_request_critical_section(sync_t *self, component_id_t id) {
    if (self && self->impl && valid_component(id)) self->impl->request_critical_section(self, id);
}

bool sync_is_inside_critical_section(sync_t *self, component_id_t id) {
    bool inside = false;
    if (!valid_component(id)) return false;
    WITH_LOCK(&self->lock, { inside = self->critical_sections[id].is_inside; });
    return inside;
}

void sync_defer_critical_section(sync_t *self, component_id_t id) {
    if (!valid_component(id)) return;
    WITH_LOCK(&self->lock, {
        if (self->critical_sections[id].is_inside) self->critical_sections[id].deferred = true;
    });
}

bool sync_current_token(sync_t *self, component_id_t id, uint32_t *session, uint32_t *sequence) {
    bool inside = false;
    if (!valid_component(id)) return false;
    WITH_LOCK(&self->lock, {
        inside = self->critical_sections[id].is_inside;
        *session = self->session;
        *sequence = self->sequence[id];
    });
    return inside;
}

bool sync_token_matches(sync_t *self, component_id_t id, uint32_t session, uint32_t sequence, uint8_t owner) {
    bool matches = false;
    if (!valid_component(id)) return false;
    WITH_LOCK(&self->lock, {
        matches = self->session == session && self->sequence[id] == sequence &&
                  owner >= 1 && owner <= N_DEVICES && self->destination[id] == owner;
    });
    return matches;
}

void sync_on_tick(sync_t *self, uint32_t dt_ms) {
    if (!self || !self->is_leader) return;
    for (component_id_t id = 0; id < RS_LAST_COMPONENT_ID; ++id) {
        sync_msg_t msg = {.version = SYNC_VERSION, .kind = SYNC_MSG_TOKEN_GRANT};
        bool retry = false;
        WITH_LOCK(&self->lock, {
            sync_leader_t *leader = &self->_st.leader;
            if (leader->is_token_out[id]) {
                uint32_t left = TOKEN_RETRY_MS - leader->retry_ms[id];
                if (dt_ms >= left) {
                    leader->retry_ms[id] = 0;
                    msg.token_grant = ((sync_token_grant_t){
                        leader->first_device_to_get_token, id, self->session, self->sequence[id]});
                    retry = true;
                } else leader->retry_ms[id] += dt_ms;
            }
        });
        /* Do not hold the state mutex while sending or invoking a callback. */
        if (retry) rs_broadcast(self->rs, RS_SYNC, &msg, sizeof(msg));
    }
}

void sync_destroy(sync_t *self) {
    rs_register_component(self->rs, RS_SYNC, (ring_callback_t){0});
    mutex_destroy(&self->lock);
}
