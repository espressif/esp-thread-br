/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread Command Line Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef ESP_OT_RCP_UPDATE_H
#define ESP_OT_RCP_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Attempts to update the RCP.
 *
 * @note If @p running_rcp_version is not NULL, the stored version will be
 *       compared with the running version to determine whether an update
 *       is required. If it is NULL, the stored version will be used to
 *       force an update.
 *
 * @note This function must be called after esp_rcp_update_init.
 *       This function may call esp_restart() if an update is performed.
 *
 * @param[in] running_rcp_version  The currently running RCP version.
 */
void esp_ot_try_update_rcp(const char *running_rcp_version);

/**
 * @brief Registers the handlers used to process RCP-related errors.
 *
 * @note The registered handlers rely on esp_rcp_update_init, and therefore
 *       esp_rcp_update_init must be called before any handler is triggered.
 *       However, the registration itself does not require a specific call order.
 *       This helper installs a set of default error handlers and requires no
 *       user parameters. For custom behavior, skip this helper and register
 *       your own handlers via the lower-level APIs instead.
 */
void esp_ot_register_rcp_handler(void);

/**
 * @brief Triggers an RCP update if the running RCP version does not match
 *        the version stored in flash.
 *
 * @note This function must be called after esp_rcp_update_init, which prepares
 *       the update flow, and after esp_openthread_init, which ensures the
 *       OpenThread stack and its RCP version API are available.
 */
void esp_ot_update_rcp_if_different(void);

#ifdef __cplusplus
}
#endif

#endif // ESP_OT_RCP_UPDATE_H
