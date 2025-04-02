/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "br_m5stack_common.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "core/lv_obj_tree.h"

typedef struct {
    lv_obj_t *ta;
    void *user_data;
} br_m5stack_kb_context_t;

static br_m5stack_kb_callback_t br_m5stack_kb_callback = NULL;
static br_m5stack_kb_context_t kb_context;
static lv_obj_t *s_base_page = NULL;
static lv_obj_t *s_warning_page = NULL;

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

    lv_obj_del(page);
    btn = NULL;

exit:
    BR_M5STACK_DELETE_OBJ_IF_EXIST(btn);
}

void br_m5stack_hidden_page_from_button(lv_event_t *e)
{
    esp_err_t ret = ESP_OK;
    (void)ret;
    lv_obj_t *btn = NULL;
    lv_obj_t *page = NULL;

    btn = lv_event_get_target(e);
    ESP_GOTO_ON_FALSE(btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to get the target button for hiding");
    page = lv_obj_get_parent(btn);
    ESP_GOTO_ON_FALSE(page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to get the parent page of button for hiding");

    br_m5stack_hidden_page(page);
    btn = NULL;

exit:
    BR_M5STACK_DELETE_OBJ_IF_EXIST(btn);
}

void br_m5stack_display_page_from_button(lv_event_t *e)
{
    lv_obj_t *page = lv_event_get_user_data(e);
    ESP_RETURN_ON_FALSE(page, , BR_M5STACK_TAG, "Failed to get the page for display");
    br_m5stack_display_page(page);
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
    lv_label_set_text(img_text, "Espressif");
    lv_obj_set_style_text_color(img_text, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(img_text, &lv_font_montserrat_20, LV_PART_MAIN);
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
    ESP_GOTO_ON_FALSE(label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create a lable for button");
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_center(label);

    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, w - 10);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

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

    return new_page;
}

void br_m5stack_add_btn_to_page(lv_obj_t *page, lv_obj_t *btn, lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    lv_obj_set_parent(btn, page);
    lv_obj_align(btn, align, x, y);
}

static void kb_event_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    ESP_RETURN_ON_FALSE(kb, , BR_M5STACK_TAG, "Failed to handle keyboard event");
    br_m5stack_kb_context_t *context = (br_m5stack_kb_context_t *)lv_event_get_user_data(e);
    lv_obj_t *ta = context->ta;
    lv_obj_t *kb_base_page = lv_obj_get_parent(kb);

    lv_textarea_set_one_line(ta, true);

    if (e->code == LV_EVENT_READY) {
        char input_str[128];
        strcpy(input_str, lv_textarea_get_text(ta));
        lv_obj_del(kb_base_page);
        if (br_m5stack_kb_callback) {
            br_m5stack_kb_callback(input_str, context->user_data);
            br_m5stack_kb_callback = NULL;
        }
    } else if (e->code == LV_EVENT_CANCEL) {
        br_m5stack_kb_callback = NULL;
        lv_obj_del(kb_base_page);
    }
}

void br_m5stack_create_keyboard(lv_obj_t *page, br_m5stack_kb_callback_t callback, void *user_data, bool only_num)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *kb_base_page = NULL;
    lv_obj_t *ta = NULL;
    lv_obj_t *kb = NULL;

    kb_base_page = br_m5stack_create_blank_page(page);
    ESP_GOTO_ON_FALSE(kb_base_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create a page for keyboard");
    ta = lv_textarea_create(kb_base_page);
    ESP_GOTO_ON_FALSE(ta, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create a textbox");
    kb = lv_keyboard_create(kb_base_page);
    ESP_GOTO_ON_FALSE(kb, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create a keyboard");

    lv_obj_set_size(ta, 280, 20);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 15);
    if (only_num) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    }
    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(70));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);

    kb_context.ta = ta;
    kb_context.user_data = user_data;
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_VALUE_CHANGED, (void *)(&kb_context));
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, (void *)(&kb_context));
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, (void *)(&kb_context));
    br_m5stack_kb_callback = callback;

exit:
    if (ret != ESP_OK) {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(kb_base_page);
    }
}

static void warning_page_cb(lv_timer_t *timer)
{
    lv_obj_del(s_warning_page);
    s_warning_page = NULL;
    lv_timer_del(timer);
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

exit:
    if (ret != ESP_OK) {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(s_warning_page);
    }
    return s_warning_page;
}

void br_m5stack_delete_page(lv_obj_t *page)
{
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
}