/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_netif.h"

#include <openthread/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OT_WIFI_DISCONNECTED,
    OT_WIFI_CONNECTED,
    OT_WIFI_RECONNECTING,
} esp_ot_wifi_state_t;

/**
 * @brief User command "wifi" process.
 *
 */
otError esp_ot_process_wifi_cmd(void *aContext, uint8_t aArgsLength, char *aArgs[]);

/**
 * @brief This function set or clear the border router initalization flag.
 *
 * @param[in]   initialized  border router is or not initalized.
 */
void esp_ot_wifi_border_router_init_flag_set(bool initialized);

/**
 * @brief This function connect the wifi.
 *
 * @param[in]   ssid        ssid of wifi
 * @param[in]   password    password of wifi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_connect(const char *ssid, const char *password);

/**
 * @brief This function initalize the nvs for wifi configurations.
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_init(void);

/**
 * @brief This function set the wifi ssid.
 *
 * @param[in]   ssid    pointer to ssid of wifi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_set_ssid(const char *ssid);

/**
 * @brief This function get the wifi ssid.
 *
 * @param[in]   password    pointer to password of wifi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_get_ssid(char *ssid);

/**
 * @brief This function set the wifi password.
 *
 * @param[in]   ssid    pointer to ssid of wifi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_set_password(const char *password);

/**
 * @brief This function get the wifi password.
 *
 * @param[in]   password    pointer to password of wifi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_get_password(char *password);

/**
 * @brief This function clear the wifi configurations stored in nvs.
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_clear(void);

#ifdef __cplusplus
}
#endif
