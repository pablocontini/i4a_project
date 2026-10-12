#ifndef I4A_SERVER_TEST_CALLBACKS_H
#define I4A_SERVER_TEST_CALLBACKS_H
#include <stdint.h>
#define PEER_SERVER 1
void node_on_peer_connected(uint32_t net,uint32_t mask,int peer);
void node_on_peer_lost(uint32_t net,uint32_t mask,int peer);
void node_on_peer_message(void *data,uint16_t len);

#endif
