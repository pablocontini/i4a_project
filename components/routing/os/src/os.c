#include "os/os.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdarg.h>
#include <stdio.h>
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_random.h"

typedef struct { int64_t time_ms; char tag[16]; char text[176]; } diagnostic_t;
static QueueHandle_t diagnostics;
static volatile unsigned diagnostic_drops;
static void diagnostic_task(void *arg) {
    (void)arg;
    diagnostic_t entry;
    unsigned reported=0;
    for (;;) {
        if (xQueueReceive(diagnostics,&entry,pdMS_TO_TICKS(1000))) {
            flockfile(stdout);
            printf("\nI4AROUTE t_ms=%lld tag=%s %s\n",(long long)entry.time_ms,entry.tag,entry.text);
            fflush(stdout); funlockfile(stdout);
        }
        if (reported!=diagnostic_drops) {
            reported=diagnostic_drops;
            flockfile(stdout); printf("\nI4AROUTE tag=diagnostics event=dropped count=%u\n",reported);
            fflush(stdout); funlockfile(stdout);
        }
    }
}
bool os_diagnostics_start(void) {
    if (diagnostics) return true;
    diagnostics=xQueueCreate(32,sizeof(diagnostic_t));
    if (!diagnostics) return false;
    if (xTaskCreate(diagnostic_task,"routing_diag",3072,NULL,1,NULL)!=pdPASS) {
        vQueueDelete(diagnostics); diagnostics=NULL; return false;
    }
    return true;
}
void os_diag(const char *tag,const char *fmt,...) {
    if (!diagnostics) return;
    diagnostic_t entry={.time_ms=esp_timer_get_time()/1000};
    snprintf(entry.tag,sizeof(entry.tag),"%s",tag);
    va_list args; va_start(args,fmt); vsnprintf(entry.text,sizeof(entry.text),fmt,args); va_end(args);
    if (!xQueueSend(diagnostics,&entry,0)) ++diagnostic_drops;
}
uint64_t os_monotonic_ms(void) { return (uint64_t)esp_timer_get_time()/1000; }
uint32_t os_random_u32(void) { return esp_random(); }

#define LOG_MESSAGE_BUFFER_SIZE 512

bool mutex_create(mutex_t *m)
{
    if (!m) return false;
    *m = (mutex_t)xSemaphoreCreateMutex();
    return (*m != NULL);
}

bool mutex_lock(mutex_t *m)
{
    if (!m || *m == NULL) return false;
    return (xSemaphoreTake((SemaphoreHandle_t)(*m), portMAX_DELAY) == pdTRUE);
}

bool mutex_unlock(mutex_t *m)
{
    if (!m || *m == NULL) return false;
    return (xSemaphoreGive((SemaphoreHandle_t)(*m)) == pdTRUE);
}

bool mutex_try_lock(mutex_t *m)
{
    if (!m || *m == NULL) return false;
    return xSemaphoreTake((SemaphoreHandle_t)*m, 0) == pdTRUE;
}

void mutex_destroy(mutex_t *m)
{
    if (!m || *m == NULL) return;
    vSemaphoreDelete((SemaphoreHandle_t)(*m));
    *m = NULL;
}

void os_panic(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buffer[LOG_MESSAGE_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    ESP_LOGE("PANIC", "%s", buffer);
    va_end(args);

}

void os_log(os_log_level_t level, const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buffer[LOG_MESSAGE_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    switch (level) {
        case OS_LOG_LEVEL_INFO:
            ESP_LOGI(tag, "%s", buffer);
            break;
        case OS_LOG_LEVEL_WARNING:
            ESP_LOGW(tag, "%s", buffer);
            break;
        case OS_LOG_LEVEL_ERROR:
        case OS_LOG_LEVEL_CRITICAL:
            ESP_LOGE(tag, "%s", buffer);
            break;
        default:
            ESP_LOGI(tag, "%s", buffer);
            break;
    }

    va_end(args);
}

void os_delay_ms(uint32_t milliseconds){
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}
