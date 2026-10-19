#include <stdlib.h>
#include <string.h>
#include "shared_state/shared_state.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define MAX_EVENTS 2048

typedef struct { int held; } lock_t;
static void *known[64];
static unsigned nlocks, lock_errors;
bool mutex_create(mutex_t *p) {
    *p = calloc(1, sizeof(lock_t));
    if (*p) known[nlocks++] = *p;
    return *p != NULL;
}
static bool valid_lock(mutex_t *p) {
    /* A mutex handle may be copied into shared_data, but its raw allocation
     * must never be passed where the API expects the address of a handle. */
    for (unsigned i=0; i<nlocks; ++i) if (known[i] == p) { ++lock_errors; return false; }
    for (unsigned i=0; p && i<nlocks; ++i) if (known[i] == *p) return true;
    ++lock_errors;
    return false;
}
bool mutex_lock(mutex_t *p) {
    if (!valid_lock(p)) return false;
    lock_t *v = *p;
    if (!v || v->held) { ++lock_errors; return false; }
    v->held = 1; return true;
}
bool mutex_unlock(mutex_t *p) {
    if (!valid_lock(p)) return false;
    lock_t *v = *p;
    if (!v || !v->held) { ++lock_errors; return false; }
    v->held = 0; return true;
}
void mutex_destroy(mutex_t *p) {
    if (!valid_lock(p)) return;
    free(*p); *p = NULL;
}
void os_panic(const char *fmt, ...) { (void)fmt; ++lock_errors; }
void os_log(os_log_level_t l, const char *t, const char *f, ...) {(void)l;(void)t;(void)f;}
uint32_t esp_random(void) { return 0x126789ab; }

typedef struct { int source; component_id_t component; uint16_t len; uint8_t data[512]; } event_t;
static ring_share_t ring[N_DEVICES];
static sync_t nodes[N_DEVICES];
static shared_state_t shared[N_DEVICES];
static mutex_t data_locks[N_DEVICES];
static unsigned values[N_DEVICES], callbacks[N_DEVICES], mutations[N_DEVICES];
static bool pending[N_DEVICES];
static event_t events[MAX_EVENTS], last_state;
static unsigned head, tail;
static int drop_destination, fail_writer;
static bool fail_token;

void rs_register_component(ring_share_t *rs, component_id_t id, ring_callback_t cb) { rs->components[id] = cb; }
bool rs_broadcast(ring_share_t *rs, component_id_t id, const void *data, uint16_t len) {
    int source = (int)(rs - ring);
    if (len > 512 || tail == MAX_EVENTS) { ++lock_errors; return false; }
    if (id == RS_SHARED_STATE && source == fail_writer) { fail_writer = -1; return false; }
    if (id == RS_SYNC) {
        sync_msg_t msg;
        memcpy(&msg, data, sizeof(msg));
        if (msg.kind == SYNC_MSG_TOKEN_GRANT && fail_token) { fail_token = false; return false; }
        if (msg.kind == SYNC_MSG_TOKEN_GRANT && msg.token_grant.destination == drop_destination) {
            drop_destination = -1; return true;
        }
    }
    event_t *ev = &events[tail++];
    ev->source = source; ev->component = id; ev->len = len;
    memcpy(ev->data, data, len);
    if (id == RS_SHARED_STATE) last_state = *ev;
    return true;
}

static void update(void *ctx) {
    int index = (int)(size_t)ctx;
    ++callbacks[index];
    if (!pending[index]) {
        ++values[index]; ++mutations[index];
        pending[index] = true;
    }
    if (ss_refresh(&shared[index], RS_ROUTING)) pending[index] = false;
    else sync_defer_critical_section(&nodes[index], RS_ROUTING);
}
static int drain(void) {
    unsigned steps = 0;
    while (head < tail) {
        if (++steps > 1500) return 0;
        event_t ev = events[head++];
        for (int hop=1; hop<N_DEVICES; ++hop) {
            int index = (ev.source + hop) % N_DEVICES;
            ring_callback_t cb = ring[index].components[ev.component];
            if (cb.callback) cb.callback(cb.context, ev.data, ev.len);
        }
    }
    head = tail = 0;
    return 1;
}
static int setup(void) {
    nlocks = lock_errors = head = tail = 0;
    drop_destination = fail_writer = -1; fail_token = false;
    memset(ring, 0, sizeof(ring)); memset(values, 0, sizeof(values));
    memset(callbacks, 0, sizeof(callbacks)); memset(mutations, 0, sizeof(mutations));
    memset(pending, 0, sizeof(pending));
    for (int i=0; i<N_DEVICES; ++i) {
        if (!sync_init(&nodes[i], &ring[i], i+1) ||
            !ss_init(&shared[i], &nodes[i], &ring[i], i+1) ||
            !mutex_create(&data_locks[i])) return 0;
        ss_watch(&shared[i], RS_ROUTING, (shared_data_t){data_locks[i], &values[i], sizeof(values[i])});
        sync_register_critical_section(&nodes[i], RS_ROUTING, update, (void *)(size_t)i);
    }
    return 1;
}
static void teardown(void) {
    for (int i=0; i<N_DEVICES; ++i) {
        ss_destroy(&shared[i]); sync_destroy(&nodes[i]); mutex_destroy(&data_locks[i]);
    }
}

EXPORT int test_token_loss_replays_without_duplicate_callbacks(void) {
    for (int destination=1; destination<=N_DEVICES; ++destination) {
        CHECK(setup()); drop_destination = destination;
        sync_request_critical_section(&nodes[4], RS_ROUTING); CHECK(drain());
        CHECK(nodes[4]._st.leader.is_token_out[RS_ROUTING]);
        sync_on_tick(&nodes[4], 4999); CHECK(head == tail);
        sync_on_tick(&nodes[4], 1); CHECK(drain());
        CHECK(!nodes[4]._st.leader.is_token_out[RS_ROUTING]);
        for (int i=0; i<N_DEVICES; ++i) CHECK(callbacks[i] == 1 && values[i] == 5);
        CHECK(lock_errors == 0); teardown(); CHECK(lock_errors == 0);
    }
    return 0;
}

EXPORT int test_failed_token_send_and_request_coalescing(void) {
    CHECK(setup()); fail_token = true;
    sync_request_critical_section(&nodes[4], RS_ROUTING);
    for (int i=0; i<300; ++i) sync_request_critical_section(&nodes[4], RS_ROUTING);
    CHECK(nodes[4]._st.leader.requested_tokens[RS_ROUTING] == 1);
    sync_on_tick(&nodes[4], 5000); CHECK(drain());
    for (int i=0; i<N_DEVICES; ++i) CHECK(callbacks[i] == 2 && values[i] == 10);
    CHECK(lock_errors == 0); teardown(); return 0;
}

EXPORT int test_failed_state_publication_retains_token_until_retry(void) {
    for (int writer=0; writer<N_DEVICES; ++writer) {
        CHECK(setup()); fail_writer = writer;
        sync_request_critical_section(&nodes[4], RS_ROUTING); CHECK(drain());
        CHECK(nodes[4]._st.leader.is_token_out[RS_ROUTING]);
        CHECK(pending[writer]);
        for (int i=0; i<N_DEVICES; ++i) CHECK(mutations[i] == (unsigned)(i<=writer));
        sync_on_tick(&nodes[4], 5000); CHECK(drain());
        CHECK(!nodes[4]._st.leader.is_token_out[RS_ROUTING]);
        for (int i=0; i<N_DEVICES; ++i) {
            CHECK(mutations[i] == 1 && values[i] == 5 && !pending[i]);
            CHECK(callbacks[i] == (unsigned)(i == writer ? 2 : 1));
        }
        CHECK(lock_errors == 0); teardown();
    }
    return 0;
}

EXPORT int test_stale_malformed_and_wrong_owner_state_is_ignored(void) {
    CHECK(setup()); sync_request_critical_section(&nodes[4], RS_ROUTING); CHECK(drain());
    event_t old = last_state;
    sync_request_critical_section(&nodes[4], RS_ROUTING); CHECK(drain());
    ring_callback_t state_cb = ring[0].components[RS_SHARED_STATE];
    state_cb.callback(state_cb.context, old.data, old.len);
    CHECK(values[0] == 10);
    state_cb.callback(state_cb.context, NULL, 0);
    for (unsigned len=0; len<last_state.len; ++len)
        state_cb.callback(state_cb.context, last_state.data, len);
    event_t bad = last_state;
    bad.data[1] = 255;
    state_cb.callback(state_cb.context, bad.data, bad.len);
    bad = last_state; bad.data[2] = 1;
    state_cb.callback(state_cb.context, bad.data, bad.len);
    bad = last_state; bad.data[4] ^= 1;
    state_cb.callback(state_cb.context, bad.data, bad.len);
    CHECK(values[0] == 10);
    CHECK(!ss_refresh(&shared[0], RS_ROUTING));
    CHECK(lock_errors == 0); teardown(); return 0;
}

EXPORT int test_invalid_and_old_tokens_cannot_execute_callbacks(void) {
    CHECK(setup()); sync_request_critical_section(&nodes[4], RS_ROUTING); CHECK(drain());
    ring_callback_t cb = ring[0].components[RS_SYNC];
    sync_msg_t msg = {.version=2, .kind=SYNC_MSG_TOKEN_GRANT,
        .token_grant={1, RS_ROUTING, nodes[0].session, 1}};
    cb.callback(cb.context, (uint8_t *)&msg, sizeof(msg)); CHECK(drain());
    CHECK(callbacks[0] == 1);
    msg.token_grant.sequence = 0; cb.callback(cb.context, (uint8_t *)&msg, sizeof(msg));
    msg.token_grant.sequence = 1; msg.token_grant.cs_id = 255;
    cb.callback(cb.context, (uint8_t *)&msg, sizeof(msg));
    msg.token_grant.cs_id = RS_ROUTING; msg.token_grant.session ^= 1;
    cb.callback(cb.context, (uint8_t *)&msg, sizeof(msg));
    msg.version = 1; cb.callback(cb.context, (uint8_t *)&msg, sizeof(msg));
    cb.callback(cb.context, NULL, 0);
    CHECK(callbacks[0] == 1 && lock_errors == 0); teardown(); return 0;
}
