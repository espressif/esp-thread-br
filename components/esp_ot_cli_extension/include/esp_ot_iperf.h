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
 * @brief User command "iperf" process.
 *
 */
otError esp_ot_process_iperf(void *aContext, uint8_t aArgsLength, char *aArgs[]);

/**
 * default length to fit into a single 15.4 packet
 */
#define OT_IPERF_DEFAULT_LEN 81

#ifdef __cplusplus
}
#endif
