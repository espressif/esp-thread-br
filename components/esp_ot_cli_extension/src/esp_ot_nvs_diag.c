/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_nvs_diag.h"
#include "esp_log.h"
#include "esp_ot_cli_extension.h"
#include "nvs.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "openthread/cli.h"

static TaskHandle_t nvs_daemon_task_handle = NULL;
static uint32_t nvs_print_interval = 0;

extern void nvs_dump(const char *partName);

static void nvs_detail_status_print(void)
{
    nvs_dump(NVS_DEFAULT_PART_NAME);
}

void nvs_basic_status_print(void)
{
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NVS_DEFAULT_PART_NAME, &nvs_stats);
    if (err != ESP_OK) {
        ESP_LOGE(OT_EXT_CLI_TAG, "Fail to get nvs status");
        return;
    }
    otCliOutputFormat("namespace count: %u\n", nvs_stats.namespace_count);
    otCliOutputFormat("total entries: %u\n", nvs_stats.total_entries);
    otCliOutputFormat("available entries: %u\n", nvs_stats.available_entries);
    otCliOutputFormat("used entries: %u\n", nvs_stats.used_entries);
    otCliOutputFormat("free entries: %u\n", nvs_stats.free_entries);
}

static void nvs_daemon_task_worker(void *pvParameters)
{
    uint32_t interval = *(uint32_t *)(pvParameters);
    while (true) {
        nvs_basic_status_print();
        vTaskDelay(pdMS_TO_TICKS(interval));
    }
}

otError esp_ot_process_nvs_diag(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    (void)(aContext);
    if (aArgsLength == 0) {
        otCliOutputFormat("---nvsdiag parameter---\n");
        otCliOutputFormat("status                                   :     print the status of nvs\n");
        otCliOutputFormat("detail                                   :     print detailed usage information of nvs\n");
        otCliOutputFormat("deamon                                   :     print the status of nvs deamon task\n");
        otCliOutputFormat("deamon start <interval>                  :     create the daemon task, print nvs status "
                          "every <interval> milliseconds\n");
        otCliOutputFormat("deamon stop                              :     delete the daemon task\n");
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("print the status of nvs                  :     nvsdiag status\n");
        otCliOutputFormat("print detailed usage information of nvs  :     nvsdiag detail\n");
        otCliOutputFormat("print the status of nvs deamon task      :     nvsdiag deamon\n");
        otCliOutputFormat("create a daemon task (interval=1s)       :     nvsdiag deamon start 1000\n");
        otCliOutputFormat("delete the daemon task                   :     nvsdiag deamon stop\n");
    } else {
        if (strcmp(aArgs[0], "status") == 0) {
            nvs_basic_status_print();
        } else if (strcmp(aArgs[0], "detail") == 0) {
            nvs_basic_status_print();
            nvs_detail_status_print();
        } else if (strcmp(aArgs[0], "deamon") == 0) {
            if (aArgsLength == 1) {
                otCliOutputFormat("nvs daemon task: %s\n", (nvs_daemon_task_handle != NULL) ? "enabled" : "disabled");
            } else if (aArgsLength == 2 && strcmp(aArgs[1], "stop") == 0) {
                if (nvs_daemon_task_handle == NULL) {
                    ESP_LOGE(OT_EXT_CLI_TAG, "nvs daemon task is not running");
                    return OT_ERROR_NONE;
                }
                ESP_LOGI(OT_EXT_CLI_TAG, "Deleting nvs daemon task");
                vTaskDelete(nvs_daemon_task_handle);
                nvs_daemon_task_handle = NULL;
            } else if (aArgsLength == 3 && strcmp(aArgs[1], "start") == 0) {
                if (nvs_daemon_task_handle != NULL) {
                    ESP_LOGE(OT_EXT_CLI_TAG, "nvs daemon task is running");
                    return OT_ERROR_NONE;
                }
                nvs_print_interval = atoi(aArgs[2]);
                if (nvs_print_interval <= 0) {
                    ESP_LOGE(OT_EXT_CLI_TAG, "Invalid interval");
                    return OT_ERROR_INVALID_ARGS;
                }
                if (pdPASS !=
                    xTaskCreate(nvs_daemon_task_worker, "nvs_daemon", 4096, &nvs_print_interval, 4,
                                &nvs_daemon_task_handle)) {
                    nvs_daemon_task_handle = NULL;
                    ESP_LOGE(OT_EXT_CLI_TAG, "Fail to create nvs daemon task");
                    return OT_ERROR_FAILED;
                }
            } else {
                return OT_ERROR_INVALID_ARGS;
            }
        } else {
            return OT_ERROR_INVALID_ARGS;
        }
    }
    return OT_ERROR_NONE;
}