#include "neighbor_manager/blocks.h"
#include "nvs.h"

bool nm_block_store_load(const char *provider_uuid, nm_blocks_t *blocks) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("block_leases", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nm_blocks_init(blocks, provider_uuid);
        return true;
    }
    if (err != ESP_OK) return false;
    size_t size = sizeof(*blocks);
    err = nvs_get_blob(handle, "owners_v1", blocks, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nm_blocks_init(blocks, provider_uuid);
        return true;
    }
    return err == ESP_OK && size == sizeof(*blocks) && nm_blocks_valid(blocks, provider_uuid);
}

bool nm_block_store_save(const nm_blocks_t *blocks) {
    nvs_handle_t handle;
    if (!nm_blocks_valid(blocks, blocks->provider_uuid) ||
        nvs_open("block_leases", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, "owners_v1", blocks, sizeof(*blocks));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
