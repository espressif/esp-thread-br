/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "br_m5stack_common.h"
#include "br_m5stack_layout.h"
#include "br_m5stack_screen_dimming.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "lvgl.h"
#include "nvs.h"
#include "bsp/esp-bsp.h"
#include "core/lv_obj_tree.h"

static lv_obj_t *s_base_page = NULL;
static lv_obj_t *s_warning_page = NULL;
static lv_obj_t *s_factoryreset_page = NULL;
static lv_timer_t *s_factoryreset_timer = NULL;

void br_m5stack_delete_page_from_button(lv_event_t *e)
{
    esp_err_t ret = ESP_OK;
    (void)ret;
    lv_obj_t *btn = NULL;
    lv_obj_t *page = NULL;

    btn = lv_event_get_target(e);
    ESP_GOTO_ON_FALSE(btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to get the target button for deleting");
    page = lv_obj_get_parent(btn);
    ESP_GOTO_ON_FALSE(page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to get the parent page of button for deleting");

    // If deleting the factory reset page, return to main page
    if (page == s_factoryreset_page) {
        // Cancel the timer if it exists
        if (s_factoryreset_timer) {
            lv_timer_del(s_factoryreset_timer);
            s_factoryreset_timer = NULL;
        }
        s_factoryreset_page = NULL;
        br_m5stack_delete_page(page);
        br_m5stack_show_main_page();
    } else {
        br_m5stack_delete_page(page);
    }
    btn = NULL;

exit:
    BR_M5STACK_DELETE_OBJ_IF_EXIST(btn);
}

void br_m5stack_add_esp_tiny_logo(lv_obj_t *page)
{
    lv_obj_t *img_logo = NULL;
    lv_obj_t *img_text = NULL;

    LV_IMG_DECLARE(esp_logo_tiny);
    img_logo = lv_img_create(page);
    ESP_RETURN_ON_FALSE(img_logo, , BR_M5STACK_TAG, "Failed to create an image logo");
    lv_img_set_src(img_logo, &esp_logo_tiny);
    lv_obj_align(img_logo, LV_ALIGN_TOP_LEFT, 0, 0);
    img_text = lv_label_create(page);
    ESP_RETURN_ON_FALSE(img_text, , BR_M5STACK_TAG, "Failed to create an image text");
    lv_label_set_text(img_text, "M5Stack Thread Border Router");
    lv_obj_set_style_text_color(img_text, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(img_text, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align_to(img_text, img_logo, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
}

void br_m5stack_create_base(void)
{
    s_base_page = lv_obj_create(lv_scr_act());
    assert(s_base_page);
    lv_obj_add_flag(s_base_page, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *br_m5stack_create_button(lv_coord_t w, lv_coord_t h, lv_event_cb_t event_cb, lv_event_code_t filter,
                                   char *txt, void *user_data)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *btn = NULL;
    lv_obj_t *label = NULL;

    btn = lv_btn_create(s_base_page);
    ESP_GOTO_ON_FALSE(btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create a button");
    lv_obj_set_style_bg_color(btn, lv_color_make(220, 220, 220), LV_STATE_DEFAULT);
    lv_obj_set_size(btn, w, h);

    if (event_cb) {
        lv_obj_add_event_cb(btn, event_cb, filter, user_data);
    }

    label = lv_label_create(btn);
    ESP_GOTO_ON_FALSE(label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create a label for button");
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_center(label);

    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, w - 10);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    add_touch_event_handler(btn);

exit:
    if (ret != ESP_OK) {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(btn);
    }
    return btn;
}

lv_obj_t *br_m5stack_create_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color,
                                  lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    ESP_RETURN_ON_FALSE(label, NULL, BR_M5STACK_TAG, "Failed to create a label");
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_color(label, color, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_align(label, align, x, y);

    add_touch_event_handler(label);

    return label;
}

void br_m5stack_modify_label(lv_obj_t *obj, const char *txt)
{
    lv_label_set_text(obj, txt);
}

lv_obj_t *br_m5stack_create_blank_page(lv_obj_t *parent)
{
    lv_obj_t *new_page = lv_obj_create(parent);
    ESP_RETURN_ON_FALSE(new_page, NULL, BR_M5STACK_TAG, "Failed to create a blank page");
    lv_obj_set_size(new_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(new_page, lv_obj_get_style_bg_color(parent, LV_STATE_DEFAULT), LV_STATE_DEFAULT);
    lv_obj_center(new_page);
    lv_obj_set_style_pad_all(new_page, 0, LV_PART_MAIN);

    add_touch_event_handler(new_page);
    // Disable dimming when creating overlay pages (pages on lv_layer_top)
    if (parent == lv_layer_top()) {
        disable_screen_dimming();
    }

    return new_page;
}

void br_m5stack_add_btn_to_page(lv_obj_t *page, lv_obj_t *btn, lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    lv_obj_set_parent(btn, page);
    lv_obj_align(btn, align, x, y);
}

static void warning_page_cb(lv_timer_t *timer)
{
    lv_obj_del(s_warning_page);
    s_warning_page = NULL;
    lv_timer_del(timer);
    // Re-enable dimming when warning page is deleted (back to main page)
    enable_screen_dimming();
}

static void factoryreset_page_cb(lv_timer_t *timer)
{
    // Check if page still exists (might have been deleted manually)
    if (s_factoryreset_page) {
        br_m5stack_delete_page(s_factoryreset_page);
        s_factoryreset_page = NULL;
    }
    s_factoryreset_timer = NULL;
    lv_timer_del(timer);
    br_m5stack_show_main_page();
}

lv_obj_t *br_m5stack_create_warn(char *warning, uint32_t delay_ms)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *label = NULL;

    s_warning_page = br_m5stack_create_blank_page(lv_layer_top());
    ESP_GOTO_ON_FALSE(s_warning_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create a warning page");
    label = br_m5stack_create_label(s_warning_page, warning, &lv_font_montserrat_20, lv_color_make(255, 0, 0),
                                    LV_ALIGN_CENTER, 0, 0);
    ESP_GOTO_ON_FALSE(label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create a label for warning page");

    if (delay_ms != 0) {
        lv_timer_create(warning_page_cb, delay_ms, NULL);
    }

    add_touch_event_handler(s_warning_page);

exit:
    if (ret != ESP_OK) {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(s_warning_page);
    }
    return s_warning_page;
}

void br_m5stack_delete_page(lv_obj_t *page)
{
    // Check if we're deleting an overlay page and should re-enable dimming
    lv_obj_t *main_page = br_m5stack_get_main_page();
    bool is_overlay_page = (page != main_page && lv_obj_get_parent(page) == lv_layer_top());
    if (is_overlay_page) {
        // Re-enable dimming when deleting overlay pages (back to main page)
        enable_screen_dimming();
    }
    lv_obj_del(page);
}

void br_m5stack_hidden_page(lv_obj_t *page)
{
    lv_obj_move_background(page);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
}

void br_m5stack_display_page(lv_obj_t *page)
{
    lv_obj_move_foreground(page);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    // Enable dimming when displaying the main page
    if (page == br_m5stack_get_main_page()) {
        enable_screen_dimming();
    } else {
        disable_screen_dimming();
    }
}

// Factory reset functions
static void br_m5stack_do_factoryreset(lv_event_t *e)
{
    esp_err_t ret = ESP_OK;
    (void)ret;
    br_m5stack_err_msg_t err_msg = "Failed to do factoryreset";
    nvs_handle_t ot_nvs_handle;
    esp_err_t err = ESP_OK;

    err = nvs_open("openthread", NVS_READWRITE, &ot_nvs_handle);
    ESP_GOTO_ON_ERROR(err, exit, BR_M5STACK_TAG, "Failed to open NVS namespace (0x%x)", err);
    err = nvs_erase_all(ot_nvs_handle);
    ESP_GOTO_ON_ERROR(err, exit, BR_M5STACK_TAG, "Failed to erase all OpenThread settings (0x%x)", err);
    nvs_close(ot_nvs_handle);

    err = nvs_open("wifi_config", NVS_READWRITE, &ot_nvs_handle);
    ESP_GOTO_ON_ERROR(err, exit, BR_M5STACK_TAG, "Failed to open NVS namespace (0x%x)", err);
    err = nvs_erase_all(ot_nvs_handle);
    ESP_GOTO_ON_ERROR(err, exit, BR_M5STACK_TAG, "Failed to erase all Wi-Fi settings (0x%x)", err);
    nvs_close(ot_nvs_handle);

    esp_restart();

exit:
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

static void br_m5stack_factoryreset_confirm(lv_event_t *e)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *label = NULL;
    lv_obj_t *yes_btn = NULL;
    lv_obj_t *no_btn = NULL;
    const uint32_t timeout_ms = 10000; // 10 seconds timeout

    s_factoryreset_page = br_m5stack_create_blank_page(lv_layer_top());
    ESP_GOTO_ON_FALSE(s_factoryreset_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the factoryreset page");

    label = br_m5stack_create_label(s_factoryreset_page, "Are you sure you want to factory reset?",
                                    &lv_font_montserrat_16, lv_color_black(), LV_ALIGN_CENTER, 0, -20);
    ESP_GOTO_ON_FALSE(label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the label for factoryreset page");

    yes_btn = br_m5stack_create_button(100, 50, br_m5stack_do_factoryreset, LV_EVENT_CLICKED, "Yes", NULL);
    ESP_GOTO_ON_FALSE(yes_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the yes button for factoryreset page");
    br_m5stack_add_btn_to_page(s_factoryreset_page, yes_btn, LV_ALIGN_LEFT_MID, 40, 60);

    no_btn = br_m5stack_create_button(100, 50, br_m5stack_delete_page_from_button, LV_EVENT_CLICKED, "No", NULL);
    ESP_GOTO_ON_FALSE(no_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the no button for factoryreset page");
    br_m5stack_add_btn_to_page(s_factoryreset_page, no_btn, LV_ALIGN_RIGHT_MID, -40, 60);

    // Create timer to automatically close the factory reset page after timeout
    s_factoryreset_timer = lv_timer_create(factoryreset_page_cb, timeout_ms, NULL);
    ESP_GOTO_ON_FALSE(s_factoryreset_timer, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create factoryreset timer");

exit:
    if (ret != ESP_OK) {
        if (s_factoryreset_timer) {
            lv_timer_del(s_factoryreset_timer);
            s_factoryreset_timer = NULL;
        }
        BR_M5STACK_DELETE_OBJ_IF_EXIST(s_factoryreset_page);
        s_factoryreset_page = NULL;
    }
}

lv_obj_t *br_m5stack_create_factoryreset_button(lv_obj_t *page)
{
    lv_obj_t *btn =
        br_m5stack_create_button(130, 50, br_m5stack_factoryreset_confirm, LV_EVENT_CLICKED, "factoryreset", NULL);
    ESP_RETURN_ON_FALSE(btn, NULL, BR_M5STACK_TAG, "Failed to create factoryreset button");

    add_touch_event_handler(btn);

    return btn;
}