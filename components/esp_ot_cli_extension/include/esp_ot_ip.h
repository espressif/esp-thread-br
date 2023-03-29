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
 * @brief User command "ip" process.
 *
 */
otError esp_ot_process_ip(void *aContext, uint8_t aArgsLength, char *aArgs[]);

typedef enum { UNICAST_DEL, UNICAST_ADD, MULTICAST_DEL, MULTICAST_ADD } action_type;

#ifdef __cplusplus
}
#endif
