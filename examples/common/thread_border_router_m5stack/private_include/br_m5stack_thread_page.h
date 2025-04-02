/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BR_M5STACK_DATASET_CHANNEL,
    BR_M5STACK_DATASET_NETWORKKEY,
    BR_M5STACK_DATASET_PANID,
    BR_M5STACK_DATASET_MAX,
} br_m5stack_dataset_flag_t;

lv_obj_t *br_m5stack_thread_ui_init(lv_obj_t *page);

lv_obj_t *br_m5stack_create_factoryreset_button(lv_obj_t *page);

#ifdef __cplusplus
} /* extern "C" */
#endif
