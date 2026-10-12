#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "sync/sync.h"
#include "shared_state/shared_state.h"
#define CHECK(x) do {if (!(x)) {printf("FAIL line=%d\n",__LINE__);return 1;}} while(0)
typedef struct {unsigned to; component_id_t component; uint16_t len; uint8_t data[512];} frame_t;
static frame_t frames[4096]; static unsigned head,tail;
static ring_share_t rings[5]; static sync_t nodes[5]; static shared_state_t tables[5];
static ring_callback_t handlers[5][RS_LAST_COMPONENT_ID];
static unsigned calls[5], active, violation, drops, drop_destination, table_drops, sends;
static uint32_t values[5]; static uint64_t clock_ms;
static bool duplicate, false_with_delivery, table_mode, mutated, maintenance;
bool mutex_create(mutex_t *m) {*m=(void *)1;return true;}
bool mutex_lock(mutex_t *m) {(void)m;return true;}
bool mutex_unlock(mutex_t *m) {(void)m;return true;}
void mutex_destroy(mutex_t *m) {(void)m;}
void os_panic(const char *fmt,...) {(void)fmt;violation=1;}
void os_log(os_log_level_t level,const char *tag,const char *fmt,...) {(void)level;(void)tag;(void)fmt;}
void os_diag(const char *tag,const char *fmt,...) {(void)tag;(void)fmt;}
uint64_t os_monotonic_ms(void) {return clock_ms;}
uint32_t os_random_u32(void) {return 12345;}
bool rs_maintenance(ring_share_t *r) {(void)r;return maintenance;}
static unsigned index_of(ring_share_t *r) {return (unsigned)(r-rings);}
void rs_register_component(ring_share_t *r,component_id_t c,ring_callback_t h) {handlers[index_of(r)][c]=h;}
static void enqueue(unsigned to,component_id_t c,const void *data,uint16_t len) {
    if (tail>=4096 || len>512) {violation=1;return;}
    frames[tail].to=to;frames[tail].component=c;frames[tail].len=len;
    memcpy(frames[tail++].data,data,len);
}
bool rs_broadcast(ring_share_t *r,component_id_t c,const void *data,uint16_t len) {
    ++sends;
    bool fail=false; unsigned source=index_of(r);
    if (c==RS_SYNC) {
        const sync_msg_t *m=data;
        if (m->kind==SYNC_MSG_TOKEN_GRANT && drops && m->token_grant.destination==drop_destination) {--drops;fail=true;}
    } else if (c==RS_SHARED_STATE && table_drops) {--table_drops;fail=true;}
    if (!fail || false_with_delivery) {
        for (unsigned i=0;i<5;++i) if (i!=source) {
            enqueue(i,c,data,len);
            if (duplicate && c==RS_SYNC) enqueue(i,c,data,len);
        }
    }
    return !fail;
}
static void callback(void *ctx) {
    unsigned i=(unsigned)(size_t)ctx;
    if (++active!=1) violation=1;
    ++calls[i];
    if (table_mode) {
        if (!ss_flush(&tables[i],RS_ROUTING)) {sync_defer_completion(&nodes[i],RS_ROUTING);--active;return;}
        if (i==0 && !mutated) {mutated=true;values[0]=42;ss_refresh(&tables[0],RS_ROUTING);}
        if (!ss_flush(&tables[i],RS_ROUTING)) sync_defer_completion(&nodes[i],RS_ROUTING);
    }
    --active;
}
static void drain(void) {
    while(head<tail && !violation) {
        frame_t f=frames[head++];ring_callback_t h=handlers[f.to][f.component];
        if(h.callback) h.callback(h.context,f.data,f.len);
    }
    if(head==tail) head=tail=0;
}
static int setup(void) {
    memset(rings,0,sizeof(rings));memset(nodes,0,sizeof(nodes));memset(tables,0,sizeof(tables));
    memset(handlers,0,sizeof(handlers));memset(calls,0,sizeof(calls));memset(values,0,sizeof(values));
    head=tail=active=violation=drops=drop_destination=table_drops=sends=0;
    clock_ms=0;duplicate=false_with_delivery=table_mode=mutated=maintenance=false;
    for(unsigned i=0;i<5;++i) {
        CHECK(sync_init(&nodes[i],&rings[i],i+1));
        CHECK(ss_init(&tables[i],&nodes[i],&rings[i],i+1));
        sync_register_critical_section(&nodes[i],RS_ROUTING,callback,(void *)(size_t)i);
        ss_watch(&tables[i],RS_ROUTING,(shared_data_t){.lock=(void *)1,.ptr=&values[i],.length=sizeof(values[i])});
    }
    return 0;
}
static int dropped_hop(unsigned hop,bool partial) {
    CHECK(setup()==0);drop_destination=hop;drops=1;false_with_delivery=partial;
    sync_request_critical_section(&nodes[4],RS_ROUTING);drain();
    if(!partial) {CHECK(nodes[4]._st.leader.is_token_out[RS_ROUTING]);clock_ms=5001;sync_on_tick(&nodes[4]);drain();}
    for(unsigned i=0;i<5;++i) CHECK(calls[i]==1);
    CHECK(!nodes[4]._st.leader.is_token_out[RS_ROUTING] && !violation);
    return 0;
}
static int duplicate_and_stale(void) {
    CHECK(setup()==0);duplicate=true;
    sync_request_critical_section(&nodes[4],RS_ROUTING);drain();
    for(unsigned i=0;i<5;++i) CHECK(calls[i]==1);
    duplicate=false;sync_request_critical_section(&nodes[4],RS_ROUTING);drain();
    sync_msg_t old={.kind=SYNC_MSG_TOKEN_GRANT,.version=2,.token_grant={.destination=1,.cs_id=RS_ROUTING,.session=12345,.sequence=1}};
    enqueue(0,RS_SYNC,&old,sizeof(old));old.token_grant.sequence=3;old.token_grant.session=9876;
    enqueue(0,RS_SYNC,&old,sizeof(old));drain();
    for(unsigned i=0;i<5;++i) CHECK(calls[i]==2);
    CHECK(!violation);return 0;
}
static int failed_table_holds_turn(void) {
    CHECK(setup()==0);table_mode=true;table_drops=2;
    sync_request_critical_section(&nodes[4],RS_ROUTING);drain();
    CHECK(calls[0]==1 && calls[1]==0 && nodes[0].completed[RS_ROUTING]==0);
    CHECK(tables[0].dirty[RS_ROUTING] && mutated);
    clock_ms=5001;sync_on_tick(&nodes[4]);drain();
    CHECK(calls[0]==2);
    for(unsigned i=0;i<5;++i) CHECK(values[i]==42);
    CHECK(!tables[0].dirty[RS_ROUTING] && !nodes[4]._st.leader.is_token_out[RS_ROUTING]);
    return 0;
}
static int coalesced_and_maintenance(void) {
    CHECK(setup()==0);drop_destination=1;drops=1;
    sync_request_critical_section(&nodes[4],RS_ROUTING);
    for(unsigned i=0;i<15;++i) sync_request_critical_section(&nodes[4],RS_ROUTING);
    CHECK(nodes[4]._st.leader.requested_tokens[RS_ROUTING]==1);
    unsigned before=sends;clock_ms=6000;maintenance=true;sync_on_tick(&nodes[4]);CHECK(sends==before);
    maintenance=false;sync_on_tick(&nodes[4]);drain();
    for(unsigned i=0;i<5;++i) CHECK(calls[i]==2);
    return 0;
}
int main(void) {
    for(unsigned i=1;i<=5;++i) {if(dropped_hop(i,false)) return 1;}
    if(dropped_hop(1,true) || duplicate_and_stale() || failed_table_holds_turn() || coalesced_and_maintenance()) return 1;
    puts("coordination: 5 lost hops, partial delivery, duplicates/stale epochs, table retry, coalescing and maintenance OK");return 0;
}
