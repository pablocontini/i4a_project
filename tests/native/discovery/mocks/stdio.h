#ifndef DISCOVERY_NATIVE_STDIO_H
#define DISCOVERY_NATIVE_STDIO_H
/* Keep the standalone runner's output visible; production discovery code
 * does not depend on stdio or the neighboring suites' silent UART mock. */
int printf(const char *format, ...);
#endif
