#include "shared_state/shared_state.h"

#include <string.h>

#define TAG "shared_state"

typedef struct { uint8_t component, version, owner, reserved; uint32_t session, sequence; } ss_header_t;

static void on_sibling_message(void *ctx, const uint8_t *msg, uint16_t len) {
    shared_state_t *self = ctx;

    if (len < sizeof(ss_header_t)) return;
    ss_header_t header;
    memcpy(&header,msg,sizeof(header));
    uint8_t component=header.component;
    if (component>=RS_LAST_COMPONENT_ID || header.version!=2 || header.owner<1 || header.owner>N_DEVICES ||
        !header.session || !header.sequence) return;
    if (self->sync->session && header.session!=self->sync->session) return;
    shared_data_t *data=&self->data[component];
    if (!data->ptr || data->length != len-sizeof(header)) return;
    if (self->seen_session[component]==header.session &&
        (header.sequence<self->seen_sequence[component] ||
         (header.sequence==self->seen_sequence[component] && header.owner<self->seen_owner[component]))) return;
    /* A writer may update repeatedly within one turn; SPI preserves its FIFO order. */
    WITH_LOCK(data->lock, { memcpy(data->ptr,msg+sizeof(header),data->length); });
    self->seen_session[component]=header.session;
    self->seen_sequence[component]=header.sequence;
    self->seen_owner[component]=header.owner;
    os_diag(TAG,"event=table_received orientation=%u owner=%u sequence=%lu",self->orientation,header.owner,(unsigned long)header.sequence);
    if (data->on_updated) data->on_updated(data->context);

}

bool ss_init(shared_state_t *self, sync_t *sync, ring_share_t *rs, orientation_t orientation) {
    memset(self, 0, sizeof(shared_state_t));

    self->sync = sync;
    self->rs = rs;
    self->orientation = orientation;

    if (!mutex_create(&self->broadcast_lock))
        return false;

    rs_register_component(
        self->rs, RS_SHARED_STATE,
        (ring_callback_t){
            .callback = on_sibling_message,
            .context = self,
        }
    );

    return true;
}

bool ss_flush(shared_state_t *self, component_id_t component) {
    if (component>=RS_LAST_COMPONENT_ID || !sync_is_inside_critical_section(self->sync,component)) return false;
    if (!self->dirty[component]) return true;
    shared_data_t *data=&self->data[component];
    uint8_t buffer[sizeof(ss_header_t)+data->length];
    ss_header_t header={.component=component,.version=2,.owner=self->orientation,
        .session=self->sync->session,.sequence=self->sync->active_sequence[component]};
    memcpy(buffer,&header,sizeof(header));
    WITH_LOCK(data->lock,{ memcpy(buffer+sizeof(header),data->ptr,data->length); });
    bool sent;
    WITH_LOCK(&self->broadcast_lock,{ sent=rs_broadcast(self->rs,RS_SHARED_STATE,buffer,sizeof(buffer)); });
    self->dirty[component]=!sent;
    if (sent) {
        self->seen_session[component]=header.session;
        self->seen_sequence[component]=header.sequence;
        self->seen_owner[component]=header.owner;
    }
    os_diag(TAG,"event=table_publish orientation=%u sequence=%lu sent=%u",self->orientation,(unsigned long)header.sequence,sent);
    return sent;
}

void ss_watch(shared_state_t *self, component_id_t component, shared_data_t data) {
    self->data[component] = data;  // TODO: There could be a race condition here
}

void ss_refresh(shared_state_t *self, component_id_t component) {
    if (!sync_is_inside_critical_section(self->sync, component)) {
        os_panic("[shared_state] Not inside critical section -- aborting");
        return;
    }

    self->dirty[component]=true;
    (void)ss_flush(self,component);
}

void ss_destroy(shared_state_t *self) {
    rs_register_component(
        self->rs, RS_SHARED_STATE,
        (ring_callback_t){
            .callback = NULL,
            .context = NULL,
        }
    );
    mutex_destroy(&self->broadcast_lock);
}
