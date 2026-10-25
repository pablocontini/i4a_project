#include "neighbor_manager/blocks.h"
#include "nvs.h"
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static nm_blocks_t durable;
static size_t blob_size;
static bool exists;
static int fail_open, fail_set, fail_commit;
static unsigned closes, commits;

esp_err_t nvs_open(const char *space, unsigned mode, nvs_handle_t *handle) {
    if (strcmp(space,"block_leases")) return -1;
    if (fail_open) return fail_open;
    if (!exists && mode==NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    *handle=1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size) {
    if (handle!=1 || strcmp(key,"owners_v1")) return -1;
    if (!exists) return ESP_ERR_NVS_NOT_FOUND;
    if (*size<blob_size) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(data,&durable,blob_size); *size=blob_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    if (handle!=1 || strcmp(key,"owners_v1") || size!=sizeof(durable)) return -1;
    if (fail_set) return fail_set;
    memcpy(&durable,data,size); blob_size=size; exists=true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { if (handle!=1) return -1; ++commits; return fail_commit; }
void nvs_close(nvs_handle_t handle) { if (handle==1) ++closes; }

EXPORT int test_block_store_preserves_owner_across_reload_and_rejects_corruption(void) {
    exists=false; fail_open=fail_set=fail_commit=0; closes=commits=0;
    nm_blocks_t book, reloaded;
    CHECK(nm_block_store_load("000000000000",&book));
    nm_network_t parent={0x0A000000,0xFF000000}, empty={0}, offer;
    CHECK(nm_blocks_offer(&book,"A00000000000",parent,empty,3,&offer));
    CHECK(nm_block_store_save(&book) && commits==1 && closes==1);
    CHECK(nm_block_store_load("000000000000",&reloaded));
    CHECK(!memcmp(&book,&reloaded,sizeof(book)));
    CHECK(!nm_block_store_load("B00000000000",&reloaded));
    durable.version=999;
    CHECK(!nm_block_store_load("000000000000",&reloaded));
    durable=book; blob_size=sizeof(book)-1;
    CHECK(!nm_block_store_load("000000000000",&reloaded));
    blob_size=sizeof(book); durable.leases[0].network.mask=0;
    CHECK(!nm_block_store_load("000000000000",&reloaded));
    return 0;
}

EXPORT int test_block_store_io_failure_never_reports_a_successful_commit(void) {
    exists=false; fail_open=fail_set=fail_commit=0; closes=commits=0;
    nm_blocks_t book;
    nm_blocks_init(&book,"000000000000");
    fail_open=-1;
    CHECK(!nm_block_store_save(&book) && !commits && !closes);
    CHECK(!nm_block_store_load("000000000000",&book));
    fail_open=0; fail_set=-2;
    CHECK(!nm_block_store_save(&book) && !commits && closes==1);
    fail_set=0; fail_commit=-3;
    CHECK(!nm_block_store_save(&book) && commits==1 && closes==2);
    fail_commit=0;
    CHECK(nm_block_store_save(&book) && commits==2 && closes==3);
    return 0;
}
