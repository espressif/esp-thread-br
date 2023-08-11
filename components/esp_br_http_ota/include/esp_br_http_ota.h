/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief This function performs Border Router OTA by downloading from a HTTPS server.
 *
 * @param[in] http_config       The HTTP server download config
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 *  - ESP_ERR_INVALID_STASTE    If the RCP update is not initialized.
 *  - ESP_ERR_INVALID_ARG       If the http config is NULL or does not contain an url.
 *
 */
esp_err_t esp_br_http_ota(esp_http_client_config_t *http_config);

#define OTA_MAX_WRITE_SIZE 16

#ifdef __cplusplus
} /* extern "C" */
#endif
