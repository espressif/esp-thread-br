/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "br_m5stack_layout.h"
#include "br_m5stack_common.h"
#include "br_m5stack_epskc_page.h"
#include "br_m5stack_thread_page.h"
#include "br_m5stack_wifi_page.h"

#include "esp_check.h"
#include "esp_err.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "core/lv_obj_tree.h"

lv_obj_t *s_main_page = NULL;

void br_m5stack_bsp_init(void)
{
    bsp_i2c_init();
    bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                             .buffer_size = BSP_LCD_H_RES * 10,
                             .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
                             .flags = {
                                 .buff_dma = true,
                                 .buff_spiram = false,
                             }};
    cfg.lvgl_port_cfg.task_stack = 10240;
    bsp_display_start_with_config(&cfg);
}

void br_m5stack_display_init(void)
{
    bsp_display_lock(0);
    bsp_display_backlight_on();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(237, 238, 239), LV_STATE_DEFAULT);
    br_m5stack_create_base();
    bsp_display_unlock();
}

void br_m5stack_create_ui(void)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *label = NULL;
    lv_obj_t *wifi_btn = NULL;
    lv_obj_t *thread_btn = NULL;
    lv_obj_t *epskc_btn = NULL;
    lv_obj_t *factoryreset_btn = NULL;

    br_m5stack_add_esp_tiny_logo(lv_layer_top());

    s_main_page = br_m5stack_create_blank_page(lv_scr_act());
    ESP_GOTO_ON_FALSE(s_main_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the main page");

    lv_obj_set_style_pad_all(s_main_page, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_main_page, 0, LV_PART_MAIN);

    label = br_m5stack_create_label(s_main_page, "Thread Border Router", &lv_font_montserrat_26, lv_color_black(),
                                    LV_ALIGN_TOP_MID, 0, 35);
    ESP_GOTO_ON_FALSE(label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Thread Border Router label");

    wifi_btn = br_m5stack_wifi_ui_init(s_main_page);
    ESP_GOTO_ON_FALSE(wifi_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi buttion for main page");
    br_m5stack_add_btn_to_page(s_main_page, wifi_btn, LV_ALIGN_BOTTOM_LEFT, 10, -20);

    thread_btn = br_m5stack_thread_ui_init(s_main_page);
    ESP_GOTO_ON_FALSE(thread_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Thread buttion for main page");
    br_m5stack_add_btn_to_page(s_main_page, thread_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -20);

    epskc_btn = br_m5stack_epskc_ui_init(s_main_page);
    ESP_GOTO_ON_FALSE(epskc_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Ephemeral Key buttion for main page");
    br_m5stack_add_btn_to_page(s_main_page, epskc_btn, LV_ALIGN_CENTER, 0, 0);

    factoryreset_btn = br_m5stack_create_factoryreset_button(s_main_page);
    ESP_GOTO_ON_FALSE(factoryreset_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the factoryreset buttion for main page");
    br_m5stack_add_btn_to_page(s_main_page, factoryreset_btn, LV_ALIGN_TOP_RIGHT, 0, 0);

exit:
    ESP_ERROR_CHECK(ret);
}