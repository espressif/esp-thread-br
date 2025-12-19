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

void setup_inactivity_monitor(void);

void add_touch_event_handler(lv_obj_t *obj);

void set_main_page_reference(lv_obj_t *main_page);

void enable_screen_dimming(void);

void disable_screen_dimming(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
