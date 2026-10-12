#ifndef I4A_OFFER_POLICY_H
#define I4A_OFFER_POLICY_H
#include <stdbool.h>
#include <stdint.h>
/* Host-order canonical logical networks, not DHCP /30 addresses. */
/* NULL means accepted; otherwise a stable diagnostic rejection reason. */
const char *node_offer_rejection(uint32_t device_subnet, uint32_t device_mask,
                        bool provisioned, bool original_uplink,
                        uint32_t offered_subnet, uint32_t offered_mask);
bool node_offer_allowed(uint32_t device_subnet, uint32_t device_mask,
                        bool provisioned, bool original_uplink,
                        uint32_t offered_subnet, uint32_t offered_mask);
#endif
