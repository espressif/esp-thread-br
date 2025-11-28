/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "br_m5stack_epskc_page.h"
#include "br_m5stack_common.h"
#include "br_m5stack_layout.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_ot_wifi_cmd.h"
#include "lvgl.h"
#include "nvs.h"
#include "qrcode.h"
#include "bsp/esp-bsp.h"
#include "misc/lv_area.h"
#include "openthread/border_agent.h"
#include "openthread/border_routing.h"
#include "openthread/dataset.h"
#include "openthread/dataset_ftd.h"
#include "openthread/error.h"
#include "openthread/thread.h"
#include "openthread/verhoeff_checksum.h"

static lv_obj_t *s_epskc_page = NULL;
_Atomic(lv_obj_t *) s_epskc_display_page;

static void delete_epskc_display_page(void *user_data)
{
    lv_obj_t *page = (lv_obj_t *)user_data;
    br_m5stack_delete_page(page);
    // Hide the main epskc page and show main page with webgui
    if (s_epskc_page) {
        br_m5stack_hidden_page(s_epskc_page);
    }
    br_m5stack_show_main_page();
}

static void br_m5stack_meshcop_e_remove_handler(void *args, esp_event_base_t base, int32_t event_id, void *data)
{
    lv_obj_t *page = NULL;
    page = atomic_exchange(&s_epskc_display_page, page);
    if (page) {
        lv_async_call(delete_epskc_display_page, (void *)page);
    }
}

static void epskc_exit_btn_handler(lv_event_t *e)
{
    // Stop ephemeral key and delete the display page
    lv_obj_t *page = NULL;
    page = atomic_exchange(&s_epskc_display_page, page);
    if (page) {
        esp_openthread_lock_acquire(portMAX_DELAY);
        otBorderAgentEphemeralKeyStop(esp_openthread_get_instance());
        esp_openthread_lock_release();
        delete_epskc_display_page(page);
    }
}

// Static variables for QR code display callback
static lv_img_dsc_t s_qrcode_img_dsc;
static int s_qrcode_scale = 3;

// Callback function to display QR code on LVGL image
static void qrcode_display_func(esp_qrcode_handle_t qrcode)
{
    int qr_width = esp_qrcode_get_size(qrcode);
    int qr_height = esp_qrcode_get_size(qrcode);
    int scale = s_qrcode_scale;
    int img_width = qr_width * scale;
    int img_height = qr_height * scale;

    if (s_qrcode_img_dsc.data) {
        heap_caps_free((void *)s_qrcode_img_dsc.data);
        s_qrcode_img_dsc.data = NULL;
    }

    // Calculate buffer size for RGB565 format (2 bytes per pixel)
    size_t buf_size = img_width * img_height * 2;
    uint16_t *buf = (uint16_t *)heap_caps_calloc(1, buf_size, MALLOC_CAP_DEFAULT);
    if (!buf) {
        ESP_LOGE(BR_M5STACK_TAG, "Failed to allocate image buffer");
        return;
    }

    // Fill with white (0xFFFF for RGB565)
    memset(buf, 0xFF, buf_size);

    // Draw QR code pixels (black = 0x0000 for RGB565)
    for (int y = 0; y < qr_height; y++) {
        for (int x = 0; x < qr_width; x++) {
            if (esp_qrcode_get_module(qrcode, x, y)) {
                // Draw black pixel (scaled)
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x * scale + sx;
                        int py = y * scale + sy;
                        if (px < img_width && py < img_height) {
                            buf[py * img_width + px] = 0x0000; // Black
                        }
                    }
                }
            }
        }
    }

    // Update image descriptor
    s_qrcode_img_dsc.header.w = img_width;
    s_qrcode_img_dsc.header.h = img_height;
    s_qrcode_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_qrcode_img_dsc.data_size = buf_size;
    s_qrcode_img_dsc.data = (const uint8_t *)buf;
}

static lv_obj_t *create_qrcode_image(lv_obj_t *parent, const char *text, int scale)
{
    // Initialize image descriptor
    memset(&s_qrcode_img_dsc, 0, sizeof(lv_img_dsc_t));
    s_qrcode_img_dsc.header.always_zero = 0;
    s_qrcode_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;

    // Set static variables for callback
    s_qrcode_scale = scale;

    // Configure QR code
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.max_qrcode_version = 5;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    cfg.display_func = qrcode_display_func;

    // Generate QR code (callback will allocate s_qrcode_img_dsc.data if successful)
    ESP_RETURN_ON_FALSE(esp_qrcode_generate(&cfg, text) == ESP_OK, NULL, BR_M5STACK_TAG, "Failed to generate QR code");

    // Create image object
    lv_obj_t *img = lv_img_create(parent);
    ESP_RETURN_ON_FALSE(img != NULL, NULL, BR_M5STACK_TAG, "Failed to create image object");

    lv_img_set_src(img, &s_qrcode_img_dsc);

    return img;
}

static void create_ephemeral_key_page(char *key_txt)
{
    esp_err_t ret = ESP_OK;
    br_m5stack_err_msg_t err_msg = "";
    lv_obj_t *page = NULL;
    lv_obj_t *label = NULL;
    lv_obj_t *qrcode_img = NULL;
    lv_obj_t *exit_btn = NULL;
    otError err = OT_ERROR_NONE;
    char txt[13] = "";

    esp_openthread_lock_acquire(portMAX_DELAY);
    err = otBorderAgentEphemeralKeyStart(esp_openthread_get_instance(), key_txt,
                                         CONFIG_OPENTHREAD_EPHEMERALKEY_LIFE_TIME * 1000,
                                         CONFIG_OPENTHREAD_EPHEMERALKEY_PORT);
    esp_openthread_lock_release();

    BR_M5STACK_GOTO_ON_FALSE(err == OT_ERROR_NONE, exit, "Fail to apply ephemeral key");
    page = br_m5stack_create_blank_page(s_epskc_page);
    ESP_GOTO_ON_FALSE(page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the page to display Ephemeral Key");
    atomic_store(&s_epskc_display_page, (lv_obj_t *)page);

    // Create QR code image (display key text in QR code)
    qrcode_img = create_qrcode_image(page, key_txt, 4);
    ESP_GOTO_ON_FALSE(qrcode_img, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create QR code image");
    lv_obj_align(qrcode_img, LV_ALIGN_CENTER, 0, -30);

    // Create label to display key text
    snprintf(txt, 13, "%.3s-%.3s-%.3s", key_txt, key_txt + 3, key_txt + 6);
    label = br_m5stack_create_label(page, txt, &lv_font_montserrat_32, lv_color_black(), LV_ALIGN_CENTER, 0, 35);
    ESP_GOTO_ON_FALSE(label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the label of Ephemeral Key");

    exit_btn = br_m5stack_create_button(100, 50, epskc_exit_btn_handler, LV_EVENT_CLICKED, "exit", NULL);
    ESP_GOTO_ON_FALSE(exit_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the exit button");
    br_m5stack_add_btn_to_page(page, exit_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

exit:
    if (ret == ESP_OK) {
        BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
    } else {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(page);
    }
}

static bool generate_ephemeral_key(char *key_buf, bool random)
{
    char checksum;
    bool is_generated = true;

    if (random) {
        for (size_t i = 0; i < 8; ++i) {
            uint32_t rand_num = esp_random();
            key_buf[i] = '0' + (rand_num % 10);
        }
        key_buf[8] = '\0';
    } else {
        if (strlen(key_buf) != 8) {
            return false;
        }
    }
    is_generated = (otVerhoeffChecksumCalculate(key_buf, &checksum) == OT_ERROR_NONE);
    key_buf[8] = checksum;
    key_buf[9] = '\0';

    return is_generated;
}

static void br_m5stack_epskc_page_display(lv_event_t *e)
{
    esp_err_t error = ESP_FAIL;
    br_m5stack_err_msg_t err_msg = "";
    otInstance *instance = NULL;
    esp_ot_wifi_state_t wifi_state = OT_WIFI_DISCONNECTED;
    otDeviceRole thread_role = OT_DEVICE_ROLE_DISABLED;
    otBorderRoutingState br_state = OT_BORDER_ROUTING_STATE_UNINITIALIZED;
    char random_digits[10];

    esp_openthread_lock_acquire(portMAX_DELAY);
    instance = esp_openthread_get_instance();
    wifi_state = esp_ot_wifi_state_get();
    BR_M5STACK_GOTO_ON_FALSE(wifi_state == OT_WIFI_CONNECTED, exit, "Wi-Fi is not connected");
    thread_role = otThreadGetDeviceRole(instance);
    BR_M5STACK_GOTO_ON_FALSE(thread_role != OT_DEVICE_ROLE_DISABLED && thread_role != OT_DEVICE_ROLE_DETACHED, exit,
                             (thread_role == OT_DEVICE_ROLE_DETACHED)
                                 ? "Thread is connecting..\nPlease wait"
                                 : "Thread is not connected\nPlease check the state");
    br_state = otBorderRoutingGetState(esp_openthread_get_instance());
    BR_M5STACK_GOTO_ON_FALSE(br_state == OT_BORDER_ROUTING_STATE_RUNNING, exit, "Border router is not running");
    error = ESP_OK;

exit:
    esp_openthread_lock_release();
    if (error == ESP_OK) {
        // Display the page first
        br_m5stack_display_page(s_epskc_page);
        // Generate random key and display it on the page
        BR_M5STACK_GOTO_ON_FALSE(generate_ephemeral_key(random_digits, true), exit2,
                                 "Failed to generate ephemeral key");
        create_ephemeral_key_page(random_digits);
    } else {
        BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
    }
    return;

exit2:
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

lv_obj_t *br_m5stack_epskc_ui_init(lv_obj_t *page)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *epskc_page_btn = NULL;

    esp_openthread_lock_acquire(portMAX_DELAY);
    esp_openthread_register_meshcop_e_handler(br_m5stack_meshcop_e_remove_handler, false);
    esp_openthread_lock_release();

    s_epskc_page = br_m5stack_create_blank_page(page);
    ESP_GOTO_ON_FALSE(s_epskc_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Ephemeral Key page");

    epskc_page_btn = br_m5stack_create_button(160, 70, br_m5stack_epskc_page_display, LV_EVENT_CLICKED,
                                              "Share Thread Network Credential", NULL);
    ESP_GOTO_ON_FALSE(epskc_page_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Share Thread Network Credential button");
    // Set button background color to orange and text color to black
    lv_obj_set_style_bg_color(epskc_page_btn, lv_color_make(255, 140, 0), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(epskc_page_btn, lv_color_make(255, 120, 0), LV_STATE_PRESSED);
    lv_obj_t *btn_label = lv_obj_get_child(epskc_page_btn, 0);
    if (btn_label) {
        lv_obj_set_style_text_color(btn_label, lv_color_black(), LV_STATE_DEFAULT);
    }

exit:
    if (ret != ESP_OK) {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(epskc_page_btn);
        BR_M5STACK_DELETE_OBJ_IF_EXIST(s_epskc_page);
    }
    return epskc_page_btn;
}
