#include "peer_session.h"
#include "lwip/sockets.h"
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

static nm_registry_t registry;
static uint8_t input[1024], output[1024];
static unsigned input_size, input_offset, output_size, fragment;
static uint64_t clock_ms;
static const char *uuid;
static uint8_t orientation;
static bool reject_commit, fail_renew, fail_send, interrupted_send;
static unsigned reservations, shutdowns;
static peer_session_t *send_during_open;
static bool local_provisioned, reject_allocation;
static nm_blocks_t blocks;

bool mutex_create(mutex_t *m) { *m = (void *)1; return true; }
bool mutex_lock(mutex_t *m) { return m && *m == (void *)1; }
bool mutex_unlock(mutex_t *m) { return m && *m == (void *)1; }
int64_t esp_timer_get_time(void) { return clock_ms * 1000; }
uint32_t esp_random(void) { return 42; }
uint8_t node_get_device_orientation(void) { return orientation; }
const char *node_get_uuid(void) { return uuid; }
bool node_is_network_provided(void) { return local_provisioned; }
uint32_t node_get_device_subnet(void) { return 0x0A400000; }
uint32_t node_get_device_mask(void) { return 0xFFE00000; }
bool nm_reserve(const char *peer, const nm_link_id_t *link) {
    ++reservations;
    return nm_registry_reserve(&registry, orientation, peer, link, clock_ms);
}
bool nm_commit(const nm_link_id_t *link) {
    return !reject_commit && nm_registry_advance(&registry, orientation, link, NM_COMMITTED, clock_ms);
}
bool nm_activate(const nm_link_id_t *link) {
    return nm_registry_advance(&registry, orientation, link, NM_ACTIVE, clock_ms);
}
bool nm_renew(const nm_link_id_t *link) { return !fail_renew && nm_activate(link); }
void nm_release(const nm_link_id_t *link) { nm_registry_release(&registry, orientation, link); }
bool nm_owns(const nm_link_id_t *link) { return nm_registry_owns(&registry, orientation, link, clock_ms); }
bool nm_provide_block(const nm_link_id_t *link, nm_network_t parent,
                      nm_network_t peer_network, nm_network_t *offer) {
    return !reject_allocation && nm_owns(link) &&
        nm_blocks_offer(&blocks, registry.claims[orientation].peer_uuid,
                        parent, peer_network, orientation+1U, offer);
}

int shutdown(int sock, int how) { (void)sock; (void)how; ++shutdowns; return 0; }
int setsockopt(int sock, int level, int option, const void *value, unsigned len) {
    (void)sock; (void)level; (void)option; (void)value; (void)len;
    if (send_during_open) {
        uint8_t message = 1;
        peer_session_send(send_during_open, &message, 1);
        send_during_open = NULL;
    }
    return 0;
}
int send(int sock, const void *data, unsigned len, int flags) {
    (void)sock; (void)flags;
    if (interrupted_send) { interrupted_send = false; errno = EINTR; return -1; }
    if (fail_send) { errno = EPIPE; return -1; }
    if (len > fragment) len = fragment;
    if (output_size + len > sizeof(output)) return -1;
    memcpy(output + output_size, data, len);
    output_size += len;
    return len;
}
int recv(int sock, void *data, unsigned len, int flags) {
    (void)sock; (void)flags;
    if (len > fragment) len = fragment;
    if (len > input_size - input_offset) len = input_size - input_offset;
    memcpy(data, input + input_offset, len);
    input_offset += len;
    return len;
}
int lwip_select(int nfds, fd_set *readers, fd_set *writers, fd_set *except, struct timeval *timeout) {
    (void)nfds; (void)readers; (void)writers; (void)except; (void)timeout;
    if (input_offset < input_size) return 1;
    clock_ms += 200;
    return 0;
}

static void reset(bool station) {
    memset(&registry, 0, sizeof(registry));
    input_size = input_offset = output_size = reservations = shutdowns = 0;
    clock_ms = 0;
    fragment = 1024;
    reject_commit = fail_renew = fail_send = interrupted_send = false;
    send_during_open = NULL;
    uuid = station ? "A00000000000" : "B00000000000";
    orientation = station ? 0 : 1;
    local_provisioned = !station;
    reject_allocation = false;
    nm_blocks_init(&blocks, uuid);
    const uint8_t hello[32] = { 'I', '4', 'A', 'N', 2, station ? 0 : 1, station ? 1 : 0, 0 };
    memcpy(input, hello, sizeof(hello));
    memcpy(input + 8, station ? "B00000000000" : "A00000000000", 12);
    if (!station) input[23] = 42;
    if (station) { input[24]=10; input[28]=255; }
    input[32] = input[33] = input[34] = input[35] = 1;
    input_size = 36;
}

EXPORT int test_identity_admission_both_roles_and_framing(void) {
    for (unsigned station = 0; station < 2; ++station) {
        reset(station);
        fragment = 1;
        interrupted_send = true;
        peer_session_t session;
        CHECK(peer_session_init(&session));
        send_during_open = &session;
        CHECK(peer_session_open(&session, 3, station, "I4A_S_B00000000000"));
        CHECK(shutdowns == 0); /* Background traffic must not abort admission. */
        CHECK(session.admitted && nm_owns(&session.link) && reservations == 1);
        CHECK(output_size == 36 && output[32] == 1 && output[33] == 1 && output[34] == 1 && output[35] == 1);
        const uint8_t frames[] = {0, 3, 10, 11, 12, 0, 2, 13, 14};
        memcpy(input + input_size, frames, sizeof(frames));
        input_size += sizeof(frames);
        uint8_t data[512];
        CHECK(peer_session_receive(&session, data, sizeof(data)) == 3 && data[2] == 12);
        CHECK(peer_session_receive(&session, data, sizeof(data)) == 2 && data[1] == 14);
        CHECK(peer_session_send(&session, data, 2));
        CHECK(output_size == 40 && output[36] == 0 && output[37] == 2 && output[39] == 14);
        fail_send = true;
        CHECK(!peer_session_send(&session, data, 2) && shutdowns == 1);
        nm_link_id_t previous = session.link;
        peer_session_close(&session);
        CHECK(!session.admitted && !nm_owns(&previous));
        CHECK(!peer_session_send(&session, data, 2));
    }
    return 0;
}

EXPORT int test_duplicate_and_lost_reservation_never_admit(void) {
    reset(false);
    nm_link_id_t existing = { .initiator_uuid = "A00000000000", .station_orientation = 2,
                             .ap_orientation = 3, .session = 1 };
    CHECK(nm_registry_reserve(&registry, 3, "A00000000000", &existing, 0));
    CHECK(nm_registry_advance(&registry, 3, &existing, NM_COMMITTED, 0));
    CHECK(nm_registry_advance(&registry, 3, &existing, NM_ACTIVE, 0));
    peer_session_t session;
    CHECK(peer_session_init(&session));
    CHECK(!peer_session_open(&session, 3, false, NULL));
    CHECK(!session.admitted && output[32] == 0);
    peer_session_close(&session);
    CHECK(nm_registry_owns(&registry, 3, &existing, clock_ms));
    reset(false);
    CHECK(peer_session_init(&session));
    reject_commit = true;
    CHECK(!peer_session_open(&session, 3, false, NULL));
    CHECK(!session.admitted && output[32] == 1 && output[33] == 0);
    peer_session_close(&session);
    CHECK(!nm_registry_blocks(&registry, "A00000000000", clock_ms));
    return 0;
}

EXPORT int test_incompatible_identity_and_incomplete_frames(void) {
    for (unsigned bad = 0; bad < 5; ++bad) {
        reset(true);
        if (bad == 0) input[0] = 1; /* Legacy routing frame. */
        if (bad == 1) input[4] = 1;
        if (bad == 2) input[5] = 1;
        if (bad == 3) memcpy(input + 8, "A00000000000", 12);
        if (bad == 4) input[6] = 3; /* Does not match scanned AP orientation. */
        peer_session_t session;
        CHECK(peer_session_init(&session));
        CHECK(!peer_session_open(&session, 3, true, "I4A_S_B00000000000"));
        CHECK(!session.admitted && reservations == 0);
        peer_session_close(&session);
    }
    reset(false);
    peer_session_t session;
    CHECK(peer_session_init(&session));
    CHECK(peer_session_open(&session, 3, false, NULL));
    input[input_size++] = 0;
    input[input_size++] = 3;
    input[input_size++] = 1; /* Body stops after one of three bytes. */
    uint8_t data[512];
    CHECK(peer_session_receive(&session, data, sizeof(data)) == -1);
    CHECK(clock_ms >= 5000 && clock_ms < NM_ACTIVE_MS);
    peer_session_close(&session);
    return 0;
}

EXPORT int test_session_allocation_precedes_admission_and_clears_stale_offer(void) {
    reset(false);
    reject_allocation=true;
    peer_session_t session;
    CHECK(peer_session_init(&session));
    CHECK(!peer_session_open(&session,3,false,NULL));
    CHECK(!session.admitted && output[34]==0 && output_size==35);
    uint32_t network=1, mask=1;
    CHECK(!peer_session_provision_offer(&session,&network,&mask) && !network && !mask);
    peer_session_close(&session);
    reset(false);
    CHECK(peer_session_init(&session) && peer_session_open(&session,3,false,NULL));
    CHECK(peer_session_provision_offer(&session,&network,&mask));
    CHECK(network==0x0A400000 && mask==0xFFE00000);
    CHECK(peer_session_check_peer_network(&session,network,mask) && !shutdowns);
    CHECK(!peer_session_check_peer_network(&session,0x0A600000,mask) && shutdowns==1);
    peer_session_close(&session);
    CHECK(!peer_session_provision_offer(&session,&network,&mask) && !network && !mask);
    reset(true);
    input[24]=10; input[25]=1; /* Noncanonical advertised /8. */
    CHECK(peer_session_init(&session));
    CHECK(!peer_session_open(&session,3,true,"I4A_S_B00000000000") && !reservations);
    peer_session_close(&session);
    return 0;
}

EXPORT int test_expired_or_unrenewed_session_stops_transport(void) {
    for (unsigned expired = 0; expired < 2; ++expired) {
        reset(false);
        peer_session_t session;
        CHECK(peer_session_init(&session));
        CHECK(peer_session_open(&session, 3, false, NULL));
        clock_ms = expired ? NM_ACTIVE_MS : 5000;
        fail_renew = !expired;
        uint8_t data[512];
        CHECK(peer_session_receive(&session, data, sizeof(data)) == -1);
        peer_session_close(&session);
        CHECK(!nm_registry_blocks(&registry, "A00000000000", clock_ms));
    }
    reset(false);
    peer_session_t session;
    CHECK(peer_session_init(&session));
    CHECK(peer_session_open(&session, 3, false, NULL));
    peer_session_shutdown(&session);
    uint8_t data[1] = {1};
    CHECK(peer_session_receive(&session, data, sizeof(data)) == -1);
    CHECK(!peer_session_send(&session, data, 1));
    peer_session_close(&session);
    return 0;
}
