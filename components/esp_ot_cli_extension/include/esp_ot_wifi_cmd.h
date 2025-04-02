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
 * @brief This function sets or clears the border router initalization flag.
 *
 * @param[in]   initialized  border router is or not initalized.
 */
void esp_ot_wifi_border_router_init_flag_set(bool initialized);

/**
 * @brief This function connects to Wi-Fi.
 *
 * @param[in]   ssid        ssid of Wi-Fi
 * @param[in]   password    password of Wi-Fi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_connect(const char *ssid, const char *password);

/**
 * @brief This function disconnects Wi-Fi.
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_disconnect(void);

/**
 * @brief This function initializes the nvs for Wi-Fi configurations.
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_init(void);

/**
 * @brief This function sets the Wi-Fi ssid.
 *
 * @param[in]   ssid    pointer to ssid of Wi-Fi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_set_ssid(const char *ssid);

/**
 * @brief This function gets the Wi-Fi ssid.
 *
 * @param[in]   password    pointer to password of Wi-Fi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_get_ssid(char *ssid);

/**
 * @brief This function sets the Wi-Fi password.
 *
 * @param[in]   ssid    pointer to ssid of Wi-Fi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_set_password(const char *password);

/**
 * @brief This function gets the Wi-Fi password.
 *
 * @param[in]   password    pointer to password of Wi-Fi
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_get_password(char *password);

/**
 * @brief This function clears the Wi-Fi configurations stored in nvs.
 *
 * @return
 *      - ESP_OK on success
 *      - others on failure
 */
esp_err_t esp_ot_wifi_config_clear(void);

/**
 * @brief This function gets the Wi-Fi state.
 *
 * @return
 *      OT_WIFI_DISCONNECTED if Wi-Fi is disconnected
 *      OT_WIFI_CONNECTED if Wi-Fi is connected
 *      OT_WIFI_RECONNECTING if Wi-Fi is reconnecting
 */
esp_ot_wifi_state_t esp_ot_wifi_state_get(void);

#ifdef __cplusplus
}
#endif
