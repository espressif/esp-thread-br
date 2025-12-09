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
#include "esp_ot_rcp_update.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_spinel.h"
#include "esp_rcp_update.h"
#include "openthread/platform/radio.h"

#define TAG "ot_rcp_update"
#define ESP_RCP_VERSION_MAX_SIZE 100

void esp_ot_try_update_rcp(const char *running_rcp_version)
{
    char internal_rcp_version[ESP_RCP_VERSION_MAX_SIZE];
    if (running_rcp_version) {
        ESP_LOGI(TAG, "Running RCP Version: %s", running_rcp_version);
    }
    if (esp_rcp_load_version_in_storage(internal_rcp_version, sizeof(internal_rcp_version)) == ESP_OK) {
        ESP_LOGI(TAG, "Internal RCP Version: %s", internal_rcp_version);
        if (running_rcp_version && strcmp(internal_rcp_version, running_rcp_version) == 0) {
            esp_rcp_mark_image_verified(true);
        } else {
            esp_openthread_rcp_deinit();
            if (esp_rcp_update() == ESP_OK) {
                esp_rcp_mark_image_verified(true);
            } else {
                esp_rcp_mark_image_verified(false);
            }
            esp_restart();
        }
    } else {
        ESP_LOGI(TAG, "RCP firmware not found in storage");
        esp_rcp_mark_image_verified(false);
        esp_restart();
    }
}

static void rcp_hardware_reset_handler(void)
{
    esp_rcp_reset();
}

static void rcp_compatibility_error_handler(void)
{
    esp_ot_try_update_rcp(NULL);
}

static void rcp_failure_handler(void)
{
    esp_ot_try_update_rcp(NULL);
}

void esp_ot_register_rcp_handler(void)
{
    esp_openthread_register_rcp_failure_handler(rcp_hardware_reset_handler);
    esp_openthread_set_compatibility_error_callback(rcp_compatibility_error_handler);
    esp_openthread_set_coprocessor_reset_failure_callback(rcp_failure_handler);
}

void esp_ot_update_rcp_if_different(void)
{
    esp_openthread_lock_acquire(portMAX_DELAY);
    const char *running_rcp_version = otPlatRadioGetVersionString(esp_openthread_get_instance());
    esp_openthread_lock_release();
    esp_ot_try_update_rcp(running_rcp_version);
}