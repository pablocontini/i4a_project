#include "../../../components/internal/ring_link_lowlevel/ring_link_lowlevel.c"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static bool taken, error;
static unsigned transfers;
static unsigned diagnostic_count;
static ip_diag_stage_t last_stage;
static esp_err_t transmit_result = ESP_OK;
void ip_diag_packet(const void *data, size_t length, ip_diag_stage_t stage, int rc) {
    (void)data; (void)length; (void)rc;
    if (taken) error = true;
    ++diagnostic_count; last_stage = stage;
}
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
    (void)p; (void)size; if (!taken) error = true; ++transfers; return transmit_result;
}
uint32_t ring_link_compute_crc32(ring_link_payload_t *p) { (void)p; return 42; }
EXPORT int test_lowlevel_pauses_ip_and_preserves_internal_forwarding(void) {
    QueueHandle_t *queue = NULL;
    transfers = diagnostic_count = 0; transmit_result = ESP_OK;
    taken = error = false; s_maintenance = false;
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

EXPORT int test_lowlevel_diagnostics_distinguish_gate_drop_and_tx_failure(void) {
    QueueHandle_t *queue = NULL;
    transfers = diagnostic_count = 0; transmit_result = ESP_OK;
    taken = error = s_maintenance = false; s_tx_semaphore_handle = NULL;
    CHECK(ring_link_lowlevel_init(&queue) == ESP_OK);
    ring_link_payload_t ip = {.buffer_type = RING_LINK_PAYLOAD_TYPE_ESP_NETIF};
    ring_link_payload_t internal = {.buffer_type = RING_LINK_PAYLOAD_TYPE_INTERNAL};
    CHECK(ring_link_lowlevel_transmit_payload(&ip) == ESP_OK);
    CHECK(transfers == 1 && diagnostic_count == 1 && last_stage == IP_DIAG_SPI_WIRE_TX_OK);
    ring_link_lowlevel_set_maintenance(true);
    CHECK(ring_link_lowlevel_transmit_payload(&ip) == ESP_OK);
    CHECK(transfers == 1 && diagnostic_count == 2 && last_stage == IP_DIAG_SPI_GATE_DROP);
    CHECK(ring_link_lowlevel_transmit_payload(&internal) == ESP_OK);
    CHECK(transfers == 2 && diagnostic_count == 2);
    ring_link_lowlevel_set_maintenance(false);
    transmit_result = ESP_FAIL;
    CHECK(ring_link_lowlevel_transmit_payload(&ip) == ESP_FAIL);
    CHECK(transfers == 3 && diagnostic_count == 3 && last_stage == IP_DIAG_SPI_WIRE_TX_FAIL);
    CHECK(!taken && !error);
    return 0;
}
