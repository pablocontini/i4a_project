#include "leader.h"
#include <string.h>
#include "os/os.h"
#define RETRY_MS 5000u
#define GET_CTX(s) (&((s)->_st.leader))

static sync_msg_t grant_message(sync_t *sync, component_id_t id) {
    sync_leader_t *s = GET_CTX(sync);
    return (sync_msg_t){.kind=SYNC_MSG_TOKEN_GRANT, .version=2,
        .token_grant={.cs_id=id, .destination=s->first_device_to_get_token,
                      .session=sync->session, .sequence=s->sequence[id]}};
}
static sync_msg_t begin_circulation(sync_t *sync, component_id_t id) {
    sync_leader_t *s = GET_CTX(sync);
    if (++s->sequence[id] == 0) os_panic("Sync sequence exhausted; restart node");
    s->is_token_out[id] = true;
    s->last_send_ms[id] = os_monotonic_ms();
    return grant_message(sync, id);
}
static void send_grant(sync_t *sync, const sync_msg_t *msg, bool retry) {
    bool ok = rs_broadcast(sync->rs, RS_SYNC, (const uint8_t *)msg, sizeof(*msg));
    os_diag("sync", "event=grant_send orientation=%u cs=%u session=%lu sequence=%lu retry=%u sent=%u",
        sync->orientation, msg->token_grant.cs_id, (unsigned long)msg->token_grant.session,
        (unsigned long)msg->token_grant.sequence, retry, ok);
}
static void request_critical_section(sync_t *sync, component_id_t id) {
    if (id >= RS_LAST_COMPONENT_ID) return;
    bool send = false;
    sync_msg_t msg = {0};
    WITH_LOCK(&sync->state_lock, {
        sync_leader_t *s = GET_CTX(sync);
        if (s->is_token_out[id]) s->requested_tokens[id] = 1;
        else { msg = begin_circulation(sync, id); send = true; }
    });
    if (send) send_grant(sync, &msg, false);
}
static void on_token_request(sync_t *sync, const sync_token_request_t *request) {
    if (request->source < 1 || request->source > N_DEVICES) return;
    request_critical_section(sync, request->cs_id);
}
static void on_token_grant(sync_t *sync, const sync_token_grant_t *grant) {
    if (grant->destination != sync->orientation || grant->cs_id >= RS_LAST_COMPONENT_ID) return;
    component_id_t id = grant->cs_id;
    bool accept = false;
    WITH_LOCK(&sync->state_lock, {
        sync_leader_t *s = GET_CTX(sync);
        if (s->is_token_out[id] && !s->completing[id] && grant->session == sync->session &&
            grant->sequence == s->sequence[id]) { s->completing[id] = true; accept = true; }
    });
    if (!accept) return;
    sync->active_sequence[id]=grant->sequence;
    sync->deferred[id]=false;
    sync_cs_callback_t *cb = &sync->critical_sections[id];
    if (cb->callback) { cb->is_inside = true; cb->callback(cb->context); cb->is_inside = false; }
    if (sync->deferred[id]) {
        WITH_LOCK(&sync->state_lock, { GET_CTX(sync)->completing[id]=false; });
        os_diag("sync", "event=completion_deferred orientation=%u cs=%u sequence=%lu", sync->orientation,id,(unsigned long)grant->sequence);
        return;
    }
    bool send = false;
    sync_msg_t msg = {0};
    WITH_LOCK(&sync->state_lock, {
        sync_leader_t *s = GET_CTX(sync);
        s->completing[id] = false;
        s->is_token_out[id] = false;
        if (s->requested_tokens[id]) { s->requested_tokens[id] = 0; msg=begin_circulation(sync,id); send=true; }
    });
    os_diag("sync", "event=circulation_done orientation=%u cs=%u sequence=%lu", sync->orientation, id, (unsigned long)grant->sequence);
    if (send) send_grant(sync, &msg, false);
}
void sync_on_tick(sync_t *sync) {
    if (!sync || !sync->is_leader || rs_maintenance(sync->rs)) return;
    for (component_id_t id=0; id<RS_LAST_COMPONENT_ID; ++id) {
        bool retry = false;
        sync_msg_t msg = {0};
        WITH_LOCK(&sync->state_lock, {
            sync_leader_t *s = GET_CTX(sync);
            uint64_t now = os_monotonic_ms();
            if (s->is_token_out[id] && !s->completing[id] && now-s->last_send_ms[id]>=RETRY_MS) {
                s->last_send_ms[id]=now; msg=grant_message(sync,id); retry=true;
            }
        });
        if (retry) send_grant(sync, &msg, true);
    }
}
static sync_impl_t IMPL={.on_token_grant=on_token_grant, .on_token_request=on_token_request,
                         .request_critical_section=request_critical_section};
sync_impl_t *sync_leader_init(sync_t *sync) {
    memset(GET_CTX(sync),0,sizeof(sync_leader_t));
    GET_CTX(sync)->first_device_to_get_token=ORIENTATION_NORTH;
    return &IMPL;
}
