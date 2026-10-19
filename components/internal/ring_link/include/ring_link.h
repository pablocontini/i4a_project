#pragma once

#include "ring_link_lowlevel.h"
#include "ring_link_internal.h"
#include "ring_link_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ring_link_init(void);
typedef struct {
    uint32_t crc_error, unknown_type, local_queue_full, forward_fail;
    /* Untrusted raw type byte of bad-CRC frames, capture diagnostics only. */
    uint32_t crc_internal, crc_ip, crc_other;
} ring_link_diagnostics_t;
void ring_link_get_diagnostics(ring_link_diagnostics_t *out);


#ifdef __cplusplus
}
#endif


