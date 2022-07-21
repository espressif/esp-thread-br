/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_openthread.h"
#include "stdio.h"
#include "openthread/dataset.h"
#include "openthread/ip6.h"
#include "openthread/netdata.h"
#include "openthread/thread_ftd.h"

/*---------------------------------------------
        Usual Method
-----------------------------------------------*/
esp_err_t hex_to_string(uint8_t hex[], char str[], size_t size);
esp_err_t string_to_hex(char str[], uint8_t hex[], size_t size);
/*---------------------------------------------
        Thread netWork Status
-----------------------------------------------*/
typedef struct thread_ipv6_status {
    otIp6Address link_local_address;
    otIp6Address local_address;
    otIp6Address mesh_local_address;
    otIp6Prefix mesh_local_prefix;
} thread_ipv6_status_t;

typedef struct thread_network_status {
    otNetworkName name;
    otPanId panid;
    uint32_t partition_id;
    otExtendedPanId xpanid;
} thread_network_status_t;

typedef struct thread_information_status {
    uint16_t version;
    uint8_t version_api;
    otPskc PSKc;
} thread_information_status_t;

typedef struct thread_rcp_status {
    uint8_t channel;
    otDeviceRole state;
    otExtAddress EUI64;
    int8_t txpower;
    char *version;
} thread_rcp_status_t;

#define WPAN_STATUS_ASSOCIATED "associated"
#define WPAN_STATUS_ASSICIATING "associating"
typedef struct thread_wpan_status {
    char service[12];
} thread_wpan_status_t;

typedef struct openthread_status {
    thread_ipv6_status_t ipv6;
    thread_network_status_t network;
    thread_information_status_t information;
    thread_rcp_status_t rcp;
    thread_wpan_status_t wpan;
} openthread_status_t;

esp_err_t init_default_ot_status(openthread_status_t *thread_status);

cJSON *ot_status_struct_convert2_json(openthread_status_t *status);
openthread_status_t *ot_status_json_convert2_struct(cJSON *root);

void free_ot_status(openthread_status_t *status);

/*---------------------------------------------
        Scan Thread netWork
-----------------------------------------------*/
typedef struct thread_network_informaiton {
    uint16_t id;
    otNetworkName network_name;
    otExtendedPanId extended_panid;
    uint16_t panid;
    otExtAddress extended_address;
    uint8_t channel;
    int8_t rssi;
    uint8_t lqi;
} thread_network_information_t;

typedef struct thread_network_list {
    thread_network_information_t *network;
    struct thread_network_list *next;
} thread_network_list_t;

cJSON *network_struct_convert2_json(thread_network_information_t *network);
thread_network_information_t network_json_convert2_struct(cJSON *root);

esp_err_t init_available_thread_networks_list(thread_network_list_t *list);
esp_err_t append_available_thread_networks_list(thread_network_list_t *list, thread_network_information_t network);
void destroy_available_thread_networks_list(thread_network_list_t *list);
void free_available_network(thread_network_information_t *network);

/*---------------------------------------------
        Form Thread Network
-----------------------------------------------*/
typedef enum {
    FORM_ERROR_NONE,
    FORM_ERROR_TYPE,
    FORM_ERROR_NETWORK_NAME,
    FORM_ERROR_NETWORK_CHANNEL,
    FORM_ERROR_NETWORK_PANID,
    FORM_ERROR_NETWORK_EXTPANID,
    FORM_ERROR_NETWORK_KEY,
    FORM_ERROR_NETWORK_PREFIX,
    FORM_ERROR_PASSPHRASE,
} check_form_param_t;

typedef struct thread_network_form_param {
    otNetworkName network_name;
    uint16_t channel;
    otPanId panid;
    otExtendedPanId extended_panid;
    otNetworkKey key;
    char *on_mesh_prefix;
    char *passphrase;
    bool default_route;
    check_form_param_t check;
} thread_network_form_param_t;

thread_network_form_param_t form_param_json_convert2_struct(cJSON *content);
void free_network_form_param(thread_network_form_param_t *param);

#ifdef __cplusplus
}
#endif
