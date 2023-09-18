/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "openthread/border_agent.h"
#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_openthread.h"
#include "stdio.h"
#include <stdint.h>
#include "openthread/dataset.h"
#include "openthread/ip6.h"
#include "openthread/netdata.h"
#include "openthread/netdiag.h"
#include "openthread/thread_ftd.h"

#define OPENTHREAD_INVALID_RLOC16 "0xffff"
#define WPAN_STATUS_OFFLINE "offline"
#define WPAN_STATUS_ASSOCIATED "associated"
#define WPAN_STATUS_ASSICIATING "associating"
#define NETWORK_PASSPHRASE_MAX_SIZE 64
#define NETWORK_PSKD_MAX_SIZE 64
#define CREDENTIAL_TYPE_NETWORK_KEY "networkKeyType"
#define CREDENTIAL_TYPE_PSKD "pskdType"
#define DIAGSET_NODE_MAX_TIMEOUT_SECOND 5
#define RLOC_STRING_MAX_SIZE 7

#define ESP_OT_REST_ACCEPT_HEADER "Accept"
#define ESP_OT_REST_CONTENT_TYPE_HEADER "Content-Type"

#define ESP_OT_REST_CONTENT_TYPE_JSON "application/json"
#define ESP_OT_REST_CONTENT_TYPE_PLAIN "text/plain"

#define ESP_OT_REST_DATASET_TYPE "DatasetType"
#define ESP_OT_DATASET_TYPE_ACTIVE "active"
#define ESP_OT_DATASET_TYPE_PENDING "pending"

#define HTTPD_201 "201 Created"
#define HTTPD_409 "409 Conflict"

/**
 * @brief When checking the otError, eixt.
 *
 */
#define ERROR_EXIT(x, goto_tag, log_tag, format, ...)                                    \
    do {                                                                                 \
        otError err_rc_ = (otError)(x);                                                  \
        if (unlikely(err_rc_ != OT_ERROR_NONE)) {                                        \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret = err_rc_;                                                               \
            goto goto_tag;                                                               \
        }                                                                                \
    } while (0)

/*---------------------------------------------
        Thread netWork Status
-----------------------------------------------*/
typedef struct thread_ipv6_status {
    otIp6Address link_local_address;
    otIp6Address routing_local_address;
    otIp6Address mesh_local_address;
    otIp6Prefix mesh_local_prefix;
} thread_ipv6_status_t;

typedef struct thread_network_status {
    otNetworkName name;
    otPanId panid;
    uint32_t partition_id;
    otExtendedPanId xpanid;
    otBorderAgentId baid;
} thread_network_status_t;

typedef struct thread_information_status {
    char *version;
    int version_api;
    otDeviceRole role;
    otPskc PSKc;
} thread_information_status_t;

typedef struct thread_rcp_status {
    uint8_t channel;
    otExtAddress EUI64;
    int8_t txpower;
    char *version;
} thread_rcp_status_t;

typedef struct thread_wpan_status {
    char service[12];
} thread_wpan_status_t;

typedef struct openthread_status {
    thread_ipv6_status_t ipv6;
    thread_network_status_t network;
    thread_information_status_t information;
    thread_rcp_status_t rcp;
    thread_wpan_status_t wpan;
} openthread_properties_t;

/*---------------------------------------------
        Discover Thread netWork
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

/*---------------------------------------------
        Form Thread Network
-----------------------------------------------*/
typedef struct thread_network_form_param {
    otNetworkName network_name;
    uint16_t channel;
    otPanId panid;
    otExtendedPanId extended_panid;
    otNetworkKey network_key;
    char on_mesh_prefix[OT_IP6_PREFIX_STRING_SIZE + 3];
    char passphrase[NETWORK_PASSPHRASE_MAX_SIZE];
    bool default_route;
} thread_network_formation_param_t;

/*---------------------------------------------
        Join Thread Network
-----------------------------------------------*/
typedef struct thread_network_join_param {
    int index;
    uint8_t credentialType[16];
    otNetworkKey networkKey;
    char prefix[OT_IP6_PREFIX_STRING_SIZE + 3];
    char pskd[NETWORK_PSKD_MAX_SIZE];
    bool defaultRoute;
} thread_network_join_param_t;

/*---------------------------------------------
        Thread Network Topology
-----------------------------------------------*/
typedef struct thread_diagnosticTlv_list {
    struct thread_diagnosticTlv_list *next;
    otNetworkDiagTlv *diagTlv;
} thread_diagnosticTlv_list_t;

typedef struct thread_diagnosticTlv_set {
    char rloc16[RLOC_STRING_MAX_SIZE]; /* rloc16 string e.g. 0x0001 */
    time_t timeout;                    /* if timeout <=0,remove diagTlv_next list. */
    struct thread_diagnosticTlv_set *next;
    thread_diagnosticTlv_list_t *diagTlv_next;
} thread_diagnosticTlv_set_t;

typedef struct thread_node_informaiton {
    uint32_t role;
    uint32_t router_number;
    uint16_t rloc16;
    otExtendedPanId extended_panid;
    otExtAddress extended_address;
    otIp6Address rloc_address;
    otLeaderData leader_data;
    otNetworkName network_name;
} thread_node_informaiton_t;

/*---------------------------------------------
                Usual Method
-----------------------------------------------*/
esp_err_t hex_to_string(const uint8_t hex[], char str[], size_t size);
esp_err_t string_to_hex(char str[], uint8_t hex[], size_t size);

void otbr_properties_reset(openthread_properties_t *properties);
cJSON *otbr_properties_struct_convert2_json(openthread_properties_t *properties);

void avaiable_network_reset(thread_network_information_t *network);
cJSON *avaiable_network_struct_convert2_json(thread_network_information_t *network);

esp_err_t initialize_available_thread_networks_list(thread_network_list_t *list);
esp_err_t append_available_thread_networks_list(thread_network_list_t *list, thread_network_information_t network);
void destroy_available_thread_networks_list(thread_network_list_t *list);

void network_formation_param_reset(thread_network_formation_param_t *param);
esp_err_t network_formation_param_json_convert2_struct(const cJSON *root, cJSON *log,
                                                       thread_network_formation_param_t *param);

void network_join_param_reset(thread_network_join_param_t *param);
esp_err_t network_join_param_json_convert2_struct(const cJSON *root, cJSON *log, thread_network_join_param_t *param);

esp_err_t initialize_thread_diagnosticTlv_list(thread_diagnosticTlv_list_t *list);
esp_err_t append_thread_diagnosticTlv_list(thread_diagnosticTlv_list_t *list, otNetworkDiagTlv diagTlv);
void destroy_thread_diagnosticTlv_list(thread_diagnosticTlv_list_t *list);

esp_err_t initialize_thread_diagnosticTlv_set(thread_diagnosticTlv_set_t *set, const char *rloc16);
esp_err_t update_thread_diagnosticTlv_set(thread_diagnosticTlv_set_t *set, char *rloc16,
                                          thread_diagnosticTlv_list_t *list);
void destroy_thread_diagnosticTlv_set(thread_diagnosticTlv_set_t *set);
cJSON *dailnosticTlv_set_convert2_json(const thread_diagnosticTlv_set_t *set);

void thread_node_information_reset(thread_node_informaiton_t *node);
cJSON *thread_node_struct_convert2_json(thread_node_informaiton_t *node);

cJSON *Timestamp2Json(const otTimestamp aTimestamp);
cJSON *SecurityPolicy2Json(const otSecurityPolicy aSecurityPolicy);
cJSON *ActiveDataset2Json(const otOperationalDataset aActiveDataset);
cJSON *PendingDataset2Json(const otOperationalDataset aPendingDataset);

esp_err_t Json2Timestamp(const cJSON *jsonTimestamp, otTimestamp *aTimestamp);
esp_err_t Json2SecurityPolicy(const cJSON *jsonTimestamp, otSecurityPolicy *aSecurityPolicy);
esp_err_t Json2ActiveDataset(const cJSON *jsonActiveDataset, otOperationalDataset *aActiveDataset);
esp_err_t JsonString2ActiveDataset(const cJSON *JsonStringActiveDataset, otOperationalDataset *aActiveDataset);
esp_err_t Json2PendingDataset(const cJSON *jsonPendingDataset, otOperationalDataset *aPendingDataset);

void ot_br_web_response_code_get(uint16_t errcode, char *status_buf);
esp_err_t convert_ot_err_to_response_code(otError errcode, char *status_buf);

#ifdef __cplusplus
}
#endif
