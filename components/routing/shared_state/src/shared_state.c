#include "shared_state/shared_state.h"
#include <string.h>

/* All five boards must run the same v2 protocol. */
#define SHARED_STATE_VERSION 2
typedef struct {
    uint8_t version, component, owner, reserved;
    uint32_t session, sequence;
} state_header_t;

static bool newer(shared_state_t *self, component_id_t id, uint32_t seq, uint8_t owner) {
    return seq > self->sequence[id] ||
           (seq == self->sequence[id] && owner > self->owner[id]);
}

static void on_sibling_message(void *ctx, const uint8_t *msg, uint16_t len) {
    shared_state_t *self = ctx;
    state_header_t header;
    if (!msg || len < sizeof(header)) return;
    memcpy(&header, msg, sizeof(header));
    if (header.version != SHARED_STATE_VERSION || header.reserved ||
        header.component >= RS_LAST_COMPONENT_ID || header.owner < 1 ||
        header.owner > N_DEVICES || !header.session || !header.sequence) return;
    component_id_t id = header.component;
    shared_data_t *data = &self->data[id];
    if (!data->ptr || !data->lock || len != sizeof(header) + data->length ||
        !sync_token_matches(self->sync, id, header.session, header.sequence, header.owner)) return;
    WITH_LOCK(&data->lock, {
        if (newer(self, id, header.sequence, header.owner)) {
            memcpy(data->ptr, msg + sizeof(header), data->length);
            self->sequence[id] = header.sequence;
            self->owner[id] = header.owner;
        }
    });
}

bool ss_init(shared_state_t *self, sync_t *sync, ring_share_t *rs, orientation_t orientation) {
    memset(self, 0, sizeof(*self));
    self->sync = sync;
    self->rs = rs;
    self->orientation = orientation;
    if (!mutex_create(&self->broadcast_lock)) return false;
    rs_register_component(rs, RS_SHARED_STATE, (ring_callback_t){on_sibling_message, self});
    return true;
}

void ss_watch(shared_state_t *self, component_id_t id, shared_data_t data) {
    if ((unsigned)id >= RS_LAST_COMPONENT_ID) return;
    self->data[id] = data;
}

bool ss_refresh(shared_state_t *self, component_id_t id) {
    if ((unsigned)id >= RS_LAST_COMPONENT_ID) return false;
    shared_data_t *data = &self->data[id];
    uint32_t session, sequence;
    if (!data->ptr || !data->lock ||
        !sync_current_token(self->sync, id, &session, &sequence)) return false;
    bool sent = false;
    state_header_t header = {
        SHARED_STATE_VERSION, id, self->orientation, 0, session, sequence};
    WITH_LOCK(&self->broadcast_lock, {
        uint8_t buffer[sizeof(state_header_t) + UINT8_MAX];
        memcpy(buffer, &header, sizeof(header));
        WITH_LOCK(&data->lock, {
            memcpy(buffer + sizeof(header), data->ptr, data->length);
            /* Reserve the current writer even if transmission fails, so a late
             * earlier writer cannot overwrite the uncommitted local changes. */
            if (newer(self, id, sequence, self->orientation)) {
                self->sequence[id] = sequence;
                self->owner[id] = self->orientation;
            }
        });
        sent = rs_broadcast(self->rs, RS_SHARED_STATE, buffer, sizeof(header) + data->length);
    });
    return sent;
}

void ss_destroy(shared_state_t *self) {
    rs_register_component(self->rs, RS_SHARED_STATE, (ring_callback_t){0});
    mutex_destroy(&self->broadcast_lock);
}
