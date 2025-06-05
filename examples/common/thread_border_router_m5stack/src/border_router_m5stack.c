/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "border_router_m5stack.h"

#include "br_m5stack_animation.h"
#include "br_m5stack_layout.h"

#include "esp_err.h"
#include "bsp/esp-bsp.h"

static void ot_br_m5stack_worker(void *ctx)
{
    br_m5stack_bsp_init();
    br_m5stack_display_init();
    boot_animation_start(br_m5stack_create_ui);

    vTaskDelete(NULL);
}

esp_err_t border_router_m5stack_init(void)
{
    xTaskCreate(ot_br_m5stack_worker, "ot_br_m5stack_main", 8192, NULL, 5, NULL);
    return ESP_OK;
}