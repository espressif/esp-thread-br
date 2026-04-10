/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_br_web_api.h"
#include "cJSON.h"
#include "esp_br_web_base.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_net_stack.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "malloc.h"
#include "sdkconfig.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <assert.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "openthread/border_agent.h"
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
    ESP_RETURN_ON_FALSE(!ret, NULL, API_TAG, "Failed to handle available network request");
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
        config.mDefaultRoute = (bool)param.defaultRoute;
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
