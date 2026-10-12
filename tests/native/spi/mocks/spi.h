#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#define pdTRUE 1
#define DMA_ATTR
#define SPI_BUFFER_SIZE 64
#define SPI_QUEUE_SIZE 40
#define SPI_RECEIVER_HOST 1
#define SPI_SENDER_HOST 2
#define SPI_DMA_CH_AUTO 3
#define SPI_RECEIVER_GPIO_MOSI 13
#define SPI_RECEIVER_GPIO_SCLK 14
#define SPI_RECEIVER_GPIO_CS 15
#define SPI_SENDER_GPIO_MOSI 23
#define SPI_SENDER_GPIO_SCLK 18
#define SPI_SENDER_GPIO_CS 5
#define SPI_FREQ 8000000
typedef void *spi_device_handle_t;
typedef struct { size_t length, trans_len; void *rx_buffer; } spi_slave_transaction_t;
typedef struct { int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num, intr_flags; } spi_bus_config_t;
typedef struct { int mode, spics_io_num, queue_size, flags; void *post_trans_cb; } spi_slave_interface_config_t;
typedef struct { int command_bits, address_bits, dummy_bits, clock_speed_hz, duty_cycle_pos, mode, spics_io_num, cs_ena_pretrans, cs_ena_posttrans, queue_size; } spi_device_interface_config_t;
typedef struct { size_t length; void *tx_buffer; } spi_transaction_t;
esp_err_t spi_slave_initialize(int host, const spi_bus_config_t *bus, const spi_slave_interface_config_t *cfg, int dma);
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t *bus, int dma);
esp_err_t spi_bus_add_device(int host, const spi_device_interface_config_t *cfg, spi_device_handle_t *handle);
esp_err_t spi_device_transmit(spi_device_handle_t handle, spi_transaction_t *t);
esp_err_t spi_slave_queue_trans(int host, spi_slave_transaction_t *t, TickType_t wait);
esp_err_t spi_slave_get_trans_result(int host, spi_slave_transaction_t **t, TickType_t wait);
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *, unsigned, void *, unsigned, void *, unsigned);

unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
