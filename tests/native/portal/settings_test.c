/* Exercise production setting validation, persistence and failed commits.
 * Password derivation is stubbed: these cases do not evaluate cryptography. */
#include <stdio.h>
#include <string.h>
#ifdef __TINYC__
/* TinyCC's Windows CRT headers predate this POSIX function. */
static size_t native_strnlen(const char *text, size_t capacity) {
    size_t length = 0;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}
#define strnlen native_strnlen
#endif
#include "../../../components/integration/portal_settings/src/portal_settings.c"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
typedef struct { const char *key; unsigned char value[64]; size_t length; } slot_t;
static slot_t committed[5], staged[5];
static bool fail_commit;
static unsigned commits, writes;

static int find_slot(slot_t *slots, const char *key) {
    for (int i = 0; i < 5; ++i) {
        if (slots[i].key != NULL && strcmp(slots[i].key, key) == 0) return i;
    }
    return -1;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    (void)name; (void)mode; *handle = 1;
    memcpy(staged, committed, sizeof(staged)); return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *output, size_t *length) {
    (void)handle;
    int i = find_slot(staged, key);
    if (i < 0) return ESP_ERR_NVS_NOT_FOUND;
    if (output != NULL) {
        if (*length < staged[i].length) return ESP_FAIL;
        memcpy(output, staged[i].value, staged[i].length);
    }
    *length = staged[i].length;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
    (void)handle;
    int i = find_slot(staged, key);
    if (i < 0) for (int j = 0; j < 5; ++j) if (staged[j].key == NULL) { i = j; break; }
    if (i < 0 || length > sizeof(staged[i].value)) return ESP_FAIL;
    staged[i].key = key;
    staged[i].length = length;
    memcpy(staged[i].value, value, length);
    ++writes; return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value) {
    size_t length = 1; return nvs_get_blob(handle, key, value, &length);
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value) {
    return nvs_set_blob(handle, key, &value, 1);
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle; ++commits;
    if (fail_commit) return ESP_FAIL;
    memcpy(committed, staged, sizeof(committed)); return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
void esp_fill_random(void *buffer, size_t length) { memset(buffer, 0xa7, length); }
int mbedtls_pkcs5_pbkdf2_hmac_ext(
    int md, const unsigned char *password, size_t password_length,
    const unsigned char *salt, size_t salt_length, unsigned iterations,
    unsigned output_length, unsigned char *output) {
    (void)md; (void)password; (void)password_length; (void)salt;
    (void)salt_length; (void)iterations;
    memset(output, 0x11, output_length); return 0;
}
static void reboot_settings(void) {
    initialized = false;
    memset(&antenna_powers, 0, sizeof(antenna_powers));
    memset(&antenna_rssi_thresholds, 0, sizeof(antenna_rssi_thresholds));
}
static void reset_settings(void) {
    memset(committed, 0, sizeof(committed));
    memset(staged, 0, sizeof(staged));
    commits = writes = 0;
    fail_commit = false;
    reboot_settings();
}
static int thresholds_and_powers_survive_reload_for_all_orientations(void) {
    reset_settings();
    CHECK(portal_settings_init() == ESP_OK);
    portal_antenna_rssi_config_t defaults = portal_settings_get_antenna_rssi_thresholds();
    for (unsigned i = 0; i < 4; ++i) CHECK(defaults.dbm[i] == -128);
    const portal_antenna_rssi_config_t configured = {{-128, -77, -66, -1}};
    const portal_antenna_power_config_t powers = {{8, 20, 60, 80}};
    CHECK(portal_settings_set_antenna_rssi_thresholds(&configured) == ESP_OK);
    CHECK(portal_settings_set_antenna_powers(&powers) == ESP_OK);
    unsigned completed_commits = commits;
    reboot_settings();
    CHECK(portal_settings_init() == ESP_OK);
    CHECK(commits == completed_commits);
    portal_antenna_rssi_config_t loaded = portal_settings_get_antenna_rssi_thresholds();
    portal_antenna_power_config_t loaded_powers = portal_settings_get_antenna_powers();
    CHECK(memcmp(&loaded, &configured, sizeof(loaded)) == 0);
    CHECK(memcmp(&loaded_powers, &powers, sizeof(loaded_powers)) == 0);
    CHECK(portal_settings_set_antenna_rssi_thresholds(&configured) == ESP_OK);
    CHECK(commits == completed_commits);
    return 0;
}
static int invalid_thresholds_and_failed_commit_keep_previous_settings(void) {
    reset_settings();
    CHECK(portal_settings_init() == ESP_OK);
    const portal_antenna_rssi_config_t configured = {{-88, -77, -66, -55}};
    CHECK(portal_settings_set_antenna_rssi_thresholds(&configured) == ESP_OK);
    unsigned committed_before = commits, written_before = writes;
    const portal_antenna_rssi_config_t invalid = {{-88, 0, -66, -55}};
    CHECK(portal_settings_set_antenna_rssi_thresholds(&invalid) == ESP_ERR_INVALID_ARG);
    CHECK(portal_settings_set_antenna_rssi_thresholds(NULL) == ESP_ERR_INVALID_ARG);
    CHECK(commits == committed_before && writes == written_before);
    CHECK(!portal_settings_is_valid_rssi_threshold(-129));
    CHECK(!portal_settings_is_valid_rssi_threshold(0));
    CHECK(portal_settings_is_valid_rssi_threshold(-128));
    CHECK(portal_settings_is_valid_rssi_threshold(-1));
    const portal_antenna_rssi_config_t changed = {{-80, -70, -60, -50}};
    fail_commit = true;
    CHECK(portal_settings_set_antenna_rssi_thresholds(&changed) == ESP_FAIL);
    portal_antenna_rssi_config_t loaded = portal_settings_get_antenna_rssi_thresholds();
    CHECK(memcmp(&loaded, &configured, sizeof(loaded)) == 0);
    fail_commit = false;
    reboot_settings();
    CHECK(portal_settings_init() == ESP_OK);
    loaded = portal_settings_get_antenna_rssi_thresholds();
    CHECK(memcmp(&loaded, &configured, sizeof(loaded)) == 0);
    return 0;
}
int main(void) {
    int (*cases[])(void) = {
        thresholds_and_powers_survive_reload_for_all_orientations,
        invalid_thresholds_and_failed_commit_keep_previous_settings
    };
    const char *names[] = {
        "thresholds_and_powers_survive_reload_for_all_orientations",
        "invalid_thresholds_and_failed_commit_keep_previous_settings"
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int line = cases[i]();
        if (line) { printf("%s failed at settings_test.c:%d\n", names[i], line); return 1; }
        printf("%s: OK\n", names[i]);
    }
    return 0;
}
