/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_br_http_ota.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_rcp_firmware.h"
#include "esp_rcp_ota.h"

#define DEFAULT_REQUEST_SIZE 64 * 1024
#define TAG "BR_OTA"
#define DOWNLOAD_BUFFER_SIZE 1024

static char s_download_data_buf[DOWNLOAD_BUFFER_SIZE];

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

static int http_client_read_check_connection(esp_http_client_handle_t client, char *data, size_t size)
{
    int len = esp_http_client_read(client, data, size);

    if (len == 0 && !esp_http_client_is_complete_data_received(client) &&
        (errno == ENOTCONN || errno == ECONNRESET || errno == ECONNABORTED)) {
        return -1;
    }
    return len;
}

static esp_err_t download_ota_image(esp_http_client_config_t *config)
{
    esp_err_t ret = ESP_OK;
    bool download_done = false;
    esp_rcp_ota_handle_t rcp_ota_handle = 0;
    esp_ota_handle_t host_ota_handle = 0;
    uint32_t br_fw_downloaded = 0;
    uint32_t br_fw_size = 0;
    char *br_fist_write_ptr = NULL;
    size_t br_first_write_size = 0;
    ESP_LOGI(TAG, "Downloading from %s\n", config->url);
    esp_http_client_handle_t http_client = esp_http_client_init(config);
    ESP_RETURN_ON_FALSE(http_client != NULL, ESP_FAIL, TAG, "Failed to create HTTP client");
    ESP_GOTO_ON_ERROR(esp_rcp_ota_begin(&rcp_ota_handle), exit, TAG, "Failed to begin RCP OTA");
    ESP_GOTO_ON_ERROR(_http_connect(http_client), exit, TAG, "Failed to connect to HTTP server");

    while (!download_done) {
        int len = http_client_read_check_connection(http_client, s_download_data_buf, sizeof(s_download_data_buf));
        size_t rcp_ota_received_len = 0;
        ESP_GOTO_ON_FALSE(len >= 0, ESP_FAIL, exit, TAG, "Failed to download");
        download_done = esp_http_client_is_complete_data_received(http_client);
        ESP_GOTO_ON_ERROR(esp_rcp_ota_receive(rcp_ota_handle, s_download_data_buf, len, &rcp_ota_received_len), exit,
                          TAG, "Failed to receive host RCP OTA data");
        if (esp_rcp_ota_get_state(rcp_ota_handle) == ESP_RCP_OTA_STATE_FINISHED) {
            br_fw_size = esp_rcp_ota_get_subfile_size(rcp_ota_handle, FILETAG_HOST_FIRMWARE);
            br_fist_write_ptr = &s_download_data_buf[rcp_ota_received_len];
            br_first_write_size = len - rcp_ota_received_len;
            break;
        }
    }
    if (br_fw_size > 0) {
        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
        ESP_GOTO_ON_FALSE(update_partition != NULL, ESP_ERR_NOT_FOUND, exit, TAG, "Failed to find ota partition");
        ESP_GOTO_ON_ERROR(esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &host_ota_handle), exit, TAG,
                          "Failed to begin host OTA");
        if (br_fist_write_ptr && br_first_write_size > 0) {
            ESP_GOTO_ON_ERROR(esp_ota_write(host_ota_handle, br_fist_write_ptr, br_first_write_size), exit, TAG,
                              "Failed to write ota");
            br_fw_downloaded += br_first_write_size;
            ESP_LOGD(TAG, "Border Router firmware download %lu/%lu bytes", br_fw_downloaded, br_fw_size);
        }
        while (!download_done && br_fw_downloaded < br_fw_size) {
            int len = http_client_read_check_connection(http_client, s_download_data_buf, sizeof(s_download_data_buf));
            download_done = esp_http_client_is_complete_data_received(http_client);
            ESP_GOTO_ON_FALSE(len >= 0, ESP_FAIL, exit, TAG, "Failed to download");
            br_fw_downloaded += len;
            ESP_GOTO_ON_ERROR(esp_ota_write(host_ota_handle, s_download_data_buf, len), exit, TAG,
                              "Failed to write ota");
            ESP_LOGD(TAG, "Border Router firmware download %lu/%lu bytes", br_fw_downloaded, br_fw_size);
        }
        ret = esp_ota_end(host_ota_handle);
        host_ota_handle = 0;
        ESP_GOTO_ON_ERROR(ret, exit, TAG, "Failed to end host OTA");
        ESP_GOTO_ON_ERROR(esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL)), exit, TAG,
                          "Failed to set boot partition");
    }
    ret = esp_rcp_ota_end(rcp_ota_handle);
    if (ret != ESP_OK) {
        // rollback the host boot partition when failing to end RCP OTA
        esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
    }
    rcp_ota_handle = 0;
exit:
    _http_cleanup(http_client);
    if (ret != ESP_OK && host_ota_handle) {
        esp_ota_abort(host_ota_handle);
    }
    if (ret != ESP_OK && rcp_ota_handle) {
        esp_rcp_ota_abort(rcp_ota_handle);
    }
    return ret;
}

esp_err_t esp_br_http_ota(esp_http_client_config_t *http_config)
{
    return download_ota_image(http_config);
}
