#include "peer_session.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <string.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

/* Real peer_session/registry code; only RPC delivery, socket I/O and time vary. */
static nm_registry_t registry;
static uint8_t input[128];
static unsigned input_size, input_offset, renew_calls, shutdowns, release_calls;
static uint8_t local_orientation;
static uint64_t clock_ms, renew_times[64];
static bool fail_renew, lose_during_renew, stop_during_renew;
static uint32_t rpc_delay_ms;
static peer_session_t *active;

bool mutex_create(mutex_t *m) { *m=(void *)1; return true; }
bool mutex_lock(mutex_t *m) { return m && *m==(void *)1; }
bool mutex_unlock(mutex_t *m) { return m && *m==(void *)1; }
int64_t esp_timer_get_time(void) { return clock_ms*1000; }
uint32_t esp_random(void) { return 42; }
uint8_t node_get_device_orientation(void) { return local_orientation; }
const char *node_get_uuid(void) { return "B00000000000"; }
bool node_is_network_provided(void) { return true; }
uint32_t node_get_device_subnet(void) { return 0x0A400000; }
uint32_t node_get_device_mask(void) { return 0xFFE00000; }
bool nm_reserve(const char *peer, const nm_link_id_t *link) {
    return nm_registry_reserve(&registry,local_orientation,peer,link,clock_ms);
}
bool nm_commit(const nm_link_id_t *link) {
    return nm_registry_advance(&registry,local_orientation,link,NM_COMMITTED,clock_ms);
}
bool nm_activate(const nm_link_id_t *link) {
    return nm_registry_advance(&registry,local_orientation,link,NM_ACTIVE,clock_ms);
}
bool nm_renew(const nm_link_id_t *link) {
    if (renew_calls<64) renew_times[renew_calls]=clock_ms;
    ++renew_calls;
    clock_ms+=rpc_delay_ms;
    if (lose_during_renew) ++registry.claims[local_orientation].link.session;
    if (stop_during_renew) peer_session_shutdown(active);
    return !fail_renew && nm_activate(link);
}
void nm_release(const nm_link_id_t *link) {
    ++release_calls;
    nm_registry_release(&registry,local_orientation,link);
}
bool nm_owns(const nm_link_id_t *link) {
    return nm_registry_owns(&registry,local_orientation,link,clock_ms);
}
bool nm_provide_block(const nm_link_id_t *link, nm_network_t parent,
                      nm_network_t peer, nm_network_t *offer) {
    (void)parent; (void)peer;
    *offer=(nm_network_t){0x0A400000,0xFFE00000};
    return nm_owns(link);
}
int shutdown(int sock,int how) { (void)sock;(void)how;++shutdowns;return 0; }
int setsockopt(int sock,int level,int option,const void *value,unsigned len) {
    (void)sock;(void)level;(void)option;(void)value;(void)len;return 0;
}
int send(int sock,const void *data,unsigned len,int flags) {
    (void)sock;(void)data;(void)flags;return len;
}
int recv(int sock,void *data,unsigned len,int flags) {
    (void)sock;(void)flags;
    if (len>input_size-input_offset) len=input_size-input_offset;
    memcpy(data,input+input_offset,len);input_offset+=len;return len;
}
int lwip_select(int nfds,fd_set *readers,fd_set *writers,fd_set *except,struct timeval *timeout) {
    (void)nfds;(void)readers;(void)writers;(void)except;(void)timeout;
    if (input_offset<input_size) return 1;
    clock_ms+=200;return 0;
}

static void reset_test(peer_session_t *session) {
    memset(&registry,0,sizeof(registry));memset(input,0,sizeof(input));
    input_size=36;input_offset=renew_calls=shutdowns=release_calls=0;clock_ms=0;
    memset(renew_times,0,sizeof(renew_times));
    fail_renew=lose_during_renew=stop_during_renew=false;rpc_delay_ms=0;active=session;
    local_orientation=1;
}
static bool open_session(peer_session_t *session) {
    reset_test(session);
    const uint8_t hello[32]={'I','4','A','N',2,1,0,0};
    memcpy(input,hello,sizeof(hello));memcpy(input+8,"A00000000000",12);input[23]=42;
    input[32]=input[33]=input[34]=input[35]=1;
    return peer_session_init(session) && peer_session_open(session,3,false,NULL);
}
static void queue_frame(void) {
    input_offset=0;input_size=3;input[0]=0;input[1]=1;input[2]=99;
}
static int receive_frame(peer_session_t *session) {
    uint8_t buffer[4]={0};queue_frame();return peer_session_receive(session,buffer,sizeof(buffer));
}

EXPORT int test_transient_renew_failure_preserves_transport_and_retries(void) {
    peer_session_t session;CHECK(open_session(&session));
    clock_ms=5000;fail_renew=true;
    CHECK(receive_frame(&session)==1 && nm_owns(&session.link));
    CHECK(renew_calls==1 && session.renewed_ms==0);
    clock_ms=5999;CHECK(receive_frame(&session)==1 && renew_calls==1);
    clock_ms=6000;fail_renew=false;
    CHECK(receive_frame(&session)==1 && renew_calls==2 && session.renewed_ms==6000);
    CHECK(registry.claims[1].deadline_ms==36000);
    return 0;
}

EXPORT int test_persistent_rpc_loss_expires_without_extending_lease_or_spinning(void) {
    peer_session_t session;CHECK(open_session(&session));fail_renew=true;
    for (clock_ms=5000;clock_ms<30000;clock_ms+=100) {
        CHECK(receive_frame(&session)==1);
        CHECK(registry.claims[1].deadline_ms==30000 && session.renewed_ms==0);
    }
    CHECK(renew_calls==25);
    for (unsigned i=1;i<renew_calls;++i) CHECK(renew_times[i]-renew_times[i-1]>=1000);
    CHECK(receive_frame(&session)==-1 && !nm_owns(&session.link));
    CHECK(renew_calls==25);
    return 0;
}

EXPORT int test_slow_failed_rpc_keeps_valid_lease_and_rate_limits_from_completion(void) {
    peer_session_t session;CHECK(open_session(&session));
    clock_ms=5000;fail_renew=true;rpc_delay_ms=3000;
    CHECK(receive_frame(&session)==1 && clock_ms==8000 && renew_calls==1);
    clock_ms=8999;CHECK(receive_frame(&session)==1 && renew_calls==1);
    clock_ms=9000;CHECK(receive_frame(&session)==1 && clock_ms==12000 && renew_calls==2);
    CHECK(registry.claims[1].deadline_ms==30000 && session.renewed_ms==0);
    return 0;
}

EXPORT int test_expiry_during_rpc_prevents_buffer_delivery(void) {
    peer_session_t session;CHECK(open_session(&session));
    clock_ms=29500;fail_renew=true;rpc_delay_ms=3000;
    CHECK(receive_frame(&session)==-1 && clock_ms==32500);
    CHECK(input_offset==0 && !nm_owns(&session.link));
    return 0;
}

EXPORT int test_replaced_owner_during_rpc_prevents_buffer_delivery(void) {
    peer_session_t session;CHECK(open_session(&session));
    clock_ms=5000;fail_renew=true;lose_during_renew=true;
    CHECK(receive_frame(&session)==-1 && input_offset==0 && !nm_owns(&session.link));
    return 0;
}

EXPORT int test_shutdown_during_rpc_prevents_buffer_delivery(void) {
    peer_session_t session;CHECK(open_session(&session));
    clock_ms=5000;fail_renew=true;stop_during_renew=true;
    CHECK(receive_frame(&session)==-1 && input_offset==0 && session.stopping && shutdowns==1);
    return 0;
}

EXPORT int test_partial_frame_still_has_bounded_completion_time(void) {
    peer_session_t session;CHECK(open_session(&session));
    clock_ms=4000;fail_renew=true;rpc_delay_ms=3000;
    input_offset=0;input_size=3;input[0]=0;input[1]=3;input[2]=99;
    uint8_t buffer[4];
    CHECK(peer_session_receive(&session,buffer,sizeof(buffer))==-1);
    CHECK(clock_ms>=9000 && clock_ms<=9200 && nm_owns(&session.link));
    CHECK(renew_calls==1 && session.renewed_ms==0);
    return 0;
}

EXPORT int test_lost_ownership_and_explicit_stop_prevent_send(void) {
    peer_session_t session;CHECK(open_session(&session));
    ++registry.claims[1].link.session;
    uint8_t data=99;
    CHECK(!peer_session_send(&session,&data,1) && shutdowns==1);
    CHECK(open_session(&session));peer_session_shutdown(&session);
    CHECK(!peer_session_send(&session,&data,1) && renew_calls==0);
    return 0;
}

EXPORT int test_lost_renew_reply_does_not_allow_another_radio_to_replace_active_root(void) {
    peer_session_t north;
    reset_test(&north);local_orientation=0;
    const uint8_t hello[32]={'I','4','A','N',2,0,0,0};
    memcpy(input,hello,sizeof(hello));memcpy(input+8,"000000000000",12);
    input[24]=10;input[28]=255; /* Root's canonical 10.0.0.0/8. */
    input[32]=input[33]=input[34]=input[35]=1;
    CHECK(peer_session_init(&north));
    CHECK(peer_session_open(&north,3,true,"I4A_N_000000000000"));
    CHECK(north.admitted && nm_owns(&north.link) && registry.claims[0].phase==NM_ACTIVE);

    clock_ms=5000;fail_renew=true;
    CHECK(receive_frame(&north)==1 && north.admitted && nm_owns(&north.link));
    CHECK(release_calls==0 && registry.claims[0].deadline_ms==30000);
    nm_link_id_t south={.initiator_uuid="B00000000000",.session=43,
                        .station_orientation=1,.ap_orientation=1};
    CHECK(!nm_registry_reserve(&registry,1,"000000000000",&south,clock_ms));
    CHECK(nm_owns(&north.link) && registry.claims[1].phase==NM_FREE && release_calls==0);

    clock_ms=29999;
    CHECK(receive_frame(&north)==1 && nm_owns(&north.link) && north.admitted);
    CHECK(!nm_registry_reserve(&registry,1,"000000000000",&south,clock_ms));
    CHECK(release_calls==0 && registry.claims[0].deadline_ms==30000);

    clock_ms=30000;
    CHECK(receive_frame(&north)==-1 && !nm_owns(&north.link));
    CHECK(nm_registry_reserve(&registry,1,"000000000000",&south,clock_ms));
    peer_session_close(&north); /* Stale cleanup cannot release South's claim. */
    CHECK(release_calls==1 && !north.admitted);
    CHECK(nm_registry_advance(&registry,1,&south,NM_COMMITTED,clock_ms));
    CHECK(nm_registry_advance(&registry,1,&south,NM_ACTIVE,clock_ms));
    CHECK(nm_registry_owns(&registry,1,&south,clock_ms));
    return 0;
}
