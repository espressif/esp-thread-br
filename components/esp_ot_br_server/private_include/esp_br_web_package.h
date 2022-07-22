/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_br_web_library.h"

/* define the version of web package version to distinguish different version.*/
#define WEB_PACKAGE_PACKAGE_VERSION_NUMBER "v1.0.0"
#define WEB_PACKAGE_VERSION_MAX_SIZE 10
/* a data package for swapping data between server and client. */
typedef struct web_data_package {
    char version[WEB_PACKAGE_VERSION_MAX_SIZE];
    char *content;
} web_data_package_t;

esp_err_t init_ot_server_package(openthread_status_t *status, thread_network_list_t *list);
void free_ot_server_package(openthread_status_t *thread_status, thread_network_list_t *list);
esp_err_t verify_ot_server_package(cJSON *root);

cJSON *encode_thread_status_package(void);
esp_err_t decode_thread_status_package(web_data_package_t *arg);

cJSON *encode_available_thread_networks_package(void);
esp_err_t decode_available_thread_networks_package(web_data_package_t *arg);

cJSON *form_openthread_network(cJSON *content);
cJSON *thread_network_add_on_mesh_prefix(cJSON *content);
cJSON *thread_network_delete_on_mesh_prefix(cJSON *content);

#ifdef __cplusplus
}
#endif
