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

#if CONFIG_BR_BOARD_DEV_KIT
#define HOST_PIN_TO_RCP_RESET 7
#define HOST_PIN_TO_RCP_BOOT 8
#define HOST_PIN_TO_RCP_TX 17
#define HOST_PIN_TO_RCP_RX 18
#else
// #define HOST_PIN_TO_RCP_RESET 4
// #define HOST_PIN_TO_RCP_BOOT 5
#define HOST_PIN_TO_RCP_RESET CONFIG_PIN_TO_RCP_RESET
#define HOST_PIN_TO_RCP_BOOT CONFIG_PIN_TO_RCP_BOOT
#define HOST_PIN_TO_RCP_TX CONFIG_PIN_TO_RCP_TX
#define HOST_PIN_TO_RCP_RX CONFIG_PIN_TO_RCP_RX
#endif

#define RCP_FIRMWARE_DIR "/spiffs/ot_rcp"

#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()              \
    {                                                      \
        .radio_mode = RADIO_MODE_UART_RCP,                 \
        .radio_uart_config = {                             \
            .port = 1,                                     \
            .uart_config =                                 \
                {                                          \
                    .baud_rate = 115200,                   \
                    .data_bits = UART_DATA_8_BITS,         \
                    .parity = UART_PARITY_DISABLE,         \
                    .stop_bits = UART_STOP_BITS_1,         \
                    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, \
                    .rx_flow_ctrl_thresh = 0,              \
                    .source_clk = UART_SCLK_APB,           \
                },                                         \
            .rx_pin = HOST_PIN_TO_RCP_TX,                  \
            .tx_pin = HOST_PIN_TO_RCP_RX,                  \
        },                                                 \
    }

#define ESP_OPENTHREAD_RCP_UPDATE_CONFIG()                                                                             \
    {                                                                                                                  \
        .rcp_type = RCP_TYPE_ESP32H2_UART, .uart_rx_pin = HOST_PIN_TO_RCP_TX, .uart_tx_pin = HOST_PIN_TO_RCP_RX,       \
        .uart_port = 1, .uart_baudrate = 115200, .reset_pin = HOST_PIN_TO_RCP_RESET, .boot_pin = HOST_PIN_TO_RCP_BOOT, \
        .update_baudrate = 230400, .firmware_dir = "/spiffs/ot_rcp", .target_chip = ESP32H2_CHIP,                      \
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
                    .source_clk = UART_SCLK_APB,               \
                },                                             \
            .rx_pin = UART_PIN_NO_CHANGE,                      \
            .tx_pin = UART_PIN_NO_CHANGE,                      \
        },                                                     \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                                   \
    {                                                                                          \
        .storage_partition_name = "ot_storage", .netif_queue_size = 10, .task_queue_size = 10, \
    }
