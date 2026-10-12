#ifndef NATIVE_PORTAL_ESP_ERR_H
#define NATIVE_PORTAL_ESP_ERR_H
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_ARG (-3)
#define ESP_ERR_INVALID_STATE (-4)
const char *esp_err_to_name(esp_err_t err);
#define ESP_ERROR_CHECK(expression) assert((expression) == ESP_OK)

#endif
