/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "stdint.h"
#include <esp_err.h>
#include <openthread/error.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief User command "heapdiag" process.
 *
 */
otError esp_ot_process_heap_diag(void *aContext, uint8_t aArgsLength, char *aArgs[]);

/**
 * @brief Initialize heap diag. This function will initialize heap trace standalone and start heap trace
 *        if CONFIG_HEAP_TRACE_STANDALONE is selected.
 *
 */
esp_err_t esp_ot_heap_diag_init(void);

#ifdef __cplusplus
}
#endif
