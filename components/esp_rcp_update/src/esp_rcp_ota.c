/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_rcp_firmware.h>
#include <esp_rcp_ota.h>
#include <esp_rcp_update.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>

#define IMAGE_HEADER_MAX_LEN sizeof(esp_rcp_subfile_info_t) * MAX_SUBFILE_INFO
#define OTA_MAX_WRITE_SIZE 16

typedef struct rcp_ota_entry_ {
    esp_rcp_ota_handle_t handle;
    esp_rcp_ota_state_t state;
    uint32_t header_size;
    uint32_t header_read;
    uint8_t image_header_buffer[IMAGE_HEADER_MAX_LEN];
    uint32_t rcp_firmware_size;
    uint32_t rcp_firmware_downloaded;
    FILE *rcp_fp;
    LIST_ENTRY(rcp_ota_entry_) entries;
} rcp_ota_entry_t;

static LIST_HEAD(rcp_ota_entries_head,
                 rcp_ota_entry_) s_rcp_ota_entries_head = LIST_HEAD_INITIALIZER(s_rcp_ota_entries_head);

static esp_rcp_ota_handle_t s_ota_last_handle = 0;

const static char *TAG = "esp_rcp_ota";

esp_err_t esp_rcp_ota_begin(esp_rcp_ota_handle_t *out_handle)
{
    rcp_ota_entry_t *new_entry = NULL;
    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "out_handle cannot be NULL");

    new_entry = (rcp_ota_entry_t *)calloc(1, sizeof(rcp_ota_entry_t));
    ESP_RETURN_ON_FALSE(new_entry, ESP_ERR_NO_MEM, TAG, "Failed to allocate memory for RCP OTA handle");
    LIST_INSERT_HEAD(&s_rcp_ota_entries_head, new_entry, entries);

    new_entry->state = ESP_RCP_OTA_STATE_READ_HEADER;
    new_entry->header_size = 0;
    new_entry->header_read = 0;
    new_entry->rcp_firmware_size = 0;
    new_entry->rcp_firmware_downloaded = 0;
    new_entry->rcp_fp = NULL;
    memset(new_entry->image_header_buffer, 0, sizeof(new_entry->image_header_buffer));
    new_entry->handle = ++s_ota_last_handle;
    *out_handle = new_entry->handle;
    return ESP_OK;
}

static rcp_ota_entry_t *find_esp_rcp_ota_entry(esp_rcp_ota_handle_t handle)
{
    rcp_ota_entry_t *ret = NULL;
    for (ret = LIST_FIRST(&s_rcp_ota_entries_head); ret != NULL; ret = LIST_NEXT(ret, entries)) {
        if (ret->handle == handle) {
            return ret;
        }
    }
    return NULL;
}

esp_rcp_ota_state_t esp_rcp_ota_get_state(esp_rcp_ota_handle_t handle)
{
    rcp_ota_entry_t *entry = find_esp_rcp_ota_entry(handle);
    return entry ? entry->state : ESP_RCP_OTA_STATE_INVALID;
}

uint32_t esp_rcp_ota_get_subfile_size(esp_rcp_ota_handle_t handle, esp_rcp_filetag_t tag)
{
    rcp_ota_entry_t *entry = find_esp_rcp_ota_entry(handle);
    if (!entry || entry->header_size % sizeof(esp_rcp_subfile_info_t) != 0) {
        return 0;
    }
    size_t subfile_info_num = entry->header_size / sizeof(esp_rcp_subfile_info_t);
    for (size_t i = 0; i < subfile_info_num; ++i) {
        esp_rcp_subfile_info_t *subfile_info =
            (esp_rcp_subfile_info_t *)(&entry->image_header_buffer[i * sizeof(esp_rcp_subfile_info_t)]);
        if (subfile_info->tag == tag) {
            return subfile_info->size;
        }
    }
    return 0;
}

static void parse_image_header(rcp_ota_entry_t *entry)
{
    size_t subfile_info_num = entry->header_size / sizeof(esp_rcp_subfile_info_t);
    for (size_t i = 0; i < subfile_info_num; ++i) {
        esp_rcp_subfile_info_t *subfile_info =
            (esp_rcp_subfile_info_t *)(&entry->image_header_buffer[i * sizeof(esp_rcp_subfile_info_t)]);
        if (subfile_info->tag == FILETAG_IMAGE_HEADER || subfile_info->tag == FILETAG_RCP_VERSION ||
            subfile_info->tag == FILETAG_RCP_BOOTLOADER || subfile_info->tag == FILETAG_RCP_FLASH_ARGS ||
            subfile_info->tag == FILETAG_RCP_PARTITION_TABLE || subfile_info->tag == FILETAG_RCP_FIRMWARE) {
            entry->rcp_firmware_size += subfile_info->size;
        }
    }
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

static esp_err_t receive_header(const uint8_t *data, size_t size, rcp_ota_entry_t *entry, size_t *consumed_size)
{
    if (entry->header_size == 0) {
        if (entry->header_read < sizeof(esp_rcp_subfile_info_t)) {
            size_t copy_size = size > sizeof(esp_rcp_subfile_info_t) - entry->header_read
                ? sizeof(esp_rcp_subfile_info_t) - entry->header_read
                : size;
            memcpy(entry->image_header_buffer + entry->header_read, data, copy_size);
            data = data + copy_size;
            size -= copy_size;
            entry->header_read += copy_size;
            *consumed_size += copy_size;
        }
        if (entry->header_read >= sizeof(esp_rcp_subfile_info_t)) {
            esp_rcp_subfile_info_t *subfile_info = (esp_rcp_subfile_info_t *)(entry->image_header_buffer);
            if (subfile_info->tag != FILETAG_IMAGE_HEADER || subfile_info->offset != 0 ||
                subfile_info->size % sizeof(esp_rcp_subfile_info_t) != 0) {
                ESP_LOGE(TAG, "Invalid image header");
                return ESP_ERR_INVALID_ARG;
            } else {
                entry->header_size = subfile_info->size;
            }
        }
    }

    if (entry->header_size > 0 && entry->header_read < entry->header_size) {
        size_t copy_size =
            size > entry->header_size - entry->header_read ? entry->header_size - entry->header_read : size;
        memcpy(entry->image_header_buffer + entry->header_read, data, copy_size);
        data += copy_size;
        size -= copy_size;
        entry->header_read += copy_size;
        *consumed_size += copy_size;
    }
    if (entry->header_size > 0 && entry->header_read >= entry->header_size) {
        parse_image_header(entry);
        if (entry->rcp_firmware_size > 0) {
            entry->state = ESP_RCP_OTA_STATE_DOWNLOAD_RCP_FW;
        } else {
            entry->state = ESP_RCP_OTA_STATE_FINISHED;
        }
    }
    return ESP_OK;
}

static esp_err_t receive_rcp_fw(const uint8_t *data, size_t size, rcp_ota_entry_t *entry, size_t *consumed_size)
{
    if (entry->rcp_firmware_size == 0 || entry->rcp_firmware_size <= entry->rcp_firmware_downloaded) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!entry->rcp_fp) {
        const char *rcp_firmware_dir = esp_rcp_get_firmware_dir();
        int8_t rcp_update_seq = esp_rcp_get_next_update_seq();
        char rcp_target_path[RCP_FILENAME_MAX_SIZE];
        sprintf(rcp_target_path, "%s_%d/" ESP_RCP_IMAGE_FILENAME, rcp_firmware_dir, rcp_update_seq);
        entry->rcp_fp = fopen(rcp_target_path, "w");
        if (!entry->rcp_fp) {
            ESP_LOGE(TAG, "Fail to open %s, will delete it and create a new one", rcp_target_path);
            remove(rcp_target_path);
            entry->rcp_fp = fopen(rcp_target_path, "w");
        }
        ESP_RETURN_ON_FALSE(entry->rcp_fp, ESP_FAIL, TAG, "Fail to open %s", rcp_target_path);
        ESP_LOGI(TAG, "Start downloading the rcp firmware");
    }
    if (entry->rcp_firmware_downloaded == 0) {
        ESP_RETURN_ON_FALSE(write_file_for_length(entry->rcp_fp, entry->image_header_buffer, entry->header_size) ==
                                entry->header_size,
                            ESP_FAIL, TAG, "Failed to write data");
        entry->rcp_firmware_downloaded += entry->header_size;
        ESP_LOGD(TAG, "RCP firmware download %ld/%ld", entry->rcp_firmware_downloaded, entry->rcp_firmware_size);
    }
    if (entry->rcp_firmware_downloaded < entry->rcp_firmware_size &&
        entry->rcp_firmware_downloaded >= entry->header_size) {
        size_t copy_size = size > entry->rcp_firmware_size - entry->rcp_firmware_downloaded
            ? entry->rcp_firmware_size - entry->rcp_firmware_downloaded
            : size;
        ESP_RETURN_ON_FALSE(write_file_for_length(entry->rcp_fp, data, copy_size) == copy_size, ESP_FAIL, TAG,
                            "Failed to write data");
        entry->rcp_firmware_downloaded += copy_size;
        *consumed_size += copy_size;
        ESP_LOGD(TAG, "RCP firmware download %ld/%ld", entry->rcp_firmware_downloaded, entry->rcp_firmware_size);
    }
    if (entry->rcp_firmware_downloaded >= entry->rcp_firmware_size) {
        if (entry->rcp_fp != NULL) {
            fclose(entry->rcp_fp);
            entry->rcp_fp = NULL;
        }
        entry->state = ESP_RCP_OTA_STATE_FINISHED;
    }
    return ESP_OK;
}

esp_err_t esp_rcp_ota_receive(esp_rcp_ota_handle_t handle, const void *data, size_t size, size_t *received_size)
{
    rcp_ota_entry_t *entry = find_esp_rcp_ota_entry(handle);
    ESP_RETURN_ON_FALSE(entry, ESP_ERR_NOT_FOUND, TAG, "Invalid rcp_ota handle");
    ESP_RETURN_ON_FALSE(data && size != 0, ESP_ERR_INVALID_ARG, TAG, "Invalid data received");
    const uint8_t *cur_data = data;
    *received_size = 0;

    while (size > 0) {
        size_t consumed_size = 0;
        switch (entry->state) {
        case ESP_RCP_OTA_STATE_READ_HEADER: {
            ESP_RETURN_ON_ERROR(receive_header(cur_data, size, entry, &consumed_size), TAG, "Failed to receive header");
            break;
        }
        case ESP_RCP_OTA_STATE_DOWNLOAD_RCP_FW: {
            ESP_RETURN_ON_ERROR(receive_rcp_fw(cur_data, size, entry, &consumed_size), TAG, "Failed to receive rcp_fw");
            break;
        }
        case ESP_RCP_OTA_STATE_FINISHED:
            break;
        default:
            ESP_LOGE(TAG, "Invalid state of Border Router OTA");
            return ESP_ERR_INVALID_STATE;
            break;
        }
        cur_data += consumed_size;
        size -= consumed_size;
        *received_size = cur_data - (const uint8_t *)data;
        if (entry->state == ESP_RCP_OTA_STATE_FINISHED) {
            break;
        }
    }
    return ESP_OK;
}

esp_err_t esp_rcp_ota_end(esp_rcp_ota_handle_t handle)
{
    esp_err_t ret = ESP_OK;
    rcp_ota_entry_t *entry = find_esp_rcp_ota_entry(handle);
    ESP_RETURN_ON_FALSE(entry, ESP_ERR_NOT_FOUND, TAG, "Invalid rcp_ota handle");
    ESP_GOTO_ON_FALSE(entry->state == ESP_RCP_OTA_STATE_FINISHED, ESP_ERR_INVALID_STATE, cleanup, TAG, "Invalid State");
    // TODO: esp_rcp_submit_new_image() is not a thread-safe function, we need to make it thread-safe.
    ESP_GOTO_ON_ERROR(esp_rcp_submit_new_image(), cleanup, TAG, "Failed to submit RCP image");
cleanup:
    if (entry->rcp_fp != NULL) {
        fclose(entry->rcp_fp);
    }
    LIST_REMOVE(entry, entries);
    free(entry);
    return ret;
}

esp_err_t esp_rcp_ota_abort(esp_rcp_ota_handle_t handle)
{
    rcp_ota_entry_t *entry = find_esp_rcp_ota_entry(handle);
    ESP_RETURN_ON_FALSE(entry, ESP_ERR_NOT_FOUND, TAG, "Invalid rcp_ota handle");

    if (entry->rcp_fp != NULL) {
        fclose(entry->rcp_fp);
    }
    LIST_REMOVE(entry, entries);
    free(entry);
    return ESP_OK;
}
