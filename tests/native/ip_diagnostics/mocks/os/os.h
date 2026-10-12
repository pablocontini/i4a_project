#ifndef IP_DIAGNOSTICS_TEST_OS_H
#define IP_DIAGNOSTICS_TEST_OS_H
#include <stdint.h>
uint64_t os_monotonic_ms(void);
void os_diag(const char *tag, const char *fmt, ...);
#endif
