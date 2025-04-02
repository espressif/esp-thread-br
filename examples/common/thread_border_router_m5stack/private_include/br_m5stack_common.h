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

#define BR_M5STACK_TAG "br_m5stack"
#define MAX_BUTTON_NUM 6
#define BUTTON_DEFAULT_HEIGHT 60
#define BUTTON_DEFAULT_WIDTH 120

typedef void (*br_m5stack_kb_callback_t)(const char *, void *);

typedef char br_m5stack_err_msg_t[128];

#define BR_M5STACK_GOTO_ON_FALSE(x, goto_tag, msg, ...)             \
    do {                                                            \
        if (!(x)) {                                                 \
            snprintf(err_msg, sizeof(err_msg), msg, ##__VA_ARGS__); \
            goto goto_tag;                                          \
        }                                                           \
    } while (0)

#define BR_M5STACK_DELETE_OBJ_IF_EXIST(x) \
    do {                                  \
        if (x) {                          \
            lv_obj_del(x);                \
            x = NULL;                     \
        }                                 \
    } while (0)

#define BR_M5STACK_CREATE_WARNING_IF_EXIST(timeout_ms)   \
    do {                                                 \
        if (err_msg[0] != 0) {                           \
            br_m5stack_create_warn(err_msg, timeout_ms); \
        }                                                \
    } while (0)

void br_m5stack_delete_page_from_button(lv_event_t *e);

void br_m5stack_hidden_page_from_button(lv_event_t *e);

void br_m5stack_display_page_from_button(lv_event_t *e);

void br_m5stack_create_base(void);

lv_obj_t *br_m5stack_create_button(lv_coord_t w, lv_coord_t h, lv_event_cb_t event_cb, lv_event_code_t filter,
                                   char *txt, void *user_data);

lv_obj_t *br_m5stack_create_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color,
                                  lv_align_t align, lv_coord_t x, lv_coord_t y);

void br_m5stack_modify_label(lv_obj_t *obj, const char *txt);

void br_m5stack_add_esp_tiny_logo(lv_obj_t *page);

lv_obj_t *br_m5stack_create_blank_page(lv_obj_t *parent);

void br_m5stack_add_btn_to_page(lv_obj_t *page, lv_obj_t *btn, lv_align_t align, lv_coord_t x, lv_coord_t y);

void br_m5stack_create_keyboard(lv_obj_t *page, br_m5stack_kb_callback_t callback, void *user_data, bool only_num);

lv_obj_t *br_m5stack_create_warn(char *warning, uint32_t delay_ms);

void br_m5stack_delete_page(lv_obj_t *page);

void br_m5stack_hidden_page(lv_obj_t *page);

void br_m5stack_display_page(lv_obj_t *page);

#ifdef __cplusplus
} /* extern "C" */
#endif
