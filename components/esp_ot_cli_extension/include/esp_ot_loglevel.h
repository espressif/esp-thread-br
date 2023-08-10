/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "stdint.h"
#include <openthread/error.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief User command "loglevel" process.
 *
 */
otError esp_ot_process_logset(void *aContext, uint8_t aArgsLength, char *aArgs[]);

#ifdef __cplusplus
}
#endif
