/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_rcp_update.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp32_port.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_loader.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define RCP_UPDATE_MAX_RETRY 3
#define RCP_VERIFIED_FLAG (1 << 5)
#define RCP_SEQ_KEY "rcp_seq"
#define TAG "RCP_UPDATE"

typedef struct esp_rcp_update_handle {
    nvs_handle_t nvs_handle;
    int8_t update_seq;
    bool verified;
    esp_rcp_update_config_t update_config;
} esp_rcp_update_handle;

static esp_rcp_update_handle s_handle;

static esp_loader_error_t connect_to_target(target_chip_t target_chip, uint32_t higher_baudrate)
{
    esp_loader_connect_args_t connect_config = ESP_LOADER_CONNECT_DEFAULT();

    esp_loader_error_t err = esp_loader_connect(&connect_config);
    if (err != ESP_LOADER_SUCCESS) {
        return err;
    }
    if (esp_loader_get_target() != target_chip) {
        return ESP_LOADER_ERROR_UNSUPPORTED_CHIP;
    }

    if (higher_baudrate) {
        ESP_RETURN_ON_ERROR(esp_loader_change_baudrate(higher_baudrate), TAG, "Failed to change bootloader baudrate");
        ESP_RETURN_ON_ERROR(loader_port_change_baudrate(higher_baudrate), TAG, "Failed to change local port baudrate");
    }
    return ESP_LOADER_SUCCESS;
}

esp_err_t esp_rcp_load_version_in_storage(char *version_str, size_t size)
{
    char fullpath[RCP_FILENAME_MAX_SIZE];
    int8_t update_seq = s_handle.update_seq;

    sprintf(fullpath, "%s_%d/rcp_version", s_handle.update_config.firmware_dir, update_seq);
    FILE *fp = fopen(fullpath, "r");
    if (fp == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    memset(version_str, 0, size);
    fread(version_str, 1, size, fp);
    fclose(fp);
    return ESP_OK;
}

esp_loader_error_t flash_binary(FILE *firmware, size_t size, size_t address)
{
    esp_loader_error_t err;
    static uint8_t payload[1024];

    ESP_LOGI(TAG, "Erasing flash (this may take a while)...");
    err = esp_loader_flash_start(address, size, sizeof(payload));
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "Erasing flash failed with error %d.", err);
        return err;
    }
    ESP_LOGI(TAG, "Start programming");

    size_t binary_size = size;
    size_t written = 0;

    ESP_LOGI(TAG, "binary_size %zu", binary_size);
    while (size > 0) {
        size_t to_read = size < sizeof(payload) ? size : sizeof(payload);
        fread(payload, 1, to_read, firmware);

        err = esp_loader_flash_write(payload, to_read);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "Packet could not be written! Error %d.", err);
            return err;
        }

        size -= to_read;
        written += to_read;
        ESP_LOGI(TAG, "left size %zu, written %zu", size, written);

        int progress = (int)(((float)written / binary_size) * 100);
        ESP_LOGI(TAG, "Progress: %d %%", progress);
        fflush(stdout);
    };

    ESP_LOGI(TAG, "Finished programming");

    err = esp_loader_flash_verify();
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "MD5 does not match. err: %d", err);
        return err;
    }
    ESP_LOGI(TAG, "Flash verified");

    return ESP_LOADER_SUCCESS;
}

static void set_rcp_seq(int seq)
{
    nvs_set_i8(s_handle.nvs_handle, RCP_SEQ_KEY, seq);
}

static void load_rcp_update_seq(esp_rcp_update_handle *handle)
{
    int8_t seq = 0;
    bool verified;
    esp_err_t err = nvs_get_i8(handle->nvs_handle, RCP_SEQ_KEY, &seq);

    if (err != ESP_OK) {
        seq = 0;
        verified = 1;
    } else {
        seq = (seq & ~RCP_VERIFIED_FLAG);
        verified = (seq & RCP_VERIFIED_FLAG);
    }
    handle->update_seq = seq;
    handle->verified = verified;
}

const char *esp_rcp_get_firmware_dir(void)
{
    return s_handle.update_config.firmware_dir;
}

int8_t esp_rcp_get_update_seq(void)
{
    return s_handle.verified ? (s_handle.update_seq) : (1 - s_handle.update_seq);
}

int8_t esp_rcp_get_next_update_seq(void)
{
    return 1 - esp_rcp_get_update_seq();
}

esp_err_t esp_rcp_submit_new_image()
{
    ESP_RETURN_ON_FALSE(s_handle.update_config.rcp_type != RCP_TYPE_INVALID, ESP_ERR_INVALID_STATE, TAG,
                        "RCP update not initialized");
    s_handle.update_seq = esp_rcp_get_next_update_seq();
    s_handle.verified = true;

    int8_t new_seq = s_handle.update_seq | RCP_VERIFIED_FLAG;
    return nvs_set_i8(s_handle.nvs_handle, RCP_SEQ_KEY, new_seq);
}

esp_err_t esp_rcp_update_init(const esp_rcp_update_config_t *update_config)
{
    ESP_RETURN_ON_ERROR(nvs_open("storage", NVS_READWRITE, &s_handle.nvs_handle), "TAG", "Failed to open nvs");
    ESP_RETURN_ON_FALSE(update_config->rcp_type == RCP_TYPE_ESP32H2_UART, ESP_ERR_INVALID_ARG, TAG,
                        "Unsuppported RCP type");

    s_handle.update_config = *update_config;
    load_rcp_update_seq(&s_handle);
    ESP_LOGI(TAG, "RCP: using update sequence %d\n", s_handle.update_seq);
    return ESP_OK;
}

esp_err_t esp_rcp_update(void)
{
    ESP_RETURN_ON_FALSE(s_handle.update_config.rcp_type != RCP_TYPE_INVALID, ESP_ERR_INVALID_STATE, TAG,
                        "RCP update not initialized");

    loader_esp32_config_t loader_config = {
        .baud_rate = s_handle.update_config.uart_baudrate,
        .uart_port = s_handle.update_config.uart_port,
        .uart_rx_pin = s_handle.update_config.uart_rx_pin,
        .uart_tx_pin = s_handle.update_config.uart_tx_pin,
        .reset_trigger_pin = s_handle.update_config.reset_pin,
        .gpio0_trigger_pin = s_handle.update_config.boot_pin,
    };
    ESP_RETURN_ON_ERROR(loader_port_esp32_init(&loader_config), TAG, "Failed to initialize UART port");
    ESP_RETURN_ON_ERROR(connect_to_target(s_handle.update_config.target_chip, s_handle.update_config.update_baudrate),
                        TAG, "Failed to connect to RCP");

    char fullpath[RCP_FILENAME_MAX_SIZE];
    int update_seq = esp_rcp_get_update_seq();
    sprintf(fullpath, "%s_%d/flash_args", s_handle.update_config.firmware_dir, update_seq);
    FILE *fp = fopen(fullpath, "r");
    ESP_RETURN_ON_FALSE(fp != NULL, ESP_ERR_NOT_FOUND, TAG, "Cannot find flash arguments");

    char flash_args[RCP_FILENAME_MAX_SIZE];
    while (fgets(flash_args, sizeof(flash_args), fp)) {
        uint32_t offset;
        if (sscanf(flash_args, "%x", &offset) != 1) {
            return ESP_FAIL;
        }
        char *filename = strchr(flash_args, ' ');
        while (isspace(*filename)) {
            filename++;
        }
        char *filename_end = filename;
        while (!isspace(*filename_end)) {
            filename_end++;
        }
        *filename_end = '\0';
        sprintf(fullpath, "%s_%d/%s", s_handle.update_config.firmware_dir, update_seq, filename);

        ESP_LOGI(TAG, "Flashing %s to offset 0x%x", fullpath, offset);
        FILE *binary_fp = fopen(fullpath, "r");
        ESP_RETURN_ON_FALSE(binary_fp != NULL, ESP_ERR_NOT_FOUND, TAG, "Failed to open %s", fullpath);
        fseek(binary_fp, 0, SEEK_END);
        size_t binary_size = ftell(binary_fp);
        fseek(binary_fp, 0, SEEK_SET);
        int num_retry = 0;
        while (flash_binary(binary_fp, binary_size, offset) != ESP_LOADER_SUCCESS) {
            ESP_LOGW(TAG, "Failed to flash %s, retrying...", fullpath);
            num_retry++;
            if (num_retry > RCP_UPDATE_MAX_RETRY) {
                ESP_LOGE(TAG, "Failed to update RCP, abort and reboot");
                abort();
            }
        }
        fclose(binary_fp);
    }
    fclose(fp);
    set_rcp_seq(update_seq);
    esp_loader_reset_target();
    return ESP_OK;
}

void esp_rcp_update_deinit(void)
{
    nvs_close(s_handle.nvs_handle);
}

esp_err_t esp_rcp_mark_image_verified(bool verified)
{
    ESP_RETURN_ON_FALSE(s_handle.update_config.rcp_type != RCP_TYPE_INVALID, ESP_ERR_INVALID_STATE, TAG,
                        "RCP update not initialized");
    int8_t val;
    if (!verified) {
        val = 1 - s_handle.update_seq;
    } else {
        val = s_handle.update_seq | RCP_VERIFIED_FLAG;
    }
    s_handle.verified = verified;
    return nvs_set_i8(s_handle.nvs_handle, RCP_SEQ_KEY, val);
}

void esp_rcp_reset(void)
{
    gpio_config_t io_conf = {};
    uint8_t boot_pin = s_handle.update_config.boot_pin;
    uint8_t reset_pin = s_handle.update_config.reset_pin;
    io_conf.pin_bit_mask = ((1ULL << boot_pin) | (1ULL << reset_pin));
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(boot_pin, 1);
    gpio_set_level(reset_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(reset_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}
