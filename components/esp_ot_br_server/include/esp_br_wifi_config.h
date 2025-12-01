/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi Configuration and SoftAP API for ESP Thread Border Router
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Start WiFi configuration mode (SoftAP + Web server)
 *
 * This function will:
 * - Start a SoftAP with SSID "ESP-ThreadBR-XXXX" (XXXX is MAC address suffix)
 * - Start a Web server on http://192.168.4.1
 * - Start DNS server for captive portal support
 *
 * The SoftAP SSID will be logged automatically.
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_NO_MEM: Failed to allocate memory
 *      - ESP_FAIL: Failed to start SoftAP or Web server
 */
esp_err_t esp_br_wifi_config_start(void);

/**
 * @brief Get configured WiFi credentials (after user submits via web interface)
 *
 * This function waits for WiFi configuration to be completed by the user via web interface,
 * then returns the configured SSID and password.
 *
 * @param[out] ssid Buffer to store the configured WiFi SSID (can be NULL if not needed)
 * @param[in] ssid_len Maximum length of the ssid buffer
 * @param[out] password Buffer to store the configured WiFi password (can be NULL if not needed)
 * @param[in] password_len Maximum length of the password buffer
 * @param[in] timeout_ms Maximum time to wait in milliseconds (0 = wait forever)
 * @return
 *      - ESP_OK: WiFi configuration received and returned successfully
 *      - ESP_ERR_TIMEOUT: Timeout waiting for configuration
 *      - ESP_ERR_INVALID_STATE: WiFi config mode is not active
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 */
esp_err_t esp_br_wifi_config_get_configured_wifi(char *ssid, size_t ssid_len, char *password, size_t password_len,
                                                 uint32_t timeout_ms);

/**
 * @brief Stop WiFi configuration mode
 *
 * @return
 *      - ESP_OK: Success
 */
esp_err_t esp_br_wifi_config_stop(void);

/**
 * @brief Check if WiFi configuration mode is active
 *
 * @return true if WiFi config mode is active, false otherwise
 */
bool esp_br_wifi_config_is_active(void);

/**
 * @brief Get current SoftAP SSID and IP address
 *
 * This function can be used to get the SoftAP information when the mode is active.
 *
 * @param[out] ssid Buffer to store the SoftAP SSID (can be NULL if not needed)
 * @param[in] ssid_len Maximum length of the ssid buffer
 * @param[out] ip_addr Buffer to store the SoftAP IP address (can be NULL if not needed)
 * @param[in] ip_addr_len Maximum length of the ip_addr buffer
 * @return
 *      - ESP_OK: SoftAP info retrieved successfully
 *      - ESP_ERR_INVALID_STATE: SoftAP mode is not active
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 */
esp_err_t esp_br_wifi_config_get_softap_info(char *ssid, size_t ssid_len, char *ip_addr, size_t ip_addr_len);

#ifdef __cplusplus
}
#endif
