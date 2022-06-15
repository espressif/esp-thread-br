/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void esp_openthread_process_rcp_command(void *aContext, uint8_t aArgsLength, char *aArgs[]);

void esp_set_rcp_server_cert(const char *cert);

#ifdef __cplusplus
} /* extern "C" */
#endif
