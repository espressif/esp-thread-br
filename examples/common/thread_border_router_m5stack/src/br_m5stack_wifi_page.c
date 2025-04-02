/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "br_m5stack_wifi_page.h"
#include "br_m5stack_common.h"
#include "br_m5stack_layout.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_task_queue.h"
#include "esp_ot_wifi_cmd.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "protocol_examples_common.h"
#include <string.h>
#include "bsp/esp-bsp.h"
#include "misc/lv_area.h"

#define SSIDMAXLEN 32
#define PASSWORDMAXLEN 64

typedef struct wifi_msg_label {
    lv_obj_t *state;
    lv_obj_t *ssid;
    lv_obj_t *password;
    lv_obj_t *webserver;
} wifi_msg_label_t;

static wifi_msg_label_t s_wifi_msg_label;
esp_ot_wifi_state_t s_wifi_state = OT_WIFI_DISCONNECTED;
const char s_wifi_state_msg[4][30] = {"disconnected", "connected", "reconnecting..."};
static lv_obj_t *s_wifi_page = NULL;
static lv_obj_t *s_wifi_connect_page = NULL;
static char s_web_server[40] = "";
static uint8_t set_ssid = 1;
static uint8_t set_password = 2;
static char s_wifi_ssid[32] = "";
static char s_wifi_password[64] = "";

static void br_m5stack_wifi_update_state(void *user_data)
{
    char txt[24] = "";
    snprintf(txt, sizeof(txt), "State: %s", s_wifi_state_msg[s_wifi_state]);
    br_m5stack_modify_label(s_wifi_msg_label.state, txt);
}

static char *get_web_server(void)
{
    char wifi_ipv4_address[16] = "";
    strcpy(s_web_server, "None webserver");
    esp_netif_t *netif = esp_openthread_get_backbone_netif();
    if (netif != NULL && s_wifi_state == OT_WIFI_CONNECTED) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(netif, &ip_info);
        sprintf(wifi_ipv4_address, IPSTR, IP2STR(&ip_info.ip));
        sprintf(s_web_server, "http://%s:80/index.html", wifi_ipv4_address);
    }
    return s_web_server;
}

static void br_m5stack_wifi_update_web_server(void *user_data)
{
    char *txt = (char *)user_data;
    br_m5stack_modify_label(s_wifi_msg_label.webserver, txt);
}

static void handle_wifi_disconnect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (s_wifi_state != OT_WIFI_DISCONNECTED) {
        s_wifi_state = OT_WIFI_DISCONNECTED;
        lv_async_call(br_m5stack_wifi_update_state, (void *)s_wifi_state);
    }
}

static void handle_wifi_connect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (s_wifi_state != OT_WIFI_CONNECTED) {
        s_wifi_state = OT_WIFI_CONNECTED;
        lv_async_call(br_m5stack_wifi_update_state, (void *)s_wifi_state);
    }
}

static void handle_wifi_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    sprintf(s_web_server, "http://%d.%d.%d.%d:80/index.html", IP2STR(&event->ip_info.ip));
    lv_async_call(br_m5stack_wifi_update_web_server, s_web_server);
}

static void handle_wifi_lost_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    sprintf(s_web_server, "None web server");
    lv_async_call(br_m5stack_wifi_update_web_server, s_web_server);
}

static void wifi_connect_result_handler(void *user_data)
{
    esp_err_t error = (esp_err_t)user_data;
    if (s_wifi_connect_page) {
        br_m5stack_delete_page(s_wifi_connect_page);
        s_wifi_connect_page = NULL;
    }
    if (error != ESP_OK) {
        br_m5stack_create_warn("Fail to connect to wifi", 1000);
    }
}

static void wifi_connect_task(void *ctx)
{
    esp_err_t error = ESP_OK;

    error = esp_ot_wifi_connect(s_wifi_ssid, s_wifi_password);
    if (error == ESP_OK && esp_openthread_get_backbone_netif() == NULL) {
        esp_openthread_set_backbone_netif(get_example_netif());
        error = esp_openthread_border_router_init();
        esp_ot_wifi_border_router_init_flag_set(true);
    }
    lv_async_call(wifi_connect_result_handler, (void *)error);
}

static void br_m5stack_wifi_connect(lv_event_t *e)
{
    br_m5stack_err_msg_t err_msg = "";
    esp_ot_wifi_state_t tmp_state = OT_WIFI_DISCONNECTED;
    esp_err_t error = ESP_FAIL;
    s_wifi_ssid[0] = '\0';
    s_wifi_password[0] = '\0';

    esp_openthread_lock_acquire(portMAX_DELAY);
    tmp_state = esp_ot_wifi_state_get();
    BR_M5STACK_GOTO_ON_FALSE(tmp_state == OT_WIFI_DISCONNECTED, exit, "Wifi is %s\ncannot connect repeatedly",
                             s_wifi_state_msg[tmp_state]);
    error = esp_ot_wifi_config_get_ssid(s_wifi_ssid);
    BR_M5STACK_GOTO_ON_FALSE(error == ESP_OK && strlen(s_wifi_ssid) > 0, exit, "None ssid for wifi");
    esp_ot_wifi_config_get_password(s_wifi_password);

    error = esp_openthread_task_queue_post(wifi_connect_task, NULL);
    BR_M5STACK_GOTO_ON_FALSE(error == ESP_OK, exit, "Fail to connect to wifi1");

exit:
    esp_openthread_lock_release();
    if (error == ESP_OK) {
        s_wifi_connect_page = br_m5stack_create_warn("Wi-Fi connecting...", 0);
    }
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

static void br_m5stack_wifi_disconnect(lv_event_t *e)
{
    esp_openthread_lock_acquire(portMAX_DELAY);
    esp_ot_wifi_disconnect();
    esp_openthread_lock_release();
}

static void br_m5stack_wifi_update_config(void)
{
    char ssid[SSIDMAXLEN] = "";
    char password[PASSWORDMAXLEN] = "";
    char ssid_txt[SSIDMAXLEN + 8] = "";
    char password_txt[PASSWORDMAXLEN + 16] = "";

    esp_openthread_lock_acquire(portMAX_DELAY);
    esp_ot_wifi_config_get_ssid(ssid);
    esp_ot_wifi_config_get_password(password);
    esp_openthread_lock_release();

    snprintf(ssid_txt, sizeof(ssid_txt), "SSID: %s", ssid);
    snprintf(password_txt, sizeof(password_txt), "Password: %s", password);

    br_m5stack_modify_label(s_wifi_msg_label.ssid, ssid_txt);
    br_m5stack_modify_label(s_wifi_msg_label.password, password_txt);
}

static void br_m5stack_wifi_set_config_callback(const char *input, void *user_data)
{
    esp_err_t error = ESP_OK;
    uint8_t flag = *(uint8_t *)user_data;
    br_m5stack_err_msg_t err_msg = "";

    esp_openthread_lock_acquire(portMAX_DELAY);
    if (flag == set_ssid) {
        BR_M5STACK_GOTO_ON_FALSE(strlen(input) < SSIDMAXLEN, exit, "SSID too long");
        error = esp_ot_wifi_config_set_ssid(input);
        BR_M5STACK_GOTO_ON_FALSE(error == ESP_OK, exit, "Failed to set SSID");
    } else if (flag == set_password) {
        BR_M5STACK_GOTO_ON_FALSE(strlen(input) < PASSWORDMAXLEN, exit, "Password too long");
        error = esp_ot_wifi_config_set_password(input);
        BR_M5STACK_GOTO_ON_FALSE(error == ESP_OK, exit, "Failed to set password");
    }

exit:
    esp_openthread_lock_release();
    br_m5stack_wifi_update_config();
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

static void br_m5stack_wifi_set_config(lv_event_t *e)
{
    br_m5stack_err_msg_t err_msg = "";

    esp_openthread_lock_acquire(portMAX_DELAY);
    esp_ot_wifi_state_t tmp_state = esp_ot_wifi_state_get();
    esp_openthread_lock_release();

    BR_M5STACK_GOTO_ON_FALSE(tmp_state == OT_WIFI_DISCONNECTED, exit, "Wi-Fi is %s\ndisconnect it before setting",
                             s_wifi_state_msg[tmp_state]);

    br_m5stack_create_keyboard(lv_layer_top(), br_m5stack_wifi_set_config_callback, lv_event_get_user_data(e), false);

exit:
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

static void br_m5stack_wifi_refresh(lv_event_t *e)
{
    br_m5stack_wifi_update_config();
    br_m5stack_wifi_update_web_server(get_web_server());
    br_m5stack_wifi_update_state((void *)s_wifi_state);
}

lv_obj_t *br_m5stack_wifi_ui_init(lv_obj_t *page)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *con_btn = NULL;
    lv_obj_t *discon_btn = NULL;
    lv_obj_t *set_ssid_btn = NULL;
    lv_obj_t *set_pskc_btn = NULL;
    lv_obj_t *refresh_btn = NULL;
    lv_obj_t *exit_btn = NULL;
    lv_obj_t *wifi_page_btn = NULL;
    memset(&s_wifi_msg_label, 0, sizeof(wifi_msg_label_t));

    s_wifi_page = br_m5stack_create_blank_page(page);
    ESP_GOTO_ON_FALSE(s_wifi_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi page");

    s_wifi_msg_label.ssid = br_m5stack_create_label(s_wifi_page, "SSID: ", &lv_font_montserrat_16, lv_color_black(),
                                                    LV_ALIGN_TOP_MID, 0, 20);
    ESP_GOTO_ON_FALSE(s_wifi_msg_label.ssid, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi SSID label");

    s_wifi_msg_label.password = br_m5stack_create_label(s_wifi_page, "Password: ", &lv_font_montserrat_16,
                                                        lv_color_black(), LV_ALIGN_TOP_MID, 0, 40);
    ESP_GOTO_ON_FALSE(s_wifi_msg_label.password, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Wi-Fi password label");

    s_wifi_msg_label.state = br_m5stack_create_label(s_wifi_page, "State: ", &lv_font_montserrat_16, lv_color_black(),
                                                     LV_ALIGN_TOP_MID, 0, 60);
    ESP_GOTO_ON_FALSE(s_wifi_msg_label.state, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi state label");

    s_wifi_msg_label.webserver = br_m5stack_create_label(s_wifi_page, "None webserver", &lv_font_montserrat_16,
                                                         lv_color_black(), LV_ALIGN_TOP_MID, 0, 80);
    ESP_GOTO_ON_FALSE(s_wifi_msg_label.webserver, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the web server label");

    esp_openthread_lock_acquire(portMAX_DELAY);
    s_wifi_state = (esp_ot_wifi_state_get() == OT_WIFI_CONNECTED) ? OT_WIFI_CONNECTED : OT_WIFI_DISCONNECTED;
    esp_openthread_lock_release();
    br_m5stack_wifi_refresh(NULL);

    con_btn = br_m5stack_create_button(100, 50, br_m5stack_wifi_connect, LV_EVENT_CLICKED, "connect", NULL);
    ESP_GOTO_ON_FALSE(con_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi connection button");
    br_m5stack_add_btn_to_page(s_wifi_page, con_btn, LV_ALIGN_BOTTOM_LEFT, 0, -58);

    discon_btn = br_m5stack_create_button(100, 50, br_m5stack_wifi_disconnect, LV_EVENT_CLICKED, "disconnect", NULL);
    ESP_GOTO_ON_FALSE(discon_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi disconnection button");
    br_m5stack_add_btn_to_page(s_wifi_page, discon_btn, LV_ALIGN_BOTTOM_RIGHT, 0, -58);

    set_ssid_btn =
        br_m5stack_create_button(100, 50, br_m5stack_wifi_set_config, LV_EVENT_CLICKED, "set ssid", (void *)&set_ssid);
    ESP_GOTO_ON_FALSE(set_ssid_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi SSID setting button");
    br_m5stack_add_btn_to_page(s_wifi_page, set_ssid_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    set_pskc_btn = br_m5stack_create_button(100, 50, br_m5stack_wifi_set_config, LV_EVENT_CLICKED, "set password",
                                            (void *)&set_password);
    ESP_GOTO_ON_FALSE(set_pskc_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Wi-Fi password setting button");
    br_m5stack_add_btn_to_page(s_wifi_page, set_pskc_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    refresh_btn = br_m5stack_create_button(75, 40, br_m5stack_wifi_refresh, LV_EVENT_CLICKED, "refresh", NULL);
    ESP_GOTO_ON_FALSE(refresh_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi refresh button");
    br_m5stack_add_btn_to_page(s_wifi_page, refresh_btn, LV_ALIGN_BOTTOM_MID, 0, -33);

    exit_btn = br_m5stack_create_button(40, 25, br_m5stack_hidden_page_from_button, LV_EVENT_CLICKED, "exit", NULL);
    ESP_GOTO_ON_FALSE(exit_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi exit button");
    br_m5stack_add_btn_to_page(s_wifi_page, exit_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    br_m5stack_hidden_page(s_wifi_page);

    wifi_page_btn = br_m5stack_create_button(100, 60, br_m5stack_display_page_from_button, LV_EVENT_CLICKED, "Wi-Fi",
                                             (void *)s_wifi_page);
    ESP_GOTO_ON_FALSE(wifi_page_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Wi-Fi page button");

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &handle_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &handle_wifi_connect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handle_wifi_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &handle_wifi_lost_ip, NULL));

exit:
    if (ret != ESP_OK) {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(wifi_page_btn);
        BR_M5STACK_DELETE_OBJ_IF_EXIST(s_wifi_page);
    }
    return wifi_page_btn;
}
