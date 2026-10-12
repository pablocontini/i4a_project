#ifndef I4A_SERVER_TEST_NODE_H
#define I4A_SERVER_TEST_NODE_H
#include <stdbool.h>
int node_get_device_orientation(void);
void node_set_peer_offer_accepted(bool accepted);
void node_reject_wireless_peer(void);

#endif
