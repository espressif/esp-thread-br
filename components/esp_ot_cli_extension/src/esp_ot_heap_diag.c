/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_heap_diag.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#if CONFIG_HEAP_TRACING
#include "esp_heap_trace.h"
#endif
#include "esp_log.h"
#include "esp_ot_cli_extension.h"
#include "string.h"
#include "openthread/cli.h"

#include "esp_heap_task_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static TaskHandle_t s_heap_daemon_task = NULL;
static int s_heap_daemon_period_ms = 0;

static void print_heap_usage(void)
{
    printf("\tDescription\tInternal\tSPIRAM\n");
    printf("Current Free Memory\t%d\t\t%d\n",
           heap_caps_get_free_size(MALLOC_CAP_8BIT) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("Largest Free Block\t%d\t\t%d\n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    printf("Min. Ever Free Size\t%d\t\t%d\n", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
           heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

static void heap_daemon_task_worker(void *aContext)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(s_heap_daemon_period_ms));
        print_heap_usage();
    }
}

#if CONFIG_HEAP_TASK_TRACKING
#define HEAP_TASKS_NUM 10
#define HEAP_BLOCKS_NUM 30

static void heap_trace_task_handler(void)
{
    static size_t num_totals = 0;
    static heap_task_totals_t s_totals[HEAP_TASKS_NUM];
    static heap_task_block_t s_blocks[HEAP_BLOCKS_NUM];

    heap_task_info_params_t heap_info;
    memset(&heap_info, 0, sizeof(heap_info));
    heap_info.caps[0] = MALLOC_CAP_8BIT; // Gets heap with CAP_8BIT capabilities
    heap_info.mask[0] = MALLOC_CAP_8BIT;
    heap_info.caps[1] = MALLOC_CAP_32BIT; // Gets heap info with CAP_32BIT capabilities
    heap_info.mask[1] = MALLOC_CAP_32BIT;
    heap_info.tasks = NULL; // Passing NULL captures heap info for all tasks
    heap_info.num_tasks = 0;
    heap_info.totals = s_totals; // Gets task wise allocation details
    heap_info.num_totals = &num_totals;
    heap_info.max_totals = HEAP_TASKS_NUM; // Maximum length of "s_totals"
    heap_info.blocks =
        s_blocks; // Gets block wise allocation details. For each block, gets owner task, address and size
    heap_info.max_blocks = HEAP_BLOCKS_NUM; // Maximum length of "s_blocks"
    heap_caps_get_per_task_info(&heap_info);

    for (int i = 0; i < *heap_info.num_totals; i++) {
        otCliOutputFormat("Task: %s -> CAP_8BIT: %u CAP_32BIT: %u\n",
                          heap_info.totals[i].task ? pcTaskGetName(heap_info.totals[i].task) : "Pre-Scheduler allocs",
                          (unsigned int)heap_info.totals[i].size[0],  // Heap size with CAP_8BIT capabilities
                          (unsigned int)heap_info.totals[i].size[1]); // Heap size with CAP32_BIT capabilities
    }
}
#endif // CONFIG_HEAP_TASK_TRACKING

otError esp_ot_process_heap_diag(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    (void)(aContext);
    if (aArgsLength == 0) {
        otCliOutputFormat("---heapdiag command parameter---\n");
        otCliOutputFormat("print               : print current heap usage\n");
        otCliOutputFormat("daemon on <preriod> : start the daemon task to print heap usage per <period> ms\n");
        otCliOutputFormat("daemon off          : stop the daemon task for heap usage print\n");
#if CONFIG_HEAP_TRACING_STANDALONE
        otCliOutputFormat("tracereset          : reset the heap trace baseline\n");
        otCliOutputFormat("tracedump           : dump the last collected heap trace\n");
#endif // CONFIG_HEAP_TRACING_STANDALONE
#if CONFIG_HEAP_TASK_TRACKING
        otCliOutputFormat("tracetask           : dump heap usage of each task\n");
#endif // CONFIG_HEAP_TASK_TRACKING
    } else {
        if (strcmp(aArgs[0], "print") == 0) {
            print_heap_usage();
        } else if (strcmp(aArgs[0], "daemon") == 0) {
            if (aArgsLength <= 1) {
                return OT_ERROR_INVALID_ARGS;
            } else {
                if (strcmp(aArgs[1], "on") == 0) {
                    if (aArgsLength <= 2) {
                        return OT_ERROR_INVALID_ARGS;
                    } else {
                        int period = strtol(aArgs[2], NULL, 10);
                        if (period <= 0) {
                            return OT_ERROR_INVALID_ARGS;
                        }
                        s_heap_daemon_period_ms = period;
                        if (!s_heap_daemon_task) {
                            if (xTaskCreate(heap_daemon_task_worker, "heap_daemon", 2560, NULL, 6,
                                            &s_heap_daemon_task) != pdTRUE) {
                                ESP_LOGE(OT_EXT_CLI_TAG, "Failed to create heap daemon task");
                                return OT_ERROR_FAILED;
                            }
                        }
                    }
                } else if (strcmp(aArgs[1], "off") == 0) {
                    if (s_heap_daemon_task) {
                        vTaskDelete(s_heap_daemon_task);
                        s_heap_daemon_task = NULL;
                    } else {
                        return OT_ERROR_INVALID_STATE;
                    }
                }
            }
        }
#if CONFIG_HEAP_TRACING_STANDALONE
        else if (strcmp(aArgs[0], "tracereset") == 0) {
            if (heap_trace_stop() != ESP_OK) {
                ESP_LOGE(OT_EXT_CLI_TAG, "Failed to stop heap trace");
                return OT_ERROR_FAILED;
            }
            if (heap_trace_start(HEAP_TRACE_LEAKS) == 0) {
                ESP_LOGE(OT_EXT_CLI_TAG, "Failed to start heap trace");
                return OT_ERROR_FAILED;
            }
        } else if (strcmp(aArgs[0], "tracedump") == 0) {
            heap_trace_dump();
        }
#endif // CONFIG_HEAP_TRACING_STANDALONE
#if CONFIG_HEAP_TASK_TRACKING
        else if (strcmp(aArgs[0], "tracetask") == 0) {
            heap_trace_task_handler();
        }
#endif // CONFIG_HEAP_TASK_TRACKING
        else {
            return OT_ERROR_INVALID_ARGS;
        }
    }
    return OT_ERROR_NONE;
}

#if CONFIG_HEAP_TRACING_STANDALONE
#define HEAP_TRACE_RECORDS_MAX_NUM 100
static heap_trace_record_t s_trace_records[HEAP_TRACE_RECORDS_MAX_NUM];
#endif

esp_err_t esp_ot_heap_diag_init(void)
{
#if CONFIG_HEAP_TRACING_STANDALONE
    ESP_RETURN_ON_ERROR(heap_trace_init_standalone(s_trace_records, HEAP_TRACE_RECORDS_MAX_NUM), OT_EXT_CLI_TAG,
                        "Failed to initialize heap trace standalone");
    ESP_RETURN_ON_ERROR(heap_trace_start(HEAP_TRACE_LEAKS), OT_EXT_CLI_TAG, "Failed to start heap trace");
#endif // CONFIG_HEAP_TRACING_STANDALONE
    return ESP_OK;
}
