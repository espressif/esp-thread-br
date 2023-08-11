/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_br_http_ota.h"
#include "esp_br_firmware.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_rcp_update.h"

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

static int http_read_for(esp_http_client_handle_t http_client, char *data, size_t size)
{
    int read_len = 0;
    while (read_len < size) {
        int len = http_client_read_check_connection(http_client, data, size - read_len);
        if (len < 0) {
            return read_len;
        }
        read_len += len;
    }
    return read_len;
}

static esp_err_t download_br_ota_firmware(esp_http_client_handle_t http_client, uint32_t br_firmware_size)
{
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_RETURN_ON_FALSE(update_partition != NULL, ESP_ERR_NOT_FOUND, TAG, "Failed to find ota partition");

    esp_ota_handle_t ota_handle;
    int ret = ESP_OK;
    ESP_GOTO_ON_ERROR(esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle), exit, TAG,
                      "Failed to start ota");
    ESP_LOGI(TAG, "Downloading Border Router firmware data...");
    bool download_done = false;
    uint32_t read_size = 0;
    while (!download_done) {
        int len = http_client_read_check_connection(http_client, s_download_data_buf, sizeof(s_download_data_buf));
        download_done = esp_http_client_is_complete_data_received(http_client);
        ESP_GOTO_ON_FALSE(len >= 0, ESP_FAIL, exit, TAG, "Failed to download");
        read_size += len;
        ESP_GOTO_ON_ERROR(esp_ota_write(ota_handle, s_download_data_buf, len), exit, TAG, "Failed to write ota");
        ESP_LOGI(TAG, "%lu/%lu bytes", read_size, br_firmware_size);
    }
    ESP_GOTO_ON_FALSE(read_size == br_firmware_size, ESP_FAIL, exit, TAG, "Incomplete firmware");

exit:
    if (ret == ESP_OK) {
        ret = esp_ota_end(ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to submit OTA");
        }
    } else {
        esp_ota_abort(ota_handle);
    }
    return ret;
}

static int write_file_for_length(FILE *fp, const void *buf, size_t size)
{
    static const int k_max_retry = 5;
    int retry_count = 0;
    int offset = 0;
    const uint8_t *data = (const uint8_t *)buf;
    while (offset < size) {
        int ret =
            fwrite(data + offset, 1, ((size - offset) < OTA_MAX_WRITE_SIZE ? (size - offset) : OTA_MAX_WRITE_SIZE), fp);
        if (ret < 0) {
            return ret;
        }
        if (ret == 0) {
            retry_count++;
        } else {
            offset += ret;
            retry_count = 0;
        }
        if (retry_count > k_max_retry) {
            return -1;
        }
    }
    return size;
}

static esp_err_t download_ota_image(esp_http_client_config_t *config, const char *rcp_firmware_dir,
                                    int8_t rcp_update_seq)
{
    char rcp_target_path[RCP_FILENAME_MAX_SIZE];
    bool download_done = false;

    sprintf(rcp_target_path, "%s_%d/" ESP_BR_RCP_IMAGE_FILENAME, rcp_firmware_dir, rcp_update_seq);

    ESP_LOGI(TAG, "Downloading %s, RCP target file %s\n", config->url, rcp_target_path);
    esp_http_client_handle_t http_client = esp_http_client_init(config);
    ESP_RETURN_ON_FALSE(http_client != NULL, ESP_FAIL, TAG, "Failed to create HTTP client");
    esp_err_t ret = ESP_OK;
    FILE *fp = fopen(rcp_target_path, "w");
    if (!fp) {
        ESP_LOGE(TAG, "Fail to open %s, will delete it and create a new one", rcp_target_path);
        remove(rcp_target_path);
        fp = fopen(rcp_target_path, "w");
    }

    ESP_GOTO_ON_FALSE(fp != NULL, ESP_FAIL, exit, TAG, "Failed to open target file");
    ESP_GOTO_ON_ERROR(_http_connect(http_client), exit, TAG, "Failed to connect to HTTP server");

    // First decide the br firmware offset
    uint32_t header_size = 0;
    uint32_t read_size = 0;
    uint32_t br_firmware_offset = 0;
    uint32_t br_firmware_size = 0;
    while (!download_done) {
        esp_br_subfile_info_t subfile_info;
        int len = http_read_for(http_client, (char *)&subfile_info, sizeof(subfile_info));
        ESP_LOGI(TAG, "subfile_info: tag 0x%lx size %lu offset %lu\n", subfile_info.tag, subfile_info.size,
                 subfile_info.offset);
        download_done = esp_http_client_is_complete_data_received(http_client);
        ESP_GOTO_ON_FALSE(len == sizeof(subfile_info), ESP_FAIL, exit, TAG, "Incomplete header");

        read_size += len;
        if (subfile_info.tag == FILETAG_IMAGE_HEADER) {
            header_size = subfile_info.size;
        } else if (subfile_info.tag == FILETAG_BR_FIRMWARE) {
            br_firmware_offset = subfile_info.offset;
            br_firmware_size = subfile_info.size;
        }
        ESP_GOTO_ON_FALSE(write_file_for_length(fp, &subfile_info, len) == len, ESP_FAIL, exit, TAG,
                          "Failed to write data");
        if (read_size >= header_size) {
            break;
        }
    }

    while (!download_done && read_size < br_firmware_offset) {
        int target_read_size = sizeof(s_download_data_buf) < br_firmware_offset - read_size
            ? sizeof(s_download_data_buf)
            : br_firmware_offset - read_size;
        int len = http_client_read_check_connection(http_client, s_download_data_buf, target_read_size);
        download_done = esp_http_client_is_complete_data_received(http_client);

        ESP_GOTO_ON_FALSE(len >= 0, ESP_FAIL, exit, TAG, "Failed to download");
        if (len > 0) {
            int r = write_file_for_length(fp, s_download_data_buf, len);
            if (r != len) {
                ESP_GOTO_ON_FALSE(r == len, ESP_FAIL, exit, TAG, "Failed to write OTA");
            }
        }
        read_size += len;
        ESP_LOGI(TAG, "Downloaded %lu bytes", read_size);
    }

    ESP_GOTO_ON_FALSE(read_size == br_firmware_offset, ESP_FAIL, exit, TAG, "Incomplete RCP image");
    ESP_GOTO_ON_ERROR(download_br_ota_firmware(http_client, br_firmware_size), exit, TAG,
                      "Failed to download OTA firmware");
    ESP_GOTO_ON_ERROR(esp_rcp_submit_new_image(), exit, TAG, "Failed to submit RCP image");
    ret = esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set new OTA boot partition");
        // Try to revert the RCP image submission. The RCP image update sequence will be set back to the original number
        // by calling esp_rcp_submit_new_image again.
        esp_rcp_submit_new_image();
    }

exit:
    _http_cleanup(http_client);
    if (fp != NULL) {
        fclose(fp);
    }
    return ret;
}

esp_err_t esp_br_http_ota(esp_http_client_config_t *http_config)
{
    const char *firmware_dir = esp_rcp_get_firmware_dir();
    ESP_RETURN_ON_FALSE(http_config != NULL, ESP_ERR_INVALID_ARG, TAG, "NULL http config");
    ESP_RETURN_ON_FALSE(http_config->url != NULL, ESP_ERR_INVALID_ARG, TAG, "NULL http url");

    int8_t rcp_update_seq = esp_rcp_get_next_update_seq();
    ESP_RETURN_ON_ERROR(download_ota_image(http_config, firmware_dir, rcp_update_seq), TAG,
                        "Failed to download ota image");
    return ESP_OK;
}
