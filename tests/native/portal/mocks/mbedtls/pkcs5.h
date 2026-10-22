#ifndef NATIVE_PORTAL_MBEDTLS_PKCS5_H
#define NATIVE_PORTAL_MBEDTLS_PKCS5_H
#include <stddef.h>
int mbedtls_pkcs5_pbkdf2_hmac_ext(
    int, const unsigned char *, size_t, const unsigned char *, size_t,
    unsigned, unsigned, unsigned char *);
#endif
