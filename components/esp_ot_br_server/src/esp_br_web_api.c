/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"

#include "cJSON.h"
#include "esp_br_web.h"
#include "esp_br_web_api.h"
#include "esp_br_web_base.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_net_stack.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "malloc.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>
#include "freertos/task.h"
#include "openthread/mesh_diag.h"
#include "openthread/platform/radio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "openthread/border_agent.h"
#include "openthread/border_agent_ephemeral_key.h"
#include "openthread/border_router.h"
#include "openthread/commissioner.h"
#include "openthread/dataset.h"
#include "openthread/dataset_ftd.h"
#include "openthread/error.h"
#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/netdata.h"
#include "openthread/ping_sender.h"
#include "openthread/server.h"
#include "openthread/thread_ftd.h"

/* TODO(IDF-5.2-compat): Remove this fallback once IDF release/v5.2 is no longer maintained. */
#ifndef ESP_RETURN_VOID_ON_FALSE
#if defined(CONFIG_COMPILER_OPTIMIZATION_CHECKS_SILENT)
#define ESP_RETURN_VOID_ON_FALSE(a, log_tag, format, ...) \
    do {                                                  \
        (void)log_tag;                                    \
        if (unlikely(!(a))) {                             \
            return;                                       \
        }                                                 \
    } while (0)
#else
#define ESP_RETURN_VOID_ON_FALSE(a, log_tag, format, ...)                                \
    do {                                                                                 \
        if (unlikely(!(a))) {                                                            \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            return;                                                                      \
        }                                                                                \
    } while (0)
#endif
#endif

#define API_TAG "web_api"

/* Forward declarations for semaphores initialized in esp_br_web_api_init() */
static SemaphoreHandle_t s_discover_done_semaphore;
static SemaphoreHandle_t s_join_done_semaphore;
static SemaphoreHandle_t s_diagnostic_semaphore;
static SemaphoreHandle_t s_ping_done_semaphore;
static SemaphoreHandle_t s_ping_mutex;

void esp_br_web_api_init(void)
{
    s_discover_done_semaphore = xSemaphoreCreateBinary();
    s_join_done_semaphore = xSemaphoreCreateBinary();
    s_diagnostic_semaphore = xSemaphoreCreateMutex();
    s_ping_done_semaphore = xSemaphoreCreateBinary();
    s_ping_mutex = xSemaphoreCreateMutex();
}

static const char s_ot_state[5][10] = {"disabled", "detached", "child", "router", "leader"};

/*----------------------------------------------------------------------
                            tools
----------------------------------------------------------------------*/
static void add_prefix_field(char *prefix)
{
    if (prefix == NULL || strstr(prefix, "/"))
        return;
    strcat(prefix, "/64");
    return;
}

static esp_err_t parse_ipv6_prefix_from_string(char *string, otIp6Prefix *prefix)
{
    ESP_RETURN_ON_FALSE(string, ESP_ERR_INVALID_ARG, API_TAG, "Invalid string [NULL]");
    add_prefix_field(string);
    char *pos = strstr(string, "/");
    ESP_RETURN_ON_FALSE(pos, ESP_FAIL, API_TAG, "Failed to convert string[%s] to prefix", string);
    prefix->mLength = atoi(pos + 1);
    *pos = '\0';
    ESP_RETURN_ON_FALSE(!otIp6AddressFromString(string, &prefix->mPrefix), ESP_ERR_INVALID_ARG, API_TAG,
                        "Failed to get prefix from string[%s]", string);

    return ESP_OK;
}

/*----------------------------------------------------------------------
                            Resource REST API
----------------------------------------------------------------------*/
cJSON *handle_ot_resource_node_rloc_request()
{
    char rloc[OT_IP6_ADDRESS_STRING_SIZE];
    esp_openthread_lock_acquire(portMAX_DELAY);
    otIp6AddressToString((const otIp6Address *)otThreadGetRloc(esp_openthread_get_instance()), rloc,
                         OT_IP6_ADDRESS_STRING_SIZE);
    esp_openthread_lock_release();
    return cJSON_CreateString(rloc);
}

cJSON *handle_ot_resource_node_rloc16_request()
{
    uint16_t rloc16 = 0xffff;
    esp_openthread_lock_acquire(portMAX_DELAY);
    rloc16 = otThreadGetRloc16(esp_openthread_get_instance());
    esp_openthread_lock_release();
    return cJSON_CreateNumber(rloc16);
}

cJSON *handle_ot_resource_node_state_request()
{
    otDeviceRole state = 0;
    esp_openthread_lock_acquire(portMAX_DELAY);
    state = otThreadGetDeviceRole(esp_openthread_get_instance());
    char state_str[10];
    strcpy(state_str, s_ot_state[state]);
    esp_openthread_lock_release();
    return cJSON_CreateString(state_str);
}

otError handle_ot_resource_node_state_put_request(cJSON *request)
{
    otError ret = OT_ERROR_NONE;

    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *ins = esp_openthread_get_instance();
    if (cJSON_IsString(request)) {
        const char *state = cJSON_GetStringValue(request);
        if (strcmp(state, "enable") == 0) {
            if (!otIp6IsEnabled(ins)) {
                ERROR_EXIT(otIp6SetEnabled(ins, true), exit, API_TAG, "Invalid State");
            }
            ERROR_EXIT(otThreadSetEnabled(ins, true), exit, API_TAG, "Invalid State");
        } else if (strcmp(state, "disable") == 0) {
            ERROR_EXIT(otThreadSetEnabled(ins, false), exit, API_TAG, "Invalid State");
            ERROR_EXIT(otIp6SetEnabled(ins, false), exit, API_TAG, "Invalid State");
        } else {
            ret = OT_ERROR_INVALID_ARGS;
        }
    } else {
        ret = OT_ERROR_INVALID_ARGS;
    }
exit:
    esp_openthread_lock_release();
    return ret;
}

cJSON *handle_ot_resource_node_extaddress_request()
{
    char format[OT_EXT_ADDRESS_SIZE * 2 + 1];
    esp_openthread_lock_acquire(portMAX_DELAY);
    const otExtAddress *address = otLinkGetExtendedAddress(esp_openthread_get_instance());
    esp_openthread_lock_release();
    ESP_RETURN_ON_FALSE(!hex_to_string(address->m8, format, OT_EXT_ADDRESS_SIZE), NULL, API_TAG,
                        "Failed to convert thread extended address");
    return cJSON_CreateString(format);
}

cJSON *handle_ot_resource_node_network_name_request()
{
    const char *ot_network_name;
    esp_openthread_lock_acquire(portMAX_DELAY);
    ot_network_name = otThreadGetNetworkName(esp_openthread_get_instance());
    esp_openthread_lock_release();
    return cJSON_CreateString(ot_network_name);
}

cJSON *handle_ot_resource_node_leader_data_request()
{
    cJSON *root = NULL;
    otLeaderData data;
    esp_openthread_lock_acquire(portMAX_DELAY);
    if (otThreadGetLeaderData(esp_openthread_get_instance(), &data) == OT_ERROR_NONE) {
        root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "PartitionId", cJSON_CreateNumber(data.mPartitionId));
        cJSON_AddItemToObject(root, "Weighting", cJSON_CreateNumber(data.mWeighting));
        cJSON_AddItemToObject(root, "DataVersion", cJSON_CreateNumber(data.mDataVersion));
        cJSON_AddItemToObject(root, "StableDataVersion", cJSON_CreateNumber(data.mStableDataVersion));
        cJSON_AddItemToObject(root, "LeaderRouterId", cJSON_CreateNumber(data.mLeaderRouterId));
    } else {
        ESP_LOGE(API_TAG, "Failed to get thread leader data");
    }
    esp_openthread_lock_release();
    return root;
}

cJSON *handle_ot_resource_node_numofrouter_request()
{
    esp_openthread_lock_acquire(portMAX_DELAY);
    int8_t max_router_id = otThreadGetMaxRouterId(esp_openthread_get_instance());
    otRouterInfo router_info;
    uint8_t router_number = 0;
    for (uint8_t i = 0; i <= max_router_id; ++i) {
        if (otThreadGetRouterInfo(esp_openthread_get_instance(), i, &router_info) != OT_ERROR_NONE)
            continue;
        ++router_number;
    }
    esp_openthread_lock_release();
    return cJSON_CreateNumber(router_number);
}

cJSON *handle_ot_resource_node_extpanid_request()
{
    char format[OT_EXT_PAN_ID_SIZE * 2 + 1];
    esp_openthread_lock_acquire(portMAX_DELAY);
    const otExtendedPanId *extpanid = otThreadGetExtendedPanId(esp_openthread_get_instance());
    esp_openthread_lock_release();
    ESP_RETURN_ON_FALSE(!hex_to_string(extpanid->m8, format, OT_EXT_ADDRESS_SIZE), NULL, API_TAG,
                        "Failed to convert thread extended panid");
    return cJSON_CreateString(format);
}

cJSON *handle_ot_resource_node_baid_request()
{
    char format[OT_BORDER_AGENT_ID_LENGTH * 2 + 1];
    otBorderAgentId id;
    esp_openthread_lock_acquire(portMAX_DELAY);
    otError err = otBorderAgentGetId(esp_openthread_get_instance(), &id);
    esp_openthread_lock_release();
    ESP_RETURN_ON_FALSE(err == OT_ERROR_NONE, NULL, API_TAG, "Failed to get border agent id");
    ESP_RETURN_ON_FALSE(!hex_to_string(id.mId, format, OT_BORDER_AGENT_ID_LENGTH), NULL, API_TAG,
                        "Failed to convert border agent id");
    return cJSON_CreateString(format);
}

cJSON *handle_ot_resource_node_get_dataset_request(const cJSON *request, cJSON *log)
{
    uint16_t errcode = 200;
    cJSON *response = NULL;
    char format[255] = "";
    otOperationalDataset dataset;
    otOperationalDatasetTlvs datasetTlvs;
    otError ret = OT_ERROR_NONE;
    const char *accept_format =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, ESP_OT_REST_ACCEPT_HEADER));
    const char *dataset_type =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, ESP_OT_REST_DATASET_TYPE));

    if (!accept_format || !dataset_type) {
        cJSON_SetValuestring(log, "Error: Missing dataset type or accept format");
        return NULL;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *ins = esp_openthread_get_instance();
    if (strcmp(accept_format, ESP_OT_REST_CONTENT_TYPE_PLAIN) == 0) {
        if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_ACTIVE) == 0) {
            ERROR_EXIT(otDatasetGetActiveTlvs(ins, &datasetTlvs), exit, API_TAG,
                       "Failed to get Thread active dataset tlv");
        } else if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_PENDING) == 0) {
            ERROR_EXIT(otDatasetGetPendingTlvs(ins, &datasetTlvs), exit, API_TAG,
                       "Failed to get Thread pending dataset tlv");
        } else {
            ESP_GOTO_ON_FALSE(false, OT_ERROR_FAILED, exit, API_TAG, "Invalid DatasetTlv Type");
        }
        ESP_GOTO_ON_FALSE(sizeof(format) >= (datasetTlvs.mLength * 2), OT_ERROR_FAILED, exit, API_TAG, "Invalid Size");
        ESP_GOTO_ON_FALSE(hex_to_string(datasetTlvs.mTlvs, format, datasetTlvs.mLength) == ESP_OK, OT_ERROR_FAILED,
                          exit, API_TAG, "Failed to convert Thread dataset tlv");
        response = cJSON_CreateString(format);
    } else {
        if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_ACTIVE) == 0) {
            ERROR_EXIT(otDatasetGetActive(ins, &dataset), exit, API_TAG, "Failed to get Thread active dataset");
            response = ActiveDataset2Json(dataset);
        } else if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_PENDING) == 0) {
            ERROR_EXIT(otDatasetGetPending(ins, &dataset), exit, API_TAG, "Failed to get Thread pending dataset");
            response = PendingDataset2Json(dataset);
        } else {
            ESP_GOTO_ON_FALSE(false, OT_ERROR_FAILED, exit, API_TAG, "Invalid Dataset Type");
        }
    }
exit:
    esp_openthread_lock_release();
    if (ret != OT_ERROR_NONE) {
        errcode = 204;
    }
    cJSON_AddItemToObject(log, "ErrorCode", cJSON_CreateNumber(errcode));
    return response;
}

void handle_ot_resource_node_set_dataset_request(const cJSON *request, cJSON *log)
{
    uint16_t errcode = 200;
    otOperationalDataset dataset;
    otOperationalDatasetTlvs datasetTlvs;
    otOperationalDatasetTlvs datasetUpdateTlvs;
    otError ret = OT_ERROR_NONE;
    const char *content_format =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, ESP_OT_REST_CONTENT_TYPE_HEADER));
    const char *dataset_type =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, ESP_OT_REST_DATASET_TYPE));

    ESP_RETURN_VOID_ON_FALSE(dataset_type, API_TAG, "Invalid dataset type");
    ESP_RETURN_VOID_ON_FALSE(content_format, API_TAG, "Invalid content format");
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *ins = esp_openthread_get_instance();

    if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_ACTIVE) == 0) {
        ESP_GOTO_ON_FALSE(otThreadGetDeviceRole(ins) == OT_DEVICE_ROLE_DISABLED, OT_ERROR_INVALID_STATE, exit, API_TAG,
                          "Invalid State");
        ret = otDatasetGetActiveTlvs(ins, &datasetTlvs);
    } else if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_PENDING) == 0) {
        ret = otDatasetGetPendingTlvs(ins, &datasetTlvs);
    } else {
        ESP_GOTO_ON_FALSE(false, OT_ERROR_FAILED, exit, API_TAG, "Invalid Dataset Type");
    }

    if (ret == OT_ERROR_NOT_FOUND) {
        ESP_GOTO_ON_FALSE(otDatasetCreateNewNetwork(ins, &dataset) == OT_ERROR_NONE, OT_ERROR_FAILED, exit, API_TAG,
                          "Cannot create a new dataset");
        otDatasetConvertToTlvs(&dataset, &datasetTlvs);
        errcode = 201;
        ret = OT_ERROR_NONE;
    }

    bool isTlv = (strcmp(content_format, ESP_OT_REST_CONTENT_TYPE_PLAIN) == 0);

    if (isTlv) {
        char *datasetTlvsStr = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, "DatasetData"));
        ESP_GOTO_ON_FALSE(datasetTlvsStr && strlen(datasetTlvsStr) <= OT_OPERATIONAL_DATASET_MAX_LENGTH * 2,
                          OT_ERROR_INVALID_ARGS, exit, API_TAG, "Invalid DatasetTlvs");
        ESP_GOTO_ON_FALSE(string_to_hex(datasetTlvsStr, datasetUpdateTlvs.mTlvs, strlen(datasetTlvsStr) / 2) == ESP_OK,
                          OT_ERROR_INVALID_ARGS, exit, API_TAG, "Invalid DatasetTlvs");
        datasetUpdateTlvs.mLength = strlen(datasetTlvsStr) / 2;
        ERROR_EXIT(otDatasetParseTlvs(&datasetUpdateTlvs, &dataset), exit, API_TAG, "Invalid DatasetTlvs");
        ERROR_EXIT(otDatasetUpdateTlvs(&dataset, &datasetTlvs), exit, API_TAG, "Cannot update DatasetTlvs");
    } else {
        cJSON *datasetJson = cJSON_GetObjectItemCaseSensitive(request, "DatasetData");
        if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_ACTIVE) == 0) {
            ESP_GOTO_ON_FALSE(Json2ActiveDataset(datasetJson, &dataset) == ESP_OK, OT_ERROR_INVALID_ARGS, exit, API_TAG,
                              "Invalid Active Dataset");
        } else if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_PENDING) == 0) {
            ESP_GOTO_ON_FALSE(Json2PendingDataset(datasetJson, &dataset) == ESP_OK, OT_ERROR_INVALID_ARGS, exit,
                              API_TAG, "Invalid Pending Dataset");
        } else {
            ESP_GOTO_ON_FALSE(false, OT_ERROR_FAILED, exit, API_TAG, "Invalid Dataset Type");
        }
        ERROR_EXIT(otDatasetUpdateTlvs(&dataset, &datasetTlvs), exit, API_TAG, "Cannot update DatasetTlvs");
    }

    if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_ACTIVE) == 0) {
        ERROR_EXIT(otDatasetSetActiveTlvs(ins, &datasetTlvs), exit, API_TAG, "Cannot set Active DatasetTlvs");
    } else if (strcmp(dataset_type, ESP_OT_DATASET_TYPE_PENDING) == 0) {
        ERROR_EXIT(otDatasetSetPendingTlvs(ins, &datasetTlvs), exit, API_TAG, "Cannot set Pending DatasetTlvs");
    } else {
        ESP_GOTO_ON_FALSE(false, OT_ERROR_FAILED, exit, API_TAG, "Invalid Dataset Type");
    }

exit:
    esp_openthread_lock_release();

    if (ret != OT_ERROR_NONE) {
        switch (ret) {
        case OT_ERROR_INVALID_ARGS:
            errcode = 400;
            break;
        case OT_ERROR_INVALID_STATE:
            errcode = 409;
            break;
        default:
            errcode = 500;
            break;
        }
    }
    cJSON_AddItemToObject(log, "ErrorCode", cJSON_CreateNumber(errcode));
}

/*----------------------------------------------------------------------
                                thread status
----------------------------------------------------------------------*/
static esp_err_t get_openthread_ipv6_properties(otInstance *ins, thread_ipv6_status_t *ipv6)
{
    ESP_RETURN_ON_FALSE(ins && ipv6, ESP_FAIL, API_TAG, "Invalid instance or ipv6 information");
    memcpy(&ipv6->link_local_address, otThreadGetLinkLocalIp6Address(ins),
           sizeof(otIp6Address));                                                           /* 1. linkLocalAddress */
    memcpy(&ipv6->routing_local_address, otThreadGetRloc(ins), sizeof(otIp6Address));       /* 2. routingLocalAddress */
    memcpy(&ipv6->mesh_local_address, otThreadGetMeshLocalEid(ins), sizeof(otIp6Address));  /* 3. meshLocalAddress */
    memcpy(&ipv6->mesh_local_prefix, otThreadGetMeshLocalPrefix(ins), sizeof(otIp6Prefix)); /* 4. meshLocalPrefix */

    return ESP_OK;
}

static esp_err_t get_openthread_network_properties(otInstance *ins, thread_network_status_t *network)
{
    ESP_RETURN_ON_FALSE(ins && network, ESP_FAIL, API_TAG, "Invalid instance or openthread network");
    char output[64] = "";
    sprintf(output, "%s", otThreadGetNetworkName(ins));
    ESP_RETURN_ON_FALSE(strlen(output) < sizeof(otNetworkName), ESP_FAIL, API_TAG, "Network name is too long");

    memcpy(&network->name, output, strlen(output) + 1);                               /* 1. name */
    network->panid = otLinkGetPanId(ins);                                             /* 2. PANID */
    network->partition_id = otThreadGetPartitionId(ins);                              /* 3. paritionID */
    memcpy(&network->xpanid, otThreadGetExtendedPanId(ins), sizeof(otExtendedPanId)); /* 4. XPANID */
    otBorderAgentGetId(ins, &network->baid);                                          /* 5. border agent id*/
    return ESP_OK;
}

static esp_err_t get_openthread_information_properties(otInstance *ins, thread_information_status_t *ot_info)
{
    ESP_RETURN_ON_FALSE(ins && ot_info, ESP_FAIL, API_TAG, "Invalid instance or openthread information");
    ot_info->version = (char *)otGetVersionString(); /* 1. version */
    ot_info->version_api = OPENTHREAD_API_VERSION;   /* 2. version_api */
    ot_info->role = otThreadGetDeviceRole(ins);      /* 3. role */
    otThreadGetPskc(ins, &ot_info->PSKc);            /* 4. PSKc */
    return ESP_OK;
}

static esp_err_t get_openthread_rcp_properties(otInstance *ins, thread_rcp_status_t *rcp)
{
    otError error = OT_ERROR_NONE;
    ESP_RETURN_ON_FALSE(ins && rcp, ESP_FAIL, API_TAG, "Invalid instance or openthread rcp");
    rcp->channel = otLinkGetChannel(ins);                    /* 1. channel */
    error = otPlatRadioGetTransmitPower(ins, &rcp->txpower); /* 2. txPower */
    otLinkGetFactoryAssignedIeeeEui64(ins, &rcp->EUI64);     /* 3. eui */
    rcp->version = (char *)otPlatRadioGetVersionString(ins); /* 4. rcp Version */

    return !error ? ESP_OK : ESP_FAIL;
}

static esp_err_t get_openthread_wpan_properties(otInstance *ins, thread_wpan_status_t *wpan)
{
    ESP_RETURN_ON_FALSE(ins && wpan, ESP_FAIL, API_TAG, "Invalid instance or openthread wpan");
    const char *role = otThreadDeviceRoleToString(otThreadGetDeviceRole(ins));
    ESP_RETURN_ON_FALSE(ins && role, ESP_FAIL, API_TAG, "Failed to get thread device role");
    if (!strcmp(role, "disabled")) {
        memcpy(wpan->service, WPAN_STATUS_OFFLINE, sizeof(WPAN_STATUS_OFFLINE));
    } else if (!strcmp(role, "detached")) {
        memcpy(wpan->service, WPAN_STATUS_ASSOCIATING, sizeof(WPAN_STATUS_ASSOCIATING));
    } else {
        memcpy(wpan->service, WPAN_STATUS_ASSOCIATED, sizeof(WPAN_STATUS_ASSOCIATED));
    }

    return ESP_OK;
}

static esp_err_t get_openthread_properties(openthread_properties_t *properties)
{
    esp_err_t ret = ESP_OK;
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *ins = esp_openthread_get_instance(); /* get the api of openthread */
    ESP_GOTO_ON_FALSE(ins, ESP_FAIL, exit, API_TAG, "Failed to get openthread instance");

    /* get openthread border router information */
    ESP_GOTO_ON_ERROR(get_openthread_ipv6_properties(ins, &properties->ipv6), exit, API_TAG,
                      "Failed to get status of ipv6"); /* ipv6 */
    ESP_GOTO_ON_ERROR(get_openthread_network_properties(ins, &properties->network), exit, API_TAG,
                      "Failed to get status of network"); /* network */
    ESP_GOTO_ON_ERROR(get_openthread_information_properties(ins, &properties->information), exit, API_TAG,
                      "Failed to get status of information"); /* ot information */
    ESP_GOTO_ON_ERROR(get_openthread_rcp_properties(ins, &properties->rcp), exit, API_TAG,
                      "Failed to get status of rcp"); /* rcp */
    ESP_GOTO_ON_ERROR(get_openthread_wpan_properties(ins, &properties->wpan), exit, API_TAG,
                      "Failed to get status of wpan"); /* wpan */

exit:
    esp_openthread_lock_release();
    return ret;
}

cJSON *handle_openthread_network_properties_request()
{
    otError ret = OT_ERROR_NONE;
    cJSON *root = NULL;
    openthread_properties_t properties;
    otbr_properties_reset(&properties);
    ESP_GOTO_ON_FALSE(!get_openthread_properties(&properties), OT_ERROR_FAILED, exit, API_TAG,
                      "Failed to get openthread status");
    root = otbr_properties_struct_convert2_json(&properties);
exit:
    return ret ? NULL : root;
}

/*----------------------------------------------------------------------
               scan thread available networks
----------------------------------------------------------------------*/
static thread_network_list_t *s_networkList = NULL;
static uint8_t s_networkList_count = 0;
/* s_discover_done_semaphore is initialized in esp_br_web_api_init() */

static void build_availableNetworks_list(otActiveScanResult *result)
{
    s_networkList_count++;
    thread_network_information_t network;

    network.id = s_networkList_count;
    memcpy(&network.network_name, &result->mNetworkName, sizeof(otNetworkName));
    memcpy(&network.extended_panid.m8, &result->mExtendedPanId.m8, sizeof(otExtendedPanId));
    network.panid = result->mPanId;
    memcpy(&network.extended_address.m8, &result->mExtAddress.m8, sizeof(otExtAddress));
    network.channel = result->mChannel;
    network.rssi = result->mRssi;
    network.lqi = result->mLqi;

    append_available_thread_networks_list(s_networkList, network);

    ESP_LOGI(API_TAG, "<===================== Thread Scan ========================>");
    ESP_LOGI(API_TAG, "Find available network");
    ESP_LOGI(API_TAG, "ID: %d, Network Name: %s", network.id, network.network_name.m8);
    ESP_LOGI(API_TAG, "<==========================================================>");
    return;
}

static void handle_active_scan_event(otActiveScanResult *aResult, void *aContext)
{
    if ((aResult) == NULL) {
        xSemaphoreGive(s_discover_done_semaphore);
    } else {
        build_availableNetworks_list(aResult);
    }
}

static otError get_openthread_available_networks(void)
{
    otError ret = OT_ERROR_NONE;
    uint32_t scanChannels = 0;
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *ins = esp_openthread_get_instance();
    if (!otIp6IsEnabled(ins)) {
        ESP_GOTO_ON_FALSE(OT_ERROR_NONE == (ret = otIp6SetEnabled(ins, true)), ret, exit, API_TAG,
                          "Failed to enable IPv6 interface for scanning");
    }
    ESP_GOTO_ON_FALSE(OT_ERROR_NONE ==
                          (ret = otThreadDiscover(ins, scanChannels, OT_PANID_BROADCAST, false, false,
                                                  &handle_active_scan_event, NULL)),
                      ret, exit, API_TAG, "Failed to discover network");
exit:
    esp_openthread_lock_release();
    return ret;
}

cJSON *handle_openthread_available_network_request(void)
{
    otError ret = OT_ERROR_NONE;
    cJSON *networks = cJSON_CreateArray();

    destroy_available_thread_networks_list(s_networkList);

    s_networkList = (thread_network_list_t *)malloc(sizeof(thread_network_list_t));
    ESP_GOTO_ON_FALSE(s_networkList, OT_ERROR_NO_BUFS, exit, API_TAG, "Failed to alloc network list");
    s_networkList_count = 0;

    initialize_available_thread_networks_list(s_networkList);

    ESP_GOTO_ON_FALSE(!get_openthread_available_networks(), OT_ERROR_FAILED, exit, API_TAG,
                      "Failed to get thread network list");
    xSemaphoreTake(s_discover_done_semaphore, portMAX_DELAY);

    ESP_GOTO_ON_FALSE(s_networkList, OT_ERROR_INVALID_ARGS, exit, API_TAG, "Invalid network list");

    thread_network_list_t *head = s_networkList->next; /* skip head node */

    while (head != NULL) {
        cJSON *node = available_network_struct_convert2_json(head->network);
        cJSON_AddItemToArray(networks, node);
        head = head->next;
    }

exit:
    if (ret) {
        cJSON_Delete(networks);
        return NULL;
    }
    return networks;
}

/*----------------------------------------------------------------------
                       form thread network
----------------------------------------------------------------------*/
otError handle_openthread_form_network_request(const cJSON *request, cJSON *log)
{
    otError ret = OT_ERROR_NONE;
    thread_network_formation_param_t param;
    network_formation_param_reset(&param);
    ESP_RETURN_ON_FALSE(log, OT_ERROR_INVALID_ARGS, API_TAG, "Form log requires cJSON String type");
    ESP_RETURN_ON_FALSE(!network_formation_param_json_convert2_struct(request, log, &param), OT_ERROR_INVALID_ARGS,
                        API_TAG, "Failed to parse FORM request");

    otInstance *ins = esp_openthread_get_instance();
    otOperationalDataset dataset;
    add_prefix_field(param.on_mesh_prefix);

    esp_openthread_lock_acquire(portMAX_DELAY);
    ret = OT_ERROR_FAILED;
    ERROR_EXIT(otThreadSetEnabled(ins, false), exit, API_TAG, "Failed to stop Thread");
    ESP_LOGI(API_TAG, "thread stop");

    ERROR_EXIT(otIp6SetEnabled(ins, false), exit, API_TAG, "Failed to execute config down");
    ESP_LOGI(API_TAG, "ifconfig down");

    ERROR_EXIT(otDatasetCreateNewNetwork(ins, &dataset), exit, API_TAG, "Failed to create new network");
    dataset.mNetworkName = param.network_name;
    dataset.mNetworkKey = param.network_key;
    dataset.mChannel = param.channel;
    dataset.mExtendedPanId = param.extended_panid;
    dataset.mPanId = param.panid;
    ESP_LOGI(API_TAG, "dataset init new");

    ERROR_EXIT(otDatasetSetActive(ins, &dataset), exit, API_TAG, "Failed to set active dataset");
    ESP_LOGI(API_TAG, "dataset submit active");

    ERROR_EXIT(otIp6SetEnabled(ins, true), exit, API_TAG, "Failed to execute ifconfig up");
    ESP_LOGI(API_TAG, "ifconfig up");

    ERROR_EXIT(otThreadSetEnabled(ins, true), exit, API_TAG, "Failed to start thread");
    ESP_LOGI(API_TAG, "thread start");

    ret = OT_ERROR_NONE;
exit:
    esp_openthread_lock_release();
    if (ret == OT_ERROR_NONE)
        cJSON_SetValuestring(log, "Submit successful, forming...");
    else
        cJSON_SetValuestring(log, "Form Network: Failure");
    return ret;
}

/*----------------------------------------------------------------------
                       join thread network
----------------------------------------------------------------------*/
static otError s_join_state = OT_ERROR_NONE;
/* s_join_done_semaphore is initialized in esp_br_web_api_init() */

/**
 * @brief The function is the callback of otJoinerStart()
 *
 */
static void join_network_handler(otError error, void *context)
{
    s_join_state = error;
    switch (error) {
    case OT_ERROR_NONE:
        ESP_LOGI(API_TAG, "Join success");
        break;

    default:
        ESP_LOGI(API_TAG, "Join failed [%s]", otThreadErrorToString(error));
        break;
    }
    if (s_join_done_semaphore) {
        xSemaphoreGive(s_join_done_semaphore);
    }
}

static esp_err_t network_prefix_add(otBorderRouterConfig *config)
{
    char prefix_str[OT_IP6_ADDRESS_STRING_SIZE];
    otIp6AddressToString(&config->mPrefix.mPrefix, prefix_str, OT_IP6_ADDRESS_STRING_SIZE);
    ESP_LOGW(API_TAG, "Add on mesh prefix: %s/%d", prefix_str, config->mPrefix.mLength);

    ESP_RETURN_ON_FALSE(!otBorderRouterAddOnMeshPrefix(esp_openthread_get_instance(), config), ESP_FAIL, API_TAG,
                        "Failed to add mesh prefix");
    return ESP_OK;
}

otError handle_openthread_join_network_request(const cJSON *request, cJSON *log)
{
    otError ret = OT_ERROR_NONE;
    thread_network_join_param_t param;
    thread_network_list_t *list = s_networkList;
    uint8_t count = s_networkList_count;
    otOperationalDataset dataset;
    otBorderRouterConfig config;
    otInstance *ins = esp_openthread_get_instance();

    network_join_param_reset(&param);
    ESP_RETURN_ON_FALSE(log, OT_ERROR_INVALID_ARGS, API_TAG, "Join log requires cJSON String type");
    ESP_RETURN_ON_FALSE(!network_join_param_json_convert2_struct(request, log, &param), OT_ERROR_INVALID_ARGS, API_TAG,
                        "Failed to parse JOIN request");
    /* join active dataset */
    if (!memcmp(param.credentialType, CREDENTIAL_TYPE_NETWORK_KEY, sizeof(CREDENTIAL_TYPE_NETWORK_KEY))) {
        cJSON_SetValuestring(log, "Warning: Click `scan` and Try against");
        ESP_RETURN_ON_FALSE(list && count, OT_ERROR_INVALID_ARGS, API_TAG,
                            "Try against after scanning the available network");
        cJSON_SetValuestring(log, "Error: Invalid Index");
        ESP_RETURN_ON_FALSE(param.index <= count, OT_ERROR_INVALID_ARGS, API_TAG, "Error: Invalid index[%d]",
                            param.index);
        while (list) {
            if (list->network->id == param.index)
                break;
            list = list->next;
        }
        cJSON_SetValuestring(log, "Error: Can not find network");
        ESP_RETURN_ON_FALSE(list, OT_ERROR_INVALID_STATE, API_TAG, "Cannot find network, try against");

        esp_openthread_lock_acquire(portMAX_DELAY);

        ERROR_EXIT(otThreadSetEnabled(ins, false), exit, API_TAG, "Failed to stop Thread");
        ERROR_EXIT(otIp6SetEnabled(ins, false), exit, API_TAG, "Failed to set ifconfig down");

        memset(&dataset, 0, sizeof(otOperationalDataset));
        dataset.mChannel = list->network->channel;
        dataset.mComponents.mIsChannelPresent = true;
        dataset.mPanId = list->network->panid;
        dataset.mComponents.mIsPanIdPresent = true;
        dataset.mNetworkKey = param.networkKey;
        dataset.mComponents.mIsNetworkKeyPresent = true;

        ERROR_EXIT(otDatasetSetActive(ins, &dataset), exit, API_TAG, "Failed to active dataset");
        ERROR_EXIT(otIp6SetEnabled(ins, true), exit, API_TAG, "Failed to set ifconfig up");
    } else if ((!memcmp(param.credentialType, CREDENTIAL_TYPE_PSKD, sizeof(CREDENTIAL_TYPE_PSKD)))) {
        esp_openthread_lock_acquire(portMAX_DELAY);

        ERROR_EXIT(otIp6SetEnabled(ins, false), exit, API_TAG, "Failed to set ifconfig down");
        ERROR_EXIT(otIp6SetEnabled(ins, true), exit, API_TAG, "Failed to set ifconfig up");

        s_join_state = OT_ERROR_GENERIC;
        ERROR_EXIT(otJoinerStart(ins, param.pskd, NULL, "openthread-esp32s3", CONFIG_IDF_TARGET, "a17d2505a0-091f68ed7",
                                 NULL, &join_network_handler, NULL),
                   exit, API_TAG, "Failed to start joiner");

        /* Release lock while waiting for joiner callback to avoid blocking other requests */
        esp_openthread_lock_release();
        if (xSemaphoreTake(s_join_done_semaphore, pdMS_TO_TICKS(10000)) != pdTRUE) {
            ESP_LOGW(API_TAG, "Joiner timed out waiting for callback");
            ret = OT_ERROR_RESPONSE_TIMEOUT;
            goto exit_no_unlock;
        }
        esp_openthread_lock_acquire(portMAX_DELAY);
        ERROR_EXIT(s_join_state, exit, API_TAG, "Failed to join network");
    } else {
        ret = OT_ERROR_PARSE;
        goto exit_no_unlock;
    }
    ERROR_EXIT(otThreadSetEnabled(ins, true), exit, API_TAG, "Failed to thread start");

    /* Only add on-mesh prefix if one was provided */
    if (param.prefix[0] != '\0') {
        memset(&config, 0x00, sizeof(otBorderRouterConfig));
        config.mPreferred = true;
        config.mSlaac = true;
        config.mStable = true;
        config.mOnMesh = true;
        config.mDefaultRoute = param.defaultRoute;
        ERROR_EXIT(parse_ipv6_prefix_from_string(param.prefix, &config.mPrefix), exit, API_TAG,
                   "Failed to parse prefix");
        ERROR_EXIT(network_prefix_add(&config), exit, API_TAG, "Failed to add thread prefix");
    }

exit:
    esp_openthread_lock_release();
exit_no_unlock:
    if (ret == OT_ERROR_NONE)
        cJSON_SetValuestring(log, "Join request submitted, attaching...");
    else
        cJSON_SetValuestring(log, "Join Network: Failure");
    return ret;
}

/*----------------------------------------------------------------------
                    thread network settings
----------------------------------------------------------------------*/
otError handle_openthread_add_network_prefix_request(const cJSON *request)
{
    otError ret = OT_ERROR_NONE;
    char str_prefix[OT_IP6_PREFIX_STRING_SIZE];
    bool default_route = false;
    otBorderRouterConfig config;

    ESP_RETURN_ON_FALSE(request, OT_ERROR_INVALID_ARGS, API_TAG, "Failed to parse the json type of prefix");
    char *str = cJSON_GetStringValue(cJSON_GetObjectItem(request, "prefix"));
    ESP_RETURN_ON_FALSE(str, OT_ERROR_INVALID_ARGS, API_TAG, "Failed to get prefix");
    ESP_RETURN_ON_FALSE(strlen(str) + 1 < OT_IP6_PREFIX_STRING_SIZE, OT_ERROR_INVALID_ARGS, API_TAG, "Prefix too long");
    memset(str_prefix, 0x00, OT_IP6_PREFIX_STRING_SIZE);
    memcpy(str_prefix, str, strlen(str) + 1);
    cJSON *default_route_item = cJSON_GetObjectItem(request, "defaultRoute");
    ESP_RETURN_ON_FALSE(default_route_item, OT_ERROR_INVALID_ARGS, API_TAG,
                        "Failed to get prefix or parameter from root");
    default_route = cJSON_IsTrue(default_route_item);

    memset(&config, 0x00, sizeof(otBorderRouterConfig));
    config.mPreferred = true;
    config.mSlaac = true;
    config.mStable = true;
    config.mOnMesh = true;
    config.mDefaultRoute = default_route;
    ESP_RETURN_ON_FALSE(!parse_ipv6_prefix_from_string(str_prefix, &config.mPrefix), OT_ERROR_FAILED, API_TAG,
                        "Failed to parse prefix");
    esp_openthread_lock_acquire(portMAX_DELAY);
    ERROR_EXIT(network_prefix_add(&config), exit, API_TAG, "Failed to add thread prefix");
#if OPENTHREAD_CONFIG_BORDER_ROUTER_ENABLE
    ERROR_EXIT(otBorderRouterRegister(esp_openthread_get_instance()), exit, API_TAG, "Failed to register in data net");
#else
    ERROR_EXIT(otServerRegister(esp_openthread_get_instance()), exit, API_TAG, "Failed to register in data net");
#endif

exit:
    esp_openthread_lock_release();
    return ret;
}

otError handle_openthread_delete_network_prefix_request(const cJSON *request)
{
    otError ret = OT_ERROR_NONE;
    char str_prefix[OT_IP6_PREFIX_STRING_SIZE];
    otIp6Prefix ip6_prefix;
    ESP_RETURN_ON_FALSE(request, OT_ERROR_INVALID_ARGS, API_TAG, "Failed to parse the json type of prefix");
    char *str = cJSON_GetStringValue(cJSON_GetObjectItem(request, "prefix"));
    ESP_RETURN_ON_FALSE(str, OT_ERROR_INVALID_ARGS, API_TAG, "Failed to get prefix");
    ESP_RETURN_ON_FALSE(strlen(str) + 1 < OT_IP6_PREFIX_STRING_SIZE, OT_ERROR_INVALID_ARGS, API_TAG, "Prefix too long");
    memset(str_prefix, 0x00, OT_IP6_PREFIX_STRING_SIZE);
    memcpy(str_prefix, str, strlen(str) + 1);
    ESP_RETURN_ON_FALSE(!parse_ipv6_prefix_from_string(str_prefix, &ip6_prefix), OT_ERROR_FAILED, API_TAG,
                        "Failed to parse prefix");

    esp_openthread_lock_acquire(portMAX_DELAY);
    ERROR_EXIT(otBorderRouterRemoveOnMeshPrefix(esp_openthread_get_instance(), &ip6_prefix), exit, API_TAG,
               "Failed to remove thread prefix");
#if OPENTHREAD_CONFIG_BORDER_ROUTER_ENABLE
    ERROR_EXIT(otBorderRouterRegister(esp_openthread_get_instance()), exit, API_TAG, "Failed to register in data net");
#else
    ERROR_EXIT(otServerRegister(esp_openthread_get_instance()), exit, API_TAG, "Failed to register in data net");
#endif

exit:
    esp_openthread_lock_release();
    return ret;
}

/*----------------------------------------------------------------------
                    thread network commission
----------------------------------------------------------------------*/
static void handle_commissioner_state_changed(otCommissionerState state, void *ctx)
{
    ESP_LOGW(API_TAG, "Commissioner State: %d", state);
}
static void handle_commissioner_join_event(otCommissionerJoinerEvent event, const otJoinerInfo *info,
                                           const otExtAddress *address, void *aContext)
{
    static const char *const kEventStrings[] = {
        "start",    // (0) OT_COMMISSIONER_JOINER_START
        "connect",  // (1) OT_COMMISSIONER_JOINER_CONNECTED
        "finalize", // (2) OT_COMMISSIONER_JOINER_FINALIZE
        "end",      // (3) OT_COMMISSIONER_JOINER_END
        "remove",   // (4) OT_COMMISSIONER_JOINER_REMOVED
    };

    ESP_LOGI(API_TAG, "Commissioner: Joiner %s", kEventStrings[(uint8_t)event]);
    if (address) {
        ESP_LOGI(API_TAG, "Commissioner: Joiner address %x:%x:%x:%x:%x:%x:%x:%x", address->m8[7], address->m8[6],
                 address->m8[5], address->m8[4], address->m8[3], address->m8[2], address->m8[1], address->m8[0]);
    }
}

otError handle_openthread_network_commission_request(const cJSON *request)
{
    char *pskd = NULL;
    otInstance *ins = esp_openthread_get_instance();
    otError ret = OT_ERROR_NONE;

    ESP_RETURN_ON_FALSE(request, OT_ERROR_INVALID_ARGS, API_TAG, "Failed to parse pskd");
    pskd = cJSON_GetStringValue(cJSON_GetObjectItem(request, "pskd"));
    ESP_RETURN_ON_FALSE(pskd, OT_ERROR_INVALID_ARGS, API_TAG, "Failed to get pskd value");

    esp_openthread_lock_acquire(portMAX_DELAY);
    for (int i = 0; i < 5; i++) {
        switch (otCommissionerGetState(ins)) {
        case OT_COMMISSIONER_STATE_DISABLED:
            ESP_LOGI(API_TAG, "commissioner[disable]");
            ERROR_EXIT(
                otCommissionerStart(ins, &handle_commissioner_state_changed, &handle_commissioner_join_event, NULL),
                exit, API_TAG, "Failed to start commission");
            break;
        case OT_COMMISSIONER_STATE_PETITION:
            ESP_LOGI(API_TAG, "commissioner[petition]");
            break;
        case OT_COMMISSIONER_STATE_ACTIVE: {
            otJoinerDiscerner discerner;
            uint32_t timeout = 120; // s
            memset(&discerner, 0, sizeof(discerner));
            ESP_LOGI(API_TAG, "commissioner[active]");

            if (discerner.mLength) {
                ERROR_EXIT(otCommissionerAddJoinerWithDiscerner(ins, &discerner, pskd, timeout), exit, API_TAG,
                           "Failed to add joiner with discerner");
            } else {
                ERROR_EXIT(otCommissionerAddJoiner(ins, NULL, pskd, timeout), exit, API_TAG, "Failed to add joiner");
            }
        } break;
        default:
            ESP_LOGE(API_TAG, "Invalid commissioner state");
            break;
        }
    }
exit:
    esp_openthread_lock_release();
    return ret;
}

/*----------------------------------------------------------------------
                       thread network Topology
----------------------------------------------------------------------*/
static thread_diagnosticTlv_set_t *s_diagnosticTlv_set = NULL;
/* s_diagnostic_semaphore is initialized in esp_br_web_api_init() */
/* Only request TLV types the topology page actually uses:
 *   0=ExtAddress, 1=Address16, 2=Mode, 5=Route, 6=LeaderData,
 *   8=IPv6AddressList, 16=ChildTable */
static const uint8_t kAllTlvTypes[] = {0, 1, 2, 5, 6, 8, 16};
static const char *kMulticastAddrAllRouters = "ff03::2";
static volatile bool s_diag_collecting = false;           /* true while actively collecting responses */
static volatile int s_diag_response_count = 0;            /* number of responses received this collection */
static volatile TickType_t s_diag_last_response_tick = 0; /* tick of most recent response */
#define DIAG_QUIET_PERIOD_MS 3000                         /* stop after no new response for 3s */
#define DIAG_MIN_WAIT_MS 2000                             /* always wait at least 2s */
#define DIAG_MAX_TIMEOUT_MS 30000                         /* hard cap to avoid blocking forever */
#define DIAG_POLL_INTERVAL_MS 500                         /* polling interval */

/**
 * @brief Update the diagnostic Tlv set with @param key and @param diag_list
 *
 */
static void update_diagnosticTlv(char *key, thread_diagnosticTlv_list_t *diag_list)
{
    thread_diagnosticTlv_list_t *head = diag_list->next; /* skip the first invalid node. */
    free(diag_list->diagTlv);                            /* avoid to lack memory, free the first invalid node. */
    free(diag_list);
    update_thread_diagnosticTlv_set(s_diagnosticTlv_set, key, head);
}

/**
 * @brief Get the result of Thread diagnostic for Thread's topology and update the diagnostic set.
 *
 * @param[in]  aError        The error when failed to get the response.
 * @param[in]  aMessage      A pointer to the message buffer containing the received Network Diagnostic
 *                           Get response payload. Available only when @param aError is `OT_ERROR_NONE`.
 * @param[in]  aMessageInfo  A pointer to the message info for @param aMessage. Available only when
 *                           @param aError is `OT_ERROR_NONE`.
 */
static void get_diagnosticTlv_information(otError aError, const otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    thread_diagnosticTlv_list_t *diag_list = (thread_diagnosticTlv_list_t *)malloc(sizeof(thread_diagnosticTlv_list_t));
    if (diag_list == NULL) {
        ESP_LOGW(API_TAG, "Diagnostic: out of memory, skipping node");
        return;
    }
    otNetworkDiagTlv diagTlv;
    otNetworkDiagIterator iterator = OT_NETWORK_DIAGNOSTIC_ITERATOR_INIT;
    otError error;
    char rloc[7];
    char keyRloc[7] = "0xffee";
    initialize_thread_diagnosticTlv_list(diag_list);
    while ((error = otThreadGetNextDiagnosticTlv(aMessage, &iterator, &diagTlv)) == OT_ERROR_NONE) {
        if (diagTlv.mType == OT_NETWORK_DIAGNOSTIC_TLV_SHORT_ADDRESS) {
            sprintf(rloc, "0x%04x", diagTlv.mData.mAddr16);
            strcpy(keyRloc, rloc);
        }
        append_thread_diagnosticTlv_list(diag_list, diagTlv);
    }
    update_diagnosticTlv(keyRloc, diag_list);
}

/**
 * @brief A callback for `otThreadSendDiagnosticGet()` to form the diagnostic set.
 *
 * @param[in]  aError        The error when failed to get the response.
 * @param[in]  aMessage      A pointer to the message buffer containing the received Network Diagnostic
 *                           Get response payload. Available only when @p aError is `OT_ERROR_NONE`.
 * @param[in]  aMessageInfo  A pointer to the message info for @p aMessage. Available only when
 *                           @p aError is `OT_ERROR_NONE`.
 * @param[in]  aContext      A pointer to application-specific context.
 *
 */
#define DIAG_MIN_FREE_HEAP 20000 /* bytes – stop collecting before starving OpenThread */

static void diagnosticTlv_result_handler(otError aError, otMessage *aMessage, const otMessageInfo *aMessageInfo,
                                         void *aContext)
{
    if (aError == OT_ERROR_NONE) {
        /* Skip child nodes — the topology only needs router data.
           Children are already represented in their parent router's ChildTable TLV.
           Diagnostic responses are sent from RLOC addresses where the
           lower 10 bits encode the child ID (0 for routers).

           Note: the RLOC16 filtering and heap check below happen outside the
           s_diagnostic_semaphore, creating a small TOCTOU window with
           s_diag_collecting. This is acceptable on ESP32 where the OT task
           is pinned to a single core and callback ordering is deterministic. */
        const uint8_t *src = aMessageInfo->mPeerAddr.mFields.m8;
        uint16_t rloc16 = ((uint16_t)src[14] << 8) | src[15];
        if ((rloc16 & 0x03FF) != 0) {
            return;
        }

        if (esp_get_free_heap_size() < DIAG_MIN_FREE_HEAP) {
            ESP_LOGW(API_TAG, "Diagnostic callback: low heap (%lu), dropping result",
                     (unsigned long)esp_get_free_heap_size());
            return;
        }
        if (xSemaphoreTake(s_diagnostic_semaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (s_diag_collecting) {
                get_diagnosticTlv_information(aError, aMessage, aMessageInfo);
                s_diag_response_count++;
                s_diag_last_response_tick = xTaskGetTickCount();
            }
            xSemaphoreGive(s_diagnostic_semaphore);
        } else {
            ESP_LOGW(API_TAG, "Diagnostic callback: failed to acquire semaphore, dropping result");
        }
    }
}

/**
 * @brief the function will send diagnostic to get network's topology message and update the set of diagnostic.
 *
 */
static esp_err_t build_thread_network_topology(void)
{
    esp_err_t ret = ESP_OK;
    otInstance *ins = esp_openthread_get_instance();
    esp_openthread_lock_acquire(portMAX_DELAY);
    otIp6Address rloc16address = *otThreadGetRloc(ins);
    otIp6Address multicastAddress;
    ESP_GOTO_ON_FALSE(otThreadSendDiagnosticGet(ins, &rloc16address, kAllTlvTypes, sizeof(kAllTlvTypes),
                                                &diagnosticTlv_result_handler, NULL) == OT_ERROR_NONE,
                      ESP_FAIL, exit, API_TAG, "Fail to send diagnostic rloc16address.");
    ESP_GOTO_ON_FALSE(otIp6AddressFromString(kMulticastAddrAllRouters, &multicastAddress) == OT_ERROR_NONE, ESP_FAIL,
                      exit, API_TAG, "Fail to convert ipv6 to string.");
    ESP_GOTO_ON_FALSE(otThreadSendDiagnosticGet(ins, &multicastAddress, kAllTlvTypes, sizeof(kAllTlvTypes),
                                                &diagnosticTlv_result_handler, NULL) == OT_ERROR_NONE,
                      ESP_FAIL, exit, API_TAG, "Fail to send diagnostic multicastAddress.");
exit:
    esp_openthread_lock_release();
    return ret;
}

static thread_node_information_t get_openthread_node_information(otInstance *ins)
{
    thread_node_information_t node;
    thread_node_information_reset(&node);
    if (otThreadGetLeaderData(ins, &node.leader_data))
        ESP_LOGW(API_TAG, "Fail to get node's leader data.");

    node.role = otThreadGetDeviceRole(ins);
    node.rloc16 = otThreadGetRloc16(ins);
    memcpy(&node.extended_address, otLinkGetExtendedAddress(ins), OT_EXT_ADDRESS_SIZE);
    memcpy(&node.network_name, otThreadGetNetworkName(ins), OT_NETWORK_NAME_MAX_SIZE);
    memcpy(&node.extended_panid, otThreadGetExtendedPanId(ins), OT_EXT_PAN_ID_SIZE);
    memcpy(node.rloc_address.mFields.m8, otThreadGetRloc(ins), OT_IP6_ADDRESS_SIZE);

    uint8_t maxRouterId = otThreadGetMaxRouterId(ins);
    otRouterInfo router_info;
    node.router_number = 0;
    for (uint8_t i = 0; i <= maxRouterId; ++i) {
        if (otThreadGetRouterInfo(ins, i, &router_info) != OT_ERROR_NONE)
            continue;
        ++node.router_number;
    }
    return node;
}

cJSON *handle_ot_resource_node_information_request()
{
    esp_openthread_lock_acquire(portMAX_DELAY);
    thread_node_information_t node = get_openthread_node_information(esp_openthread_get_instance());
    esp_openthread_lock_release();
    return thread_node_struct_convert2_json(&node);
}

otError handle_ot_resource_node_delete_information_request(void)
{
    otError ret = OT_ERROR_NONE;
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *ins = esp_openthread_get_instance();
    ERROR_EXIT(otThreadSetEnabled(ins, false), exit, API_TAG, "Failed to stop Thread");
    ERROR_EXIT(otIp6SetEnabled(ins, false), exit, API_TAG, "Failed to execute config down");
    ERROR_EXIT(otInstanceErasePersistentInfo(ins), exit, API_TAG, "Failed to delete node information");
exit:
    esp_openthread_lock_release();
    return ret;
}

cJSON *handle_ot_resource_network_diagnostics_request()
{
    /* Stop accepting any late callbacks from a previous collection */
    xSemaphoreTake(s_diagnostic_semaphore, portMAX_DELAY);
    s_diag_collecting = false;

    destroy_thread_diagnosticTlv_set(s_diagnosticTlv_set);
    s_diagnosticTlv_set = NULL;
    s_diagnosticTlv_set = (thread_diagnosticTlv_set_t *)malloc(sizeof(thread_diagnosticTlv_set_t));
    initialize_thread_diagnosticTlv_set(s_diagnosticTlv_set, OPENTHREAD_INVALID_RLOC16);

    s_diag_response_count = 0;
    s_diag_last_response_tick = xTaskGetTickCount();
    s_diag_collecting = true;
    xSemaphoreGive(s_diagnostic_semaphore);

    build_thread_network_topology();

    /* Get the expected router count as a minimum threshold */
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *ins = esp_openthread_get_instance();
    uint8_t maxRouterId = otThreadGetMaxRouterId(ins);
    otRouterInfo routerInfo;
    int expected_routers = 0;
    for (uint8_t i = 0; i <= maxRouterId; i++) {
        if (otThreadGetRouterInfo(ins, i, &routerInfo) == OT_ERROR_NONE)
            expected_routers++;
    }
    esp_openthread_lock_release();

    /* Wait for responses:
     * - At least expected_routers responses received
     * - AND no new response for DIAG_QUIET_PERIOD_MS (responses done)
     * - OR hard timeout reached
     *
     * Compare in ticks (not ms) to avoid overflow when multiplying
     * TickType_t by portTICK_PERIOD_MS on systems with large tick counts. */
    const TickType_t max_timeout_ticks = pdMS_TO_TICKS(DIAG_MAX_TIMEOUT_MS);
    const TickType_t min_wait_ticks = pdMS_TO_TICKS(DIAG_MIN_WAIT_MS);
    const TickType_t quiet_ticks = pdMS_TO_TICKS(DIAG_QUIET_PERIOD_MS);
    const TickType_t poll_ticks = pdMS_TO_TICKS(DIAG_POLL_INTERVAL_MS);
    TickType_t start = xTaskGetTickCount();
    while (1) {
        vTaskDelay(poll_ticks);
        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed = now - start;
        TickType_t quiet = now - s_diag_last_response_tick;

        if (elapsed >= max_timeout_ticks) {
            ESP_LOGW(API_TAG, "Diagnostic collection: max timeout reached (%d responses)", s_diag_response_count);
            break;
        }
        if (elapsed < min_wait_ticks) {
            continue;
        }
        if (s_diag_response_count >= expected_routers && quiet >= quiet_ticks) {
            ESP_LOGI(API_TAG, "Diagnostic collection complete: %d responses from %d routers", s_diag_response_count,
                     expected_routers);
            break;
        }
    }

    /* Stop accepting new responses and serialize the result.
       Free the diagnostic set immediately after conversion so the
       JSON string serialization in the HTTP handler has more heap. */
    xSemaphoreTake(s_diagnostic_semaphore, portMAX_DELAY);
    s_diag_collecting = false;
    cJSON *result = diagnosticTlv_set_convert2_json(s_diagnosticTlv_set);
    destroy_thread_diagnosticTlv_set(s_diagnosticTlv_set);
    s_diagnosticTlv_set = NULL;
    xSemaphoreGive(s_diagnostic_semaphore);

    return result;
}

/*----------------------------------------------------------------------
+                       Set Thread dataset
+-----------------------------------------------------------------------*/
otError handle_openthread_set_dataset_request(const cJSON *request)
{
    return OT_ERROR_NONE;
}

/*----------------------------------------------------------------------
                        Ping Sender
-----------------------------------------------------------------------*/
#define PING_MAX_COUNT 10
#define PING_DEFAULT_COUNT 4
#define PING_DEFAULT_SIZE 32
#define PING_TIMEOUT_MS 8000

typedef struct {
    char sender[OT_IP6_ADDRESS_STRING_SIZE];
    uint16_t rtt;
    uint16_t size;
    uint16_t seq;
    uint8_t hop_limit;
} ping_reply_entry_t;

static ping_reply_entry_t s_ping_replies[PING_MAX_COUNT];
static uint16_t s_ping_reply_count = 0;
static otPingSenderStatistics s_ping_statistics;
/* s_ping_done_semaphore is initialized in esp_br_web_api_init() */

static void ping_reply_callback(const otPingSenderReply *aReply, void *aContext)
{
    if (s_ping_reply_count < PING_MAX_COUNT) {
        ping_reply_entry_t *entry = &s_ping_replies[s_ping_reply_count];
        otIp6AddressToString(&aReply->mSenderAddress, entry->sender, sizeof(entry->sender));
        entry->rtt = aReply->mRoundTripTime;
        entry->size = aReply->mSize;
        entry->seq = aReply->mSequenceNumber;
        entry->hop_limit = aReply->mHopLimit;
        s_ping_reply_count++;
    }
}

static void ping_statistics_callback(const otPingSenderStatistics *aStatistics, void *aContext)
{
    memcpy(&s_ping_statistics, aStatistics, sizeof(otPingSenderStatistics));
    if (s_ping_done_semaphore) {
        xSemaphoreGive(s_ping_done_semaphore);
    }
}

cJSON *handle_openthread_ping_request(const cJSON *request)
{
    ESP_RETURN_ON_FALSE(request, NULL, API_TAG, "Invalid ping request");

    /* Only one ping at a time — return busy if another is in progress */
    if (xSemaphoreTake(s_ping_mutex, 0) != pdTRUE) {
        ESP_LOGW(API_TAG, "Ping already in progress");
        return NULL;
    }

    cJSON *root = NULL;
    cJSON *addr_item = cJSON_GetObjectItemCaseSensitive(request, "address");
    if (!cJSON_IsString(addr_item) || !addr_item->valuestring) {
        ESP_LOGE(API_TAG, "Missing or invalid 'address' field");
        goto ping_exit;
    }

    cJSON *count_item = cJSON_GetObjectItemCaseSensitive(request, "count");
    uint16_t count = (cJSON_IsNumber(count_item) && count_item->valuedouble > 0) ? (uint16_t)count_item->valuedouble
                                                                                 : PING_DEFAULT_COUNT;
    if (count > PING_MAX_COUNT) {
        count = PING_MAX_COUNT;
    }

    cJSON *size_item = cJSON_GetObjectItemCaseSensitive(request, "size");
    uint16_t size = (cJSON_IsNumber(size_item) && size_item->valuedouble > 0) ? (uint16_t)size_item->valuedouble
                                                                              : PING_DEFAULT_SIZE;

    /* Reset state */
    s_ping_reply_count = 0;
    memset(s_ping_replies, 0, sizeof(s_ping_replies));
    memset(&s_ping_statistics, 0, sizeof(s_ping_statistics));

    /* Configure and start ping */
    otPingSenderConfig config;
    memset(&config, 0, sizeof(config));

    otError err = otIp6AddressFromString(addr_item->valuestring, &config.mDestination);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(API_TAG, "Failed to parse IPv6 address: %s", addr_item->valuestring);
        goto ping_exit;
    }

    config.mReplyCallback = ping_reply_callback;
    config.mStatisticsCallback = ping_statistics_callback;
    config.mCallbackContext = NULL;
    config.mSize = size;
    config.mCount = count;
    config.mInterval = 1000; /* 1 second between pings */
    config.mTimeout = 3000;  /* 3 second timeout for final reply */
    config.mHopLimit = 0;    /* default */
    config.mAllowZeroHopLimit = false;
    config.mMulticastLoop = false;

    esp_openthread_lock_acquire(portMAX_DELAY);
    err = otPingSenderPing(esp_openthread_get_instance(), &config);
    esp_openthread_lock_release();

    if (err != OT_ERROR_NONE) {
        ESP_LOGE(API_TAG, "Failed to start ping: %d", err);
        goto ping_exit;
    }

    /* Wait for ping to complete. Timeout: count * interval + extra timeout + margin */
    TickType_t wait_ticks = pdMS_TO_TICKS((count * config.mInterval) + config.mTimeout + 1000);
    if (xSemaphoreTake(s_ping_done_semaphore, wait_ticks) != pdTRUE) {
        ESP_LOGW(API_TAG, "Ping timed out waiting for statistics callback");
        /* Stop any in-progress ping */
        esp_openthread_lock_acquire(portMAX_DELAY);
        otPingSenderStop(esp_openthread_get_instance());
        esp_openthread_lock_release();
    }

    /* Build JSON response */
    root = cJSON_CreateObject();

    /* Replies array */
    cJSON *replies = cJSON_CreateArray();
    for (uint16_t i = 0; i < s_ping_reply_count; i++) {
        cJSON *reply = cJSON_CreateObject();
        cJSON_AddStringToObject(reply, "from", s_ping_replies[i].sender);
        cJSON_AddNumberToObject(reply, "seq", s_ping_replies[i].seq);
        cJSON_AddNumberToObject(reply, "size", s_ping_replies[i].size);
        cJSON_AddNumberToObject(reply, "rtt", s_ping_replies[i].rtt);
        cJSON_AddNumberToObject(reply, "hlim", s_ping_replies[i].hop_limit);
        cJSON_AddItemToArray(replies, reply);
    }
    cJSON_AddItemToObject(root, "replies", replies);

    /* Statistics */
    cJSON *stats = cJSON_CreateObject();
    cJSON_AddNumberToObject(stats, "sent", s_ping_statistics.mSentCount);
    cJSON_AddNumberToObject(stats, "received", s_ping_statistics.mReceivedCount);
    uint16_t lost = s_ping_statistics.mSentCount > s_ping_statistics.mReceivedCount
        ? s_ping_statistics.mSentCount - s_ping_statistics.mReceivedCount
        : 0;
    double loss_pct = s_ping_statistics.mSentCount > 0 ? (double)lost / s_ping_statistics.mSentCount * 100.0 : 0.0;
    cJSON_AddNumberToObject(stats, "loss_percent", loss_pct);
    if (s_ping_statistics.mReceivedCount > 0) {
        uint32_t avg_rtt = s_ping_statistics.mTotalRoundTripTime / s_ping_statistics.mReceivedCount;
        cJSON_AddNumberToObject(stats, "rtt_min", s_ping_statistics.mMinRoundTripTime);
        cJSON_AddNumberToObject(stats, "rtt_avg", avg_rtt);
        cJSON_AddNumberToObject(stats, "rtt_max", s_ping_statistics.mMaxRoundTripTime);
    }
    cJSON_AddItemToObject(root, "statistics", stats);

ping_exit:
    xSemaphoreGive(s_ping_mutex);
    return root;
}

/*----------------------------------------------------------------------
                       IPv6 Address Management
-----------------------------------------------------------------------*/
cJSON *handle_openthread_ipaddr_list_request(void)
{
    cJSON *arr = cJSON_CreateArray();
    char addr_str[OT_IP6_ADDRESS_STRING_SIZE];

    esp_openthread_lock_acquire(portMAX_DELAY);
    const otNetifAddress *addr = otIp6GetUnicastAddresses(esp_openthread_get_instance());
    while (addr) {
        cJSON *entry = cJSON_CreateObject();
        otIp6AddressToString(&addr->mAddress, addr_str, sizeof(addr_str));
        cJSON_AddStringToObject(entry, "address", addr_str);
        cJSON_AddNumberToObject(entry, "prefixLength", addr->mPrefixLength);
        const char *origin;
        switch (addr->mAddressOrigin) {
        case OT_ADDRESS_ORIGIN_THREAD:
            origin = "thread";
            break;
        case OT_ADDRESS_ORIGIN_SLAAC:
            origin = "slaac";
            break;
        case OT_ADDRESS_ORIGIN_DHCPV6:
            origin = "dhcpv6";
            break;
        case OT_ADDRESS_ORIGIN_MANUAL:
            origin = "manual";
            break;
        default:
            origin = "unknown";
            break;
        }
        cJSON_AddStringToObject(entry, "origin", origin);
        cJSON_AddBoolToObject(entry, "preferred", addr->mPreferred);
        cJSON_AddBoolToObject(entry, "meshLocal", addr->mMeshLocal);
        cJSON_AddBoolToObject(entry, "rloc", addr->mRloc);
        cJSON_AddItemToArray(arr, entry);
        addr = addr->mNext;
    }
    esp_openthread_lock_release();
    return arr;
}

cJSON *handle_openthread_add_ipaddr_request(const cJSON *request)
{
    cJSON *result = cJSON_CreateObject();
    if (!request) {
        ESP_LOGE(API_TAG, "Invalid ipaddr add request");
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "Invalid request");
        return result;
    }

    const cJSON *addr_json = cJSON_GetObjectItem(request, "address");
    if (!addr_json || !addr_json->valuestring) {
        ESP_LOGE(API_TAG, "Missing address field");
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "Missing address field");
        return result;
    }

    otNetifAddress netif_addr;
    memset(&netif_addr, 0, sizeof(netif_addr));

    esp_openthread_lock_acquire(portMAX_DELAY);
    otError err = otIp6AddressFromString(addr_json->valuestring, &netif_addr.mAddress);
    if (err == OT_ERROR_NONE) {
        netif_addr.mPrefixLength = 64;
        netif_addr.mPreferred = true;
        netif_addr.mValid = true;
        netif_addr.mAddressOrigin = OT_ADDRESS_ORIGIN_MANUAL;
        err = otIp6AddUnicastAddress(esp_openthread_get_instance(), &netif_addr);
    }
    esp_openthread_lock_release();

    if (err == OT_ERROR_NONE) {
        cJSON_AddStringToObject(result, "status", "ok");
        ESP_LOGI(API_TAG, "Added IPv6 address: %s", addr_json->valuestring);
    } else {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", otThreadErrorToString(err));
        ESP_LOGE(API_TAG, "Failed to add IPv6 address: %s (err=%d)", addr_json->valuestring, err);
    }
    return result;
}

cJSON *handle_openthread_delete_ipaddr_request(const cJSON *request)
{
    cJSON *result = cJSON_CreateObject();
    if (!request) {
        ESP_LOGE(API_TAG, "Invalid ipaddr delete request");
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "Invalid request");
        return result;
    }

    const cJSON *addr_json = cJSON_GetObjectItem(request, "address");
    if (!addr_json || !addr_json->valuestring) {
        ESP_LOGE(API_TAG, "Missing address field");
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "Missing address field");
        return result;
    }

    otIp6Address addr;

    esp_openthread_lock_acquire(portMAX_DELAY);
    otError err = otIp6AddressFromString(addr_json->valuestring, &addr);
    if (err == OT_ERROR_NONE) {
        err = otIp6RemoveUnicastAddress(esp_openthread_get_instance(), &addr);
    }
    esp_openthread_lock_release();

    if (err == OT_ERROR_NONE) {
        cJSON_AddStringToObject(result, "status", "ok");
        ESP_LOGI(API_TAG, "Removed IPv6 address: %s", addr_json->valuestring);
    } else {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", otThreadErrorToString(err));
        ESP_LOGE(API_TAG, "Failed to remove IPv6 address: %s (err=%d)", addr_json->valuestring, err);
    }
    return result;
}

/*----------------------------------------------------------------------
            Border Agent ephemeral key (ePSKc)
----------------------------------------------------------------------*/
#define EPSKC_TAP_STRING_SIZE (OT_BORDER_AGENT_EPHEMERAL_KEY_TAP_STRING_LENGTH + 1) /* 9 TAP chars + '\0' */

static const char *epskc_state_to_string(otBorderAgentEphemeralKeyState state)
{
    switch (state) {
    case OT_BORDER_AGENT_STATE_DISABLED:
        return "disabled";
    case OT_BORDER_AGENT_STATE_STOPPED:
        return "stopped";
    case OT_BORDER_AGENT_STATE_STARTED:
        return "started";
    case OT_BORDER_AGENT_STATE_CONNECTED:
        return "connected";
    case OT_BORDER_AGENT_STATE_ACCEPTED:
        return "accepted";
    default:
        return "unknown";
    }
}

/**
 * @brief Generate a random 9-digit Thread Administration Passcode (TAP) via OpenThread's
 *        own Verhoeff-checksummed TAP generator.
 */
static bool generate_epskc_tap(char *tap_buf)
{
    otBorderAgentEphemeralKeyTap tap;

    if (otBorderAgentEphemeralKeyGenerateTap(&tap) != OT_ERROR_NONE) {
        return false;
    }
    memcpy(tap_buf, tap.mTap, sizeof(tap.mTap));

    return true;
}

cJSON *handle_ot_resource_node_epskc_state_request(void)
{
    otBorderAgentEphemeralKeyState state;

    esp_openthread_lock_acquire(portMAX_DELAY);
    state = otBorderAgentEphemeralKeyGetState(esp_openthread_get_instance());
    esp_openthread_lock_release();

    return cJSON_CreateString(state == OT_BORDER_AGENT_STATE_DISABLED ? "disabled" : "enabled");
}

otError handle_ot_resource_node_epskc_state_put_request(const cJSON *request)
{
    const char *state = cJSON_IsString(request) ? cJSON_GetStringValue(request) : NULL;

    ESP_RETURN_ON_FALSE(state, OT_ERROR_INVALID_ARGS, API_TAG, "Invalid ePSKc state");
    ESP_RETURN_ON_FALSE(strcmp(state, "enable") == 0 || strcmp(state, "disable") == 0, OT_ERROR_INVALID_ARGS, API_TAG,
                        "Invalid ePSKc state[%s]", state);

    esp_openthread_lock_acquire(portMAX_DELAY);
    otBorderAgentEphemeralKeySetEnabled(esp_openthread_get_instance(), strcmp(state, "enable") == 0);
    esp_openthread_lock_release();

    return OT_ERROR_NONE;
}

cJSON *handle_ot_resource_node_epskc_key_get_request(void)
{
    cJSON *root = cJSON_CreateObject();
    otInstance *ins;
    otBorderAgentEphemeralKeyState state;
    uint16_t port;

    esp_openthread_lock_acquire(portMAX_DELAY);
    ins = esp_openthread_get_instance();
    state = otBorderAgentEphemeralKeyGetState(ins);
    port = otBorderAgentEphemeralKeyGetUdpPort(ins);
    esp_openthread_lock_release();

    cJSON_AddStringToObject(root, "state", epskc_state_to_string(state));
    cJSON_AddNumberToObject(root, "port", port);
    return root;
}

cJSON *handle_ot_resource_node_epskc_key_post_request(const cJSON *request, cJSON *log)
{
    cJSON *response = NULL;
    uint16_t errcode = 200;
    otError err = OT_ERROR_NONE;
    uint32_t lifetime = 0;
    uint16_t port = 0;
    char tap[EPSKC_TAP_STRING_SIZE] = "";
    otInstance *ins;

    if (request) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(request, "lifetime");
        if (value) {
            if (!cJSON_IsNumber(value) || value->valuedouble < 0.0 || value->valuedouble > UINT32_MAX) {
                errcode = 400;
                goto exit;
            }
            lifetime = (uint32_t)value->valuedouble;
        }

        value = cJSON_GetObjectItemCaseSensitive(request, "port");
        if (value) {
            if (!cJSON_IsNumber(value) || value->valuedouble < 0.0 || value->valuedouble > UINT16_MAX) {
                errcode = 400;
                goto exit;
            }
            port = (uint16_t)value->valuedouble;
        }
    }

    if (lifetime > OT_BORDER_AGENT_MAX_EPHEMERAL_KEY_TIMEOUT) {
        errcode = 400;
        goto exit;
    }

    if (!generate_epskc_tap(tap)) {
        ESP_LOGE(API_TAG, "Failed to generate ePSKc TAP");
        errcode = 500;
        goto exit;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);
    ins = esp_openthread_get_instance();
    err = otBorderAgentEphemeralKeyStart(ins, tap, lifetime, port);
    if (err == OT_ERROR_NONE) {
        port = otBorderAgentEphemeralKeyGetUdpPort(ins);
        esp_br_web_epskc_set_active_tap(tap);
    }
    esp_openthread_lock_release();

    switch (err) {
    case OT_ERROR_NONE:
        errcode = 200;
        break;
    case OT_ERROR_INVALID_STATE:
        errcode = 409;
        break;
    case OT_ERROR_INVALID_ARGS:
        errcode = 400;
        break;
    default:
        errcode = 500;
        break;
    }

    if (err == OT_ERROR_NONE) {
        response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "tap", tap);
        cJSON_AddNumberToObject(response, "port", port);
    }

exit:
    cJSON_AddItemToObject(log, "ErrorCode", cJSON_CreateNumber(errcode));
    return response;
}

void handle_ot_resource_node_epskc_key_delete_request(void)
{
    esp_openthread_lock_acquire(portMAX_DELAY);
    otBorderAgentEphemeralKeyStop(esp_openthread_get_instance());
    esp_br_web_epskc_set_active_tap(NULL);
    esp_openthread_lock_release();
}

/*----------------------------------------------------------------------
                    Thread REST API operations
----------------------------------------------------------------------*/

#define THREAD_REST_COLLECTION_LIMIT 200
#define THREAD_REST_JSON_API "application/vnd.api+json"

/* All collections and action state are protected by the OpenThread lock. */
typedef struct thread_rest_item {
    struct thread_rest_item *next;
    char id[37];
    const char *type;
    cJSON *attributes;
    cJSON *relationships;
    int64_t started;
    int64_t deadline;
    uint32_t attempts;
    bool active;
    bool complete;
    bool deleted;
    uint32_t token;
    uint32_t query_remaining;
    uint8_t query_type;
    bool query_busy;
    bool diagnostic_received;
    bool joiner_registered;
    cJSON *result;
    otJoinerInfo joiner;
} thread_rest_item_t;

static thread_rest_item_t *s_rest_actions, *s_rest_devices, *s_rest_diagnostics;
static thread_rest_item_t *s_rest_mesh_action, *s_rest_diag_action, *s_rest_energy_action;
static thread_rest_item_t *s_rest_query_action;
static uint32_t s_rest_next_token;
static bool s_rest_commissioner_owned;
static SemaphoreHandle_t s_rest_pending_done;
static otError s_rest_pending_result;
static bool s_rest_pending_busy;
static TaskHandle_t s_rest_task;

static const char *const s_rest_diag_names[35] = {
    [0] = "extAddress",
    [1] = "rloc16",
    [2] = "mode",
    [3] = "timeout",
    [4] = "connectivity",
    [5] = "route",
    [6] = "leaderData",
    [7] = "networkData",
    [8] = "ipv6Addresses",
    [9] = "macCounters",
    [14] = "batteryLevel",
    [15] = "supplyVoltage",
    [16] = "childTable",
    [17] = "channelPages",
    [19] = "maxChildTimeout",
    [20] = "lDevIdSubject",
    [21] = "iDevIdCert",
    [23] = "eui",
    [24] = "threadVersion",
    [25] = "vendorName",
    [26] = "vendorModel",
    [27] = "vendorSwVersion",
    [28] = "threadStackVersion",
    [29] = "children",
    [30] = "childIpv6Addresses",
    [31] = "routerNeighbors",
    [34] = "mleCounters",
};

static cJSON *rest_get(const cJSON *json, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(json, key);
}

static const char *rest_string(const cJSON *json, const char *key)
{
    return cJSON_GetStringValue(rest_get(json, key));
}

/* Reuse the component's JSON helpers on private response objects. */
static void rest_lower_keys(cJSON *json)
{
    for (cJSON *item = json ? json->child : NULL; item; item = item->next) {
        if (item->string) {
            if (!strcmp(item->string, "PSKc")) {
                memcpy(item->string, "pskc", 5);
            } else {
                item->string[0] = (char)tolower((unsigned char)item->string[0]);
            }
        }
        rest_lower_keys(item);
    }
}

static cJSON *rest_hex(const uint8_t *bytes, size_t length)
{
    char text[OT_OPERATIONAL_DATASET_MAX_LENGTH * 2 + 1];
    if (length > OT_OPERATIONAL_DATASET_MAX_LENGTH) {
        return NULL;
    }
    for (size_t i = 0; i < length; i++) {
        snprintf(text + i * 2, 3, "%02x", bytes[i]);
    }
    text[length * 2] = 0;
    return cJSON_CreateString(text);
}

static int rest_unhex(const char *text, uint8_t *bytes, size_t capacity)
{
    if (!text) {
        return -1;
    }
    size_t length = strlen(text);
    if ((length & 1) || length / 2 > capacity) {
        return -1;
    }
    for (size_t i = 0; i < length; i += 2) {
        if (!isxdigit((unsigned char)text[i]) || !isxdigit((unsigned char)text[i + 1])) {
            return -1;
        }
        char pair[3] = {text[i], text[i + 1], 0};
        bytes[i / 2] = (uint8_t)strtoul(pair, NULL, 16);
    }
    return length / 2;
}

static cJSON *rest_rloc16(uint16_t rloc)
{
    char text[7];
    snprintf(text, sizeof(text), "0x%04x", rloc);
    return cJSON_CreateString(text);
}

static cJSON *rest_address(const otIp6Address *address)
{
    char text[INET6_ADDRSTRLEN];
    return inet_ntop(AF_INET6, address->mFields.m8, text, sizeof(text)) ? cJSON_CreateString(text) : NULL;
}

static cJSON *rest_mode(otLinkModeConfig mode)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "rxOnWhenIdle", mode.mRxOnWhenIdle);
    cJSON_AddBoolToObject(json, "fullThreadDevice", mode.mDeviceType);
    cJSON_AddBoolToObject(json, "fullNetworkData", mode.mNetworkData);
    return json;
}

static cJSON *rest_leader(const otLeaderData *data)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "partitionId", data->mPartitionId);
    cJSON_AddNumberToObject(json, "weighting", data->mWeighting);
    cJSON_AddNumberToObject(json, "dataVersion", data->mDataVersion);
    cJSON_AddNumberToObject(json, "stableDataVersion", data->mStableDataVersion);
    cJSON_AddNumberToObject(json, "leaderRouterId", data->mLeaderRouterId);
    return json;
}

static unsigned rest_router_count(otInstance *instance)
{
    unsigned count = 0;
    otRouterInfo info;
    for (unsigned id = 0; id <= otThreadGetMaxRouterId(instance); id++) {
        count += otThreadGetRouterInfo(instance, id, &info) == OT_ERROR_NONE;
    }
    return count;
}

static cJSON *rest_node(otInstance *instance)
{
    otBorderAgentId ba;
    otLeaderData leader = {0};
    if (otBorderAgentGetId(instance, &ba) != OT_ERROR_NONE) {
        return NULL;
    }
    otThreadGetLeaderData(instance, &leader);
    cJSON *json = cJSON_CreateObject();
    uint16_t rloc = otThreadGetRloc16(instance);
    cJSON_AddItemToObject(json, "baId", rest_hex(ba.mId, sizeof(ba.mId)));
    cJSON_AddStringToObject(json, "baState", "");
    cJSON_AddStringToObject(json, "state", s_ot_state[otThreadGetDeviceRole(instance)]);
    cJSON_AddNumberToObject(json, "routerCount", rest_router_count(instance));
    cJSON_AddItemToObject(json, "rlocAddress", rest_address(otThreadGetRloc(instance)));
    cJSON_AddItemToObject(json, "extAddress", rest_hex(otLinkGetExtendedAddress(instance)->m8, 8));
    cJSON_AddStringToObject(json, "networkName", otThreadGetNetworkName(instance));
    cJSON_AddItemToObject(json, "rloc16", rest_rloc16(rloc));
    if (!(rloc & 0x3ff)) {
        cJSON_AddNumberToObject(json, "routerId", rloc >> 10);
    }
    cJSON_AddItemToObject(json, "leaderData", rest_leader(&leader));
    cJSON_AddItemToObject(json, "extPanId", rest_hex(otThreadGetExtendedPanId(instance)->m8, 8));
    return json;
}

static bool rest_number(const cJSON *json, double maximum)
{
    return cJSON_IsNumber(json) && isfinite(json->valuedouble) && json->valuedouble >= 0 &&
        json->valuedouble <= maximum;
}

static bool rest_timestamp(const cJSON *json, otTimestamp *timestamp)
{
    if (!cJSON_IsObject(json)) {
        return false;
    }
    cJSON *seconds = rest_get(json, "seconds"), *ticks = rest_get(json, "ticks");
    cJSON *authoritative = rest_get(json, "authoritative");
    if ((seconds && !rest_number(seconds, 281474976710655.0)) || (ticks && !rest_number(ticks, UINT16_MAX)) ||
        (authoritative && !cJSON_IsBool(authoritative))) {
        return false;
    }
    timestamp->mSeconds = seconds ? (uint64_t)seconds->valuedouble : 0;
    timestamp->mTicks = ticks ? (uint16_t)ticks->valuedouble : 0;
    timestamp->mAuthoritative = cJSON_IsTrue(authoritative);
    return true;
}

/* Decode only Thread's lower-camel-case dataset contract. */
static bool rest_parse_dataset(const cJSON *json, otOperationalDataset *dataset, bool pending)
{
    if (!cJSON_IsObject(json)) {
        return false;
    }
    const cJSON *active = pending ? rest_get(json, "activeDataset") : json;
    if (pending) {
        cJSON *delay = rest_get(json, "delay"), *timestamp = rest_get(json, "pendingTimestamp");
        if (!rest_number(delay, UINT32_MAX) || (!cJSON_IsObject(active) && !cJSON_IsString(active))) {
            return false;
        }
        dataset->mDelay = (uint32_t)delay->valuedouble;
        dataset->mComponents.mIsDelayPresent = true;
        if (cJSON_IsString(active)) {
            otOperationalDatasetTlvs tlvs;
            int length = rest_unhex(active->valuestring, tlvs.mTlvs, sizeof(tlvs.mTlvs));
            if (length <= 0) {
                return false;
            }
            tlvs.mLength = length;
            if (otDatasetParseTlvs(&tlvs, dataset) != OT_ERROR_NONE) {
                return false;
            }
            dataset->mDelay = (uint32_t)delay->valuedouble;
            dataset->mComponents.mIsDelayPresent = true;
            active = NULL;
        }
        if (timestamp && !cJSON_IsNull(timestamp)) {
            if (!rest_timestamp(timestamp, &dataset->mPendingTimestamp)) {
                return false;
            }
            dataset->mComponents.mIsPendingTimestampPresent = true;
        }
    }
    cJSON *value;
#define REST_DATASET_NUMBER(key, member, flag, maximum)            \
    if ((value = rest_get(active, key)) && !cJSON_IsNull(value)) { \
        if (!rest_number(value, maximum)) {                        \
            return false;                                          \
        }                                                          \
        dataset->member = value->valuedouble;                      \
        dataset->mComponents.flag = true;                          \
    }
#define REST_DATASET_HEX(key, member, flag)                                                            \
    if ((value = rest_get(active, key)) && !cJSON_IsNull(value)) {                                     \
        if (rest_unhex(cJSON_GetStringValue(value), dataset->member.m8, sizeof(dataset->member.m8)) != \
            sizeof(dataset->member.m8)) {                                                              \
            return false;                                                                              \
        }                                                                                              \
        dataset->mComponents.flag = true;                                                              \
    }
    REST_DATASET_NUMBER("panId", mPanId, mIsPanIdPresent, UINT16_MAX);
    REST_DATASET_NUMBER("channel", mChannel, mIsChannelPresent, UINT16_MAX);
    REST_DATASET_NUMBER("channelMask", mChannelMask, mIsChannelMaskPresent, UINT32_MAX);
    REST_DATASET_HEX("networkKey", mNetworkKey, mIsNetworkKeyPresent);
    REST_DATASET_HEX("extPanId", mExtendedPanId, mIsExtendedPanIdPresent);
    REST_DATASET_HEX("pskc", mPskc, mIsPskcPresent);
#undef REST_DATASET_NUMBER
#undef REST_DATASET_HEX
    if ((value = rest_get(active, "activeTimestamp")) && !cJSON_IsNull(value)) {
        if (!rest_timestamp(value, &dataset->mActiveTimestamp)) {
            return false;
        }
        dataset->mComponents.mIsActiveTimestampPresent = true;
    }
    if ((value = rest_get(active, "networkName")) && !cJSON_IsNull(value)) {
        const char *name = cJSON_GetStringValue(value);
        if (!name || strlen(name) > OT_NETWORK_NAME_MAX_SIZE) {
            return false;
        }
        strcpy(dataset->mNetworkName.m8, name);
        dataset->mComponents.mIsNetworkNamePresent = true;
    }
    if ((value = rest_get(active, "meshLocalPrefix")) && !cJSON_IsNull(value)) {
        const char *text = cJSON_GetStringValue(value);
        otIp6Prefix prefix;
        if (!text || otIp6PrefixFromString(text, &prefix) != OT_ERROR_NONE || prefix.mLength != 64) {
            return false;
        }
        memcpy(dataset->mMeshLocalPrefix.m8, prefix.mPrefix.mFields.m8, 8);
        dataset->mComponents.mIsMeshLocalPrefixPresent = true;
    }
    if ((value = rest_get(active, "securityPolicy")) && !cJSON_IsNull(value)) {
        if (!cJSON_IsObject(value)) {
            return false;
        }
        cJSON *rotation = rest_get(value, "rotationTime");
        if (rotation && !rest_number(rotation, UINT16_MAX)) {
            return false;
        }
        dataset->mSecurityPolicy.mRotationTime = rotation ? rotation->valuedouble : 0;
#define REST_POLICY(key, field)                              \
    {                                                        \
        cJSON *flag = rest_get(value, key);                  \
        if (flag && !cJSON_IsBool(flag)) {                   \
            return false;                                    \
        }                                                    \
        dataset->mSecurityPolicy.field = cJSON_IsTrue(flag); \
    }
        REST_POLICY("obtainNetworkKey", mObtainNetworkKeyEnabled);
        REST_POLICY("nativeCommissioning", mNativeCommissioningEnabled);
        REST_POLICY("routers", mRoutersEnabled);
        REST_POLICY("externalCommissioning", mExternalCommissioningEnabled);
        REST_POLICY("commercialCommissioning", mCommercialCommissioningEnabled);
        REST_POLICY("autonomousEnrollment", mAutonomousEnrollmentEnabled);
        REST_POLICY("networkKeyProvisioning", mNetworkKeyProvisioningEnabled);
        REST_POLICY("tobleLink", mTobleLinkEnabled);
        REST_POLICY("nonCcmRouters", mNonCcmRoutersEnabled);
#undef REST_POLICY
        dataset->mComponents.mIsSecurityPolicyPresent = true;
    }
    return true;
}

static void rest_pending_callback(otError result, void *context)
{
    (void)context;
    s_rest_pending_result = result;
    s_rest_pending_busy = false;
    xSemaphoreGive(s_rest_pending_done);
}

static void rest_dataset(const esp_br_thread_rest_request_t *request, esp_br_thread_rest_response_t *response,
                         otInstance *instance, bool pending)
{
    otOperationalDataset dataset = {0};
    otOperationalDatasetTlvs base = {0};
    otError error;
    if (request->method == HTTP_GET) {
        if (!strcmp(request->accept, "text/plain")) {
            error = pending ? otDatasetGetPendingTlvs(instance, &base) : otDatasetGetActiveTlvs(instance, &base);
            if (error == OT_ERROR_NONE) {
                response->text = malloc(base.mLength * 2 + 1);
                if (!response->text) {
                    response->status = 500;
                    return;
                }
                for (unsigned i = 0; i < base.mLength; i++) {
                    snprintf(response->text + i * 2, 3, "%02X", base.mTlvs[i]);
                }
                response->text[base.mLength * 2] = 0;
                response->content_type = "text/plain";
            }
        } else {
            error = pending ? otDatasetGetPending(instance, &dataset) : otDatasetGetActive(instance, &dataset);
            if (error == OT_ERROR_NONE) {
                response->json = pending ? PendingDataset2Json(dataset) : ActiveDataset2Json(dataset);
                rest_lower_keys(response->json);
                cJSON *active = pending ? rest_get(response->json, "activeDataset") : response->json;
                if (dataset.mComponents.mIsMeshLocalPrefixPresent) {
                    otIp6Address address = {0};
                    char prefix[INET6_ADDRSTRLEN + 4];
                    memcpy(address.mFields.m8, dataset.mMeshLocalPrefix.m8, 8);
                    inet_ntop(AF_INET6, address.mFields.m8, prefix, sizeof(prefix) - 4);
                    strcat(prefix, "/64");
                    cJSON_ReplaceItemInObjectCaseSensitive(active, "meshLocalPrefix", cJSON_CreateString(prefix));
                }
            }
        }
        if (error != OT_ERROR_NONE) {
            response->status = 204;
        }
        return;
    }
    const char *condition = request->if_none_match;
    while (*condition == ' ' || *condition == '\t') {
        condition++;
    }
    size_t condition_length = strlen(condition);
    while (condition_length && (condition[condition_length - 1] == ' ' || condition[condition_length - 1] == '\t')) {
        condition_length--;
    }
    bool only_absent = condition_length == 1 && *condition == '*';
    if (condition_length && !only_absent) {
        response->status = 400;
        return;
    }
    otDeviceRole role = otThreadGetDeviceRole(instance);
    if ((!pending && role != OT_DEVICE_ROLE_DISABLED) || (pending && role < OT_DEVICE_ROLE_CHILD)) {
        response->status = 409;
        return;
    }
    error = pending ? otDatasetGetPendingTlvs(instance, &base) : otDatasetGetActiveTlvs(instance, &base);
    if (only_absent && error == OT_ERROR_NONE) {
        response->status = 412;
        return;
    }
    response->status = error == OT_ERROR_NOT_FOUND ? 201 : 200;
    if (error == OT_ERROR_NOT_FOUND) {
        if (pending) {
            error = otDatasetGetActiveTlvs(instance, &base);
        } else {
            error = otDatasetCreateNewNetwork(instance, &dataset);
            if (error == OT_ERROR_NONE) {
                otDatasetConvertToTlvs(&dataset, &base);
            }
        }
    }
    if (error != OT_ERROR_NONE) {
        response->status = 500;
        return;
    }
    memset(&dataset, 0, sizeof(dataset));
    if (!strcmp(request->content_type, "text/plain")) {
        otOperationalDatasetTlvs update;
        int length = rest_unhex(request->body, update.mTlvs, sizeof(update.mTlvs));
        if (length < 0) {
            response->status = 400;
            return;
        }
        update.mLength = length;
        if (otDatasetParseTlvs(&update, &dataset) != OT_ERROR_NONE) {
            response->status = 500;
            return;
        }
    } else {
        cJSON *json = cJSON_Parse(request->body);
        bool valid = rest_parse_dataset(json, &dataset, pending);
        cJSON_Delete(json);
        if (!valid) {
            response->status = 400;
            return;
        }
        if (pending && !dataset.mComponents.mIsPendingTimestampPresent) {
            struct timespec now;
            otOperationalDataset previous;
            if (clock_gettime(CLOCK_REALTIME, &now)) {
                response->status = 500;
                return;
            }
            dataset.mPendingTimestamp.mSeconds = now.tv_sec & UINT64_C(0xffffffffffff);
            dataset.mPendingTimestamp.mTicks = ((uint64_t)now.tv_nsec * 32768 / 1000000000) & 0x7fff;
            if (otDatasetParseTlvs(&base, &previous) == OT_ERROR_NONE &&
                previous.mComponents.mIsPendingTimestampPresent &&
                (dataset.mPendingTimestamp.mSeconds < previous.mPendingTimestamp.mSeconds ||
                 (dataset.mPendingTimestamp.mSeconds == previous.mPendingTimestamp.mSeconds &&
                  dataset.mPendingTimestamp.mTicks <= previous.mPendingTimestamp.mTicks))) {
                dataset.mPendingTimestamp.mSeconds =
                    (previous.mPendingTimestamp.mSeconds + 1) & UINT64_C(0xffffffffffff);
                dataset.mPendingTimestamp.mTicks = 0;
            }
            dataset.mComponents.mIsPendingTimestampPresent = true;
        }
    }
    if (otDatasetUpdateTlvs(&dataset, &base) != OT_ERROR_NONE) {
        response->status = 500;
        return;
    }
    if (!pending) {
        if (otDatasetSetActiveTlvs(instance, &base) != OT_ERROR_NONE) {
            response->status = 500;
        }
        return;
    }
    if (otDatasetParseTlvs(&base, &dataset) != OT_ERROR_NONE) {
        response->status = 500;
        return;
    }
    if (!dataset.mComponents.mIsDelayPresent) {
        response->status = 400;
        return;
    }
    if (s_rest_pending_busy) {
        response->status = 409;
        return;
    }
    xSemaphoreTake(s_rest_pending_done, 0);
    otOperationalDataset empty = {0};
    s_rest_pending_busy = true;
    error = otDatasetSendMgmtPendingSet(instance, &empty, base.mTlvs, base.mLength, rest_pending_callback, NULL);
    if (error != OT_ERROR_NONE) {
        s_rest_pending_busy = false;
        response->status =
            error == OT_ERROR_ALREADY || error == OT_ERROR_BUSY || error == OT_ERROR_INVALID_STATE ? 409 : 500;
        return;
    }
    /* The permanent callback context also absorbs responses after the HTTP timeout. */
    esp_openthread_lock_release();
    bool done = xSemaphoreTake(s_rest_pending_done, pdMS_TO_TICKS(30000)) == pdTRUE;
    esp_openthread_lock_acquire(portMAX_DELAY);
    if (!done || s_rest_pending_result == OT_ERROR_RESPONSE_TIMEOUT) {
        response->status = 504;
        response->detail = "no response from leader";
    } else if (s_rest_pending_result == OT_ERROR_REJECTED) {
        response->status = 409;
        response->detail = "rejected by leader";
    } else if (s_rest_pending_result == OT_ERROR_ABORT) {
        response->status = 409;
        response->detail = "no longer attached";
    } else if (s_rest_pending_result != OT_ERROR_NONE) {
        response->status = 500;
    }
}

static bool rest_discerner(const char *text, otJoinerDiscerner *discerner)
{
    if (!text || !strchr(text, '/')) {
        return false;
    }
    char *end;
    uint64_t value = strtoull(text, &end, 0);
    if (*end != '/') {
        return false;
    }
    unsigned long bits = strtoul(end + 1, &end, 10);
    if (*end || bits == 0 || bits > 64) {
        return false;
    }
    discerner->mLength = bits;
    discerner->mValue = value;
    return true;
}

static bool rest_joiner_id(const char *text, otJoinerInfo *joiner)
{
    if (!text) {
        return false;
    }
    if (!strcmp(text, "*")) {
        joiner->mType = OT_JOINER_INFO_TYPE_ANY;
        return true;
    }
    if (strchr(text, '/')) {
        joiner->mType = OT_JOINER_INFO_TYPE_DISCERNER;
        return rest_discerner(text, &joiner->mSharedId.mDiscerner);
    }
    joiner->mType = OT_JOINER_INFO_TYPE_EUI64;
    return rest_unhex(text, joiner->mSharedId.mEui64.m8, 8) == 8;
}

static bool rest_parse_joiner(const cJSON *json, otJoinerInfo *joiner)
{
    if (!cJSON_IsObject(json)) {
        return false;
    }
    memset(joiner, 0, sizeof(*joiner));
    const char *pskd = rest_string(json, "pskd");
    if (!pskd || strlen(pskd) > OT_JOINER_MAX_PSKD_LENGTH) {
        return false;
    }
    strcpy(joiner->mPskd.m8, pskd);
    joiner->mExpirationTime = 60;
    cJSON *timeout = rest_get(json, "timeout");
    if (timeout) {
        if (!rest_number(timeout, UINT32_MAX)) {
            return false;
        }
        joiner->mExpirationTime = timeout->valuedouble;
    }
    const char *keys[] = {"joinerId", "eui", "discerner"};
    unsigned ids = 0;
    for (unsigned i = 0; i < 3; i++) {
        cJSON *value = rest_get(json, keys[i]);
        if (value && (++ids > 1 || !rest_joiner_id(cJSON_GetStringValue(value), joiner))) {
            return false;
        }
    }
    return true;
}

static cJSON *rest_joiner_json(const otJoinerInfo *joiner)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "pskd", joiner->mPskd.m8);
    cJSON_AddNumberToObject(json, "timeout", joiner->mExpirationTime);
    if (joiner->mType == OT_JOINER_INFO_TYPE_EUI64) {
        cJSON_AddItemToObject(json, "eui", rest_hex(joiner->mSharedId.mEui64.m8, 8));
    } else if (joiner->mType == OT_JOINER_INFO_TYPE_DISCERNER) {
        char text[28];
        snprintf(text, sizeof(text), "0x%016" PRIx64 "/%u", joiner->mSharedId.mDiscerner.mValue,
                 joiner->mSharedId.mDiscerner.mLength);
        cJSON_AddStringToObject(json, "discerner", text);
    } else {
        cJSON_AddStringToObject(json, "joinerId", "*");
    }
    return json;
}

static otError rest_add_joiner(otInstance *instance, const otJoinerInfo *joiner)
{
    if (joiner->mType == OT_JOINER_INFO_TYPE_DISCERNER) {
        return otCommissionerAddJoinerWithDiscerner(instance, &joiner->mSharedId.mDiscerner, joiner->mPskd.m8,
                                                    joiner->mExpirationTime);
    }
    const otExtAddress *address = joiner->mType == OT_JOINER_INFO_TYPE_EUI64 ? &joiner->mSharedId.mEui64 : NULL;
    static const uint8_t zero[8];
    if (address && !memcmp(address->m8, zero, 8)) {
        address = NULL;
    }
    return otCommissionerAddJoiner(instance, address, joiner->mPskd.m8, joiner->mExpirationTime);
}

static void rest_remove_joiner(otInstance *instance, const otJoinerInfo *joiner)
{
    if (joiner->mType == OT_JOINER_INFO_TYPE_DISCERNER) {
        otCommissionerRemoveJoinerWithDiscerner(instance, &joiner->mSharedId.mDiscerner);
    } else {
        otCommissionerRemoveJoiner(instance,
                                   joiner->mType == OT_JOINER_INFO_TYPE_EUI64 ? &joiner->mSharedId.mEui64 : NULL);
    }
}

static void rest_uuid(char id[37])
{
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    snprintf(id, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0], bytes[1],
             bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
             bytes[12], bytes[13], bytes[14], bytes[15]);
}

static void rest_created(cJSON *attributes)
{
    char timestamp[32];
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    cJSON_AddStringToObject(attributes, "created", timestamp);
}

static thread_rest_item_t *rest_find(thread_rest_item_t *head, const char *id)
{
    for (; head; head = head->next) {
        if (!head->deleted && !strcmp(head->id, id)) {
            return head;
        }
    }
    return NULL;
}

static unsigned rest_count(thread_rest_item_t *head)
{
    unsigned count = 0;
    for (; head; head = head->next) {
        count += !head->deleted;
    }
    return count;
}

static void rest_item_free(thread_rest_item_t *item)
{
    cJSON_Delete(item->attributes);
    cJSON_Delete(item->relationships);
    cJSON_Delete(item->result);
    free(item);
}

static thread_rest_item_t *rest_new(thread_rest_item_t **head, const char *type, const char *id, cJSON *attributes)
{
    if (!attributes) {
        return NULL;
    }
    thread_rest_item_t *item = calloc(1, sizeof(*item));
    if (!item) {
        cJSON_Delete(attributes);
        return NULL;
    }
    if (id) {
        strlcpy(item->id, id, sizeof(item->id));
    } else {
        rest_uuid(item->id);
    }
    item->token = ++s_rest_next_token;
    if (!item->token) {
        item->token = ++s_rest_next_token;
    }
    item->type = type;
    item->attributes = attributes;
    rest_created(attributes);
    item->started = esp_timer_get_time();
    /* Evict the oldest inactive entry. In-flight callback contexts are retained. */
    if (rest_count(*head) >= THREAD_REST_COLLECTION_LIMIT) {
        thread_rest_item_t **oldest = NULL;
        for (thread_rest_item_t **link = head; *link; link = &(*link)->next) {
            if (!(*link)->active && (head != &s_rest_actions || (*link)->complete)) {
                oldest = link;
            }
        }
        if (!oldest) {
            rest_item_free(item);
            return NULL;
        }
        thread_rest_item_t *evicted = *oldest;
        *oldest = evicted->next;
        rest_item_free(evicted);
    }
    item->next = *head;
    *head = item;
    return item;
}

static void rest_replace(cJSON *json, const char *key, cJSON *value)
{
    cJSON_DeleteItemFromObjectCaseSensitive(json, key);
    cJSON_AddItemToObject(json, key, value);
}

static void rest_status(thread_rest_item_t *action, const char *status)
{
    rest_replace(action->attributes, "status", cJSON_CreateString(status));
    action->complete = strcmp(status, "pending") && strcmp(status, "active") && strcmp(status, "waiting") &&
        strcmp(status, "undiscovered") && strcmp(status, "attempted");
    if (action->complete) {
        cJSON_DeleteItemFromObjectCaseSensitive(action->attributes, "timeout");
    }
}

static void rest_result(thread_rest_item_t *action, const thread_rest_item_t *result)
{
    cJSON_Delete(action->relationships);
    action->relationships = cJSON_CreateObject();
    cJSON *relation = cJSON_AddObjectToObject(action->relationships, "result");
    cJSON *data = cJSON_AddObjectToObject(relation, "data");
    cJSON_AddStringToObject(data, "type", "diagnostics");
    cJSON_AddStringToObject(data, "id", result->id);
}

static void rest_self(otInstance *instance)
{
    cJSON *hex = rest_hex(otLinkGetExtendedAddress(instance)->m8, 8);
    if (!hex) {
        return;
    }
    thread_rest_item_t *self = rest_find(s_rest_devices, hex->valuestring);
    if (!self) {
        cJSON *attributes = rest_node(instance);
        if (!attributes) {
            cJSON_Delete(hex);
            return;
        }
        uint8_t zero[8] = {0};
        otIp6Address zero_address = {0};
        cJSON_AddItemToObject(attributes, "mlEidIid", rest_hex(zero, 8));
        cJSON_AddItemToObject(attributes, "omrIpv6Address", rest_address(&zero_address));
        cJSON_AddStringToObject(attributes, "hostname", "");
        cJSON_AddStringToObject(attributes, "role", "");
        otLinkModeConfig mode = {0};
        cJSON_AddItemToObject(attributes, "mode", rest_mode(mode));
        rest_new(&s_rest_devices, "threadBorderRouter", hex->valuestring, attributes);
    }
    cJSON_Delete(hex);
}

static bool rest_destination_valid(const cJSON *attributes)
{
    const char *destination = rest_string(attributes, "destination");
    const char *type = rest_string(attributes, "destinationType");
    if (!destination) {
        return false;
    }
    size_t length = strlen(destination);
    if (!type) {
        return length == 6 || length == 16;
    }
    return ((!strcmp(type, "extended") || !strcmp(type, "mleid")) && length == 16) ||
        (!strcmp(type, "rloc") && length == 6);
}

static bool rest_destination(otInstance *instance, const cJSON *attributes, otIp6Address *address)
{
    const char *text = rest_string(attributes, "destination");
    const char *type = rest_string(attributes, "destinationType");
    if (!text) {
        *address = *otThreadGetRealmLocalAllThreadNodesMulticastAddress(instance);
        return true;
    }
    if (!rest_destination_valid(attributes)) {
        return false;
    }
    if (!type) {
        type = strlen(text) == 6 ? "rloc" : "extended";
    }
    *address = *otThreadGetRloc(instance);
    if (!strcmp(type, "rloc")) {
        char *end;
        unsigned long rloc = strtoul(text, &end, 16);
        if (*end || rloc > UINT16_MAX) {
            return false;
        }
        address->mFields.m8[14] = rloc >> 8;
        address->mFields.m8[15] = rloc;
        return true;
    }
    if (!strcmp(type, "mleid")) {
        return rest_unhex(text, address->mFields.m8 + 8, 8) == 8;
    }
    thread_rest_item_t *device = rest_find(s_rest_devices, text);
    const char *iid = device ? rest_string(device->attributes, "mlEidIid") : NULL;
    if (iid && rest_unhex(iid, address->mFields.m8 + 8, 8) == 8) {
        return true;
    }
    const char *rloc = device ? rest_string(device->attributes, "rloc16") : NULL;
    if (rloc) {
        unsigned value = strtoul(rloc, NULL, 16);
        address->mFields.m8[14] = value >> 8;
        address->mFields.m8[15] = value;
        return true;
    }
    cJSON *self = rest_hex(otLinkGetExtendedAddress(instance)->m8, 8);
    bool local = self && !strcmp(text, self->valuestring);
    cJSON_Delete(self);
    return local;
}

static int rest_diag_type(const char *name)
{
    if (!name) {
        return -1;
    }
    for (unsigned i = 0; i < 35; i++) {
        if (s_rest_diag_names[i] && !strcmp(s_rest_diag_names[i], name)) {
            return i;
        }
    }
    return -1;
}

static bool rest_validate_action(const cJSON *item)
{
    const char *type = rest_string(item, "type");
    cJSON *attributes = rest_get(item, "attributes");
    cJSON *timeout = rest_get(attributes, "timeout");
    if (!type || !cJSON_IsObject(attributes) || (timeout && !cJSON_IsNumber(timeout))) {
        return false;
    }
    if (!strcmp(type, "addThreadDeviceTask")) {
        otJoinerInfo joiner;
        if (!rest_parse_joiner(attributes, &joiner)) {
            return false;
        }
        if (!rest_get(attributes, "eui") && !rest_get(attributes, "joinerId") && !rest_get(attributes, "discerner")) {
            return false;
        }
        size_t length = strlen(joiner.mPskd.m8);
        if (length < 6 || length > 32) {
            return false;
        }
        for (const char *c = joiner.mPskd.m8; *c; c++) {
            if (!strchr("0123456789ABCDEFGHJKLMNPRSTUVWXY", *c)) {
                return false;
            }
        }
        return true;
    }
    if (!timeout) {
        return false;
    }
    if (!strcmp(type, "updateDeviceCollectionTask")) {
        return cJSON_IsNumber(rest_get(attributes, "maxAge")) && cJSON_IsNumber(rest_get(attributes, "maxRetries")) &&
            cJSON_IsNumber(rest_get(attributes, "deviceCount"));
    }
    bool reset = !strcmp(type, "resetNetworkDiagCounterTask");
    if (!reset || rest_get(attributes, "destination")) {
        if (!rest_destination_valid(attributes)) {
            return false;
        }
    }
    if (!strcmp(type, "getEnergyScanTask")) {
        cJSON *mask = rest_get(attributes, "channelMask"), *channel;
        if (!cJSON_IsArray(mask) || !cJSON_IsNumber(rest_get(attributes, "count")) ||
            !cJSON_IsNumber(rest_get(attributes, "period")) || !cJSON_IsNumber(rest_get(attributes, "scanDuration"))) {
            return false;
        }
        cJSON_ArrayForEach(channel, mask)
        {
            if (!cJSON_IsNumber(channel) || channel->valueint < 11 || channel->valueint > 26) {
                return false;
            }
        }
        return true;
    }
    if (reset || !strcmp(type, "getNetworkDiagnosticTask")) {
        cJSON *types = rest_get(attributes, "types"), *name;
        if (!cJSON_IsArray(types)) {
            return false;
        }
        cJSON_ArrayForEach(name, types)
        {
            int id = rest_diag_type(cJSON_GetStringValue(name));
            if (id < 0 || (reset && id != 9 && id != 34)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

/* Discovery diagnostics also identify end devices, which are absent from a
 * router-only topology response. The token discards late responses after delete. */
static void rest_discover_devices_callback(otError error, otMessage *message, const otMessageInfo *info, void *context)
{
    (void)info;
    thread_rest_item_t *action = s_rest_mesh_action;
    if (!action || action->complete || (uintptr_t)context != action->token || error != OT_ERROR_NONE || !message) {
        return;
    }
    otNetworkDiagIterator iterator = OT_NETWORK_DIAGNOSTIC_ITERATOR_INIT;
    otNetworkDiagTlv tlv;
    cJSON *attributes = cJSON_CreateObject();
    char id[17] = "";
    otInstance *instance = esp_openthread_get_instance();
    while (otThreadGetNextDiagnosticTlv(message, &iterator, &tlv) == OT_ERROR_NONE) {
        if (tlv.mType == 0) {
            cJSON *hex = rest_hex(tlv.mData.mExtAddress.m8, 8);
            if (hex) {
                strlcpy(id, hex->valuestring, sizeof(id));
                cJSON_AddItemToObject(attributes, "extAddress", hex);
            }
        } else if (tlv.mType == 1) {
            cJSON_AddItemToObject(attributes, "rloc16", rest_rloc16(tlv.mData.mAddr16));
            cJSON_AddStringToObject(attributes, "role", tlv.mData.mAddr16 & 0x3ff ? "child" : "router");
        } else if (tlv.mType == 2) {
            cJSON_AddItemToObject(attributes, "mode", rest_mode(tlv.mData.mMode));
        } else if (tlv.mType == 8) {
            for (unsigned i = 0; i < tlv.mData.mIp6AddrList.mCount; i++) {
                const otIp6Address *address = &tlv.mData.mIp6AddrList.mList[i];
                if (!memcmp(address->mFields.m8, otThreadGetMeshLocalPrefix(instance)->m8, 8)) {
                    const uint8_t rloc_iid[] = {0, 0, 0, 0xff, 0xfe, 0};
                    if (memcmp(address->mFields.m8 + 8, rloc_iid, 6)) {
                        rest_replace(attributes, "mlEidIid", rest_hex(address->mFields.m8 + 8, 8));
                    }
                } else if (!(address->mFields.m8[0] == 0xfe && (address->mFields.m8[1] & 0xc0) == 0x80) &&
                           address->mFields.m8[0] != 0xff) {
                    rest_replace(attributes, "omrIpv6Address", rest_address(address));
                }
            }
        }
    }
    if (!*id) {
        cJSON_Delete(attributes);
        return;
    }
    thread_rest_item_t *device = rest_find(s_rest_devices, id);
    if (device) {
        for (cJSON *field = attributes->child; field; field = field->next) {
            rest_replace(device->attributes, field->string, cJSON_Duplicate(field, true));
        }
        device->started = esp_timer_get_time();
        cJSON_Delete(attributes);
    } else {
        uint8_t zero[8] = {0};
        otIp6Address zero_address = {0};
        if (!rest_get(attributes, "mlEidIid")) {
            cJSON_AddItemToObject(attributes, "mlEidIid", rest_hex(zero, 8));
        }
        if (!rest_get(attributes, "omrIpv6Address")) {
            cJSON_AddItemToObject(attributes, "omrIpv6Address", rest_address(&zero_address));
        }
        cJSON_AddStringToObject(attributes, "hostname", "");
        rest_new(&s_rest_devices, "threadDevice", id, attributes);
    }
}

static void rest_mesh_callback(otError error, otMeshDiagRouterInfo *router, void *context)
{
    (void)context;
    thread_rest_item_t *action = s_rest_mesh_action;
    if (!action) {
        return;
    }
    if (router && !action->complete) {
        cJSON *id = rest_hex(router->mExtAddress.m8, 8);
        if (id) {
            thread_rest_item_t *device = rest_find(s_rest_devices, id->valuestring);
            if (!device) {
                device = rest_new(&s_rest_devices, "threadDevice", id->valuestring, cJSON_CreateObject());
            }
            if (device) {
                rest_replace(device->attributes, "extAddress", cJSON_Duplicate(id, true));
                rest_replace(device->attributes, "rloc16", rest_rloc16(router->mRloc16));
                rest_replace(device->attributes, "role", cJSON_CreateString(router->mIsLeader ? "leader" : "router"));
                rest_replace(device->attributes, "hostname", cJSON_CreateString(""));
                otIp6Address address;
                uint8_t zero[8] = {0};
                rest_replace(device->attributes, "mlEidIid", rest_hex(zero, 8));
                memset(&address, 0, sizeof(address));
                rest_replace(device->attributes, "omrIpv6Address", rest_address(&address));
                while (router->mIp6AddrIterator &&
                       otMeshDiagGetNextIp6Address(router->mIp6AddrIterator, &address) == OT_ERROR_NONE) {
                    if (!memcmp(address.mFields.m8, otThreadGetMeshLocalPrefix(esp_openthread_get_instance())->m8, 8)) {
                        if (address.mFields.m8[11] != 0xff || address.mFields.m8[12] != 0xfe) {
                            rest_replace(device->attributes, "mlEidIid", rest_hex(address.mFields.m8 + 8, 8));
                        }
                    } else if (!(address.mFields.m8[0] == 0xfe && (address.mFields.m8[1] & 0xc0) == 0x80)) {
                        rest_replace(device->attributes, "omrIpv6Address", rest_address(&address));
                    }
                }
                otLinkModeConfig mode = {.mRxOnWhenIdle = true, .mDeviceType = true, .mNetworkData = true};
                rest_replace(device->attributes, "mode", rest_mode(mode));
            }
            cJSON_Delete(id);
        }
    }
    if (error != OT_ERROR_PENDING) {
        action->active = false;
        s_rest_mesh_action = NULL;
        unsigned wanted = rest_get(action->attributes, "deviceCount")->valueint;
        unsigned retries = rest_get(action->attributes, "maxRetries")->valueint;
        if (error != OT_ERROR_NONE) {
            rest_status(action, "failed");
        } else if (action->complete) { /* Cancellation already recorded. */
        } else if (rest_count(s_rest_devices) >= wanted || action->attempts > retries) {
            rest_status(action, "completed");
        } else {
            rest_status(action, "pending");
        }
    }
}

static cJSON *rest_diag_tlv(const otNetworkDiagTlv *tlv)
{
    cJSON *json = NULL;
    switch (tlv->mType) {
    case 0:
        return rest_hex(tlv->mData.mExtAddress.m8, 8);
    case 1:
        return rest_rloc16(tlv->mData.mAddr16);
    case 2:
        return rest_mode(tlv->mData.mMode);
    case 3:
        return cJSON_CreateNumber(tlv->mData.mTimeout);
    case 4: {
        const otNetworkDiagConnectivity *c = &tlv->mData.mConnectivity;
        json = cJSON_CreateObject();
#define REST_DIAG_NUMBER(key, field) cJSON_AddNumberToObject(json, key, c->field)
        REST_DIAG_NUMBER("parentPriority", mParentPriority);
        REST_DIAG_NUMBER("linkQuality1", mLinkQuality1);
        REST_DIAG_NUMBER("linkQuality2", mLinkQuality2);
        REST_DIAG_NUMBER("linkQuality3", mLinkQuality3);
        REST_DIAG_NUMBER("leaderCost", mLeaderCost);
        REST_DIAG_NUMBER("idSequence", mIdSequence);
        REST_DIAG_NUMBER("activeRouters", mActiveRouters);
        REST_DIAG_NUMBER("sedBufferSize", mSedBufferSize);
        REST_DIAG_NUMBER("sedDatagramCount", mSedDatagramCount);
#undef REST_DIAG_NUMBER
        return json;
    }
    case 5: {
        json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "idSequence", tlv->mData.mRoute.mIdSequence);
        cJSON *routes = cJSON_AddArrayToObject(json, "routeData");
        for (unsigned i = 0; i < tlv->mData.mRoute.mRouteCount; i++) {
            const otNetworkDiagRouteData *route = &tlv->mData.mRoute.mRouteData[i];
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddNumberToObject(entry, "routeId", route->mRouterId);
            cJSON_AddNumberToObject(entry, "linkQualityIn", route->mLinkQualityIn);
            cJSON_AddNumberToObject(entry, "linkQualityOut", route->mLinkQualityOut);
            cJSON_AddNumberToObject(entry, "routeCost", route->mRouteCost);
            cJSON_AddItemToArray(routes, entry);
        }
        return json;
    }
    case 6:
        return rest_leader(&tlv->mData.mLeaderData);
    case 7:
        return rest_hex(tlv->mData.mNetworkData.m8, tlv->mData.mNetworkData.mCount);
    case 8:
        json = cJSON_CreateArray();
        for (unsigned i = 0; i < tlv->mData.mIp6AddrList.mCount; i++) {
            cJSON_AddItemToArray(json, rest_address(&tlv->mData.mIp6AddrList.mList[i]));
        }
        return json;
    case 9: {
        const otNetworkDiagMacCounters *c = &tlv->mData.mMacCounters;
        json = cJSON_CreateObject();
#define REST_DIAG_NUMBER(key, field) cJSON_AddNumberToObject(json, key, c->field)
        REST_DIAG_NUMBER("ifInUnknownProtos", mIfInUnknownProtos);
        REST_DIAG_NUMBER("ifInErrors", mIfInErrors);
        REST_DIAG_NUMBER("ifOutErrors", mIfOutErrors);
        REST_DIAG_NUMBER("ifInUcastPkts", mIfInUcastPkts);
        REST_DIAG_NUMBER("ifInBroadcastPkts", mIfInBroadcastPkts);
        REST_DIAG_NUMBER("ifInDiscards", mIfInDiscards);
        REST_DIAG_NUMBER("ifOutUcastPkts", mIfOutUcastPkts);
        REST_DIAG_NUMBER("ifOutBroadcastPkts", mIfOutBroadcastPkts);
        REST_DIAG_NUMBER("ifOutDiscards", mIfOutDiscards);
#undef REST_DIAG_NUMBER
        return json;
    }
    case 14:
        return cJSON_CreateNumber(tlv->mData.mBatteryLevel);
    case 15:
        return cJSON_CreateNumber(tlv->mData.mSupplyVoltage);
    case 16:
        json = cJSON_CreateArray();
        for (unsigned i = 0; i < tlv->mData.mChildTable.mCount; i++) {
            const otNetworkDiagChildEntry *child = &tlv->mData.mChildTable.mTable[i];
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddNumberToObject(entry, "childId", child->mChildId);
            cJSON_AddNumberToObject(entry, "timeout", child->mTimeout);
            cJSON_AddItemToObject(entry, "mode", rest_mode(child->mMode));
            cJSON_AddItemToArray(json, entry);
        }
        return json;
    case 17:
        return rest_hex(tlv->mData.mChannelPages.m8, tlv->mData.mChannelPages.mCount);
    case 19:
        return cJSON_CreateNumber(tlv->mData.mMaxChildTimeout);
    case 23:
        return rest_hex(tlv->mData.mEui64.m8, 8);
    case 24:
        return cJSON_CreateNumber(tlv->mData.mVersion);
    case 25:
        return cJSON_CreateString(tlv->mData.mVendorName);
    case 26:
        return cJSON_CreateString(tlv->mData.mVendorModel);
    case 27:
        return cJSON_CreateString(tlv->mData.mVendorSwVersion);
    case 28:
        return cJSON_CreateString(tlv->mData.mThreadStackVersion);
    case 34: {
        const otNetworkDiagMleCounters *c = &tlv->mData.mMleCounters;
        json = cJSON_CreateObject();
#define REST_DIAG_NUMBER(key, field) cJSON_AddNumberToObject(json, key, c->field)
        REST_DIAG_NUMBER("radioDisabledCount", mDisabledRole);
        REST_DIAG_NUMBER("detachedRoleCount", mDetachedRole);
        REST_DIAG_NUMBER("childRoleCount", mChildRole);
        REST_DIAG_NUMBER("routerRoleCount", mRouterRole);
        REST_DIAG_NUMBER("leaderRoleCount", mLeaderRole);
        REST_DIAG_NUMBER("attachAttemptsCount", mAttachAttempts);
        REST_DIAG_NUMBER("partIdChangesCount", mPartitionIdChanges);
        REST_DIAG_NUMBER("betterPartIdAttachAttemptsCount", mBetterPartitionAttachAttempts);
        REST_DIAG_NUMBER("newParentCount", mParentChanges);
        REST_DIAG_NUMBER("totalTrackingTime", mTrackedTime);
        REST_DIAG_NUMBER("radioDisabledTime", mDisabledTime);
        REST_DIAG_NUMBER("detachedRoleTime", mDetachedTime);
        REST_DIAG_NUMBER("childRoleTime", mChildTime);
        REST_DIAG_NUMBER("routerRoleTime", mRouterTime);
        REST_DIAG_NUMBER("leaderRoleTime", mLeaderTime);
#undef REST_DIAG_NUMBER
        return json;
    }
    default:
        return NULL;
    }
}

static void rest_diagnostic_finish(thread_rest_item_t *action)
{
    cJSON *attributes = action->result;
    action->result = NULL;
    thread_rest_item_t *result = rest_new(&s_rest_diagnostics, "networkDiagnostics", NULL, attributes);
    if (result) {
        rest_result(action, result);
        rest_status(action, "completed");
    } else {
        rest_status(action, "failed");
    }
    action->active = false;
    if (s_rest_diag_action == action) {
        s_rest_diag_action = NULL;
    }
}

static cJSON *rest_query_array(thread_rest_item_t *action, const char *key)
{
    if (!action->result) {
        action->result = cJSON_CreateObject();
    }
    cJSON *array = rest_get(action->result, key);
    if (!array) {
        array = cJSON_AddArrayToObject(action->result, key);
    }
    return array;
}

static void rest_query_done(otError error)
{
    if (!s_rest_query_action || error == OT_ERROR_PENDING) {
        return;
    }
    thread_rest_item_t *action = s_rest_query_action;
    action->query_busy = false;
    s_rest_query_action = NULL;
    if (error != OT_ERROR_NONE) {
        action->active = false;
        rest_status(action, "failed");
        if (s_rest_diag_action == action) {
            s_rest_diag_action = NULL;
        }
    } else {
        action->query_remaining &= ~(UINT32_C(1) << action->query_type);
    }
}

static void rest_children_callback(otError error, const otMeshDiagChildEntry *child, void *context)
{
    thread_rest_item_t *action = s_rest_query_action;
    if (!action || (uintptr_t)context != action->token || action->complete) {
        return;
    }
    if (child) {
        cJSON *entry = cJSON_CreateObject();
        otLinkModeConfig mode = {.mRxOnWhenIdle = child->mRxOnWhenIdle,
                                 .mDeviceType = child->mDeviceTypeFtd,
                                 .mNetworkData = child->mFullNetData};
        cJSON_AddItemToObject(entry, "mode", rest_mode(mode));
        cJSON_AddBoolToObject(entry, "cslSynchronized", child->mCslSynchronized);
        cJSON_AddBoolToObject(entry, "supportsErrorRate", child->mSupportsErrRate);
        cJSON_AddItemToObject(entry, "rloc16", rest_rloc16(child->mRloc16));
        cJSON_AddItemToObject(entry, "extAddress", rest_hex(child->mExtAddress.m8, 8));
#define REST_CHILD_NUMBER(key, field) cJSON_AddNumberToObject(entry, key, child->field)
        cJSON_AddNumberToObject(entry, "childId", child->mRloc16 & 0x3ff);
        REST_CHILD_NUMBER("threadVersion", mVersion);
        REST_CHILD_NUMBER("timeout", mTimeout);
        REST_CHILD_NUMBER("age", mAge);
        REST_CHILD_NUMBER("linkAge", mConnectionTime);
        if (child->mSupervisionInterval) {
            REST_CHILD_NUMBER("supervisionInterval", mSupervisionInterval);
        }
        REST_CHILD_NUMBER("linkMargin", mLinkMargin);
        REST_CHILD_NUMBER("averageRssi", mAverageRssi);
        REST_CHILD_NUMBER("lastRssi", mLastRssi);
        REST_CHILD_NUMBER("queuedMessageCount", mQueuedMessageCount);
        if (child->mSupportsErrRate) {
            cJSON_AddNumberToObject(entry, "frameErrorRate", child->mFrameErrorRate / 65535.0);
            cJSON_AddNumberToObject(entry, "messageErrorRate", child->mMessageErrorRate / 65535.0);
        }
        if (child->mCslSynchronized) {
            REST_CHILD_NUMBER("cslPeriod", mCslPeriod);
            REST_CHILD_NUMBER("cslTimeout", mCslTimeout);
            REST_CHILD_NUMBER("cslChannel", mCslChannel);
        }
#undef REST_CHILD_NUMBER
        cJSON_AddItemToArray(rest_query_array(action, "children"), entry);
    }
    rest_query_done(error);
}

static void rest_child_addresses_callback(otError error, uint16_t rloc, otMeshDiagIp6AddrIterator *iterator,
                                          void *context)
{
    thread_rest_item_t *action = s_rest_query_action;
    if (!action || (uintptr_t)context != action->token || action->complete) {
        return;
    }
    if (iterator) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddItemToObject(entry, "rloc16", rest_rloc16(rloc));
        cJSON *addresses = cJSON_AddArrayToObject(entry, "ipv6Addresses");
        otIp6Address address;
        while (otMeshDiagGetNextIp6Address(iterator, &address) == OT_ERROR_NONE) {
            cJSON_AddItemToArray(addresses, rest_address(&address));
        }
        cJSON_AddItemToArray(rest_query_array(action, "childIpv6Addresses"), entry);
    }
    rest_query_done(error);
}

static void rest_neighbors_callback(otError error, const otMeshDiagRouterNeighborEntry *neighbor, void *context)
{
    thread_rest_item_t *action = s_rest_query_action;
    if (!action || (uintptr_t)context != action->token || action->complete) {
        return;
    }
    if (neighbor) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddBoolToObject(entry, "supportsErrorRate", neighbor->mSupportsErrRate);
        cJSON_AddItemToObject(entry, "rloc16", rest_rloc16(neighbor->mRloc16));
        cJSON_AddItemToObject(entry, "extAddress", rest_hex(neighbor->mExtAddress.m8, 8));
        cJSON_AddNumberToObject(entry, "threadVersion", neighbor->mVersion);
        cJSON_AddNumberToObject(entry, "linkAge", neighbor->mConnectionTime);
        cJSON_AddNumberToObject(entry, "linkMargin", neighbor->mLinkMargin);
        cJSON_AddNumberToObject(entry, "averageRssi", neighbor->mAverageRssi);
        cJSON_AddNumberToObject(entry, "lastRssi", neighbor->mLastRssi);
        if (neighbor->mSupportsErrRate) {
            cJSON_AddNumberToObject(entry, "frameErrorRate", neighbor->mFrameErrorRate / 65535.0);
            cJSON_AddNumberToObject(entry, "messageErrorRate", neighbor->mMessageErrorRate / 65535.0);
        }
        cJSON_AddItemToArray(rest_query_array(action, "routerNeighbors"), entry);
    }
    rest_query_done(error);
}

static void rest_start_query(thread_rest_item_t *action, otInstance *instance)
{
    if (action->query_busy || s_rest_query_action || s_rest_mesh_action) {
        return;
    }
    if (!action->query_remaining) {
        rest_diagnostic_finish(action);
        return;
    }
    otIp6Address destination;
    if (!rest_destination(instance, action->attributes, &destination)) {
        return;
    }
    uint16_t rloc = ((uint16_t)destination.mFields.m8[14] << 8) | destination.mFields.m8[15];
    /* ML-EID destinations resolve through the device collection's RLOC. */
    const char *destination_type = rest_string(action->attributes, "destinationType");
    if (destination_type && !strcmp(destination_type, "mleid")) {
        const char *iid = rest_string(action->attributes, "destination");
        for (thread_rest_item_t *device = s_rest_devices; device; device = device->next) {
            const char *device_iid = rest_string(device->attributes, "mlEidIid");
            const char *device_rloc = rest_string(device->attributes, "rloc16");
            if (device_iid && device_rloc && !strcmp(device_iid, iid)) {
                rloc = strtoul(device_rloc, NULL, 16);
                break;
            }
        }
    }
    action->query_type = __builtin_ctz(action->query_remaining);
    action->query_busy = true;
    s_rest_query_action = action;
    rest_query_array(action, s_rest_diag_names[action->query_type]);
    otError error;
    void *context = (void *)(uintptr_t)action->token;
    if (action->query_type == 29) {
        error = otMeshDiagQueryChildTable(instance, rloc, rest_children_callback, context);
    } else if (action->query_type == 30) {
        error = otMeshDiagQueryChildrenIp6Addrs(instance, rloc, rest_child_addresses_callback, context);
    } else {
        error = otMeshDiagQueryRouterNeighborTable(instance, rloc, rest_neighbors_callback, context);
    }
    if (error != OT_ERROR_NONE) {
        s_rest_query_action = NULL;
        action->query_busy = false;
        if (error != OT_ERROR_BUSY) {
            action->active = false;
            rest_status(action, "failed");
        }
    }
}

static void rest_diagnostic_callback(otError error, otMessage *message, const otMessageInfo *info, void *context)
{
    (void)context;
    thread_rest_item_t *action = s_rest_diag_action;
    if (!action || (uintptr_t)context != action->token || action->complete || !message || error != OT_ERROR_NONE) {
        return;
    }
    cJSON *attributes = cJSON_CreateObject();
    otNetworkDiagIterator iterator = OT_NETWORK_DIAGNOSTIC_ITERATOR_INIT;
    otNetworkDiagTlv tlv;
    while (otThreadGetNextDiagnosticTlv(message, &iterator, &tlv) == OT_ERROR_NONE) {
        if (tlv.mType < 35 && s_rest_diag_names[tlv.mType]) {
            cJSON *value = rest_diag_tlv(&tlv);
            if (value) {
                rest_replace(attributes, s_rest_diag_names[tlv.mType], value);
            }
        }
    }
    (void)info;
    cJSON_Delete(action->result);
    action->result = attributes;
    action->diagnostic_received = true;
    if (!action->query_remaining) {
        rest_diagnostic_finish(action);
    }
}

static void rest_energy_callback(uint32_t mask, const uint8_t *energy, uint8_t count, void *context)
{
    (void)context;
    thread_rest_item_t *action = s_rest_energy_action;
    if (!action || (uintptr_t)context != action->token || action->complete) {
        return;
    }
    cJSON *attributes = cJSON_CreateObject();
    otIp6Address destination;
    if (rest_destination(esp_openthread_get_instance(), action->attributes, &destination)) {
        cJSON_AddItemToObject(attributes, "origin", rest_hex(destination.mFields.m8 + 8, 8));
    }
    cJSON *report = cJSON_AddArrayToObject(attributes, "report");
    unsigned channels = __builtin_popcount(mask);
    unsigned channel_index = 0;
    for (unsigned channel = 0; channel < 32; channel++) {
        if (!(mask & (UINT32_C(1) << channel))) {
            continue;
        }
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "channel", channel);
        cJSON *rssi = cJSON_AddArrayToObject(entry, "maxRssi");
        for (unsigned i = channel_index; i < count; i += channels) {
            cJSON_AddItemToArray(rssi, cJSON_CreateNumber((int8_t)energy[i]));
        }
        cJSON_AddItemToArray(report, entry);
        channel_index++;
    }
    thread_rest_item_t *result = rest_new(&s_rest_diagnostics, "energyScanReport", NULL, attributes);
    if (result) {
        rest_result(action, result);
        rest_status(action, "completed");
    } else {
        rest_status(action, "failed");
    }
    action->active = false;
    s_rest_energy_action = NULL;
}

static void rest_joiner_callback(otCommissionerJoinerEvent event, const otJoinerInfo *joiner,
                                 const otExtAddress *joiner_id, void *context)
{
    (void)joiner_id;
    (void)context;
    if (!joiner) {
        return;
    }
    for (thread_rest_item_t *action = s_rest_actions; action; action = action->next) {
        if (action->complete || strcmp(action->type, "addThreadDeviceTask") || !action->active) {
            continue;
        }
        if (action->joiner.mType != joiner->mType) {
            continue;
        }
        if (joiner->mType == OT_JOINER_INFO_TYPE_EUI64 &&
            memcmp(action->joiner.mSharedId.mEui64.m8, joiner->mSharedId.mEui64.m8, 8)) {
            continue;
        }
        if (joiner->mType == OT_JOINER_INFO_TYPE_DISCERNER &&
            (action->joiner.mSharedId.mDiscerner.mValue != joiner->mSharedId.mDiscerner.mValue ||
             action->joiner.mSharedId.mDiscerner.mLength != joiner->mSharedId.mDiscerner.mLength)) {
            continue;
        }
        const char *status = rest_string(action->attributes, "status");
        switch (event) {
        case OT_COMMISSIONER_JOINER_START:
            status = "attempted";
            break;
        case OT_COMMISSIONER_JOINER_CONNECTED:
            break;
        case OT_COMMISSIONER_JOINER_FINALIZE:
            break;
        case OT_COMMISSIONER_JOINER_END:
            status = "completed";
            break;
        case OT_COMMISSIONER_JOINER_REMOVED:
            status = !strcmp(status, "attempted") ? "failed" : "stopped";
            break;
        }
        rest_status(action, status);
        if (action->complete) {
            action->active = false;
        }
    }
}

static void rest_cancel(thread_rest_item_t *action, otInstance *instance)
{
    if (!action->complete) {
        rest_status(action, "stopped");
    }
    if (s_rest_mesh_action == action) {
        otMeshDiagCancel(instance);
        s_rest_mesh_action = NULL;
        action->active = false;
    }
    if (!strcmp(action->type, "addThreadDeviceTask") && action->joiner_registered) {
        rest_remove_joiner(instance, &action->joiner);
        action->active = false;
        action->joiner_registered = false;
    }
    /* Diagnostic and energy APIs may still deliver callbacks. They use permanent
     * service slots, never an item pointer supplied by an HTTP request. */
    if (s_rest_query_action == action) {
        otMeshDiagCancel(instance);
        s_rest_query_action = NULL;
        action->query_busy = false;
    }
    if (s_rest_diag_action == action) {
        s_rest_diag_action = NULL;
        action->active = false;
    }
    if (s_rest_energy_action == action) {
        s_rest_energy_action = NULL;
        action->active = false;
    }
}

static void rest_poll(otInstance *instance)
{
    int64_t now = esp_timer_get_time();
    for (thread_rest_item_t *action = s_rest_actions; action; action = action->next) {
        if (action->complete) {
            if (action->joiner_registered) {
                rest_remove_joiner(instance, &action->joiner);
                action->joiner_registered = false;
            }
            continue;
        }
        if (now >= action->deadline) {
            rest_cancel(action, instance);
            if (strcmp(action->type, "addThreadDeviceTask")) {
                rest_status(action, "failed");
            }
            continue;
        }
        rest_replace(action->attributes, "timeout", cJSON_CreateNumber((action->deadline - now) / 1000000));
        if (action->active) {
            if (action->diagnostic_received && !strcmp(action->type, "getNetworkDiagnosticTask")) {
                rest_start_query(action, instance);
            }
            continue;
        }
        if (!strcmp(action->type, "updateDeviceCollectionTask")) {
            if (s_rest_mesh_action || s_rest_query_action || s_rest_diag_action) {
                continue;
            }
            otMeshDiagDiscoverConfig config = {.mDiscoverIp6Addresses = true, .mDiscoverChildTable = true};
            s_rest_mesh_action = action;
            otError error = otMeshDiagDiscoverTopology(instance, &config, rest_mesh_callback, NULL);
            if (error == OT_ERROR_NONE) {
                action->active = true;
                action->attempts++;
                rest_status(action, "active");
                const uint8_t types[] = {0, 1, 2, 8, 23};
                otThreadSendDiagnosticGet(instance, otThreadGetRealmLocalAllThreadNodesMulticastAddress(instance),
                                          types, sizeof(types), rest_discover_devices_callback,
                                          (void *)(uintptr_t)action->token);
            } else {
                s_rest_mesh_action = NULL;
            }
            continue;
        }
        if (!strcmp(action->type, "addThreadDeviceTask") || !strcmp(action->type, "getEnergyScanTask")) {
            otCommissionerState state = otCommissionerGetState(instance);
            if (state == OT_COMMISSIONER_STATE_DISABLED) {
                if (otCommissionerStart(instance, NULL, rest_joiner_callback, NULL) == OT_ERROR_NONE) {
                    s_rest_commissioner_owned = true;
                }
                rest_status(action, "waiting");
                continue;
            }
            if (state != OT_COMMISSIONER_STATE_ACTIVE) {
                continue;
            }
        }
        if (!strcmp(action->type, "addThreadDeviceTask")) {
            if (rest_add_joiner(instance, &action->joiner) == OT_ERROR_NONE) {
                action->active = true;
                action->joiner_registered = true;
                rest_status(action, "undiscovered");
            }
            continue;
        }
        otIp6Address destination;
        if (!rest_destination(instance, action->attributes, &destination)) {
            continue;
        }
        if (!strcmp(action->type, "getEnergyScanTask")) {
            if (s_rest_energy_action) {
                continue;
            }
            uint32_t mask = 0;
            cJSON *channel;
            cJSON_ArrayForEach(channel, rest_get(action->attributes, "channelMask"))
            {
                mask |= UINT32_C(1) << channel->valueint;
            }
            s_rest_energy_action = action;
            otError error =
                otCommissionerEnergyScan(instance, mask, rest_get(action->attributes, "count")->valueint,
                                         rest_get(action->attributes, "period")->valueint,
                                         rest_get(action->attributes, "scanDuration")->valueint, &destination,
                                         rest_energy_callback, (void *)(uintptr_t)action->token);
            if (error == OT_ERROR_NONE) {
                action->active = true;
                rest_status(action, "active");
            } else {
                s_rest_energy_action = NULL;
            }
            continue;
        }
        uint8_t types[35], count = 0;
        bool included[35] = {false};
        cJSON *type;
        cJSON_ArrayForEach(type, rest_get(action->attributes, "types"))
        {
            int id = rest_diag_type(type->valuestring);
            if (id >= 0 && !included[id]) {
                included[id] = true;
                if (id >= 29 && id <= 31) {
                    action->query_remaining |= UINT32_C(1) << id;
                } else {
                    types[count++] = id;
                }
            }
        }
        if (!strcmp(action->type, "resetNetworkDiagCounterTask")) {
            rest_status(action,
                        otThreadSendDiagnosticReset(instance, &destination, types, count) == OT_ERROR_NONE ? "completed"
                                                                                                           : "failed");
        } else {
            if (s_rest_diag_action || s_rest_mesh_action) {
                continue;
            }
            s_rest_diag_action = action;
            if (!count) {
                action->active = true;
                action->diagnostic_received = true;
                action->result = cJSON_CreateObject();
                rest_status(action, "active");
                continue;
            }
            otError error = otThreadSendDiagnosticGet(instance, &destination, types, count, rest_diagnostic_callback,
                                                      (void *)(uintptr_t)action->token);
            if (error == OT_ERROR_NONE) {
                action->active = true;
                rest_status(action, "active");
            } else {
                s_rest_diag_action = NULL;
            }
        }
    }
    if (s_rest_commissioner_owned) {
        bool needed = false;
        for (thread_rest_item_t *action = s_rest_actions; action; action = action->next) {
            needed |= !action->complete &&
                (!strcmp(action->type, "addThreadDeviceTask") || !strcmp(action->type, "getEnergyScanTask"));
        }
        if (!needed) {
            otCommissionerStop(instance);
            s_rest_commissioner_owned = false;
        }
    }
}

static void rest_process_task(void *argument)
{
    (void)argument;
    while (true) {
        esp_openthread_lock_acquire(portMAX_DELAY);
        rest_poll(esp_openthread_get_instance());
        esp_openthread_lock_release();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t esp_br_thread_rest_init(void)
{
    if (s_rest_task) {
        return ESP_OK;
    }
    if (!s_rest_pending_done) {
        s_rest_pending_done = xSemaphoreCreateBinary();
    }
    if (!s_rest_pending_done) {
        return ESP_ERR_NO_MEM;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    rest_self(esp_openthread_get_instance());
    esp_openthread_lock_release();
    return xTaskCreate(rest_process_task, "thread_rest", 8192, NULL, 4, &s_rest_task) == pdPASS ? ESP_OK
                                                                                                : ESP_ERR_NO_MEM;
}

static bool rest_field_selected(const char *fields, const char *key, bool *nested)
{
    *nested = false;
    if (!fields || !*fields) {
        return true;
    }
    size_t length = strlen(key);
    for (const char *p = fields; *p;) {
        while (*p == ' ' || *p == ',') {
            p++;
        }
        const char *end = strchr(p, ',');
        if (!end) {
            end = p + strlen(p);
        }
        size_t count = end - p;
        if (count == length && !strncmp(p, key, length)) {
            return true;
        }
        if (count > length && p[length] == '.' && !strncmp(p, key, length)) {
            *nested = true;
        }
        p = *end ? end + 1 : end;
    }
    return *nested;
}

static void rest_filter(cJSON *attributes, const char *fields)
{
    if (!fields || !*fields) {
        return;
    }
    for (cJSON *item = attributes->child, *next; item; item = next) {
        next = item->next;
        if (!item->string || !strcmp(item->string, "created") || !strcmp(item->string, "updated")) {
            continue;
        }
        bool nested;
        if (!rest_field_selected(fields, item->string, &nested)) {
            cJSON_DeleteItemFromObjectCaseSensitive(attributes, item->string);
        } else if (nested && (cJSON_IsObject(item) || cJSON_IsArray(item))) {
            size_t key_length = strlen(item->string);
            char *subfields = calloc(strlen(fields) + 1, 1);
            if (!subfields) {
                continue;
            }
            size_t written = 0;
            for (const char *p = fields; *p;) {
                while (*p == ',' || *p == ' ') {
                    p++;
                }
                const char *end = strchr(p, ',');
                if (!end) {
                    end = p + strlen(p);
                }
                if ((size_t)(end - p) > key_length && !strncmp(p, item->string, key_length) && p[key_length] == '.') {
                    size_t count = end - p - key_length - 1;
                    if (written) {
                        subfields[written++] = ',';
                    }
                    memcpy(subfields + written, p + key_length + 1, count);
                    written += count;
                }
                p = *end ? end + 1 : end;
            }
            if (cJSON_IsObject(item)) {
                rest_filter(item, subfields);
            } else {
                for (cJSON *element = item->child; element; element = element->next) {
                    if (cJSON_IsObject(element)) {
                        rest_filter(element, subfields);
                    }
                }
            }
            free(subfields);
        }
    }
}

static cJSON *rest_item_json(thread_rest_item_t *item, const esp_br_thread_rest_request_t *request, bool json_api)
{
    const char *fields = NULL;
    bool any_fields = false;
    for (cJSON *query = request->query ? request->query->child : NULL; query; query = query->next) {
        if (query->string && !strncmp(query->string, "fields[", 7)) {
            size_t length = strlen(query->string);
            if (length > 8 && query->string[length - 1] == ']') {
                thread_rest_item_t *collection = !strncmp(request->path, "/api/actions", 12) ? s_rest_actions
                    : !strncmp(request->path, "/api/diagnostics", 16)                        ? s_rest_diagnostics
                                                                                             : s_rest_devices;
                bool recognized = false;
                for (thread_rest_item_t *entry = collection; entry; entry = entry->next) {
                    recognized |= length == strlen(entry->type) + 8 &&
                        !strncmp(query->string + 7, entry->type, strlen(entry->type));
                }
                if (!recognized) {
                    continue;
                }
                any_fields = true;
                if (length == strlen(item->type) + 8 && !strncmp(query->string + 7, item->type, strlen(item->type))) {
                    fields = query->valuestring;
                }
            }
        }
    }
    if (any_fields && !fields) {
        return NULL;
    }
    cJSON *attributes = cJSON_Duplicate(item->attributes, true);
    if (!attributes) {
        return NULL;
    }
    rest_filter(attributes, fields);
    if (!json_api) {
        cJSON_DeleteItemFromObjectCaseSensitive(attributes, "created");
        cJSON_DeleteItemFromObjectCaseSensitive(attributes, "updated");
        return attributes;
    }
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", item->id);
    cJSON_AddStringToObject(json, "type", item->type);
    cJSON_AddItemToObject(json, "attributes", attributes);
    if (item->relationships) {
        cJSON_AddItemToObject(json, "relationships", cJSON_Duplicate(item->relationships, true));
    }
    return json;
}

static cJSON *rest_meta(thread_rest_item_t *head, bool actions)
{
    cJSON *meta = cJSON_CreateObject();
    cJSON *collection = cJSON_AddObjectToObject(meta, "collection");
    cJSON_AddNumberToObject(collection, "offset", 0);
    cJSON_AddNumberToObject(collection, "limit", THREAD_REST_COLLECTION_LIMIT);
    cJSON_AddNumberToObject(collection, "total", rest_count(head));
    if (actions) {
        unsigned pending = 0;
        for (; head; head = head->next) {
            pending += !head->complete && !head->deleted;
        }
        cJSON_AddNumberToObject(collection, "pending", pending);
    }
    return meta;
}

static void rest_collection_delete(thread_rest_item_t **head, const char *id, otInstance *instance)
{
    for (thread_rest_item_t **link = head; *link;) {
        thread_rest_item_t *item = *link;
        if (id && strcmp(id, item->id)) {
            link = &item->next;
            continue;
        }
        if (head == &s_rest_actions) {
            rest_cancel(item, instance);
        }
        *link = item->next;
        rest_item_free(item);
        if (id) {
            break;
        }
    }
}

static void rest_actions_post(const esp_br_thread_rest_request_t *request, esp_br_thread_rest_response_t *response)
{
    if (!*request->content_type) {
        response->status = 406;
        return;
    }
    if (strcmp(request->content_type, THREAD_REST_JSON_API)) {
        response->status = 415;
        return;
    }
    cJSON *root = cJSON_Parse(request->body);
    if (!root) {
        response->status = 400;
        return;
    }
    cJSON *data = rest_get(root, "data"), *datum;
    if (!cJSON_IsArray(data)) {
        response->status = 422;
        goto exit;
    }
    cJSON_ArrayForEach(datum, data)
    {
        if (!rest_validate_action(datum)) {
            response->status = 422;
            goto exit;
        }
    }
    unsigned pending = 0;
    for (thread_rest_item_t *a = s_rest_actions; a; a = a->next) {
        pending += !a->complete;
    }
    if (pending + cJSON_GetArraySize(data) > THREAD_REST_COLLECTION_LIMIT) {
        response->status = 503;
        goto exit;
    }
    response->content_type = THREAD_REST_JSON_API;
    response->json = cJSON_CreateObject();
    cJSON *items = cJSON_AddArrayToObject(response->json, "data");
    cJSON_ArrayForEach(datum, data)
    {
        const char *requested_type = rest_string(datum, "type");
        static const char *const types[] = {"updateDeviceCollectionTask", "getNetworkDiagnosticTask",
                                            "getEnergyScanTask", "resetNetworkDiagCounterTask", "addThreadDeviceTask"};
        const char *type = NULL;
        for (unsigned i = 0; i < 5; i++) {
            if (!strcmp(requested_type, types[i])) {
                type = types[i];
            }
        }
        thread_rest_item_t *action =
            rest_new(&s_rest_actions, type, NULL, cJSON_Duplicate(rest_get(datum, "attributes"), true));
        if (!action) {
            response->status = 503;
            break;
        }
        cJSON *timeout = rest_get(action->attributes, "timeout");
        double seconds = timeout ? timeout->valuedouble : 60;
        if (seconds > INT32_MAX) {
            seconds = INT32_MAX;
        }
        if (seconds < INT32_MIN) {
            seconds = INT32_MIN;
        }
        action->deadline = action->started + (int64_t)seconds * 1000000;
        rest_status(action, "pending");
        if (!strcmp(type, "addThreadDeviceTask")) {
            rest_parse_joiner(action->attributes, &action->joiner);
            action->joiner.mExpirationTime = seconds > 0 ? seconds : 0;
            cJSON *attributes = rest_joiner_json(&action->joiner);
            cJSON *created = cJSON_DetachItemFromObjectCaseSensitive(action->attributes, "created");
            cJSON_Delete(action->attributes);
            action->attributes = attributes;
            cJSON_AddItemToObject(attributes, "created", created);
            rest_status(action, "pending");
        }
        if (rest_get(action->attributes, "destination") && !rest_get(action->attributes, "destinationType")) {
            const char *destination = rest_string(action->attributes, "destination");
            cJSON_AddStringToObject(action->attributes, "destinationType",
                                    strlen(destination) == 6 ? "rloc" : "extended");
        }
        cJSON_AddItemToArray(items, rest_item_json(action, request, true));
    }
    cJSON_AddItemToObject(response->json, "meta", rest_meta(s_rest_actions, false));
exit:
    cJSON_Delete(root);
}

static void rest_collection(const esp_br_thread_rest_request_t *request, esp_br_thread_rest_response_t *response,
                            otInstance *instance)
{
    thread_rest_item_t **head;
    const char *id = NULL;
    bool self = !strcmp(request->path, "/api/node");
    if (!strncmp(request->path, "/api/actions", 12)) {
        head = &s_rest_actions;
        if (request->path[12] == '/') {
            id = request->path + 13;
        }
    } else if (!strncmp(request->path, "/api/diagnostics", 16)) {
        head = &s_rest_diagnostics;
        if (request->path[16] == '/') {
            id = request->path + 17;
        }
    } else {
        head = &s_rest_devices;
        if (!self && request->path[12] == '/') {
            id = request->path + 13;
        }
    }
    cJSON *local_id = self ? rest_hex(otLinkGetExtendedAddress(instance)->m8, 8) : NULL;
    if (self) {
        id = local_id ? local_id->valuestring : "";
    }
    thread_rest_item_t *item = id ? rest_find(*head, id) : NULL;
    if (request->method == HTTP_POST) {
        rest_actions_post(request, response);
        goto exit;
    }
    if (id && !item) {
        response->status = 404;
        goto exit;
    }
    if (request->method == HTTP_DELETE) {
        rest_collection_delete(head, id, instance);
        response->status = 204;
        goto exit;
    }
    bool json_api = !strcmp(request->accept, THREAD_REST_JSON_API) || !strcmp(request->accept, "*/*");
    if (!json_api && strcmp(request->accept, "application/json")) {
        response->status = 406;
        goto exit;
    }
    response->content_type = json_api ? THREAD_REST_JSON_API : "application/json";
    if (id) {
        cJSON *data = rest_item_json(item, request, json_api);
        if (!data) {
            response->status = 404;
            goto exit;
        }
        if (json_api) {
            response->json = cJSON_CreateObject();
            cJSON_AddItemToObject(response->json, "data", data);
        } else {
            response->json = data;
        }
    } else {
        cJSON *data = cJSON_CreateArray();
        for (thread_rest_item_t *entry = *head; entry; entry = entry->next) {
            if (!entry->deleted) {
                esp_br_thread_rest_request_t unfiltered = *request;
                if (!json_api) {
                    unfiltered.query = NULL;
                }
                cJSON *json = rest_item_json(entry, &unfiltered, json_api);
                if (json) {
                    cJSON_AddItemToArray(data, json);
                }
            }
        }
        if (json_api) {
            response->json = cJSON_CreateObject();
            cJSON_AddItemToObject(response->json, "data", data);
            cJSON_AddItemToObject(response->json, "meta", rest_meta(*head, head == &s_rest_actions));
        } else {
            response->json = data;
        }
    }
exit:
    cJSON_Delete(local_id);
}

void esp_br_thread_rest_request(const esp_br_thread_rest_request_t *request, esp_br_thread_rest_response_t *response)
{
    response->status = 200;
    response->content_type = "application/json";
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *instance = esp_openthread_get_instance();
    const char *path = request->path;
    if (!strncmp(path, "/api/", 5)) {
        rest_collection(request, response, instance);
        goto exit;
    }
    if (!strcmp(path, "/node/dataset/active") || !strcmp(path, "/node/dataset/pending")) {
        rest_dataset(request, response, instance, !strcmp(path, "/node/dataset/pending"));
        goto exit;
    }
    cJSON *body = *request->body ? cJSON_Parse(request->body) : NULL;
    const char *state = cJSON_GetStringValue(body);
    otError error = OT_ERROR_NONE;
    if (!strcmp(path, "/node")) {
        if (request->method == HTTP_GET) {
            response->json = rest_node(instance);
            if (!response->json) {
                response->status = 500;
            }
        } else {
            error = otThreadSetEnabled(instance, false);
            if (error == OT_ERROR_NONE) {
                error = otIp6SetEnabled(instance, false);
            }
            if (error == OT_ERROR_NONE) {
                if (otInstanceErasePersistentInfo(instance) == OT_ERROR_NONE) {
                    response->reset = true;
                } else {
                    response->status = 500;
                }
            } else {
                response->status = 409;
            }
        }
    } else if (!strcmp(path, "/node/state")) {
        if (request->method == HTTP_GET) {
            response->json = cJSON_CreateString(s_ot_state[otThreadGetDeviceRole(instance)]);
        } else if (!state || (strcmp(state, "enable") && strcmp(state, "disable"))) {
            response->status = 400;
        } else {
            if (!strcmp(state, "enable")) {
                error = otIp6IsEnabled(instance) ? OT_ERROR_NONE : otIp6SetEnabled(instance, true);
                if (error == OT_ERROR_NONE) {
                    error = otThreadSetEnabled(instance, true);
                }
            } else {
                error = otThreadSetEnabled(instance, false);
                if (error == OT_ERROR_NONE) {
                    error = otIp6SetEnabled(instance, false);
                }
            }
            response->status = error == OT_ERROR_NONE ? 200
                : error == OT_ERROR_INVALID_STATE     ? 409
                : error == OT_ERROR_INVALID_ARGS      ? 400
                                                      : 500;
        }
    } else if (!strcmp(path, "/node/rloc")) {
        response->json = rest_address(otThreadGetRloc(instance));
    } else if (!strcmp(path, "/node/rloc16")) {
        response->json = cJSON_CreateNumber(otThreadGetRloc16(instance));
    } else if (!strcmp(path, "/node/ext-address")) {
        response->json = rest_hex(otLinkGetExtendedAddress(instance)->m8, 8);
    } else if (!strcmp(path, "/node/network-name")) {
        response->json = cJSON_CreateString(otThreadGetNetworkName(instance));
    } else if (!strcmp(path, "/node/ext-panid")) {
        response->json = rest_hex(otThreadGetExtendedPanId(instance)->m8, 8);
    } else if (!strcmp(path, "/node/num-of-router")) {
        response->json = cJSON_CreateNumber(rest_router_count(instance));
    } else if (!strcmp(path, "/node/leader-data")) {
        otLeaderData leader;
        if (otThreadGetLeaderData(instance, &leader) == OT_ERROR_NONE) {
            response->json = rest_leader(&leader);
        } else {
            response->status = 500;
        }
    } else if (!strcmp(path, "/node/ba-id")) {
        otBorderAgentId id;
        if (otBorderAgentGetId(instance, &id) == OT_ERROR_NONE) {
            response->json = rest_hex(id.mId, sizeof(id.mId));
        } else {
            response->status = 500;
        }
    } else if (!strcmp(path, "/node/coprocessor/version")) {
        response->json = cJSON_CreateString(otPlatRadioGetVersionString(instance));
    } else if (!strcmp(path, "/node/commissioner/state")) {
        if (request->method == HTTP_GET) {
            static const char *states[] = {"disabled", "petitioning", "active"};
            response->json = cJSON_CreateString(states[otCommissionerGetState(instance)]);
        } else if (!state || (strcmp(state, "enable") && strcmp(state, "disable"))) {
            response->status = 400;
        } else {
            s_rest_commissioner_owned = false;
            error = !strcmp(state, "enable") ? otCommissionerStart(instance, NULL, rest_joiner_callback, NULL)
                                             : otCommissionerStop(instance);
            response->status = error == OT_ERROR_NONE ? 200
                : error == OT_ERROR_INVALID_STATE     ? 409
                : error == OT_ERROR_INVALID_ARGS      ? 400
                                                      : 500;
        }
    } else if (!strcmp(path, "/node/commissioner/joiner")) {
        otJoinerInfo joiner = {0};
        if (request->method == HTTP_GET) {
            uint16_t iterator = 0;
            response->json = cJSON_CreateArray();
            while (otCommissionerGetNextJoinerInfo(instance, &iterator, &joiner) == OT_ERROR_NONE) {
                cJSON_AddItemToArray(response->json, rest_joiner_json(&joiner));
            }
        } else if (request->method == HTTP_POST) {
            if (otCommissionerGetState(instance) != OT_COMMISSIONER_STATE_ACTIVE) {
                response->status = 409;
            } else if (!rest_parse_joiner(body, &joiner)) {
                response->status = 400;
            } else {
                error = rest_add_joiner(instance, &joiner);
                response->status = error == OT_ERROR_NONE ? 200
                    : error == OT_ERROR_INVALID_ARGS      ? 400
                    : error == OT_ERROR_NO_BUFS           ? 507
                                                          : 500;
            }
        } else if (!rest_joiner_id(state, &joiner)) {
            response->status = 400;
        } else if (otCommissionerGetState(instance) != OT_COMMISSIONER_STATE_ACTIVE) {
            response->status = 409;
        } else {
            rest_remove_joiner(instance, &joiner);
        }
    } else if (!strcmp(path, "/node/ba-epskc/state")) {
        if (request->method == HTTP_GET) {
            response->json = cJSON_CreateString(
                otBorderAgentEphemeralKeyGetState(instance) == OT_BORDER_AGENT_STATE_DISABLED ? "disabled" : "enabled");
        } else if (!state || (strcmp(state, "enable") && strcmp(state, "disable"))) {
            response->status = 400;
        } else {
            otBorderAgentEphemeralKeySetEnabled(instance, !strcmp(state, "enable"));
        }
    } else if (!strcmp(path, "/node/ba-epskc/key")) {
        if (request->method == HTTP_GET) {
            response->json = cJSON_CreateObject();
            cJSON_AddStringToObject(response->json, "state",
                                    epskc_state_to_string(otBorderAgentEphemeralKeyGetState(instance)));
            cJSON_AddNumberToObject(response->json, "port", otBorderAgentEphemeralKeyGetUdpPort(instance));
        } else if (request->method == HTTP_DELETE) {
            otBorderAgentEphemeralKeyStop(instance);
            esp_br_web_epskc_set_active_tap(NULL);
        } else {
            cJSON *lifetime = rest_get(body, "lifetime"), *port = rest_get(body, "port");
            if ((*request->body && !cJSON_IsObject(body)) ||
                (lifetime && !rest_number(lifetime, OT_BORDER_AGENT_MAX_EPHEMERAL_KEY_TIMEOUT)) ||
                (port && !rest_number(port, UINT16_MAX))) {
                response->status = 400;
            } else {
                otBorderAgentEphemeralKeyTap tap;
                error = otBorderAgentEphemeralKeyGenerateTap(&tap);
                if (error == OT_ERROR_NONE) {
                    error = otBorderAgentEphemeralKeyStart(instance, tap.mTap,
                                                           lifetime ? lifetime->valuedouble
                                                                    : OT_BORDER_AGENT_DEFAULT_EPHEMERAL_KEY_TIMEOUT,
                                                           port ? port->valuedouble : 0);
                }
                if (error == OT_ERROR_NONE) {
                    esp_br_web_epskc_set_active_tap(tap.mTap);
                    response->json = cJSON_CreateObject();
                    cJSON_AddStringToObject(response->json, "tap", tap.mTap);
                    cJSON_AddNumberToObject(response->json, "port", otBorderAgentEphemeralKeyGetUdpPort(instance));
                } else {
                    response->status = error == OT_ERROR_INVALID_STATE ? 409
                        : error == OT_ERROR_INVALID_ARGS               ? 400
                                                                       : 500;
                }
            }
        }
    } else {
        response->status = 404;
    }
    cJSON_Delete(body);
exit:
    esp_openthread_lock_release();
}
