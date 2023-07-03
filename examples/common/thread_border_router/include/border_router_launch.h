/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_rcp_update.h"

void launch_openthread_border_router(const esp_openthread_platform_config_t *config,
                                     const esp_rcp_update_config_t *update_config);

#ifdef __cplusplus
} /* extern "C" */
#endif
