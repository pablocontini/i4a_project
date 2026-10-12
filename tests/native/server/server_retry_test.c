#include <stdio.h>
#include <string.h>
#include "esp_netif.h"
#include "freertos/task.h"
#include "peer_session.h"
#include "os/os.h"
#include "lwip/sockets.h"
static unsigned binds,listens,closes,waits,ready_logs;
static void (*task_fn)(void *);
void server_close(void);
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {(void)key;return (void *)1;}
int esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *i) {(void)n;memset(i,0,sizeof(*i));return 0;}
int socket(int a,int b,int c) {(void)a;(void)b;(void)c;return 10;}
int setsockopt(int a,int b,int c,const void *d,unsigned e) {(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
int bind(int a,const struct sockaddr *b,unsigned c) {(void)a;(void)b;(void)c;return ++binds==1 ? -1:0;}
int listen(int a,int b) {(void)a;(void)b;++listens;return 0;}
int lwip_select(int a,fd_set *b,void *c,void *d,struct timeval *e) {(void)a;(void)b;(void)c;(void)d;(void)e;server_close();return 0;}
int accept(int a,void *b,void *c) {(void)a;(void)b;(void)c;return -1;}
int shutdown(int a,int b) {(void)a;(void)b;return 0;}
int close(int a) {(void)a;++closes;return 0;}
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return NULL;}
int xTaskCreatePinnedToCore(void (*f)(void *),const char *n,int z,void *ctx,int pri,TaskHandle_t *h,int core) {(void)n;(void)z;(void)ctx;(void)pri;(void)core;task_fn=f;*h=(void *)1;return 1;}
void vTaskDelay(unsigned t) {(void)t;++waits;}
void vTaskDelete(void *t) {(void)t;}
void node_on_peer_connected(uint32_t a,uint32_t b,int c) {(void)a;(void)b;(void)c;}
void node_on_peer_lost(uint32_t a,uint32_t b,int c) {(void)a;(void)b;(void)c;}
void node_on_peer_message(void *a,uint16_t b) {(void)a;(void)b;}
int node_get_device_orientation(void) {return 1;}
void node_set_peer_offer_accepted(bool a) {(void)a;}
void node_reject_wireless_peer(void) {}
bool peer_session_init(peer_session_t *s) {memset(s,0,sizeof(*s));return true;}
void peer_session_start(peer_session_t *s) {(void)s;}
void peer_session_shutdown(peer_session_t *s) {(void)s;}
bool peer_session_open(peer_session_t *s,int a,bool b,const char *c) {(void)s;(void)a;(void)b;(void)c;return false;}
void peer_session_close(peer_session_t *s) {(void)s;}
int peer_session_receive(peer_session_t *s,uint8_t *d,uint16_t n) {(void)s;(void)d;(void)n;return -1;}
bool peer_session_send(peer_session_t *s,const uint8_t *d,uint16_t n) {(void)s;(void)d;(void)n;return false;}
void os_diag(const char *tag,const char *fmt,...) {(void)tag;if(strstr(fmt,"listener_ready")) ++ready_logs;}
#include "../../../components/wireless/server/server.c"
int main(void) {
 server_create();task_fn(NULL);
 if (binds!=2 || listens!=1 || closes!=2 || waits!=20 || ready_logs!=1 || server_is_up || server_task) {
  printf("FAIL binds=%u listens=%u closes=%u waits=%u ready=%u\n",binds,listens,closes,waits,ready_logs);return 1;
 }
 puts("server: failed bind retried, listener recovered, requested shutdown completed OK");return 0;
}
