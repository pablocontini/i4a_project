#pragma once

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ring_link_payload.h"
#include "ring_link_lowlevel.h"

#include "broadcast.h"

#define RING_LINK_INTERNAL_QUEUE_SIZE 10

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ring_link_internal_init(QueueHandle_t **queue);
/* Route after CRC validation and before local queue admission. The caller
 * retains buffer ownership; deliver_local selects whether to enqueue it. */
esp_err_t ring_link_internal_route(ring_link_payload_t *p, bool *deliver_local);
esp_err_t ring_link_internal_process(ring_link_payload_t *p);

#ifdef __cplusplus
}
#endif



