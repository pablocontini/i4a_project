#ifndef I4A_SERVER_TEST_PEER_SESSION_H
#define I4A_SERVER_TEST_PEER_SESSION_H
#include <stdbool.h>
#include <stdint.h>
#define PEER_SESSION_MAX_MESSAGE 512
typedef struct {bool admitted;} peer_session_t;
bool peer_session_init(peer_session_t *s);
void peer_session_start(peer_session_t *s);
void peer_session_shutdown(peer_session_t *s);
bool peer_session_open(peer_session_t *s,int sock,bool sta,const char *ssid);
void peer_session_close(peer_session_t *s);
int peer_session_receive(peer_session_t *s,uint8_t *data,uint16_t len);
bool peer_session_send(peer_session_t *s,const uint8_t *data,uint16_t len);

#endif
