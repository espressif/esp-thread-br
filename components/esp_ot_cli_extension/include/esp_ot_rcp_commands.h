/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <openthread/error.h>

#ifdef __cplusplus
extern "C" {
#endif

otError esp_openthread_process_rcp_command(void *aContext, uint8_t aArgsLength, char *aArgs[]);

#ifdef __cplusplus
} /* extern "C" */
#endif
