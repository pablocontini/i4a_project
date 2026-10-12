#include "follower.h"
#include "os/os.h"
static void on_token_grant(sync_t *sync, const sync_token_grant_t *grant) {
    if (grant->destination != sync->orientation || grant->cs_id >= RS_LAST_COMPONENT_ID ||
        !grant->session || !grant->sequence) return;
    if (!sync->session) sync->session=grant->session;
    if (sync->session != grant->session) return; /* Node-wide reset establishes a new session. */
    uint32_t *done=&sync->completed[grant->cs_id];
    if (grant->sequence < *done) return;
    bool replay=grant->sequence == *done;
    if (!replay) {
        sync->active_sequence[grant->cs_id]=grant->sequence;
        sync->deferred[grant->cs_id]=false;
        sync_cs_callback_t *cb=&sync->critical_sections[grant->cs_id];
        if (cb->callback) { cb->is_inside=true; cb->callback(cb->context); cb->is_inside=false; }
        if (sync->deferred[grant->cs_id]) return;
        *done=grant->sequence;
    }
    sync_msg_t msg={.kind=SYNC_MSG_TOKEN_GRANT, .version=2, .token_grant=*grant};
    msg.token_grant.destination=(sync->orientation % N_DEVICES)+1;
    bool ok=rs_broadcast(sync->rs,RS_SYNC,(const uint8_t *)&msg,sizeof(msg));
    os_diag("sync", "event=grant_forward orientation=%u cs=%u sequence=%lu replay=%u sent=%u",
            sync->orientation,grant->cs_id,(unsigned long)grant->sequence,replay,ok);
}
static void request_critical_section(sync_t *sync, component_id_t id) {
    sync_msg_t msg={.kind=SYNC_MSG_TOKEN_REQUEST,.version=2,
                   .token_request={.cs_id=id,.source=sync->orientation}};
    bool ok=rs_broadcast(sync->rs,RS_SYNC,(const uint8_t *)&msg,sizeof(msg));
    os_diag("sync", "event=token_request orientation=%u cs=%u sent=%u", sync->orientation,id,ok);
}
static sync_impl_t IMPL={.on_token_grant=on_token_grant, .request_critical_section=request_critical_section};
sync_impl_t *sync_follower_init(sync_t *sync) { (void)sync; return &IMPL; }
