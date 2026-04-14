/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "br_m5stack_screen_dimming.h"
#include "br_m5stack_common.h"

#include <stdatomic.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "core/lv_obj_tree.h"

#define FULL_BRIGHTNESS 100
#define DIMMED_BRIGHTNESS 30
#define DIM_TIMEOUT_S 10  /* seconds of inactivity before dimming */
#define OFF_TIMEOUT_S 300 /* seconds of inactivity before backlight off */

static esp_timer_handle_t s_inactivity_timer = NULL;
static lv_obj_t *s_main_page = NULL;
static atomic_bool s_screen_dimmed = ATOMIC_VAR_INIT(false);
static atomic_bool s_screen_off = ATOMIC_VAR_INIT(false);
static atomic_bool s_dimming_enabled = ATOMIC_VAR_INIT(false);

static void reset_s_inactivity_timer(void)
{
    if (s_inactivity_timer) {
        esp_timer_stop(s_inactivity_timer);
        esp_timer_start_once(s_inactivity_timer, DIM_TIMEOUT_S * 1000000ULL);
    }
}

static void s_inactivity_timer_callback(void *arg)
{
    if (!atomic_load(&s_dimming_enabled)) {
        return;
    }

    if (!atomic_load(&s_screen_dimmed)) {
        // Stage 1: dim the screen, then schedule the off stage
        ESP_LOGI(BR_M5STACK_TAG, "No touch for %ds, dimming screen.", DIM_TIMEOUT_S);
        bsp_display_brightness_set(DIMMED_BRIGHTNESS);
        atomic_store(&s_screen_dimmed, true);
        esp_timer_start_once(s_inactivity_timer, (OFF_TIMEOUT_S - DIM_TIMEOUT_S) * 1000000ULL);
    } else if (!atomic_load(&s_screen_off)) {
        // Stage 2: turn off the backlight entirely
        ESP_LOGI(BR_M5STACK_TAG, "No touch for %ds, turning off display.", OFF_TIMEOUT_S);
        bsp_display_backlight_off();
        atomic_store(&s_screen_off, true);
    }
}

static void create_s_inactivity_timer(void)
{
    const esp_timer_create_args_t timer_args = {.callback = s_inactivity_timer_callback, .name = "s_inactivity_timer"};

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_inactivity_timer));
}

static bool is_object_in_main_page(lv_obj_t *obj)
{
    if (!s_main_page || !obj) {
        return false;
    }

    // Check if the object is the main page itself or a descendant of it
    lv_obj_t *parent = obj;
    while (parent) {
        if (parent == s_main_page) {
            return true;
        }
        parent = lv_obj_get_parent(parent);
    }
    return false;
}

static void touch_event_handler(lv_event_t *e)
{
    if (e->code == LV_EVENT_PRESSED || e->code == LV_EVENT_RELEASED) {
        // Only handle dimming if dimming is enabled and the touch is on the main page
        if (!atomic_load(&s_dimming_enabled) || !is_object_in_main_page(lv_event_get_target(e))) {
            return;
        }

        if (atomic_load(&s_screen_off)) {
            ESP_LOGI(BR_M5STACK_TAG, "Touch detected. Turning display back on.");
            bsp_display_backlight_on();
            bsp_display_brightness_set(FULL_BRIGHTNESS);
            atomic_store(&s_screen_off, false);
            atomic_store(&s_screen_dimmed, false);
        } else if (atomic_load(&s_screen_dimmed)) {
            ESP_LOGI(BR_M5STACK_TAG, "Touch detected. Restoring full brightness.");
            bsp_display_brightness_set(FULL_BRIGHTNESS);
            atomic_store(&s_screen_dimmed, false);
        }

        reset_s_inactivity_timer();
    }
}

void setup_inactivity_monitor(void)
{
    create_s_inactivity_timer();

    if (s_inactivity_timer) {
        esp_timer_start_once(s_inactivity_timer, DIM_TIMEOUT_S * 1000000ULL);
    }
}

void set_main_page_reference(lv_obj_t *main_page)
{
    s_main_page = main_page;
}

void enable_screen_dimming(void)
{
    atomic_store(&s_dimming_enabled, true);
    if (s_inactivity_timer) {
        reset_s_inactivity_timer();
    }
}

void disable_screen_dimming(void)
{
    atomic_store(&s_dimming_enabled, false);
    if (s_inactivity_timer) {
        esp_timer_stop(s_inactivity_timer);
    }
    // Restore display when disabling dimming
    if (atomic_load(&s_screen_off)) {
        bsp_display_backlight_on();
        bsp_display_brightness_set(FULL_BRIGHTNESS);
        atomic_store(&s_screen_off, false);
        atomic_store(&s_screen_dimmed, false);
    } else if (atomic_load(&s_screen_dimmed)) {
        bsp_display_brightness_set(FULL_BRIGHTNESS);
        atomic_store(&s_screen_dimmed, false);
    }
}

void add_touch_event_handler(lv_obj_t *obj)
{
    lv_obj_add_event_cb(obj, touch_event_handler, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, touch_event_handler, LV_EVENT_RELEASED, NULL);
}
