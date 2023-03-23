/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_netif.h"

#include <openthread/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief User command "wifi" process.
 *
 */
otError esp_ot_process_wifi_cmd(void *aContext, uint8_t aArgsLength, char *aArgs[]);

void handle_wifi_addr_init(void);

/**
 * @brief Wifi netif init.
 *
 */
void esp_ot_wifi_netif_init(void);

#ifdef __cplusplus
}
#endif
