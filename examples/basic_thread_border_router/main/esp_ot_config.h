/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread Border Router Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#pragma once

#include "esp_openthread_types.h"

#if CONFIG_EXTERNAL_COEX_ENABLE
#include "esp_coexist.h"
#endif

#define RCP_FIRMWARE_DIR "/spiffs/ot_rcp"

#if CONFIG_OPENTHREAD_RADIO_SPINEL_UART
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()              \
    {                                                      \
        .radio_mode = RADIO_MODE_UART_RCP,                 \
        .radio_uart_config = {                             \
            .port = 1,                                     \
            .uart_config =                                 \
                {                                          \
                    .baud_rate = 460800,                   \
                    .data_bits = UART_DATA_8_BITS,         \
                    .parity = UART_PARITY_DISABLE,         \
                    .stop_bits = UART_STOP_BITS_1,         \
                    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, \
                    .rx_flow_ctrl_thresh = 0,              \
                    .source_clk = UART_SCLK_DEFAULT,       \
                },                                         \
            .rx_pin = CONFIG_PIN_TO_RCP_TX,                \
            .tx_pin = CONFIG_PIN_TO_RCP_RX,                \
        },                                                 \
    }
#else
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()              \
    {                                                      \
        .radio_mode = RADIO_MODE_SPI_RCP,                  \
        .radio_spi_config = {                              \
            .host_device = SPI2_HOST,                      \
            .dma_channel = 2,                              \
            .spi_interface =                               \
                {                                          \
                    .mosi_io_num = CONFIG_PIN_TO_RCP_MOSI, \
                    .miso_io_num = CONFIG_PIN_TO_RCP_MISO, \
                    .sclk_io_num = CONFIG_PIN_TO_RCP_SCLK, \
                },                                         \
            .spi_device =                                  \
                {                                          \
                    .cs_ena_pretrans = 2,                  \
                    .input_delay_ns = 100,                 \
                    .mode = 0,                             \
                    .clock_speed_hz = 2500 * 1000,         \
                    .spics_io_num = CONFIG_PIN_TO_RCP_CS,  \
                    .queue_size = 5,                       \
                },                                         \
            .intr_pin = CONFIG_PIN_TO_RCP_BOOT,            \
        },                                                 \
    }
#endif // CONFIG_OPENTHREAD_RADIO_SPINEL_UART OR  CONFIG_OPENTHREAD_RADIO_SPINEL_SPI

#define ESP_OPENTHREAD_RCP_UPDATE_CONFIG()                                                                           \
    {                                                                                                                \
        .rcp_type = RCP_TYPE_ESP32H2_UART, .uart_rx_pin = CONFIG_PIN_TO_RCP_TX, .uart_tx_pin = CONFIG_PIN_TO_RCP_RX, \
        .uart_port = 1, .uart_baudrate = 115200, .reset_pin = CONFIG_PIN_TO_RCP_RESET,                               \
        .boot_pin = CONFIG_PIN_TO_RCP_BOOT, .update_baudrate = 460800, .firmware_dir = "/rcp_fw/ot_rcp",             \
        .target_chip = ESP32H2_CHIP,                                                                                 \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                   \
    {                                                          \
        .host_connection_mode = HOST_CONNECTION_MODE_CLI_UART, \
        .host_uart_config = {                                  \
            .port = 0,                                         \
            .uart_config =                                     \
                {                                              \
                    .baud_rate = 115200,                       \
                    .data_bits = UART_DATA_8_BITS,             \
                    .parity = UART_PARITY_DISABLE,             \
                    .stop_bits = UART_STOP_BITS_1,             \
                    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,     \
                    .rx_flow_ctrl_thresh = 0,                  \
                    .source_clk = UART_SCLK_DEFAULT,           \
                },                                             \
            .rx_pin = UART_PIN_NO_CHANGE,                      \
            .tx_pin = UART_PIN_NO_CHANGE,                      \
        },                                                     \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }

#if CONFIG_EXTERNAL_COEX_ENABLE
#if CONFIG_EXTERNAL_COEX_WIRE_TYPE == EXTERNAL_COEXIST_WIRE_1
#define ESP_OPENTHREAD_DEFAULT_EXTERNAL_COEX_CONFIG() \
    {                                                 \
        .request = CONFIG_EXTERNAL_COEX_REQUEST_PIN,  \
    }
#elif CONFIG_EXTERNAL_COEX_WIRE_TYPE == EXTERNAL_COEXIST_WIRE_2
#define ESP_OPENTHREAD_DEFAULT_EXTERNAL_COEX_CONFIG()                                         \
    {                                                                                         \
        .request = CONFIG_EXTERNAL_COEX_REQUEST_PIN, .grant = CONFIG_EXTERNAL_COEX_GRANT_PIN, \
    }
#elif CONFIG_EXTERNAL_COEX_WIRE_TYPE == EXTERNAL_COEXIST_WIRE_3
#define ESP_OPENTHREAD_DEFAULT_EXTERNAL_COEX_CONFIG()                                               \
    {                                                                                               \
        .request = CONFIG_EXTERNAL_COEX_REQUEST_PIN, .priority = CONFIG_EXTERNAL_COEX_PRIORITY_PIN, \
        .grant = CONFIG_EXTERNAL_COEX_GRANT_PIN,                                                    \
    }
#elif CONFIG_EXTERNAL_COEX_WIRE_TYPE == EXTERNAL_COEXIST_WIRE_4
#define ESP_OPENTHREAD_DEFAULT_EXTERNAL_COEX_CONFIG()                                               \
    {                                                                                               \
        .request = CONFIG_EXTERNAL_COEX_REQUEST_PIN, .priority = CONFIG_EXTERNAL_COEX_PRIORITY_PIN, \
        .grant = CONFIG_EXTERNAL_COEX_GRANT_PIN, .tx_line = CONFIG_EXTERNAL_COEX_TX_LINE_PIN,       \
    }
#endif
#endif // CONFIG_EXTERNAL_COEX_ENABLE
