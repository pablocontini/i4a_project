#include "sdkconfig.h"
#include "capture_diagnostics.h"

#if CONFIG_I4A_CAPTURE_DIAGNOSTICS
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "node.h"
#include "spi.h"
#include "ring_link.h"

/* Never retain routing locks while using UART or calling a driver. Bounded
 * reads report contention instead of blocking the control plane for capture. */
static bool take(mutex_t lock) {
    return lock && xSemaphoreTake((SemaphoreHandle_t)lock, pdMS_TO_TICKS(20)) == pdTRUE;
}

static unsigned output_for(const rt_routing_table_t *table, uint32_t destination) {
    for (size_t i = 0; i < table->count; ++i)
        if ((destination & table->entries[i].mask) == table->entries[i].network)
            return table->entries[i].output;
    return table->default_gateway;
}

static void snapshot(routing_t *rt) {
    rt_routing_table_t table;
    bool table_ok = take(rt->node_state.m_lock);
    if (table_ok) {
        table = rt->node_state.routing_table;
        xSemaphoreGive((SemaphoreHandle_t)rt->node_state.m_lock);
    }
    size_t peers = 0, siblings = 0;
    bool publish = false, queue_ok = take(rt->queue_lock);
    uint32_t tick = 0;
    if (queue_ok) {
        peers = rt->external_queue.count;
        siblings = rt->internal_queue.count;
        publish = rt->pending_publish;
        tick = rt->pending_tick_ms;
        xSemaphoreGive((SemaphoreHandle_t)rt->queue_lock);
    }
    sync_t *sync = rt->deps.sync;
    uint32_t session = 0, sequence = 0, completed = 0;
    unsigned destination = 0, inside = 0, deferred = 0, out = 0;
    bool sync_ok = take(sync->lock);
    if (sync_ok) {
        session = sync->session;
        sequence = sync->sequence[RS_ROUTING];
        completed = sync->completed[RS_ROUTING];
        destination = sync->destination[RS_ROUTING];
        inside = sync->critical_sections[RS_ROUTING].is_inside;
        deferred = sync->critical_sections[RS_ROUTING].deferred;
        out = sync->is_leader && sync->_st.leader.is_token_out[RS_ROUTING];
        xSemaphoreGive((SemaphoreHandle_t)sync->lock);
    }
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t mode_error = esp_wifi_get_mode(&mode);
    wifi_ap_record_t ap = {0};
    esp_err_t station_error = esp_wifi_sta_get_ap_info(&ap);
    wifi_sta_list_t clients = {0};
    esp_err_t ap_error = esp_wifi_ap_get_sta_list(&clients);
    spi_diagnostics_t spi;
    ring_link_diagnostics_t link;
    spi_get_diagnostics(&spi);
    ring_link_get_diagnostics(&link);
    int64_t now = esp_timer_get_time() / 1000;
    flockfile(stdout);
    printf("\nI4ACAP t_ms=%lld board=%u event=routes available=%u", (long long)now, rt->orientation, table_ok);
    if (table_ok) {
        printf(" default=%u count=%u", table.default_gateway, (unsigned)table.count);
        if (table.count <= MAX_ROUTING_ENTRIES) {
            printf(" rpi_out=%u internet_out=%u entries=", output_for(&table, 0x0AFFFFFE), output_for(&table, 0x01010101));
            for (size_t i = 0; i < table.count; ++i)
                printf("%s%08" PRIx32 "/%08" PRIx32 ":%u", i ? "," : "",
                       table.entries[i].network, table.entries[i].mask, table.entries[i].output);
        } else printf(" invalid_count=1");
    }
    printf("\nI4ACAP t_ms=%lld board=%u event=coord available=%u session=%" PRIu32
           " sequence=%" PRIu32 " completed=%" PRIu32 " destination=%u inside=%u deferred=%u token_out=%u"
           " queue_available=%u peer=%u sibling=%u pending_publish=%u pending_tick_ms=%" PRIu32 "\n",
           (long long)now, rt->orientation, sync_ok, session, sequence, completed,
           destination, inside, deferred, out, queue_ok, (unsigned)peers, (unsigned)siblings, publish, tick);
    printf("I4ACAP t_ms=%lld board=%u event=radio mode_error=%d mode=%u sta_error=%d"
           " channel=%u rssi=%d bssid=%02x%02x%02x%02x%02x%02x ap_error=%d clients=%u\n",
           (long long)now, rt->orientation, mode_error, mode, station_error, ap.primary, ap.rssi,
           ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5], ap_error, clients.num);
    printf("I4ACAP t_ms=%lld board=%u event=heap free=%u minimum=%u largest=%u diag_stack_min=%u\n",
           (long long)now, rt->orientation, (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
    printf("I4ACAP t_ms=%lld board=%u event=spi rx=%" PRIu32 " short=%" PRIu32
           " no_buffer=%" PRIu32 " queue_full=%" PRIu32 " tx=%" PRIu32 " tx_fail=%" PRIu32
           " crc_checked=%" PRIu32 " crc_at_copy=%" PRIu32 "\n",
           (long long)now, rt->orientation, spi.rx_complete, spi.rx_short,
           spi.rx_no_buffer, spi.rx_queue_full, spi.tx, spi.tx_fail,
           spi.rx_crc_checked, spi.rx_crc_error);
    printf("I4ACAP t_ms=%lld board=%u event=transport crc=%" PRIu32 " unknown=%" PRIu32
           " local_full=%" PRIu32 " forward_fail=%" PRIu32
           " crc_internal=%" PRIu32 " crc_ip=%" PRIu32 " crc_other=%" PRIu32 "\n",
           (long long)now, rt->orientation, link.crc_error, link.unknown_type,
           link.local_queue_full, link.forward_fail,
           link.crc_internal, link.crc_ip, link.crc_other);
    fflush(stdout);
    funlockfile(stdout);
}

static void identity(routing_t *rt) {
    const esp_app_desc_t *app = esp_app_get_description();
    char hash[65];
    for (unsigned i = 0; i < 32; ++i) snprintf(hash + i * 2, 3, "%02x", app->app_elf_sha256[i]);
    flockfile(stdout);
    printf("\nI4ACAP t_ms=%lld board=%u event=identity uuid=%s version=%s elf_sha256=%s reset=%d period_ms=2000\n",
           (long long)(esp_timer_get_time() / 1000), rt->orientation, node_get_uuid(), app->version, hash, esp_reset_reason());
    fflush(stdout);
    funlockfile(stdout);
}

static void diagnostic_task(void *arg) {
    routing_t *rt = arg;
    for (unsigned sample = 0;; ++sample) {
        /* USB monitors can attach after boot; repeat identity without reset. */
        if (sample % 15 == 0) identity(rt);
        snapshot(rt);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
#endif

esp_err_t capture_diagnostics_start(routing_t *routing) {
#if CONFIG_I4A_CAPTURE_DIAGNOSTICS
    return xTaskCreate(diagnostic_task, "link_capture", 4096, routing, 1, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
#else
    (void)routing;
    return ESP_OK;
#endif
}
