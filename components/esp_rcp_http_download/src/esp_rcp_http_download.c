/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_rcp_http_download.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rcp_update.h"

#define DEFAULT_REQUEST_SIZE 64 * 1024
#define TAG "RCP_DOWNLOAD"
#define DOWNLOAD_BUFFER_SIZE 1024

static bool process_again(int status_code)
{
    switch (status_code) {
    case HttpStatus_MovedPermanently:
    case HttpStatus_Found:
    case HttpStatus_TemporaryRedirect:
    case HttpStatus_Unauthorized:
        return true;
    default:
        return false;
    }
    return false;
}

static esp_err_t _http_handle_response_code(esp_http_client_handle_t http_client, int status_code)
{
    esp_err_t err;
    if (status_code == HttpStatus_MovedPermanently || status_code == HttpStatus_Found ||
        status_code == HttpStatus_TemporaryRedirect) {
        err = esp_http_client_set_redirection(http_client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "URL redirection Failed");
            return err;
        }
    } else if (status_code == HttpStatus_Unauthorized) {
        esp_http_client_add_auth(http_client);
    } else if (status_code == HttpStatus_NotFound || status_code == HttpStatus_Forbidden) {
        ESP_LOGE(TAG, "File not found(%d)", status_code);
        return ESP_FAIL;
    } else if (status_code >= HttpStatus_BadRequest && status_code < HttpStatus_InternalError) {
        ESP_LOGE(TAG, "Client error (%d)", status_code);
        return ESP_FAIL;
    } else if (status_code >= HttpStatus_InternalError) {
        ESP_LOGE(TAG, "Server error (%d)", status_code);
        return ESP_FAIL;
    }

    char upgrade_data_buf[256];
    // process_again() returns true only in case of redirection.
    if (process_again(status_code)) {
        while (1) {
            /*
             *  In case of redirection, esp_http_client_read() is called
             *  to clear the response buffer of http_client.
             */
            int data_read = esp_http_client_read(http_client, upgrade_data_buf, sizeof(upgrade_data_buf));
            if (data_read <= 0) {
                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t _http_connect(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_FAIL;
    int status_code, header_ret;
    do {
        char *post_data = NULL;
        /* Send POST request if body is set.
         * Note: Sending POST request is not supported if partial_http_download
         * is enabled
         */
        int post_len = esp_http_client_get_post_field(http_client, &post_data);
        err = esp_http_client_open(http_client, post_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            return err;
        }
        if (post_len) {
            int write_len = 0;
            while (post_len > 0) {
                write_len = esp_http_client_write(http_client, post_data, post_len);
                if (write_len < 0) {
                    ESP_LOGE(TAG, "Write failed");
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
        err = _http_handle_response_code(http_client, status_code);
        if (err != ESP_OK) {
            return err;
        }
    } while (process_again(status_code));
    return err;
}

static void _http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static esp_err_t download_to_spiffs(esp_http_client_config_t *config, const char *firmware_dir, int8_t firmware_seq,
                                    const char *target_file)
{
    char target_path[RCP_FILENAME_MAX_SIZE];
    char target_url[RCP_URL_MAX_SIZE];
    bool download_done = false;
    int downloaded_length = 0;

    sprintf(target_path, "%s_%d%s", firmware_dir, firmware_seq, target_file);
    sprintf(target_url, "%s%s", config->url, target_file);

    ESP_LOGI(TAG, "Download %s to target file %s\n", target_url, target_path);
    esp_http_client_handle_t http_client = esp_http_client_init(config);
    ESP_RETURN_ON_FALSE(http_client != NULL, ESP_FAIL, TAG, "Failed to create HTTP client");
    esp_http_client_set_url(http_client, target_url);
    esp_err_t ret = ESP_OK;
    FILE *fp = fopen(target_path, "w");

    ESP_GOTO_ON_FALSE(fp != NULL, ESP_FAIL, exit, TAG, "Failed to open target file");
    ESP_GOTO_ON_ERROR(_http_connect(http_client), exit, TAG, "Failed to connect to HTTP server");

    while (!download_done) {
        char data_buf[DOWNLOAD_BUFFER_SIZE];
        int len = esp_http_client_read(http_client, data_buf, sizeof(data_buf));
        download_done = esp_http_client_is_complete_data_received(http_client);

        ESP_GOTO_ON_FALSE(len >= 0, ESP_FAIL, exit, TAG, "Failed to download");
        if (len == 0 && !download_done && (errno == ENOTCONN || errno == ECONNRESET || errno == ECONNABORTED)) {
            ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
            goto exit;
        } else if (len > 0) {
            ESP_GOTO_ON_FALSE(fwrite(data_buf, 1, len, fp) == len, ESP_FAIL, exit, TAG, "Failed to write data");
        }
        downloaded_length += len;
        ESP_LOGI(TAG, "Downloaded %d bytes", downloaded_length);
    }

exit:
    _http_cleanup(http_client);
    if (fp != NULL) {
        fclose(fp);
    }
    return ret;
}

esp_err_t esp_rcp_download_image(esp_http_client_config_t *http_config, const char *firmware_dir)
{
    ESP_RETURN_ON_FALSE(http_config != NULL, ESP_ERR_INVALID_ARG, TAG, "NULL http config");
    ESP_RETURN_ON_FALSE(http_config->url != NULL, ESP_ERR_INVALID_ARG, TAG, "NULL http url");

    int8_t update_seq = esp_rcp_get_next_update_seq();
    ESP_RETURN_ON_ERROR(download_to_spiffs(http_config, firmware_dir, update_seq, "/flash_args"), TAG,
                        "Failed to download flash_args");
    ESP_RETURN_ON_ERROR(download_to_spiffs(http_config, firmware_dir, update_seq, "/rcp_version"), TAG,
                        "Failed to download rcp_version");
    ESP_RETURN_ON_ERROR(download_to_spiffs(http_config, firmware_dir, update_seq, "/bt/bt.bin"), TAG,
                        "Failed to download bt.bin");
    ESP_RETURN_ON_ERROR(download_to_spiffs(http_config, firmware_dir, update_seq, "/pt/pt.bin"), TAG,
                        "Failed to download pt.bin");
    ESP_RETURN_ON_ERROR(download_to_spiffs(http_config, firmware_dir, update_seq, "/esp_ot_rcp.bin"), TAG,
                        "Failed to download esp_ot_rcp.bin");
    return esp_rcp_submit_new_image();
}
