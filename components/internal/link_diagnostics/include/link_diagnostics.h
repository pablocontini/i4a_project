#pragma once
#include <stdint.h>
typedef enum {LD_RX, LD_SHORT, LD_RX_FULL, LD_FREE_FAIL, LD_TX, LD_TX_FAIL,
 LD_CRC, LD_CRC_IP, LD_UNKNOWN, LD_DISPATCH_FULL, LD_RX_CRC_EARLY, LD_RX_CHANGED, LD_COUNT} ld_event_t;
void ld_count(ld_event_t event);
void ld_pressure(uint32_t wait_us, uint32_t free_count, uint32_t rx_count);
void ld_tick(void);
