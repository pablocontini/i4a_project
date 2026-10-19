#include "../../../components/internal/ring_link_lowlevel/ring_link_lowlevel.c"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static bool taken, error;
static unsigned transfers;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &taken; }
int xSemaphoreTake(SemaphoreHandle_t handle, TickType_t wait) {
    (void)wait;
    if (!handle) { error = true; return 0; }
    if (taken) { error = true; return 0; }
    taken = true; return 1;
}
int xSemaphoreGive(SemaphoreHandle_t handle) { (void)handle; taken = false; return 1; }
esp_err_t mock_init(QueueHandle_t **queue) { (void)queue; return ESP_OK; }
esp_err_t mock_free(void *p) { (void)p; return ESP_OK; }
esp_err_t mock_transmit(void *p, size_t size) {
    (void)p; (void)size; if (!taken) error = true; ++transfers; return ESP_OK;
}
uint32_t ring_link_compute_crc32(ring_link_payload_t *p) { (void)p; return 42; }
EXPORT int test_lowlevel_pauses_ip_and_preserves_internal_forwarding(void) {
    QueueHandle_t *queue = NULL;
    transfers = 0; taken = error = false; s_maintenance = false;
    s_tx_semaphore_handle = NULL;
    ring_link_lowlevel_set_maintenance(true);
    CHECK(s_maintenance && !error);
    CHECK(ring_link_lowlevel_init(&queue) == ESP_OK && s_maintenance);
    ring_link_lowlevel_set_maintenance(false);
    ring_link_payload_t ip = { .buffer_type = 2, .ttl = 5 };
    ring_link_payload_t ota = { .buffer_type = RING_LINK_PAYLOAD_TYPE_INTERNAL, .ttl = 5 };
    CHECK(ring_link_lowlevel_transmit_payload(&ip) == ESP_OK && transfers == 1);
    ring_link_lowlevel_set_maintenance(true);
    CHECK(s_maintenance && !taken);
    CHECK(ring_link_lowlevel_transmit_payload(&ip) == ESP_OK && transfers == 1);
    CHECK(ring_link_lowlevel_forward_payload(&ip) == ESP_OK && transfers == 1);
    CHECK(ring_link_lowlevel_transmit_payload(&ota) == ESP_OK && transfers == 2);
    CHECK(ring_link_lowlevel_forward_payload(&ota) == ESP_OK && transfers == 3);
    ring_link_lowlevel_set_maintenance(false);
    CHECK(ring_link_lowlevel_transmit_payload(&ip) == ESP_OK && transfers == 4);
    CHECK(!taken && !error);
    return 0;
}
