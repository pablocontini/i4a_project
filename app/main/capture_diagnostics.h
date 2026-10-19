#pragma once
#include "esp_err.h"
#include "routing/routing.h"

/* Starts only when CONFIG_I4A_CAPTURE_DIAGNOSTICS is enabled. */
esp_err_t capture_diagnostics_start(routing_t *routing);
