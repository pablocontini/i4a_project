#ifndef NATIVE_PORTAL_LWIP_TCPIP_H
#define NATIVE_PORTAL_LWIP_TCPIP_H
#define ERR_OK 0
int tcpip_callback_wait(void (*callback)(void *), void *);

#endif
