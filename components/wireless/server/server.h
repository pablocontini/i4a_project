#ifndef _SERVER_H_
#define _SERVER_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void server_create();
void server_close();
void server_wait_stopped(void);
bool server_send_message(const uint8_t *msg, uint16_t len);
bool server_get_provision_offer(uint32_t *network, uint32_t *mask);
bool server_check_peer_network(uint32_t network, uint32_t mask);

#ifdef __cplusplus
}
#endif

#endif // _SERVER_H_
