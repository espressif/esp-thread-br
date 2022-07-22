/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_br_web_package.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_net_stack.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "malloc.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "openthread/dataset.h"
#include "openthread/dataset_ftd.h"
#include "openthread/ip6.h"
#include "openthread/netdata.h"
#include "openthread/thread_ftd.h"

#define PACKAGE_TAG "web_package"

/*----------------------------------------------------------------------
                       ot_server_package
----------------------------------------------------------------------*/
/**
 * @brief Function is to check the received package whether is correct or not simply.
 *
 * @param[in] root The json type of received package.
 * @return ESP_OK: The received package is vaild, otherwise is invaild.
 */
esp_err_t verify_ot_server_package(cJSON *root)
{
    cJSON *temp = cJSON_GetObjectItem(root, "version");
    if (!(temp)) {
        ESP_LOGW(PACKAGE_TAG, "Fail to parse package.");
        return ESP_FAIL;
    } else {
        if (strcmp(temp->valuestring, WEB_PACKAGE_PACKAGE_VERSION_NUMBER)) // mismatch
        {
            ESP_LOGW(PACKAGE_TAG, "version number is mismatch.");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}
/*----------------------------------------------------------------------
                       server response message
----------------------------------------------------------------------*/
/**
 * @brief enum the case of response to client's request
 */
typedef enum {
    RESPONSE_RESULT_SUCCESS,
    RESPONSE_RESULT_FAIL,
} repsonse_result_t;

/**
 * @brief The functio is to encode @param msg to the json type of package.
 *
 * @param[in] res An enum's instance to mean the package is type.
 * @param[in] msg A string mean the main content of package.
 * @return the cSJON type of the response package.
 */
static cJSON *encode_response_client_package(repsonse_result_t res, char *msg)
{
    web_data_package_t tw = {.version = WEB_PACKAGE_PACKAGE_VERSION_NUMBER, .content = NULL};
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "version", cJSON_CreateString(tw.version));
    cJSON *content = cJSON_CreateObject();
    if (res == RESPONSE_RESULT_SUCCESS)
        cJSON_AddItemToObject(content, "success", cJSON_CreateString(msg));
    else
        cJSON_AddItemToObject(content, "fail", cJSON_CreateString(msg));
    cJSON_AddItemToObject(root, "content", content);
    return root;
}

/*----------------------------------------------------------------------
                       thread status
----------------------------------------------------------------------*/
static esp_err_t get_openthread_ipv6(otInstance *ins, thread_ipv6_status_t *ipv6)
{
    memcpy(&ipv6->link_local_address, otThreadGetLinkLocalIp6Address(ins),
           sizeof(otIp6Address));                                       /* 1. linkLocalAddress */
    const otNetifAddress *unicastAddrs = otIp6GetUnicastAddresses(ins); /* 2. LocalAddress */
    for (const otNetifAddress *addr = unicastAddrs; addr; addr = addr->mNext) {
        ipv6->local_address = addr->mAddress;
        if (addr->mAddressOrigin == 1) // slaac type
            break;
    }
    memcpy(&ipv6->mesh_local_address, otThreadGetMeshLocalEid(ins), sizeof(otIp6Address));  /* 3. meshLocalAddress */
    memcpy(&ipv6->mesh_local_prefix, otThreadGetMeshLocalPrefix(ins), sizeof(otIp6Prefix)); /* 4. meshLocalPrefix */

    return ESP_OK;
}

static esp_err_t get_openthread_network(otInstance *ins, thread_network_status_t *network)
{
    char output[64] = "";
    sprintf(output, "%s", otThreadGetNetworkName(ins));
    if (strlen(output) + 1 > sizeof(otNetworkName)) {
        ESP_LOGW(PACKAGE_TAG, "Network name is too long.");
        return ESP_FAIL;
    }
    memcpy(&network->name, output, strlen(output) + 1);                               /* 1. name */
    network->panid = otLinkGetPanId(ins);                                             /* 2. PANID */
    network->partition_id = otThreadGetPartitionId(ins);                              /*3. paritionID */
    memcpy(&network->xpanid, otThreadGetExtendedPanId(ins), sizeof(otExtendedPanId)); /*4. XPANID */
    return ESP_OK;
}

static esp_err_t get_openthread_information(otInstance *ins, thread_information_status_t *ot_info)
{
    ot_info->version = otThreadGetVersion();         /* 1. version */
    ot_info->version_api = otNetDataGetVersion(ins); /* 2. version_api */
    otThreadGetPskc(ins, &ot_info->PSKc);            /* 3. PSKc */
    return ESP_OK;
}

static esp_err_t get_openthread_rcp(otInstance *ins, thread_rcp_status_t *rcp)
{
    char output[64] = "";
    rcp->channel = otLinkGetChannel(ins);                    /* 1. channel */
    rcp->state = otThreadGetDeviceRole(ins);                 /* 2. state */
    otThreadGetParentAverageRssi(ins, &rcp->txpower);        /* 3. txPower */
    otLinkGetFactoryAssignedIeeeEui64(ins, &rcp->EUI64);     /* 4. eui */
    sprintf(output, "%s", otPlatRadioGetVersionString(ins)); /* 5. rcp Version */
    rcp->version = (char *)malloc(sizeof(output));
    if (rcp->version != NULL)
        memcpy(rcp->version, output, sizeof(output));
    else {
        ESP_LOGW(PACKAGE_TAG, "Fail to malloc memory for rcp version.");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t get_openthread_wpan(otInstance *ins, thread_wpan_status_t *wpan)
{
    if (strcmp(otThreadDeviceRoleToString(otThreadGetDeviceRole(ins)), "detached"))
        memcpy(wpan->service, WPAN_STATUS_ASSOCIATED, sizeof(WPAN_STATUS_ASSOCIATED));
    else
        memcpy(wpan->service, WPAN_STATUS_ASSICIATING, sizeof(WPAN_STATUS_ASSICIATING));
    return ESP_OK;
}

/**
 * @brief The funciton is to get openthread border router status.
 * include ipv6, network,node informatin,rcp and wpan.
 *
 * @param[in] thread_status A pointer to save the status of openthread.
 * @return ESP_OK: succeed in get status, otherwise fail it.
 * @note @param thread_status need to free
 */
static esp_err_t get_openthread_status(openthread_status_t *thread_status)
{
    otInstance *ins = esp_openthread_get_instance(); /* get the api of openthread */
    if (ins == NULL) {
        ESP_LOGW(PACKAGE_TAG, "Fail to get instance!");
        return ESP_FAIL;
    }
    /* get openthread border router information */
    ESP_RETURN_ON_ERROR(get_openthread_ipv6(ins, &thread_status->ipv6), PACKAGE_TAG,
                        "Failed to get status of ipv6"); /* ipv6 */
    ESP_RETURN_ON_ERROR(get_openthread_network(ins, &thread_status->network), PACKAGE_TAG,
                        "Failed to get status of network"); /* network */
    ESP_RETURN_ON_ERROR(get_openthread_information(ins, &thread_status->information), PACKAGE_TAG,
                        "Failed to get status of information"); /* ot information */
    ESP_RETURN_ON_ERROR(get_openthread_rcp(ins, &thread_status->rcp), PACKAGE_TAG,
                        "Failed to get status of rcp"); /* rcp */
    ESP_RETURN_ON_ERROR(get_openthread_wpan(ins, &thread_status->wpan), PACKAGE_TAG, "Failed to get status of wpan");
    /* wpan */ /* other */

    ESP_LOGI(PACKAGE_TAG, "<==================== Thread Status =======================>");
    ESP_LOGI(PACKAGE_TAG, "openthread status is collected complete.");
    ESP_LOGI(PACKAGE_TAG, "<==========================================================>");

    return ESP_OK;
}

/**
 * @brief The function is to get openthread status and pack it to the json type.
 *
 * @return the cJSON type of openthread status.
 */
cJSON *encode_thread_status_package()
{
    web_data_package_t tw = {.version = WEB_PACKAGE_PACKAGE_VERSION_NUMBER, .content = NULL};

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "version", cJSON_CreateString(tw.version));

    openthread_status_t status;
    if (get_openthread_status(&status) == ESP_FAIL) /* update the thread status struct */
    {
        ESP_LOGW(PACKAGE_TAG, "Fail to get openthread status!");
        free_ot_status(&status);
        return root;
    }

    cJSON *content = NULL;
    content = ot_status_struct_convert2_json(&status); /* require to objdect address */
    cJSON_AddItemToObject(root, "content", content);

    free_ot_status(&status);
    return root;
}

/**
 * @brief decode the thread status to the json style
 *
 * @param[in] package the ptr of web_package intance
 * @return ESP_OK
 */
esp_err_t decode_thread_status_package(web_data_package_t *package)
{
    return ESP_OK;
}

/*----------------------------------------------------------------------
               scan thread available networks
----------------------------------------------------------------------*/
/* a list for recording thread available network */
thread_network_list_t *g_networkList = NULL;
/* am unsigned char variable for counting the number of thread available networks */
uint8_t g_avainetwork_id = 0;
/* a semaphore for presenting the scanning action is completed. */
SemaphoreHandle_t semaphore_discover_done = NULL;

/**
 * @brief Thread function is to build an available Thread networks list with @param result.
 *
 * @param[in] result The returning result of function "otThreadDiscover()"
 */
static void build_availableNetworks_list(otActiveScanResult *aResult)
{
    g_avainetwork_id++;
    thread_network_information_t network;

    network.id = g_avainetwork_id;
    memcpy(&network.network_name, &aResult->mNetworkName, sizeof(otNetworkName));
    memcpy(&network.extended_panid.m8, &aResult->mExtendedPanId.m8, sizeof(otExtendedPanId));
    network.panid = aResult->mPanId;
    memcpy(&network.extended_address.m8, &aResult->mExtAddress.m8, sizeof(otExtAddress));
    network.channel = aResult->mChannel;
    network.rssi = aResult->mRssi;
    network.lqi = aResult->mLqi;

    append_available_thread_networks_list(g_networkList, network);

    ESP_LOGI(PACKAGE_TAG, "<==================== Thread Discover ======================>");
    ESP_LOGD(PACKAGE_TAG, "id:%d", network.id);
    ESP_LOGD(PACKAGE_TAG, "network:%s", network.network_name);
    ESP_LOGI(PACKAGE_TAG, "Discover available network.");
    ESP_LOGI(PACKAGE_TAG, "<==========================================================>");
    return;
}

/**
 * @brief The function is to handle the returning result of @function of "otThreadDiscover()", exit or registor
 * @param[in] result   The result of discvoering,inlcude some parameter for Thread network.
 * @param[in] context  disbaled.
 */
static void handle_active_scan_event(otActiveScanResult *aResult, void *aContext)
{
    if ((aResult) == NULL) {
        xSemaphoreGive(semaphore_discover_done);
    } else {
        build_availableNetworks_list(aResult);
    }
}

/**
 * @brief The funciton is to call @function "otThreadDiscover()" to get openthread available networks.

 * @return OT_ERROR_NONE: no error happen in the process of discovering networks, on the contrary in other cases.
 */
otError get_openthread_available_networks(void)
{
    esp_err_t ret = ESP_OK;
    otError error = OT_ERROR_NONE;
    uint32_t scanChannels = 0;
    esp_openthread_lock_acquire(portMAX_DELAY);
    ESP_GOTO_ON_ERROR(error = otThreadDiscover(esp_openthread_get_instance(), scanChannels, OT_PANID_BROADCAST, false,
                                               false, &handle_active_scan_event, NULL),
                      exit, PACKAGE_TAG, "Fail to discover network.");
    esp_openthread_lock_release();
    if (ret != ESP_OK)
        return OT_ERROR_ABORT;
    error = OT_ERROR_PENDING;
exit:
    return error;
}

/**
 * @brief The function is to pack the thread networks list to the json style,
 *        which callback @function "otThreadDiscover()" to get the result of discovering available networks.
 *        and build a list of available Thread networks.
 *
 * @return the cJSON type of the available thread networks/
 */
cJSON *encode_available_thread_networks_package()
{
    web_data_package_t tw = {
        .version = WEB_PACKAGE_PACKAGE_VERSION_NUMBER,
        .content = NULL,
    };

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "version", cJSON_CreateString(tw.version));

    destroy_available_thread_networks_list(g_networkList);

    g_networkList = (thread_network_list_t *)malloc(sizeof(thread_network_list_t));
    g_avainetwork_id = 0;

    init_available_thread_networks_list(g_networkList);

    if (!semaphore_discover_done)
        semaphore_discover_done = xSemaphoreCreateBinary(); /* create a binary semaphore */
    get_openthread_available_networks();
    xSemaphoreTake(semaphore_discover_done, portMAX_DELAY); /* wait for scan to end. */

    if (g_networkList == NULL) {
        ESP_LOGW(PACKAGE_TAG, "No network can discovered.");
        return root;
    }

    thread_network_list_t *head = g_networkList->next; /* skip head node */

    cJSON *networks = cJSON_CreateArray();
    while (head != NULL) {
        cJSON *node = network_struct_convert2_json(head->network);
        cJSON_AddItemToArray(networks, node);
        head = head->next;
    }

    cJSON *content = cJSON_CreateObject();
    cJSON_AddItemToObject(content, "threadNetworks", networks);
    cJSON_AddItemToObject(root, "content", content);
    return root;
}

/**
 * @brief Thefunction is to decode the thread network to the json style
 *
 * @param package An api of "scan" agreement package
 * @return ESP_OK
 * @note for subsequent functions.
 */
esp_err_t decode_available_thread_networks_package(web_data_package_t *agr)
{
    return ESP_OK;
}

/*----------------------------------------------------------------------
                       form thread network
----------------------------------------------------------------------*/
/**
 * @brief The function to add the prefix's field,if the prefix lacks it
 *
 * @param[in] prefix  A string of the address's prefix
 */
void add_prefix_field(char *prefix)
{
    if (prefix == NULL || strstr(prefix, "/"))
        return;
    strcat(prefix, "/64");
    return;
}

/**
 * @brief The function to check the network's parameter, create new network, config network and start it
 *
 * @param[in] content A cJSON pointer to an api of 'form' agreement package's content/
 * @return The cSJON message about @param content whether is error to client.
 */
cJSON *form_openthread_network(cJSON *content)
{
    thread_network_form_param_t param = form_param_json_convert2_struct(content);
    cJSON *ret = NULL;
    if (param.check != FORM_ERROR_NONE) {
        switch (param.check) {
        case FORM_ERROR_TYPE:
            ESP_LOGW(PACKAGE_TAG, "Package type is error.");
            ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Package type is error.");
            break;
        case FORM_ERROR_NETWORK_NAME:
            ESP_LOGW(PACKAGE_TAG, "Network Name is empty or too long.");
            ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Network Name is empty or too long.");
            break;
        case FORM_ERROR_NETWORK_CHANNEL:
            ESP_LOGW(PACKAGE_TAG, "Channel ranges 11 to 26.");
            ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Channel ranges 11 to 26.");
            break;
        case FORM_ERROR_NETWORK_PANID:
            ESP_LOGW(PACKAGE_TAG, "PANID requried \"0x\" prefix.");
            ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "PANID requried \"0x\" prefix.");
            break;
        case FORM_ERROR_NETWORK_EXTPANID:
            ESP_LOGW(PACKAGE_TAG, "Extended PANID required 16 bytes(0-f).");
            ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Extended PANID required 16 bytes(0-f).");
            break;
        case FORM_ERROR_NETWORK_KEY:
            ESP_LOGW(PACKAGE_TAG, "Network Key required 32 bytes(0-f).");
            ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Network Key required 32 bytes(0-f).");
            break;
        default:
            ESP_LOGW(PACKAGE_TAG, "None.");
            ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "None.");
            break;
        }
        return ret;
    }

    otInstance *ins = esp_openthread_get_instance();
    add_prefix_field(param.on_mesh_prefix); /*the prefix need to point the length of field*/
    otOperationalDataset dataset = {.mNetworkName = param.network_name,
                                    .mNetworkKey = param.key,
                                    .mChannel = param.channel,
                                    .mExtendedPanId = param.extended_panid,
                                    .mPanId = param.panid};

    if (otDatasetCreateNewNetwork(ins, &dataset)) /* form active dataset */
    {
        ESP_LOGW(PACKAGE_TAG, "Fail to active dataset.");
        ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Fail to active dataset.");
        return ret;
    }
    if (otIp6SetEnabled(ins, true)) /* ifconfig up */
    {
        ESP_LOGW(PACKAGE_TAG, "Fail to ifconfig up.");
        ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Fail to ifconfig up.");
        return ret;
    }
    if (otThreadSetEnabled(ins, true)) /* thread start */
    {
        ESP_LOGW(PACKAGE_TAG, "Fail to thread start.");
        ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Fail to thread start.");
        return ret;
    }

    ESP_LOGI(PACKAGE_TAG, "<==================== Thread Form ======================>");
    ESP_LOGD(PACKAGE_TAG, "name:%s", param.network_name.m8);
    ESP_LOGD(PACKAGE_TAG, "prefix:%s", param.on_mesh_prefix);
    ESP_LOGD(PACKAGE_TAG, "panid:0x%hx", param.panid);
    ESP_LOGD(PACKAGE_TAG, "channel:%d", param.channel);
    ESP_LOGD(PACKAGE_TAG, "default_route:%d", param.default_route);
    ESP_LOGI(PACKAGE_TAG, "Succeed in forming network!"); /* form successfully */
    ESP_LOGI(PACKAGE_TAG, "<==========================================================>");
    ret = encode_response_client_package(RESPONSE_RESULT_SUCCESS, "Succeed in forming network!");

    free_network_form_param(&param);
    return ret;
}

cJSON *thread_network_add_on_mesh_prefix(cJSON *content)
{
    cJSON *add_prefix = cJSON_GetObjectItem(content, "add_prefix");
    cJSON *ret = NULL;
    cJSON *temp = NULL;

    char *on_mesh_prefix = NULL;
    if (!(add_prefix)) {
        ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Package type is error.");
        return ret;
    }

    if ((temp = cJSON_GetObjectItem(add_prefix, "on_mesh_prefix"))) {
        on_mesh_prefix = (char *)malloc(strlen(temp->valuestring) + 5);
        memcpy(on_mesh_prefix, temp->valuestring, strlen(temp->valuestring) + 1);
    }

    add_prefix_field(on_mesh_prefix); /*the prefix need to point the length of field*/
    /* core
    otInstance *ins = esp_openthread_get_instance();
    ---- */
    ESP_LOGI(PACKAGE_TAG, "<================= Add Thread Prefix ======================>");
    ESP_LOGD(PACKAGE_TAG, "on mesh prefix:%s", on_mesh_prefix);
    ESP_LOGI(PACKAGE_TAG, "Succeed in adding prefix!"); /* successfully */
    ESP_LOGI(PACKAGE_TAG, "<==========================================================>");
    ret = encode_response_client_package(RESPONSE_RESULT_SUCCESS, "Succeed in adding prefix!");

    free(on_mesh_prefix);
    return ret;
}

cJSON *thread_network_delete_on_mesh_prefix(cJSON *content)
{
    cJSON *delete_prefix = cJSON_GetObjectItem(content, "delete_prefix");
    cJSON *ret = NULL;
    cJSON *temp = NULL;

    char *on_mesh_prefix = NULL;
    if (!(delete_prefix)) {
        ret = encode_response_client_package(RESPONSE_RESULT_FAIL, "Package type is error.");
        return ret;
    }

    if ((temp = cJSON_GetObjectItem(delete_prefix, "on_mesh_prefix"))) {
        on_mesh_prefix = (char *)malloc(strlen(temp->valuestring) + 1);
        memcpy(on_mesh_prefix, temp->valuestring, strlen(temp->valuestring) + 1);
    }

    add_prefix_field(on_mesh_prefix); /*the prefix need to point the length of field*/
    /* core
    otInstance *ins = esp_openthread_get_instance();
    ---- */
    ESP_LOGI(PACKAGE_TAG, "<================= Delete Thread Prefix ======================>");
    ESP_LOGD(PACKAGE_TAG, "on mesh prefix:%s", on_mesh_prefix);
    ESP_LOGI(PACKAGE_TAG, "Succeed in deleting prefix!"); /* successfully */
    ESP_LOGI(PACKAGE_TAG, "<==========================================================>");
    ret = encode_response_client_package(RESPONSE_RESULT_SUCCESS, "Succeed in deleting prefix!");

    free(on_mesh_prefix);
    return ret;
}
