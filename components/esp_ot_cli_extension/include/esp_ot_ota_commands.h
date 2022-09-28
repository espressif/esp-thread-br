/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <openthread/error.h>

#ifdef __cplusplus
extern "C" {
#endif

otError esp_openthread_process_ota_command(void *aContext, uint8_t aArgsLength, char *aArgs[]);

void esp_set_ota_server_cert(const char *cert);

#ifdef __cplusplus
} /* extern "C" */
#endif
