#ifndef I4A_PEER_SESSION_H
#define I4A_PEER_SESSION_H

#include "neighbor_manager/neighbor_manager.h"
#include "os/os.h"

#define PEER_SESSION_MAX_MESSAGE 512

typedef struct {
    mutex_t write_lock;
    int sock;
    bool stopping;
    bool admitted;
    nm_link_id_t link;
    nm_network_t provided_network;
    nm_network_t peer_network;
    uint64_t renewed_ms;
    uint64_t renew_retry_at_ms;
} peer_session_t;

bool peer_session_init(peer_session_t *session);
void peer_session_start(peer_session_t *session);
void peer_session_shutdown(peer_session_t *session);
/* Identity + reserve/commit/activate votes complete before any routing callback. */
bool peer_session_open(peer_session_t *session, int sock, bool station, const char *expected_ssid);
void peer_session_close(peer_session_t *session);
int peer_session_receive(peer_session_t *session, uint8_t *buffer, uint16_t capacity);
bool peer_session_send(peer_session_t *session, const uint8_t *data, uint16_t len);
bool peer_session_provision_offer(peer_session_t *session, uint32_t *network, uint32_t *mask);
/* A changed claim must negotiate admission again, before it can alter routes. */
bool peer_session_check_peer_network(peer_session_t *session, uint32_t network, uint32_t mask);

#endif
