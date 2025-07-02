/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#pragma once

#include "esp_err.h"

#define BSP_AW9523_ADDR 0x58
#define CORE_S3_BUS_OUT_EN BIT(1)
#define CORE_S3_BOOST_EN BIT(7)

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t br_m5stack_enable_port_c_power(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
