#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_timer.h"

#include "node.h"
#include "ring_share/ring_share.h"

#include "antenna_orientation_mode/antenna_orientation_mode.h"


/*
 * ============================================================================
 * Configuración general
 * ============================================================================
 */

/*
 * Pin de entrada para activar modo orientación.
 *
 * Este pin sólo necesita existir en el ESP central.
 *
 * Cambiar GPIO_NUM_32 por el GPIO real que uses para el DIP switch.
 *
 * Recomendación de hardware:
 *   - DIP abierto  -> pull-up interno -> nivel alto -> modo normal
 *   - DIP cerrado  -> GND             -> nivel bajo -> modo orientación
 */
#define AOM_MODE_GPIO GPIO_NUM_33

/*
 * Nivel activo del DIP.
 *
 * 0: activo en bajo, recomendado con pull-up.
 * 1: activo en alto.
 */
#define AOM_MODE_ACTIVE_LEVEL 0
/*
 * TEST SIN PCB:
 *
 * 0 = usa el pin real
 * 1 = fuerza modo orientación siempre
 *
 * Volver a 0 cuando tengas el PCB nuevo.
 */
#define AOM_FORCE_ORIENTATION_MODE 0

/*
 * Prefijo de redes que se van a reportar.
 *
 * Para nodos reales:
 *   "I4A"
 *
 * Para prueba con celular:
 *   "I4A_TEST"
 */
#define AOM_SSID_PREFIX "I4A"

/*
 * Período de escaneo y refresco de tabla.
 */
#define AOM_SCAN_PERIOD_MS     1000
#define AOM_DISPLAY_PERIOD_MS  5000
#define AOM_START_PERIOD_MS    1000

#define AOM_MAX_SSID_LEN       32
#define AOM_MAX_REPORT_ENTRIES 6

#define AOM_VERSION            1



/*
 * ============================================================================
 * Portal cautivo del modo orientación
 * ============================================================================
 */

#define AOM_AP_SSID          "Orientacion_Antenas"
#define AOM_AP_PASSWORD      "12345678"
#define AOM_AP_CHANNEL       6
#define AOM_AP_MAX_CONN      4

#define AOM_AP_IP_STR        "192.168.50.1"
#define AOM_AP_GW_STR        "192.168.50.1"
#define AOM_AP_NETMASK_STR   "255.255.255.0"

#define AOM_DNS_PORT         53
#define AOM_DNS_TASK_STACK   4096
#define AOM_DNS_TASK_PRIO    (tskIDLE_PRIORITY + 1)

#define AOM_SIMPLE_HTTP_PORT 80


/*
 * ============================================================================
 * Protocolo interno del modo orientación
 * ============================================================================
 */

typedef enum {
    AOM_MSG_START  = 1,
    AOM_MSG_REPORT = 2,
} aom_msg_kind_t;

typedef struct __attribute__((packed)) {
    uint8_t kind;
    uint8_t version;
    uint16_t reserved;
} aom_start_msg_t;

typedef struct __attribute__((packed)) {
    char ssid[AOM_MAX_SSID_LEN + 1];
    int8_t rssi;
    uint8_t channel;
    uint8_t reserved[2];
} aom_report_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t kind;
    uint8_t version;
    uint8_t orientation;
    uint8_t entry_count;
    uint32_t sequence;
    aom_report_entry_t entries[AOM_MAX_REPORT_ENTRIES];
} aom_report_msg_t;

typedef struct {
    char ssid[AOM_MAX_SSID_LEN + 1];
    int8_t rssi;
    uint8_t channel;
} aom_scan_result_t;

typedef struct {
    bool valid;
    uint8_t orientation;
    uint32_t sequence;
    int64_t last_update_ms;
    aom_report_msg_t report;
} aom_report_slot_t;


static ring_share_t *s_rs = NULL;
static volatile bool s_started = false;
static uint32_t s_sequence = 0;

static TaskHandle_t s_dns_task_handle = NULL;
static esp_netif_t *s_aom_ap_netif = NULL;

static aom_report_slot_t s_reports[4] = {0};



/*
 * ============================================================================
 * Prototipos portal cautivo
 * ============================================================================
 */

static bool aom_start_ap(void);
static void aom_dns_task(void *arg);
static void aom_simple_http_task(void *arg);
static void aom_start_captive_portal(void);


static int aom_dns_build_response(
    const uint8_t *query,
    int query_len,
    uint8_t *resp,
    int resp_len
);



/*
 * ============================================================================
 * Helpers
 * ============================================================================
 */

static const char *aom_orientation_to_str(uint8_t orientation)
{
    switch (orientation) {
        case NODE_DEVICE_ORIENTATION_NORTH:
            return "NORTE";

        case NODE_DEVICE_ORIENTATION_SOUTH:
            return "SUR";

        case NODE_DEVICE_ORIENTATION_EAST:
            return "ESTE";

        case NODE_DEVICE_ORIENTATION_WEST:
            return "OESTE";

        case NODE_DEVICE_ORIENTATION_CENTER:
            return "CENTRO";

        default:
            return "UNKNOWN";
    }
}


static int64_t aom_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}


static bool aom_is_center(void)
{
    return node_get_device_orientation() == NODE_DEVICE_ORIENTATION_CENTER;
}


/*
 * ============================================================================
 * Lectura del pin de modo orientación
 * ============================================================================
 */

bool antenna_orientation_mode_pin_is_active(void)
{
#if AOM_FORCE_ORIENTATION_MODE
    printf("AOM: modo orientacion FORZADO por software\n");
    return true;
#endif

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << AOM_MODE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);

    /*
     * Espera para estabilizar el pin después de configurar el pull-up.
     */
    vTaskDelay(pdMS_TO_TICKS(50));

    int low_count = 0;
    int high_count = 0;

    for (int i = 0; i < 10; i++) {
        int level = gpio_get_level(AOM_MODE_GPIO);

        if (level == 0) {
            low_count++;
        } else {
            high_count++;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /*
     * Como el modo orientación es activo en bajo,
     * exigimos varias lecturas en bajo para activarlo.
     */
    bool active = (low_count >= 8);

    printf(
        "AOM: pin modo orientacion GPIO=%d low_count=%d high_count=%d active=%d\n",
        AOM_MODE_GPIO,
        low_count,
        high_count,
        active
    );

    return active;
}


/*
 * ============================================================================
 * Preparación WiFi para scan
 * ============================================================================
 */

static bool aom_prepare_wifi_for_scan(void)
{
    wifi_mode_t mode;
    esp_err_t err;

    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        printf("AOM: esp_wifi_get_mode fallo: %s\n", esp_err_to_name(err));
        return false;
    }

    /*
     * Para escanear necesitamos interfaz STA.
     *
     * WIFI_MODE_NULL  = 0
     * WIFI_MODE_STA   = 1
     * WIFI_MODE_AP    = 2
     * WIFI_MODE_APSTA = 3
     */
    if (mode == WIFI_MODE_NULL) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) {
            printf("AOM: esp_wifi_set_mode(STA) fallo: %s\n", esp_err_to_name(err));
            return false;
        }
    } else if (mode == WIFI_MODE_AP) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            printf("AOM: esp_wifi_set_mode(APSTA) fallo: %s\n", esp_err_to_name(err));
            return false;
        }
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        /*
         * Si ya estaba arrancado, algunos estados pueden devolver error.
         * Para esta herramienta de instalación no lo tratamos como fatal.
         */
        printf("AOM: esp_wifi_start devolvio: %s\n", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    return true;
}


/*
 * ============================================================================
 * Scan WiFi con filtro por prefijo
 * ============================================================================
 */

static size_t aom_scan_prefix(
    const char *prefix,
    aom_scan_result_t *out,
    size_t out_cap
)
{
    if (!prefix || !out || out_cap == 0) {
        return 0;
    }

    if (!aom_prepare_wifi_for_scan()) {
        return 0;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        printf("AOM: esp_wifi_scan_start fallo: %s\n", esp_err_to_name(err));
        return 0;
    }

    uint16_t ap_count = 0;

    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        printf("AOM: esp_wifi_scan_get_ap_num fallo: %s\n", esp_err_to_name(err));
        return 0;
    }

    if (ap_count == 0) {
        return 0;
    }

    wifi_ap_record_t *records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!records) {
        printf("AOM: no hay memoria para resultados de scan\n");
        return 0;
    }

    err = esp_wifi_scan_get_ap_records(&ap_count, records);
    if (err != ESP_OK) {
        printf("AOM: esp_wifi_scan_get_ap_records fallo: %s\n", esp_err_to_name(err));
        free(records);
        return 0;
    }

    size_t n = 0;
    size_t prefix_len = strlen(prefix);

    for (uint16_t i = 0; i < ap_count && n < out_cap; i++) {
        const char *ssid = (const char *)records[i].ssid;

        if (strncmp(ssid, prefix, prefix_len) != 0) {
            continue;
        }

        strncpy(out[n].ssid, ssid, sizeof(out[n].ssid) - 1);
        out[n].ssid[sizeof(out[n].ssid) - 1] = '\0';
        out[n].rssi = records[i].rssi;
        out[n].channel = records[i].primary;

        n++;
    }

    free(records);

    return n;
}


/*
 * ============================================================================
 * Ring callback
 * ============================================================================
 */

static void aom_on_ring_message(void *ctx, const uint8_t *msg, uint16_t len)
{
    (void)ctx;

    if (!msg || len < 2) {
        return;
    }

    uint8_t kind = msg[0];

    if (kind == AOM_MSG_START) {
        if (len < sizeof(aom_start_msg_t)) {
            return;
        }

        const aom_start_msg_t *start = (const aom_start_msg_t *)msg;

        if (start->version != AOM_VERSION) {
            return;
        }

        s_started = true;
        return;
    }

    if (kind == AOM_MSG_REPORT) {
        if (len < sizeof(aom_report_msg_t)) {
            return;
        }

        if (!aom_is_center()) {
            return;
        }

        const aom_report_msg_t *report = (const aom_report_msg_t *)msg;

        if (report->version != AOM_VERSION) {
            return;
        }

        if (report->orientation >= 4) {
            return;
        }

        uint8_t idx = report->orientation;

        s_reports[idx].valid = true;
        s_reports[idx].orientation = report->orientation;
        s_reports[idx].sequence = report->sequence;
        s_reports[idx].last_update_ms = aom_now_ms();
        memcpy(&s_reports[idx].report, report, sizeof(aom_report_msg_t));

        return;
    }
}


/*
 * ============================================================================
 * Envío de mensajes
 * ============================================================================
 */

static void aom_send_start(void)
{
    if (!s_rs) {
        return;
    }

    aom_start_msg_t msg = {
        .kind = AOM_MSG_START,
        .version = AOM_VERSION,
        .reserved = 0,
    };

    rs_broadcast(
        s_rs,
        RS_ANTENNA_ORIENTATION_MODE,
        &msg,
        sizeof(msg)
    );
}


static void aom_send_report(const aom_scan_result_t *scan, size_t n)
{
    if (!s_rs) {
        return;
    }

    if (n > AOM_MAX_REPORT_ENTRIES) {
        n = AOM_MAX_REPORT_ENTRIES;
    }

    aom_report_msg_t report = {
        .kind = AOM_MSG_REPORT,
        .version = AOM_VERSION,
        .orientation = node_get_device_orientation(),
        .entry_count = (uint8_t)n,
        .sequence = s_sequence++,
    };

    for (size_t i = 0; i < n; i++) {
        strncpy(
            report.entries[i].ssid,
            scan[i].ssid,
            sizeof(report.entries[i].ssid) - 1
        );

        report.entries[i].ssid[sizeof(report.entries[i].ssid) - 1] = '\0';
        report.entries[i].rssi = scan[i].rssi;
        report.entries[i].channel = scan[i].channel;
    }

    rs_broadcast(
        s_rs,
        RS_ANTENNA_ORIENTATION_MODE,
        &report,
        sizeof(report)
    );
}


/*
 * ============================================================================
 * Tabla para el central
 * ============================================================================
 */

static void aom_clear_screen(void)
{
    printf("\033[2J\033[H");
}

static void aom_print_table(void)
{
    int64_t now = aom_now_ms();

    aom_clear_screen();

    printf("===============================================================\n");
    printf(" MODO ORIENTACION DE ANTENAS - ESP CENTRAL\n");
    printf(" Prefijo SSID: %s\n", AOM_SSID_PREFIX);
    printf("===============================================================\n");
    printf("+----------+-------------------------------+-------+-----+------+\n");
    printf("| Antena   | SSID                          | RSSI  | CH  | Hace |\n");
    printf("+----------+-------------------------------+-------+-----+------+\n");

    for (uint8_t o = 0; o < 4; o++) {
        if (!s_reports[o].valid) {
            printf(
                "| %-8s | %-29s | %-5s | %-3s | %-4s |\n",
                aom_orientation_to_str(o),
                "(sin reporte)",
                "-",
                "-",
                "-"
            );
            continue;
        }

        const aom_report_msg_t *r = &s_reports[o].report;
        int age_s = (int)((now - s_reports[o].last_update_ms) / 1000);

        if (r->entry_count == 0) {
            printf(
                "| %-8s | %-29s | %-5s | %-3s | %3ds |\n",
                aom_orientation_to_str(o),
                "(sin redes)",
                "-",
                "-",
                age_s
            );
            continue;
        }

        for (uint8_t i = 0; i < r->entry_count && i < AOM_MAX_REPORT_ENTRIES; i++) {
            printf(
                "| %-8s | %-29s | %4d  | %3u | %3ds |\n",
                i == 0 ? aom_orientation_to_str(o) : "",
                r->entries[i].ssid,
                r->entries[i].rssi,
                r->entries[i].channel,
                age_s
            );
        }
    }

    printf("+----------+-------------------------------+-------+-----+------+\n");
    printf("\n");
    printf("Para salir del modo orientacion: apagar DIP/pin y reiniciar el nodo.\n");
    printf("En modo orientacion no se intenta conectar al root ni se inicia routing normal.\n");
}


/*
 * ============================================================================
 * Loops principales
 * ============================================================================
 */

static void aom_center_loop(void)
{
    printf("AOM: ESP central en modo orientacion\n");

    aom_start_captive_portal();

    while (true) {
        aom_send_start();
        aom_print_table();

        vTaskDelay(pdMS_TO_TICKS(AOM_DISPLAY_PERIOD_MS));
    }
}


static void aom_sibling_loop(void)
{
    printf(
        "AOM: ESP no central en modo orientacion. orientation=%d\n",
        node_get_device_orientation()
    );

    aom_scan_result_t scan[AOM_MAX_REPORT_ENTRIES];

    while (true) {
        if (!s_started) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t n = aom_scan_prefix(
            AOM_SSID_PREFIX,
            scan,
            AOM_MAX_REPORT_ENTRIES
        );

        aom_send_report(scan, n);

        vTaskDelay(pdMS_TO_TICKS(AOM_SCAN_PERIOD_MS));
    }
}


/*
 * ============================================================================
 * API pública
 * ============================================================================
 */

void antenna_orientation_mode_run(ring_share_t *rs)
{
    if (!rs) {
        printf("AOM: ERROR rs=NULL\n");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    s_rs = rs;

    rs_register_component(
        s_rs,
        RS_ANTENNA_ORIENTATION_MODE,
        (ring_callback_t){
            .callback = aom_on_ring_message,
            .context = NULL,
        }
    );

    printf("AOM: callback registrado en RS_ANTENNA_ORIENTATION_MODE=%d\n",
           RS_ANTENNA_ORIENTATION_MODE);

    if (aom_is_center()) {
        aom_center_loop();
    } else {
        aom_sibling_loop();
    }
}


/*
 * ============================================================================
 * Iniciar el AP del central
 * ============================================================================
 */
static bool aom_start_ap(void)
{
    esp_err_t err;

    printf("AOM: iniciando AP de portal cautivo\n");

    /*
     * IMPORTANTE:
     * node_setup() ya llamó a device_wifi_init().
     * Por eso primero intentamos reutilizar el netif AP default existente.
     * Si no existe, recién ahí lo creamos.
     */
    if (s_aom_ap_netif == NULL) {
        s_aom_ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

        if (s_aom_ap_netif) {
            printf("AOM: reutilizando netif AP existente WIFI_AP_DEF\n");
        } else {
            printf("AOM: creando netif AP default\n");
            s_aom_ap_netif = esp_netif_create_default_wifi_ap();
        }
    }

    if (!s_aom_ap_netif) {
        printf("AOM: ERROR no se pudo obtener/crear esp_netif AP\n");
        return false;
    }

    /*
     * Detenemos WiFi antes de reconfigurar modo/AP.
     * Si ya estaba detenido, no es fatal.
     */
    err = esp_wifi_stop();
    printf("AOM: esp_wifi_stop=%s\n", esp_err_to_name(err));

    /*
     * Configuración IP fija del AP.
     */
    err = esp_netif_dhcps_stop(s_aom_ap_netif);
    printf("AOM: esp_netif_dhcps_stop=%s\n", esp_err_to_name(err));

    esp_netif_ip_info_t ip_info = {0};

    ip_info.ip.addr = esp_ip4addr_aton(AOM_AP_IP_STR);
    ip_info.gw.addr = esp_ip4addr_aton(AOM_AP_GW_STR);
    ip_info.netmask.addr = esp_ip4addr_aton(AOM_AP_NETMASK_STR);

    err = esp_netif_set_ip_info(s_aom_ap_netif, &ip_info);
    printf("AOM: esp_netif_set_ip_info=%s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        return false;
    }

    err = esp_netif_dhcps_start(s_aom_ap_netif);
    printf("AOM: esp_netif_dhcps_start=%s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        return false;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    printf("AOM: esp_wifi_set_mode(AP)=%s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        return false;
    }

    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.ap.ssid,
        AOM_AP_SSID,
        sizeof(wifi_config.ap.ssid) - 1
    );

    wifi_config.ap.ssid_len = strlen(AOM_AP_SSID);
    wifi_config.ap.channel = AOM_AP_CHANNEL;
    wifi_config.ap.max_connection = AOM_AP_MAX_CONN;

    if (strlen(AOM_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strncpy(
            (char *)wifi_config.ap.password,
            AOM_AP_PASSWORD,
            sizeof(wifi_config.ap.password) - 1
        );

        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    printf("AOM: esp_wifi_set_config(AP)=%s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        return false;
    }

    err = esp_wifi_start();
    printf("AOM: esp_wifi_start=%s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        return false;
    }

    printf("AOM: AP iniciado SSID=%s IP=%s\n", AOM_AP_SSID, AOM_AP_IP_STR);

    return true;
}



/*
 * ============================================================================
 * Generar JSON con los reportes
 * ============================================================================
 */
static void aom_build_reports_json(char *buf, size_t buf_len)
{
    int64_t now = aom_now_ms();

    size_t used = 0;

    used += snprintf(
        buf + used,
        buf_len - used,
        "{"
        "\"prefix\":\"%s\","
        "\"reports\":[",
        AOM_SSID_PREFIX
    );

    bool first_report = true;

    for (uint8_t o = 0; o < 4; o++) {
        if (!s_reports[o].valid) {
            continue;
        }

        const aom_report_msg_t *r = &s_reports[o].report;
        int age_s = (int)((now - s_reports[o].last_update_ms) / 1000);

        if (!first_report) {
            used += snprintf(buf + used, buf_len - used, ",");
        }

        first_report = false;

        used += snprintf(
            buf + used,
            buf_len - used,
            "{"
            "\"orientation\":\"%s\","
            "\"age_s\":%d,"
            "\"entries\":[",
            aom_orientation_to_str(o),
            age_s
        );

        for (uint8_t i = 0; i < r->entry_count && i < AOM_MAX_REPORT_ENTRIES; i++) {
            if (i > 0) {
                used += snprintf(buf + used, buf_len - used, ",");
            }

            used += snprintf(
                buf + used,
                buf_len - used,
                "{"
                "\"ssid\":\"%s\","
                "\"rssi\":%d,"
                "\"channel\":%u"
                "}",
                r->entries[i].ssid,
                r->entries[i].rssi,
                r->entries[i].channel
            );
        }

        used += snprintf(buf + used, buf_len - used, "]}");
    }

    snprintf(buf + used, buf_len - used, "]}");
}


/*
 * ============================================================================
 * DNS server básico
 * ============================================================================
 */
static int aom_dns_build_response(
    const uint8_t *query,
    int query_len,
    uint8_t *resp,
    int resp_len
)
{
    if (query_len < 12 || resp_len < query_len + 16) {
        return 0;
    }

    memcpy(resp, query, query_len);

    /*
     * Flags DNS response:
     * QR=1, Opcode=0, AA=1, TC=0, RD copied, RA=0, RCODE=0.
     */
    resp[2] = 0x81;
    resp[3] = 0x80;

    /*
     * ANCOUNT = 1.
     */
    resp[6] = 0x00;
    resp[7] = 0x01;

    int pos = query_len;

    /*
     * Answer name pointer to query name at offset 12.
     */
    resp[pos++] = 0xC0;
    resp[pos++] = 0x0C;

    /*
     * TYPE A.
     */
    resp[pos++] = 0x00;
    resp[pos++] = 0x01;

    /*
     * CLASS IN.
     */
    resp[pos++] = 0x00;
    resp[pos++] = 0x01;

    /*
     * TTL = 1 second.
     */
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;
    resp[pos++] = 0x01;

    /*
     * RDLENGTH = 4.
     */
    resp[pos++] = 0x00;
    resp[pos++] = 0x04;

    /*
     * RDATA = 192.168.50.1.
     */
    resp[pos++] = 192;
    resp[pos++] = 168;
    resp[pos++] = 50;
    resp[pos++] = 1;

    return pos;
}

static void aom_dns_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        printf("AOM: no se pudo crear socket DNS\n");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(AOM_DNS_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("AOM: bind DNS fallo\n");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    printf("AOM: DNS captive portal iniciado en puerto %d\n", AOM_DNS_PORT);

    uint8_t rx[512];
    uint8_t tx[512];

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int len = recvfrom(
            sock,
            rx,
            sizeof(rx),
            0,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (len <= 0) {
            continue;
        }

        int resp_len = aom_dns_build_response(rx, len, tx, sizeof(tx));

        if (resp_len > 0) {
            sendto(
                sock,
                tx,
                resp_len,
                0,
                (struct sockaddr *)&client_addr,
                client_len
            );
        }
    }
}


/*
 * ============================================================================
 * Iniciar portal cautivo completo
 * ============================================================================
 */
static void aom_start_captive_portal(void)
{
    printf("AOM: aom_start_captive_portal inicio\n");

    if (!aom_start_ap()) {
        printf("AOM: ERROR iniciando AP del portal\n");
        return;
    }

    printf("AOM: creando servidor HTTP simple\n");

    BaseType_t http_ret = xTaskCreatePinnedToCore(
        aom_simple_http_task,
        "aom_simple_http",
        12288,
        NULL,
        tskIDLE_PRIORITY + 2,
        NULL,
        1
    );

    if (http_ret != pdPASS) {
        printf("AOM: ERROR creando aom_simple_http_task\n");
    } else {
        printf("AOM: aom_simple_http_task creada OK\n");
    }

    if (!s_dns_task_handle) {
        printf("AOM: creando tarea DNS\n");

        BaseType_t ret = xTaskCreatePinnedToCore(
            aom_dns_task,
            "aom_dns",
            AOM_DNS_TASK_STACK,
            NULL,
            AOM_DNS_TASK_PRIO,
            &s_dns_task_handle,
            1
        );

        if (ret != pdPASS) {
            printf("AOM: ERROR creando tarea DNS\n");
        } else {
            printf("AOM: tarea DNS creada OK\n");
        }
    }
}

static void aom_simple_http_task(void *arg)
{
    (void)arg;

    printf("AOM_SIMPLE_HTTP: tarea iniciada\n");

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        printf("AOM_SIMPLE_HTTP: error creando socket\n");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(AOM_SIMPLE_HTTP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("AOM_SIMPLE_HTTP: error en bind puerto %d\n", AOM_SIMPLE_HTTP_PORT);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 4) < 0) {
        printf("AOM_SIMPLE_HTTP: error en listen\n");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    printf("AOM_SIMPLE_HTTP: escuchando en http://%s:%d/\n", AOM_AP_IP_STR, AOM_SIMPLE_HTTP_PORT);

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(
            listen_sock,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (client_sock < 0) {
            printf("AOM_SIMPLE_HTTP: accept fallo\n");
            continue;
        }

        char rxbuf[512];
        int len = recv(client_sock, rxbuf, sizeof(rxbuf) - 1, 0);

        if (len <= 0) {
            shutdown(client_sock, SHUT_RDWR);
            close(client_sock);
            continue;
        }

        rxbuf[len] = '\0';

        char *line_end = strstr(rxbuf, "\r\n");
        if (line_end) {
            *line_end = '\0';
        }
        printf("AOM_SIMPLE_HTTP: %s\n", rxbuf);

        bool wants_json = false;
        bool wants_html = false;
        bool wants_404 = false;
        bool wants_redirect = false;

        /* Ruteo estricto */
        if (strncmp(rxbuf, "GET /api/reports", 16) == 0) {
            wants_json = true;
        } else if (strncmp(rxbuf, "GET / ", 6) == 0 || strncmp(rxbuf, "GET /?", 6) == 0) {
            // SOLO entregamos HTML si piden explícitamente la raíz
            wants_html = true;
        } else if (strstr(rxbuf, "favicon.ico") != NULL) {
            // Evitamos que peticiones de iconos generen bucles de redirección
            wants_404 = true;
        } else {
            // CUALQUIER OTRA COSA (/generate_204, dominios externos, etc) -> Redirigir
            wants_redirect = true;
        }

        if (wants_redirect) {
            const char *body =
                "<!doctype html>"
                "<html><head><meta charset='utf-8'>"
                "<meta http-equiv='refresh' content='0; url=http://" AOM_AP_IP_STR "/'>"
                "<title>Redireccionando</title></head>"
                "<body><p>Redireccionando al portal de orientacion...</p>"
                "<p><a href='http://" AOM_AP_IP_STR "/'>Abrir portal</a></p>"
                "</body></html>";

            char header[512];
            
            // Usamos 302 Found que es el estándar más seguro para portales cautivos
            int header_len = snprintf(
                header,
                sizeof(header),
                "HTTP/1.1 302 Found\x0D\x0A"
                "Location: http://" AOM_AP_IP_STR "/\x0D\x0A"
                "Content-Type: text/html; charset=utf-8\x0D\x0A"
                "Content-Length: %u\x0D\x0A"
                "Connection: close\x0D\x0A"
                "\x0D\x0A",
                (unsigned)strlen(body)
            );

            send(client_sock, header, header_len, 0);
            send(client_sock, body, strlen(body), 0);

        } else if (wants_json) {
            char json[4096];
            aom_build_reports_json(json, sizeof(json));

            char header[256];
            int header_len = snprintf(
                header,
                sizeof(header),
                "HTTP/1.1 200 OK\x0D\x0A"
                "Content-Type: application/json\x0D\x0A"
                "Cache-Control: no-store\x0D\x0A"
                "Access-Control-Allow-Origin: *\x0D\x0A"
                "Content-Length: %u\x0D\x0A"
                "Connection: close\x0D\x0A"
                "\x0D\x0A",
                (unsigned)strlen(json)
            );

            send(client_sock, header, header_len, 0);
            send(client_sock, json, strlen(json), 0);

        } else if (wants_404) {
            const char *body = "Not Found";
            char header[256];
            int header_len = snprintf(
                header, sizeof(header),
                "HTTP/1.1 404 Not Found\x0D\x0A"
                "Content-Length: %u\x0D\x0A"
                "Connection: close\x0D\x0A"
                "\x0D\x0A",
                (unsigned)strlen(body)
            );
            send(client_sock, header, header_len, 0);
            send(client_sock, body, strlen(body), 0);

        } else if (wants_html) {
            const char *body =
                "<!doctype html>"
                "<html>"
                "<head>"
                "<meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>I4A Orientacion</title>"
                "<style>"
                "body{font-family:Arial,sans-serif;margin:16px;background:#111;color:#eee;}"
                "h2{font-size:22px;margin:0 0 8px 0;}"
                "p{color:#bbb;margin:4px 0 12px 0;}"
                "table{border-collapse:collapse;width:100%;font-size:14px;}"
                "th,td{border:1px solid #444;padding:6px;text-align:left;}"
                "th{background:#222;}"
                "tr:nth-child(even){background:#181818;}"
                ".good{color:#52d273;font-weight:bold;}"
                ".mid{color:#ffd166;font-weight:bold;}"
                ".bad{color:#ff6b6b;font-weight:bold;}"
                ".small{font-size:12px;color:#aaa;}"
                "</style>"
                "</head>"
                "<body>"
                "<h2>Modo orientacion</h2>"
                "<p class='small'>Actualizacion cada 5 segundos.</p>"
                "<table>"
                "<thead>"
                "<tr>"
                "<th>Antena</th>"
                "<th>SSID</th>"
                "<th>RSSI</th>"
                "<th>Canal</th>"
                "<th>Hace</th>"
                "</tr>"
                "</thead>"
                "<tbody id='tb'>"
                "<tr><td colspan='5'>Cargando...</td></tr>"
                "</tbody>"
                "</table>"
                "<script>"
                "function cls(r){if(r>=-70)return'good';if(r>=-82)return'mid';return'bad';}"
                "function esc(s){return String(s).replace(/[&<>]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c];});}"
                "function row(a,s,r,c,h,k){return '<tr><td>'+a+'</td><td>'+s+'</td><td class=\"'+k+'\">'+r+'</td><td>'+c+'</td><td>'+h+'</td></tr>';}"
                "function upd(){"
                "var x=new XMLHttpRequest();"
                "x.open('GET','/api/reports?t='+Date.now(),true);"
                "x.onreadystatechange=function(){"
                "if(x.readyState!==4)return;"
                "var tb=document.getElementById('tb');"
                "if(x.status!==200){tb.innerHTML='<tr><td colspan=\"5\">Error HTTP '+x.status+'</td></tr>';return;}"
                "var j;"
                "try{j=JSON.parse(x.responseText);}catch(e){tb.innerHTML='<tr><td colspan=\"5\">Error JSON</td></tr>';return;}"
                "var html='';"
                "if(!j.reports||j.reports.length===0){tb.innerHTML='<tr><td colspan=\"5\">Sin reportes</td></tr>';return;}"
                "for(var r=0;r<j.reports.length;r++){"
                "var rep=j.reports[r];"
                "if(!rep.entries||rep.entries.length===0){html+=row(esc(rep.orientation),'(sin redes)','-','-',rep.age_s+'s','');}"
                "else{"
                "for(var i=0;i<rep.entries.length;i++){"
                "var e=rep.entries[i];"
                "html+=row(i===0?esc(rep.orientation):'',esc(e.ssid),e.rssi+' dBm',e.channel,rep.age_s+'s',cls(e.rssi));"
                "}"
                "}"
                "}"
                "tb.innerHTML=html;"
                "};"
                "x.onerror=function(){document.getElementById('tb').innerHTML='<tr><td colspan=\"5\">Error de conexion</td></tr>';};"
                "x.send();"
                "}"
                "upd();"
                "setInterval(upd,5000);"
                "</script>"
                "</body>"
                "</html>";

            char header[256];
            int header_len = snprintf(
                header,
                sizeof(header),
                "HTTP/1.1 200 OK\x0D\x0A"
                "Content-Type: text/html; charset=utf-8\x0D\x0A"
                "Cache-Control: no-store\x0D\x0A"
                "Content-Length: %u\x0D\x0A"
                "Connection: close\x0D\x0A"
                "\x0D\x0A",
                (unsigned)strlen(body)
            );

            send(client_sock, header, header_len, 0);
            send(client_sock, body, strlen(body), 0);
        }

        /* 
         * RETARDO CRÍTICO: Da tiempo al ESP32 a vaciar el buffer de transmisión (TX) 
         * por el aire antes de destruir el socket. Si se omite, envía un TCP RST.
         */
        vTaskDelay(pdMS_TO_TICKS(50));

        shutdown(client_sock, SHUT_RDWR);
        close(client_sock);
    }
}