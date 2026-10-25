#pragma once
#include "device.h"
#include "esp_err.h"
esp_err_t node_radio_cut_init(DevicePtr device);
bool node_radio_cut_busy(void);
