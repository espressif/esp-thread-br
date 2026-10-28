/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "cJSON.h"
#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <openthread/border_agent.h>
#include <openthread/border_agent_ephemeral_key.h>
#include <openthread/commissioner.h>
#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/link.h>
#include <openthread/mesh_diag.h>
#include <openthread/netdiag.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <openthread/platform/radio.h>

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef int esp_err_t;
typedef void *TaskHandle_t;
typedef struct {
    bool ready;
} * SemaphoreHandle_t;
typedef void *httpd_handle_t;
typedef enum { HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_OPTIONS, HTTP_HEAD, HTTP_PATCH } httpd_method_t;
typedef struct {
    httpd_method_t method;
    const char *uri;
    size_t content_len;
    const char *input;
    size_t received;
    size_t fragment;
    cJSON *headers;
    const char *borrowed_status;
    const char *borrowed_type;
    const char *borrowed_keys[16];
    const char *borrowed_values[16];
    unsigned header_count;
    char *status;
    char *type;
    char *body;
    cJSON *sent_headers;
} httpd_req_t;
typedef struct {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *);
} httpd_uri_t;

extern otDeviceRole mock_role;
extern otCommissionerState mock_commissioner;
extern otError mock_dataset_error, mock_pending_error, mock_pending_result;
extern int64_t mock_time;
extern otOperationalDataset mock_dataset;
extern unsigned mock_registered;
void mock_reset(void);
void mock_deliver_diagnostic(uintptr_t token);
otInstance *esp_openthread_get_instance(void);
bool esp_openthread_lock_acquire(uint32_t timeout);
void esp_openthread_lock_release(void);
void esp_fill_random(void *buffer, size_t length);
int64_t esp_timer_get_time(void);
void esp_br_web_epskc_set_active_tap(const char *tap);
void esp_restart(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t semaphore, uint32_t timeout);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority,
                TaskHandle_t *handle);
void vTaskDelay(uint32_t ticks);
size_t strlcpy(char *dest, const char *source, size_t length);
size_t httpd_req_get_hdr_value_len(httpd_req_t *req, const char *key);
int httpd_req_get_hdr_value_str(httpd_req_t *req, const char *key, char *value, size_t capacity);
int httpd_req_recv(httpd_req_t *req, char *buffer, size_t length);
int httpd_resp_set_status(httpd_req_t *req, const char *status);
int httpd_resp_set_type(httpd_req_t *req, const char *type);
int httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *value);
int httpd_resp_send(httpd_req_t *req, const char *body, size_t length);
int httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route);
int hex_to_string(const uint8_t *bytes, char *text, size_t size);
