/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "br_m5stack_layout.h"
#include "br_m5stack_common.h"
#include "br_m5stack_epskc_page.h"
#include "br_m5stack_screen_dimming.h"
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
#include "esp_br_wifi_config.h"
#endif

#include "esp_check.h"
#include "esp_err.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "core/lv_obj_tree.h"
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"
#endif

lv_obj_t *s_main_page = NULL;

lv_obj_t *br_m5stack_get_main_page(void)
{
    return s_main_page;
}

#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
// Forward declaration
static void br_m5stack_display_wifi_config_info(const char *ssid, const char *ip_addr);
static void br_m5stack_update_main_page_webgui(void);
static void br_m5stack_wifi_got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
#endif

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
    lv_obj_t *epskc_btn = NULL;
    lv_obj_t *factoryreset_btn = NULL;

    setup_inactivity_monitor();

    br_m5stack_add_esp_tiny_logo(lv_layer_top());

    s_main_page = br_m5stack_create_blank_page(lv_scr_act());
    ESP_GOTO_ON_FALSE(s_main_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the main page");

    set_main_page_reference(s_main_page);

    lv_obj_set_style_pad_all(s_main_page, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_main_page, 0, LV_PART_MAIN);

    epskc_btn = br_m5stack_epskc_ui_init(s_main_page);
    ESP_GOTO_ON_FALSE(epskc_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Ephemeral Key button for main page");
    br_m5stack_add_btn_to_page(s_main_page, epskc_btn, LV_ALIGN_CENTER, 0, -20);

    factoryreset_btn = br_m5stack_create_factoryreset_button(s_main_page);
    ESP_GOTO_ON_FALSE(factoryreset_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the factoryreset button for main page");
    br_m5stack_add_btn_to_page(s_main_page, factoryreset_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
    // Register IP event handler to update webgui when Wi-Fi connects
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, br_m5stack_wifi_got_ip_handler, NULL, NULL);

    if (esp_br_wifi_config_is_active()) {
        // SoftAP is active, get info and display SoftAP page
        char ssid[32];
        char ip_addr[16];
        if (esp_br_wifi_config_get_softap_info(ssid, sizeof(ssid), ip_addr, sizeof(ip_addr)) == ESP_OK) {
            br_m5stack_display_wifi_config_info(ssid, ip_addr);
        } else {
            // Fallback: hide main page if we can't get SoftAP info
            br_m5stack_hidden_page(s_main_page);
        }
    } else {
        // Normal mode, update main page with webgui
        br_m5stack_update_main_page_webgui();
    }
#endif

exit:
    ESP_ERROR_CHECK(ret);
}

#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
static lv_obj_t *s_wifi_config_page = NULL;
static lv_obj_t *s_webgui_label = NULL;

static void br_m5stack_update_main_page_webgui(void)
{
    char webgui_url[64];

    if (!s_main_page) {
        return;
    }

    bsp_display_lock(portMAX_DELAY);

    // Get Wi-Fi STA IP address for webgui
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
            snprintf(webgui_url, sizeof(webgui_url), "Web GUI: http://" IPSTR, IP2STR(&ip_info.ip));

            // Create or update webgui label
            if (!s_webgui_label) {
                s_webgui_label = br_m5stack_create_label(s_main_page, webgui_url, &lv_font_montserrat_16,
                                                         lv_color_make(200, 100, 0), LV_ALIGN_BOTTOM_MID, 0, -65);
            } else {
                br_m5stack_modify_label(s_webgui_label, webgui_url);
                lv_obj_set_style_text_color(s_webgui_label, lv_color_make(200, 100, 0), LV_STATE_DEFAULT);
            }
        }
    }

    bsp_display_unlock();
}

static void br_m5stack_wifi_got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;

    // Only update webgui if SoftAP is not active
    if (!esp_br_wifi_config_is_active()) {
        br_m5stack_update_main_page_webgui();
        // Show main page
        if (s_main_page) {
            br_m5stack_display_page(s_main_page);
        }
    }
}

void br_m5stack_show_main_page(void)
{
    if (s_main_page) {
        br_m5stack_update_main_page_webgui();
        br_m5stack_display_page(s_main_page);
    }
}

static void br_m5stack_display_wifi_config_info(const char *ssid, const char *ip_addr)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *title_label = NULL;
    lv_obj_t *ssid_label = NULL;
    lv_obj_t *ip_label = NULL;
    lv_obj_t *hint_label = NULL;
    char ssid_text[64];
    char ip_text[64];

    bsp_display_lock(portMAX_DELAY);

    // If ssid is NULL or SoftAP is not active, switch to main page
    if (ssid == NULL || !esp_br_wifi_config_is_active()) {
        // Hide SoftAP page
        if (s_wifi_config_page) {
            br_m5stack_hidden_page(s_wifi_config_page);
        }
        // Show main page and update webgui
        if (s_main_page) {
            br_m5stack_update_main_page_webgui();
            br_m5stack_display_page(s_main_page);
        }
        bsp_display_unlock();
        return;
    }

    // SoftAP is active, hide main page and show SoftAP config page
    if (s_main_page) {
        br_m5stack_hidden_page(s_main_page);
    }

    // Create or update WiFi config page
    if (!s_wifi_config_page) {
        s_wifi_config_page = br_m5stack_create_blank_page(lv_scr_act());
        ESP_GOTO_ON_FALSE(s_wifi_config_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create WiFi config page");
    }

    // Clear existing labels
    lv_obj_clean(s_wifi_config_page);

    // Create title
    title_label = br_m5stack_create_label(s_wifi_config_page, "Wi-Fi Configuration", &lv_font_montserrat_20,
                                          lv_color_make(255, 140, 0), LV_ALIGN_TOP_MID, 0, 40);
    ESP_GOTO_ON_FALSE(title_label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create title label");

    // Create SSID label
    snprintf(ssid_text, sizeof(ssid_text), "SSID: %s", ssid);
    ssid_label = br_m5stack_create_label(s_wifi_config_page, ssid_text, &lv_font_montserrat_18, lv_color_black(),
                                         LV_ALIGN_CENTER, 0, -30);
    ESP_GOTO_ON_FALSE(ssid_label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create SSID label");

    // Create IP label
    snprintf(ip_text, sizeof(ip_text), "IP: %s", ip_addr);
    ip_label = br_m5stack_create_label(s_wifi_config_page, ip_text, &lv_font_montserrat_18, lv_color_black(),
                                       LV_ALIGN_CENTER, 0, 0);
    ESP_GOTO_ON_FALSE(ip_label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create IP label");

    // Create hint label
    hint_label = br_m5stack_create_label(s_wifi_config_page, "Connect and visit in browser", &lv_font_montserrat_14,
                                         lv_color_make(100, 100, 100), LV_ALIGN_CENTER, 0, 30);
    ESP_GOTO_ON_FALSE(hint_label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create hint label");

    // Display the page
    br_m5stack_display_page(s_wifi_config_page);

exit:
    bsp_display_unlock();
    if (ret != ESP_OK) {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(s_wifi_config_page);
    }
}
#endif