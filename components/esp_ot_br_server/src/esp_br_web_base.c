/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_br_web_base.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "malloc.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <stdlib.h>
#include "openthread/border_agent.h"
#include "openthread/dataset.h"
#include "openthread/error.h"
#include "openthread/platform/radio.h"

#define BASE_TAG "web_base"

// convert variable name to string
#define VARIABLE_NAME(name) (#name)
/*---------------------------------------------------------------------------------
                                usual method
---------------------------------------------------------------------------------*/
esp_err_t hex_to_string(const uint8_t hex[], char str[], size_t size)
{
    if (hex == NULL)
        return ESP_FAIL;
    for (int i = 0, j = 0; i < size * 2; i += 2, j++) {
        str[i] = (hex[j] & (0xf0)) >> 4;
        if (str[i] <= 9)
            str[i] = str[i] + '0';
        else
            str[i] = str[i] - 10 + 'a';

        str[i + 1] = (hex[j] & (0x0f));
        if (str[i + 1] <= 9)
            str[i + 1] = str[i + 1] + '0';
        else
            str[i + 1] = str[i + 1] - 10 + 'a';
    }
    str[size * 2] = '\0';
    return ESP_OK;
}

esp_err_t string_to_hex(char str[], uint8_t hex[], size_t size)
{
    if (!str || strlen(str) != size * 2)
        return ESP_FAIL;
    for (int i = 0, j = 0; j < size; i += 2, j++) {
        hex[j] = 0;
        if (str[i] >= '0' && str[i] <= '9')
            hex[j] = (str[i] - '0') << 4;
        else if (str[i] >= 'a' && str[i] <= 'f')
            hex[j] = (str[i] - 'a' + 10) << 4;
        else if (str[i] >= 'A' && str[i] <= 'F')
            hex[j] = (str[i] - 'A' + 10) << 4;
        else
            return ESP_FAIL;

        if (str[i + 1] >= '0' && str[i + 1] <= '9')
            hex[j] = (hex[j] & 0xf0) | ((str[i + 1] - '0') & 0x0f);
        else if (str[i + 1] >= 'a' && str[i + 1] <= 'f')
            hex[j] = (hex[j] & 0xf0) | ((str[i + 1] + 10 - 'a') & 0x0f);
        else if (str[i + 1] >= 'A' && str[i + 1] <= 'F')
            hex[j] = (hex[j] & 0xf0) | ((str[i + 1] + 10 - 'A') & 0x0f);
        else
            return ESP_FAIL;
    }
    return ESP_OK;
}

/*---------------------------------------------------------------------------------
                               thread properties
---------------------------------------------------------------------------------*/
#define CREATE_JSON_CHILD_TIEM(child_json, to_json, from_struct, type, element) \
    cJSON *child_json = cJSON_CreateObject();                                   \
    if (child_json)                                                             \
        cJSON_AddItemToObject(to_json, #element, child_json);

void otbr_properties_reset(openthread_properties_t *properties)
{
    memset(properties, 0x00, sizeof(openthread_properties_t));
}

cJSON *otbr_properties_struct_convert2_json(openthread_properties_t *properties)
{
    cJSON *root = cJSON_CreateObject();

    char address[OT_IP6_ADDRESS_STRING_SIZE];
    char prefix[OT_IP6_PREFIX_STRING_SIZE];
    otIp6AddressToString((const otIp6Address *)&properties->ipv6.link_local_address, address,
                         OT_IP6_ADDRESS_STRING_SIZE);
    cJSON_AddStringToObject(root, "IPv6:LinkLocalAddress", address);

    otIp6AddressToString((const otIp6Address *)&properties->ipv6.routing_local_address, address,
                         OT_IP6_ADDRESS_STRING_SIZE);
    cJSON_AddStringToObject(root, "IPv6:RoutingLocalAddress", address);

    otIp6AddressToString((const otIp6Address *)&properties->ipv6.mesh_local_address, address,
                         OT_IP6_ADDRESS_STRING_SIZE);
    cJSON_AddStringToObject(root, "IPv6:MeshLocalAddress", address);

    otIp6PrefixToString((const otIp6Prefix *)&properties->ipv6.mesh_local_prefix, prefix, OT_IP6_PREFIX_STRING_SIZE);
    cJSON_AddStringToObject(root, "IPv6:MeshLocalPrefix", prefix);

    char format[64];
    sprintf(format, "%s", properties->network.name.m8);
    cJSON_AddStringToObject(root, "Network:Name", format);
    sprintf(format, "0x%x", properties->network.panid);
    cJSON_AddStringToObject(root, "Network:PANID", format);
    sprintf(format, "%lu", properties->network.partition_id);
    cJSON_AddStringToObject(root, "Network:PartitionID", format);
    hex_to_string(properties->network.xpanid.m8, format, sizeof(otExtendedPanId));
    cJSON_AddStringToObject(root, "Network:XPANID", format);
    hex_to_string(properties->network.baid.mId, format, sizeof(otBorderAgentId));
    cJSON_AddStringToObject(root, "Network:BorderAgentID", format);

    cJSON_AddStringToObject(root, "OpenThread:Version", properties->information.version);
    sprintf(format, "%d", properties->information.version_api);
    cJSON_AddStringToObject(root, "OpenThread:Version API", format);
    cJSON_AddStringToObject(root, "RCP:State", otThreadDeviceRoleToString(properties->information.role));
    hex_to_string(properties->information.PSKc.m8, format, sizeof(otPskc));
    cJSON_AddStringToObject(root, "OpenThread:PSKc", format);

    sprintf(format, "%d", properties->rcp.channel);
    cJSON_AddStringToObject(root, "RCP:Channel", format);
    hex_to_string(properties->rcp.EUI64.m8, format, sizeof(otExtAddress));
    cJSON_AddStringToObject(root, "RCP:EUI64", format);
    sprintf(format, "%d dBm", properties->rcp.txpower);
    cJSON_AddStringToObject(root, "RCP:TxPower", format);
    cJSON_AddStringToObject(root, "RCP:Version", properties->rcp.version);

    cJSON_AddStringToObject(root, "WPAN service", properties->wpan.service);

    return root;
}

/*----------------------------------------------------------------------
                       Scan Thread netWork
-----------------------------------------------------------------------*/
void avaiable_network_reset(thread_network_information_t *network)
{
    memset(network, 0x00, sizeof(thread_network_information_t));
}

cJSON *avaiable_network_struct_convert2_json(thread_network_information_t *network)
{
    cJSON *root = cJSON_CreateObject();

    char format[64];
    cJSON_AddNumberToObject(root, "id", network->id);
    cJSON_AddStringToObject(root, "nn", network->network_name.m8);

    ESP_RETURN_ON_FALSE(!hex_to_string(network->extended_panid.m8, format, sizeof(network->extended_panid.m8)), root,
                        BASE_TAG, "Failed to convert extended panid");

    cJSON_AddStringToObject(root, "ep", format);

    sprintf(format, "0x%04x", network->panid);
    cJSON_AddStringToObject(root, "pi", format);

    ESP_RETURN_ON_FALSE(!hex_to_string(network->extended_address.m8, format, sizeof(network->extended_address.m8)),
                        root, BASE_TAG, "Failed to convert extended address");
    cJSON_AddStringToObject(root, "ha", format);

    cJSON_AddNumberToObject(root, "ch", network->channel);
    cJSON_AddNumberToObject(root, "ri", network->rssi);
    cJSON_AddNumberToObject(root, "li", network->lqi);

    return root;
}

esp_err_t initialize_available_thread_networks_list(thread_network_list_t *list)
{
    list->next = NULL;
    list->network = (thread_network_information_t *)malloc(sizeof(thread_network_information_t));
    ESP_RETURN_ON_FALSE(list->network, ESP_FAIL, BASE_TAG, "Failed to create available thread networks list");
    return ESP_OK;
}

esp_err_t append_available_thread_networks_list(thread_network_list_t *list, thread_network_information_t network)
{
    ESP_RETURN_ON_FALSE(list, ESP_FAIL, BASE_TAG, "Invalid network list");

    thread_network_list_t *head = list;
    while (head != NULL && head->next != NULL) {
        head = head->next;
    }
    thread_network_list_t *node = (thread_network_list_t *)malloc(sizeof(thread_network_list_t));
    if (node != NULL) {
        node->next = NULL;
        node->network = (thread_network_information_t *)malloc(sizeof(thread_network_information_t));
        if (node->network != NULL)
            memcpy(node->network, &network, sizeof(network)); /* need to free */
        else {
            ESP_LOGW(BASE_TAG, "Failed to create network node");
            return ESP_FAIL;
        }
        head->next = node;
    } else {
        ESP_LOGW(BASE_TAG, "Failed to create network list");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void destroy_available_thread_networks_list(thread_network_list_t *list)
{
    if (list == NULL)
        return;
    thread_network_list_t *pre = list;
    thread_network_list_t *next = list->next;
    while (next) {
        free(pre->network);
        free(pre);
        pre = next;
        next = pre->next;
    }
    free(pre->network);
    free(pre); // delete the end node
    list = NULL;
    return;
}

/*----------------------------------------------------------------------
                       Form Thread netWork
-----------------------------------------------------------------------*/
esp_err_t network_formation_param_json_convert2_struct(const cJSON *root, cJSON *log,
                                                       thread_network_formation_param_t *param)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(log, OT_ERROR_INVALID_ARGS, BASE_TAG, "Invalid arguement");
    if (!(root)) {
        ESP_LOGW(BASE_TAG, "Error: Package Type Error");
        cJSON_SetValuestring(log, "Error: Package Type Error");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *temp = cJSON_GetObjectItem(root, "networkName");
    if (!(temp) || strlen(temp->valuestring) > sizeof(otNetworkName)) {
        ESP_LOGW(BASE_TAG, "Error: Invalid Network Name");
        cJSON_SetValuestring(log, "Error: Invalid Network Name");
        return ESP_ERR_INVALID_ARG;
    } else {
        memcpy(param->network_name.m8, temp->valuestring,
               sizeof(otNetworkName) < strlen(temp->valuestring) + 1 ? sizeof(otNetworkName)
                                                                     : strlen(temp->valuestring) + 1);
    }
    if (!(temp = cJSON_GetObjectItem(root, "channel"))) {
        ESP_LOGW(BASE_TAG, "Error: Invalid Channel");
        cJSON_SetValuestring(log, "Error: Invalid Channel");
        return ESP_ERR_INVALID_ARG;
    }

    param->channel = cJSON_GetNumberValue(temp);
    if (param->channel < 11 || param->channel > 26) {
        ESP_LOGW(BASE_TAG, "Error: Channel Out of Range");
        cJSON_SetValuestring(log, "Error: Channel Out of Range");
        return ESP_ERR_INVALID_ARG;
    }

    if (!(temp = cJSON_GetObjectItem(root, "panId")) || sscanf(temp->valuestring, "0x%hx", &param->panid) != 1) {
        ESP_LOGW(BASE_TAG, "Error: PanId requries \"0x\"");
        cJSON_SetValuestring(log, "Error: PanId requries  \"0x\"");
        return ESP_ERR_INVALID_ARG;
    }

    if (!(temp = cJSON_GetObjectItem(root, "extPanId")) || strlen(temp->valuestring) != OT_EXT_PAN_ID_SIZE * 2 ||
        string_to_hex(temp->valuestring, param->extended_panid.m8, OT_EXT_PAN_ID_SIZE)) {
        ESP_LOGW(BASE_TAG, "Error: Extended PanId requires 16 bytes");
        cJSON_SetValuestring(log, "Error: Extended PanId requires 16 bytes");
        return ESP_ERR_INVALID_ARG;
    }

    if ((temp = cJSON_GetObjectItem(root, "prefix")) && strlen(temp->valuestring) + 1 < OT_IP6_PREFIX_STRING_SIZE) {
        memcpy(param->on_mesh_prefix, temp->valuestring, strlen(temp->valuestring) + 1);
    }

    if (!(temp = cJSON_GetObjectItem(root, "networkKey")) || strlen(temp->valuestring) != OT_NETWORK_KEY_SIZE * 2 ||
        string_to_hex(temp->valuestring, param->network_key.m8, OT_NETWORK_KEY_SIZE)) {
        ESP_LOGW(BASE_TAG, "Error: Network Key requires 32 bytes");
        cJSON_SetValuestring(log, "Error: Network Key requires 32 bytes");
        return ESP_ERR_INVALID_ARG;
    }

    if ((temp = cJSON_GetObjectItem(root, "passphrase")) &&
        strlen(temp->valuestring) + 1 < NETWORK_PASSPHRASE_MAX_SIZE) {
        memcpy(param->passphrase, temp->valuestring, strlen(temp->valuestring) + 1);
    }

    temp = cJSON_GetObjectItem(root, "defaultRoute");
    if (temp && temp->type != cJSON_NULL)
        param->default_route = (bool)cJSON_GetNumberValue(temp);
    else
        param->default_route = false;

    return ret;
}

void network_formation_param_reset(thread_network_formation_param_t *param)
{
    memset(param, 0x00, sizeof(thread_network_formation_param_t));
}

/*----------------------------------------------------------------------
                       Join Thread netWork
-----------------------------------------------------------------------*/
void network_join_param_reset(thread_network_join_param_t *param)
{
    memset(param, 0x00, sizeof(thread_network_join_param_t));
}

esp_err_t network_join_param_json_convert2_struct(const cJSON *root, cJSON *log, thread_network_join_param_t *param)
{
    cJSON *temp = cJSON_GetObjectItem(root, "index");
    ESP_RETURN_ON_FALSE(log, OT_ERROR_INVALID_ARGS, BASE_TAG, "Invalid arguement");
    if ((temp) && temp->valueint >= 0) {
        param->index = temp->valueint;
    } else {
        ESP_LOGW(BASE_TAG, "Error: Invalid Index");
        cJSON_SetValuestring(log, "Error: Invalid Index");
        return ESP_ERR_INVALID_ARG;
    }

    temp = cJSON_GetObjectItem(root, "credentialType");
    if (temp && temp->valuestring && strlen(temp->valuestring) < sizeof(param->credentialType)) {
        memcpy(param->credentialType, temp->valuestring, strlen(temp->valuestring) + 1);
    } else {
        ESP_LOGW(BASE_TAG, "Error: Invalid Credential Type");
        cJSON_SetValuestring(log, "Error: Invalid Credential Type");
        return ESP_ERR_INVALID_ARG;
    }

    temp = cJSON_GetObjectItem(root, "networkKey");
    if (temp && temp->valuestring && strlen(temp->valuestring) == OT_NETWORK_KEY_SIZE * 2) {
        string_to_hex(temp->valuestring, param->networkKey.m8, OT_NETWORK_KEY_SIZE);
    } else {
        ESP_LOGW(BASE_TAG, "Error: Invalid Network Key");
        cJSON_SetValuestring(log, "Error: Invalid Network Key");
        return ESP_ERR_INVALID_ARG;
    }

    temp = cJSON_GetObjectItem(root, "pskd");
    if (temp && temp->valuestring && strlen(temp->valuestring) + 1 < NETWORK_PSKD_MAX_SIZE) {
        memcpy(param->pskd, temp->valuestring, strlen(temp->valuestring) + 1);
    } else {
        ESP_LOGW(BASE_TAG, "Error: Invalid PSKd");
        cJSON_SetValuestring(log, "Error: Invalid PSKd");
        return ESP_ERR_INVALID_ARG;
    }

    temp = cJSON_GetObjectItem(root, "prefix");
    if (temp && temp->valuestring && strlen(temp->valuestring) + 1 < OT_IP6_PREFIX_STRING_SIZE) {
        if (!strstr(temp->valuestring, "/")) {
            memcpy(param->prefix, temp->valuestring, strlen(temp->valuestring) + 1);
            strcat(param->prefix, "/64");
        } else {
            memcpy(param->prefix, temp->valuestring, strlen(temp->valuestring) + 1);
        }
    } else {
        ESP_LOGW(BASE_TAG, "Error: Invalid Prefix");
        cJSON_SetValuestring(log, "Error: Invalid Prefix");
        return ESP_ERR_INVALID_ARG;
    }

    temp = cJSON_GetObjectItem(root, "defaultRoute");
    if (temp && temp->type != cJSON_NULL && temp->valueint)
        param->defaultRoute = true;
    else
        param->defaultRoute = false;
    return ESP_OK;
}

/*----------------------------------------------------------------------
                    Thread network Topology
-----------------------------------------------------------------------*/
esp_err_t initialize_thread_diagnosticTlv_list(thread_diagnosticTlv_list_t *list)
{
    list->next = NULL;
    list->diagTlv = (otNetworkDiagTlv *)malloc(sizeof(otNetworkDiagTlv));
    if (list->diagTlv == NULL)
        return ESP_FAIL;
    return ESP_OK;
}

esp_err_t append_thread_diagnosticTlv_list(thread_diagnosticTlv_list_t *list, otNetworkDiagTlv diagTlv)
{
    ESP_RETURN_ON_FALSE(list, ESP_ERR_INVALID_ARG, BASE_TAG, "Invalid Thread diagnostic list");
    thread_diagnosticTlv_list_t *head = list;
    while (head != NULL && head->next != NULL) { // find the end of list.
        head = head->next;
    }
    thread_diagnosticTlv_list_t *node = (thread_diagnosticTlv_list_t *)malloc(sizeof(thread_diagnosticTlv_list_t));
    if (node != NULL) {
        node->next = NULL;
        node->diagTlv = (otNetworkDiagTlv *)malloc(sizeof(otNetworkDiagTlv));
        if (node->diagTlv != NULL)
            memcpy(node->diagTlv, &diagTlv, sizeof(otNetworkDiagTlv)); /* need to free */
        else {
            ESP_LOGW(BASE_TAG, "Fail to create diagnostic node!");
            return ESP_FAIL;
        }
        head->next = node;
    } else {
        ESP_LOGW(BASE_TAG, "Fail to create diagnostic list node!");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void destroy_thread_diagnosticTlv_list(thread_diagnosticTlv_list_t *list)
{
    if (list == NULL)
        return;
    thread_diagnosticTlv_list_t *pre = list;
    thread_diagnosticTlv_list_t *next = list->next;
    while (next != NULL) {
        free(pre->diagTlv);
        free(pre);
        pre = next;
        next = pre->next;
    }
    free(pre->diagTlv);
    free(pre); // delete the end node
    return;
}

esp_err_t initialize_thread_diagnosticTlv_set(thread_diagnosticTlv_set_t *set, const char *rloc16)
{
    ESP_RETURN_ON_FALSE((set && rloc16), ESP_FAIL, BASE_TAG, "Failed to initialize diagnosticTlv set");
    memcpy(&set->rloc16, rloc16, RLOC_STRING_MAX_SIZE);
    time(&set->timeout);
    set->next = NULL;
    set->diagTlv_next = NULL;
    return ESP_OK;
}

void keep_diagnosticTlv_node_live(thread_diagnosticTlv_set_t *set)
{
    ESP_RETURN_ON_FALSE(set, , BASE_TAG, "Invalid diagnosticTlv set");
    thread_diagnosticTlv_set_t *head = set;
    thread_diagnosticTlv_set_t *next = head->next;
    time_t current_time;
    time(&current_time);
    while (next) {
        if ((int)difftime(current_time, next->timeout) >= DIAGSET_NODE_MAX_TIMEOUT_SECOND &&
            next->diagTlv_next != NULL) {
            ESP_LOGW(BASE_TAG, "Node:%s is timeout.\r\n", next->rloc16);
            destroy_thread_diagnosticTlv_list(next->diagTlv_next);
            next->diagTlv_next = NULL;
            head->next = next->next;
        }
        head = head->next;
        next = head->next;
    }
}

esp_err_t update_thread_diagnosticTlv_set(thread_diagnosticTlv_set_t *set, char *rloc16,
                                          thread_diagnosticTlv_list_t *list)
{
    thread_diagnosticTlv_set_t *head = set;
    ESP_RETURN_ON_FALSE(head, ESP_ERR_INVALID_ARG, BASE_TAG, "Invalid Thread diagnostic set");

    while (head) {
        if (!strcmp(head->rloc16, rloc16)) /* match */
            break;
        head = head->next;
    }

    if (!head) /* name is mismatch, add new list */
    {
        thread_diagnosticTlv_set_t *node = (thread_diagnosticTlv_set_t *)malloc(sizeof(thread_diagnosticTlv_set_t));
        if (node) {
            initialize_thread_diagnosticTlv_set(node, rloc16);
            node->diagTlv_next = list;
            time(&node->timeout);
            node->next = NULL;
        } else
            ESP_LOGW(BASE_TAG, "Fail to malloc dignosticTlv set.");
        head = set;
        while (head->next) head = head->next;
        head->next = node;
        ESP_LOGI(BASE_TAG, "add diagTlv %s to set.", node->rloc16);
    } else /* update diag list */
    {
        destroy_thread_diagnosticTlv_list(head->diagTlv_next);
        head->diagTlv_next = list;
        time(&head->timeout);
        ESP_LOGI(BASE_TAG, "update diagTlv %s.", head->rloc16);
    }
    return ESP_OK;
}

void destroy_thread_diagnosticTlv_set(thread_diagnosticTlv_set_t *set)
{
    if (set == NULL)
        return;
    thread_diagnosticTlv_set_t *pre = set;
    thread_diagnosticTlv_set_t *next = set->next;
    while (next) {
        destroy_thread_diagnosticTlv_list(pre->diagTlv_next);
        pre->diagTlv_next = NULL;
        free(pre);
        pre = next;
        next = pre->next;
    }
    destroy_thread_diagnosticTlv_list(pre->diagTlv_next); // destory the last node
    pre->diagTlv_next = NULL;
    free(pre);
    set = NULL;
    return;
}

static cJSON *RouteData2Json(const otNetworkDiagRouteData aRouteData)
{
    cJSON *routeData = cJSON_CreateObject();

    cJSON_AddItemToObject(routeData, "RouteId", cJSON_CreateNumber(aRouteData.mRouterId));
    cJSON_AddItemToObject(routeData, "LinkQualityOut", cJSON_CreateNumber(aRouteData.mLinkQualityOut));
    cJSON_AddItemToObject(routeData, "LinkQualityIn", cJSON_CreateNumber(aRouteData.mLinkQualityIn));
    cJSON_AddItemToObject(routeData, "RouteCost", cJSON_CreateNumber(aRouteData.mRouteCost));

    return routeData;
}

static cJSON *Route2Json(const otNetworkDiagRoute aRoute)
{
    cJSON *route = cJSON_CreateObject();

    cJSON_AddItemToObject(route, "IdSequence", cJSON_CreateNumber(aRoute.mIdSequence));

    cJSON *RouteData = cJSON_CreateArray();
    for (uint16_t i = 0; i < aRoute.mRouteCount; ++i) {
        cJSON *RouteDatavalue = RouteData2Json(aRoute.mRouteData[i]);
        cJSON_AddItemToArray(RouteData, RouteDatavalue);
    }

    cJSON_AddItemToObject(route, "RouteData", RouteData);

    return route;
}

static cJSON *LeaderData2Json(const otLeaderData aLeaderData)
{
    cJSON *leaderData = cJSON_CreateObject();

    cJSON_AddItemToObject(leaderData, "PartitionId", cJSON_CreateNumber(aLeaderData.mPartitionId));
    cJSON_AddItemToObject(leaderData, "Weighting", cJSON_CreateNumber(aLeaderData.mWeighting));
    cJSON_AddItemToObject(leaderData, "DataVersion", cJSON_CreateNumber(aLeaderData.mDataVersion));
    cJSON_AddItemToObject(leaderData, "StableDataVersion", cJSON_CreateNumber(aLeaderData.mStableDataVersion));
    cJSON_AddItemToObject(leaderData, "LeaderRouterId", cJSON_CreateNumber(aLeaderData.mLeaderRouterId));

    return leaderData;
}

static cJSON *Mode2Json(const otLinkModeConfig aMode)
{
    cJSON *mode = cJSON_CreateObject();

    cJSON_AddItemToObject(mode, "RxOnWhenIdle", cJSON_CreateNumber(aMode.mRxOnWhenIdle));
    cJSON_AddItemToObject(mode, "DeviceType", cJSON_CreateNumber(aMode.mDeviceType));
    cJSON_AddItemToObject(mode, "NetworkData", cJSON_CreateNumber(aMode.mNetworkData));

    return mode;
}

static cJSON *Connectivity2Json(const otNetworkDiagConnectivity aConnectivity)
{
    cJSON *connectivity = cJSON_CreateObject();

    cJSON_AddItemToObject(connectivity, "ParentPriority", cJSON_CreateNumber(aConnectivity.mParentPriority));
    cJSON_AddItemToObject(connectivity, "LinkQuality3", cJSON_CreateNumber(aConnectivity.mLinkQuality3));
    cJSON_AddItemToObject(connectivity, "LinkQuality2", cJSON_CreateNumber(aConnectivity.mLinkQuality2));
    cJSON_AddItemToObject(connectivity, "LinkQuality1", cJSON_CreateNumber(aConnectivity.mLinkQuality1));
    cJSON_AddItemToObject(connectivity, "LeaderCost", cJSON_CreateNumber(aConnectivity.mLeaderCost));
    cJSON_AddItemToObject(connectivity, "IdSequence", cJSON_CreateNumber(aConnectivity.mIdSequence));
    cJSON_AddItemToObject(connectivity, "ActiveRouters", cJSON_CreateNumber(aConnectivity.mActiveRouters));
    cJSON_AddItemToObject(connectivity, "SedBufferSize", cJSON_CreateNumber(aConnectivity.mSedBufferSize));
    cJSON_AddItemToObject(connectivity, "SedDatagramCount", cJSON_CreateNumber(aConnectivity.mSedDatagramCount));

    return connectivity;
}

static cJSON *IpAddr2Json(const otIp6Address aAddress)
{
    char output[64];
    otIp6AddressToString(&aAddress, output, OT_IP6_ADDRESS_STRING_SIZE);
    return cJSON_CreateString(output);
}

static cJSON *MacCounters2Json(const otNetworkDiagMacCounters aMacCounters)
{
    cJSON *macCounters = cJSON_CreateObject();

    cJSON_AddItemToObject(macCounters, "IfInUnknownProtos", cJSON_CreateNumber(aMacCounters.mIfInUnknownProtos));
    cJSON_AddItemToObject(macCounters, "IfInErrors", cJSON_CreateNumber(aMacCounters.mIfInErrors));
    cJSON_AddItemToObject(macCounters, "IfOutErrors", cJSON_CreateNumber(aMacCounters.mIfOutErrors));
    cJSON_AddItemToObject(macCounters, "IfInUcastPkts", cJSON_CreateNumber(aMacCounters.mIfInUcastPkts));
    cJSON_AddItemToObject(macCounters, "IfInBroadcastPkts", cJSON_CreateNumber(aMacCounters.mIfInBroadcastPkts));
    cJSON_AddItemToObject(macCounters, "IfInDiscards", cJSON_CreateNumber(aMacCounters.mIfInDiscards));
    cJSON_AddItemToObject(macCounters, "IfOutUcastPkts", cJSON_CreateNumber(aMacCounters.mIfOutUcastPkts));
    cJSON_AddItemToObject(macCounters, "IfOutBroadcastPkts", cJSON_CreateNumber(aMacCounters.mIfOutBroadcastPkts));
    cJSON_AddItemToObject(macCounters, "IfOutDiscards", cJSON_CreateNumber(aMacCounters.mIfOutDiscards));

    return macCounters;
}

static cJSON *ChildTableEntry2Json(const otNetworkDiagChildEntry aChildEntry)
{
    cJSON *childEntry = cJSON_CreateObject();

    cJSON_AddItemToObject(childEntry, "ChildId", cJSON_CreateNumber(aChildEntry.mChildId));
    cJSON_AddItemToObject(childEntry, "Timeout", cJSON_CreateNumber(aChildEntry.mTimeout));

    cJSON *mode = Mode2Json(aChildEntry.mMode);
    cJSON_AddItemToObject(childEntry, "Mode", mode);

    return childEntry;
}

cJSON *dailnosticTlv_set_convert2_json(const thread_diagnosticTlv_set_t *set)
{
    ESP_RETURN_ON_FALSE(set, NULL, BASE_TAG, "Invalid Diagnostic Set");
    cJSON *root = cJSON_CreateArray();
    cJSON *child = NULL;
    cJSON *addr_list = NULL;
    cJSON *table_list = NULL;
    char output[512];
    thread_diagnosticTlv_list_t *list = NULL;
    thread_diagnosticTlv_set_t *head = set->next; /* Skip the invalid header node */
    while (head) {
        child = cJSON_CreateObject();
        list = head->diagTlv_next;
        while (list && list->diagTlv) {
            switch (list->diagTlv->mType) {
            case OT_NETWORK_DIAGNOSTIC_TLV_EXT_ADDRESS:
                hex_to_string(list->diagTlv->mData.mExtAddress.m8, output, OT_EXT_ADDRESS_SIZE);
                cJSON_AddItemToObject(child, "ExtAddress", cJSON_CreateString(output));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_SHORT_ADDRESS:
                cJSON_AddItemToObject(child, "Rloc16", cJSON_CreateNumber(list->diagTlv->mData.mAddr16));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_MODE:
                cJSON_AddItemToObject(child, "Mode", Mode2Json((list->diagTlv->mData.mMode)));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_TIMEOUT:
                cJSON_AddItemToObject(child, "Timeout", cJSON_CreateNumber(list->diagTlv->mData.mTimeout));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_CONNECTIVITY:
                cJSON_AddItemToObject(child, "Connectivity", Connectivity2Json((list->diagTlv->mData.mConnectivity)));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_ROUTE:
                cJSON_AddItemToObject(child, "Route", Route2Json((list->diagTlv->mData.mRoute)));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_LEADER_DATA:
                cJSON_AddItemToObject(child, "LeaderData", LeaderData2Json((list->diagTlv->mData.mLeaderData)));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_NETWORK_DATA:
                hex_to_string(list->diagTlv->mData.mNetworkData.m8, output, list->diagTlv->mData.mNetworkData.mCount);
                cJSON_AddItemToObject(child, "NetworkData", cJSON_CreateString(output));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_IP6_ADDR_LIST: {
                addr_list = cJSON_CreateArray();
                if (list->diagTlv->mData.mIp6AddrList.mCount <= 0 || list->diagTlv->mData.mIp6AddrList.mCount >= 15)
                    break;
                for (uint16_t i = 0; i < list->diagTlv->mData.mIp6AddrList.mCount; ++i) {
                    cJSON_AddItemToArray(addr_list, IpAddr2Json((list->diagTlv->mData.mIp6AddrList.mList[i])));
                }
                cJSON_AddItemToObject(child, "IP6AddressList", addr_list);

            } break;
            case OT_NETWORK_DIAGNOSTIC_TLV_MAC_COUNTERS:
                cJSON_AddItemToObject(child, "MACCounters", MacCounters2Json((list->diagTlv->mData.mMacCounters)));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_BATTERY_LEVEL:
                cJSON_AddItemToObject(child, "BatteryLevel", cJSON_CreateNumber(list->diagTlv->mData.mBatteryLevel));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_SUPPLY_VOLTAGE:
                cJSON_AddItemToObject(child, "SupplyVoltage", cJSON_CreateNumber(list->diagTlv->mData.mSupplyVoltage));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_CHILD_TABLE: {
                table_list = cJSON_CreateArray();
                for (uint16_t i = 0; i < list->diagTlv->mData.mChildTable.mCount; ++i) {
                    cJSON_AddItemToArray(table_list,
                                         ChildTableEntry2Json((list->diagTlv->mData.mChildTable.mTable[i])));
                }
                cJSON_AddItemToObject(child, "ChildTable", table_list);

            } break;
            case OT_NETWORK_DIAGNOSTIC_TLV_CHANNEL_PAGES:
                hex_to_string(list->diagTlv->mData.mChannelPages.m8, output, list->diagTlv->mData.mChannelPages.mCount);
                cJSON_AddItemToObject(child, "ChannelPages", cJSON_CreateString(output));
                break;
            case OT_NETWORK_DIAGNOSTIC_TLV_MAX_CHILD_TIMEOUT:
                cJSON_AddItemToObject(child, "MaxChildTimeout",
                                      cJSON_CreateNumber(list->diagTlv->mData.mMaxChildTimeout));
                break;
            default:
                break;
            }
            list = list->next;
        }
        // avoid to add empty child.
        if (head->diagTlv_next && child) {
            cJSON_AddItemToArray(root, child);
        }
        head = head->next;
    }
    return root;
}

void thread_node_information_reset(thread_node_informaiton_t *node)
{
    memset(node, 0x00, sizeof(thread_node_informaiton_t));
}

cJSON *thread_node_struct_convert2_json(thread_node_informaiton_t *node)
{
    cJSON *root = cJSON_CreateObject();

    char format[64];
    cJSON_AddStringToObject(root, "NetworkName", node->network_name.m8);

    if (hex_to_string(node->extended_panid.m8, format, OT_EXT_PAN_ID_SIZE)) {
        ESP_LOGW(BASE_TAG, "Fail to convert extended panid.");
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "ExtPanId", format);

    if (hex_to_string(node->extended_address.m8, format, OT_EXT_ADDRESS_SIZE)) {
        ESP_LOGW(BASE_TAG, "Fail to convert extended address.");
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "ExtAddress", format);

    otIp6AddressToString((const otIp6Address *)&node->rloc_address, format, OT_IP6_ADDRESS_STRING_SIZE);
    cJSON_AddStringToObject(root, "RlocAddress", format);

    cJSON_AddItemToObject(root, "LeaderData", LeaderData2Json((node->leader_data)));

    cJSON_AddNumberToObject(root, "State", node->role);
    cJSON_AddNumberToObject(root, "Rloc16", node->rloc16);
    cJSON_AddNumberToObject(root, "NumOfRouter", node->router_number);

    return root;
}