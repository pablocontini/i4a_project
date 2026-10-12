#include "link_diagnostics.h"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "config.h"
#include "os/os.h"
#include <string.h>
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t counts[LD_COUNT], wait_count, wait_max, rx_high;
static uint32_t free_low=UINT32_MAX;
static uint64_t wait_total;
static int64_t next;
void ld_count(ld_event_t event) {
 if ((unsigned)event>=LD_COUNT) return;
 portENTER_CRITICAL(&lock); counts[event]++; portEXIT_CRITICAL(&lock);
}
void ld_pressure(uint32_t us,uint32_t free_count,uint32_t rx_count) {
 portENTER_CRITICAL(&lock);
 if(us>=100){wait_count++;wait_total+=us;}
 if(us>wait_max)wait_max=us;
 if(free_count<free_low)free_low=free_count;
 if(rx_count>rx_high)rx_high=rx_count;
 portEXIT_CRITICAL(&lock);
}
void ld_tick(void) {
 int64_t now=esp_timer_get_time(); if(now<next)return;next=now+5000000;
 uint32_t c[LD_COUNT],wc,wm,fl,rh;uint64_t wt;
 portENTER_CRITICAL(&lock);
 memcpy(c,counts,sizeof(c));wc=wait_count;wm=wait_max;wt=wait_total;
 fl=free_low;rh=rx_high;free_low=UINT32_MAX;rx_high=0;
 portEXIT_CRITICAL(&lock);
 os_diag("link","event=spi_counts board_id=%u rx=%lu short=%lu rx_full=%lu free_fail=%lu tx=%lu tx_fail=%lu",
 config_get_id(),(unsigned long)c[LD_RX],(unsigned long)c[LD_SHORT],
 (unsigned long)c[LD_RX_FULL],(unsigned long)c[LD_FREE_FAIL],
 (unsigned long)c[LD_TX],(unsigned long)c[LD_TX_FAIL]);
 os_diag("link","event=drop_counts board_id=%u crc=%lu crc_ip=%lu unknown=%lu dispatch_full=%lu",
 config_get_id(),(unsigned long)c[LD_CRC],(unsigned long)c[LD_CRC_IP],
 (unsigned long)c[LD_UNKNOWN],(unsigned long)c[LD_DISPATCH_FULL]);
 os_diag("link","event=rx_integrity board_id=%u crc_at_dma=%lu changed_after_dma=%lu",
 config_get_id(),(unsigned long)c[LD_RX_CRC_EARLY],(unsigned long)c[LD_RX_CHANGED]);
 os_diag("link","event=buffer_pressure board_id=%u wait_ge100us=%lu wait_total_us=%llu wait_max_us=%lu free_min=%lu rx_high=%lu",
 config_get_id(),(unsigned long)wc,(unsigned long long)wt,(unsigned long)wm,
 (unsigned long)fl,(unsigned long)rh);
}
