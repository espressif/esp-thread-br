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
 * @brief User command "tcpsockserver" process.
 *
 */
otError esp_ot_process_tcp_server(void *aContext, uint8_t aArgsLength, char *aArgs[]);

/**
 * @brief User command "tcpsockclient" process.
 *
 */
otError esp_ot_process_tcp_client(void *aContext, uint8_t aArgsLength, char *aArgs[]);

#ifdef __cplusplus
}
#endif
