#pragma once
#include <stdint.h>
typedef struct { uint8_t buffer_type, id, ttl; uint8_t data[57]; uint32_t crc32; } ring_link_payload_t;
uint32_t ring_link_compute_crc32(const ring_link_payload_t *p);
