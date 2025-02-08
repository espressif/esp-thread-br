/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_ot_cli_extension.h"
#include <stdint.h>
#include <openthread/error.h>
#include "openthread/cli.h"

otError esp_openthread_process_br_lib_compatibility_check(void *aContext, uint8_t aArgsLength, char *aArgs[]);
