/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <openthread/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief User command "mcast" process.
 *
 */
otError esp_ot_process_mcast_group(void *aContext, uint8_t aArgsLength, char *aArgs[]);

/**
 * @brief User command "udpsockserver" process.
 *
 */
otError esp_ot_process_udp_server(void *aContext, uint8_t aArgsLength, char *aArgs[]);

/**
 * @brief User command "udpsockclient" process.
 *
 */
otError esp_ot_process_udp_client(void *aContext, uint8_t aArgsLength, char *aArgs[]);

#ifdef __cplusplus
}
#endif
