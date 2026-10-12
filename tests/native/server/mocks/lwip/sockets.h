#ifndef I4A_SERVER_TEST_LWIP_SOCKETS_H
#define I4A_SERVER_TEST_LWIP_SOCKETS_H
#include <stdint.h>
#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_IP 0
#define INADDR_ANY 0
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SHUT_RDWR 2
#define htonl(x) (x)
#define ntohl(x) (x)
#define htons(x) (x)
#define fd_set server_test_fd_set
#define timeval server_test_timeval
#ifdef FD_ZERO
#undef FD_ZERO
#endif
#ifdef FD_SET
#undef FD_SET
#endif
typedef struct {int bits;} fd_set;
#define FD_ZERO(x) ((x)->bits=0)
#define FD_SET(n,x) ((x)->bits=(n))
struct timeval {long tv_sec,tv_usec;};
struct sockaddr {int unused;};
struct sockaddr_in {int sin_family;unsigned sin_port;struct {uint32_t s_addr;} sin_addr;};
int socket(int,int,int);
int setsockopt(int,int,int,const void *,unsigned);
int bind(int,const struct sockaddr *,unsigned);
int listen(int,int);
int lwip_select(int,fd_set *,void *,void *,struct timeval *);
int accept(int,void *,void *);
int shutdown(int,int);
int close(int);

#endif
