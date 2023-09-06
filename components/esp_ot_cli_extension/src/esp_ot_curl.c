/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_curl.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_ot_cli_extension.h"
#include "http_parser.h"
#include "openthread/cli.h"

static char s_arg_buf[128];

static esp_err_t _http_handle_response_code(esp_http_client_handle_t http_client, int status_code)
{
    if (status_code == HttpStatus_NotFound || status_code == HttpStatus_Forbidden) {
        ESP_LOGE(OT_EXT_CLI_TAG, "File not found(%d)", status_code);
        return ESP_FAIL;
    } else if (status_code >= HttpStatus_BadRequest && status_code < HttpStatus_InternalError) {
        ESP_LOGE(OT_EXT_CLI_TAG, "Client error (%d)", status_code);
        return ESP_FAIL;
    } else if (status_code >= HttpStatus_InternalError) {
        ESP_LOGE(OT_EXT_CLI_TAG, "Server error (%d)", status_code);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t _http_connect(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_FAIL;
    int status_code, header_ret;
    char *post_data = NULL;
    /* Send POST request if body is set.
     * Note: Sending POST request is not supported if partial_http_download
     * is enabled
     */
    int post_len = esp_http_client_get_post_field(http_client, &post_data);
    err = esp_http_client_open(http_client, post_len);
    if (err != ESP_OK) {
        ESP_LOGE(OT_EXT_CLI_TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        return err;
    }
    if (post_len) {
        int write_len = 0;
        while (post_len > 0) {
            write_len = esp_http_client_write(http_client, post_data, post_len);
            if (write_len < 0) {
                ESP_LOGE(OT_EXT_CLI_TAG, "Write failed");
                return ESP_FAIL;
            }
            post_len -= write_len;
            post_data += write_len;
        }
    }
    header_ret = esp_http_client_fetch_headers(http_client);
    if (header_ret < 0) {
        return header_ret;
    }
    status_code = esp_http_client_get_status_code(http_client);
    return _http_handle_response_code(http_client, status_code);
}

static void curl_task(void *pvParameters)
{
    struct http_parser_url url;
    http_parser_parse_url(s_arg_buf, strnlen(s_arg_buf, sizeof(s_arg_buf)), 0, &url);
    esp_http_client_config_t config = {
        .url = s_arg_buf,
        .cert_pem = NULL,
        .event_handler = NULL,
        .keep_alive_enable = true,
    };

    if (strncmp(s_arg_buf, "https", 5) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.transport_type = HTTP_TRANSPORT_OVER_SSL;
        config.skip_cert_common_name_check = false;
    }

    esp_http_client_handle_t http_client = esp_http_client_init(&config);

    if (http_client == NULL) {
        ESP_LOGE(OT_EXT_CLI_TAG, "Failed to initialize HTTP client");
        vTaskDelete(NULL);
        return;
    }
    if (_http_connect(http_client) != ESP_OK) {
        ESP_LOGE(OT_EXT_CLI_TAG, "Failed to connect to HTTP server");
        vTaskDelete(NULL);
        return;
    }

    do {
        char data_buf[1024];
        int len = esp_http_client_read(http_client, data_buf, sizeof(data_buf));
        if (len < 0 || (len == 0 && (errno == ENOTCONN || errno == ECONNRESET || errno == ECONNABORTED))) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Connection closed, errno = %d", errno);
            vTaskDelete(NULL);
            return;
        }
        data_buf[len] = 0;
        printf(data_buf);
    } while (!esp_http_client_is_complete_data_received(http_client));

    printf("\n");
    esp_http_client_close(http_client);
    vTaskDelete(NULL);
    return;
}

otError esp_openthread_process_curl(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 0) {
        otCliOutputFormat("%s", "curl HTTP_URL\n");
        return OT_ERROR_INVALID_ARGS;
    } else {
        strcpy(s_arg_buf, aArgs[0]);
        if (pdPASS != xTaskCreate(curl_task, "curl", 8192, s_arg_buf, 4, NULL)) {
            return OT_ERROR_FAILED;
        }
        return OT_ERROR_NONE;
    }
}
