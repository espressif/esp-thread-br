/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief This function initializes the page for Thread Border Router based on m5Sstack.
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t border_router_m5stack_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
