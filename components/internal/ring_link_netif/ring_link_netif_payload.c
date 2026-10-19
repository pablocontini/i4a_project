#include "ring_link_netif_common.h"

#include <stdlib.h>

bool ring_link_netif_ip_length(const ring_link_payload_t *payload, uint16_t *length)
{
    if (!payload || !length || payload->len < 20 ||
        payload->len > sizeof(payload->buffer)) return false;

    const uint8_t *ip = (const uint8_t *)payload->buffer;
    unsigned total;
    if ((ip[0] >> 4) == 4) {
        unsigned header = (ip[0] & 0x0f) * 4U;
        total = ((unsigned)ip[2] << 8) | ip[3];
        if (header < 20 || header > payload->len || total < header) return false;
    } else if ((ip[0] >> 4) == 6) {
        if (payload->len < 40) return false;
        /* IPv6 stores payload length, excluding its 40-byte fixed header. */
        total = 40U + (((unsigned)ip[4] << 8) | ip[5]);
    } else return false;

    if (total > payload->len) return false;
    *length = (uint16_t)total;
    return true;
}

esp_err_t ring_link_netif_transmit(const void *buffer, size_t len, ring_link_payload_id_t id)
{
    if (!buffer || !len) return ESP_ERR_INVALID_ARG;
    if (len > RING_LINK_NETIF_MTU) return ESP_ERR_INVALID_SIZE;

    /* calloc also clears the unused wire bytes included in the frame CRC.
     * The previous automatic frame consumed 1600 bytes beneath lwIP/socket
     * callbacks on a TCP/IP stack configured with only 3072 bytes. */
    ring_link_payload_t *payload = calloc(1, sizeof(*payload));
    if (!payload) return ESP_ERR_NO_MEM;
    payload->id = id;
    payload->ttl = RING_LINK_PAYLOAD_TTL;
    payload->src_id = config_get_id();
    payload->dst_id = CONFIG_ID_ANY;
    payload->buffer_type = RING_LINK_PAYLOAD_TYPE_ESP_NETIF;
    payload->len = len;
    memcpy(payload->buffer, buffer, len);

    esp_err_t result = ring_link_lowlevel_transmit_payload(payload);
    free(payload);
    return result;
}
