/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_br_web_library.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "malloc.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#define LIBRARY_TAG "web_library"

// convert variable name to string
#define VARIABLE_NAME(name) (#name)
/*---------------------------------------------------------------------------------
                                usual method
---------------------------------------------------------------------------------*/
/**
 * @brief The function is to convert bytes to hex's string
 * @param[in]   bytes   An unsigned int array of bytes.
 * @param[out]  str     A string to save the @param bytes's hex,whose size is @param bytes *2.
 * @param[in]   size    The size of bytes.
 * @return ESP_OK: succeed in convert type, otherwise, fail to convert type.
 */
esp_err_t hex_to_string(uint8_t hex[], char str[], size_t size)
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

/**
 * @brief The function is to convert hex's string to bytes
 * @param[in] str       A string which will be converted to bytes.
 * @param[out] bytes    A array to save @param str's byte type.
 * @param[in] size      The size of @param str.
 * @return ESP_OK: succeed in convert type, otherwise fail to convert type.
 */
esp_err_t string_to_hex(char str[], uint8_t hex[], size_t size)
{
    if (strlen(str) != size * 2)
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
            hex[j] = (hex[j] & 0xf0) | ((str[i + 1] + 10 - '0') & 0x0f);
        else if (str[i + 1] >= 'A' && str[i + 1] <= 'F')
            hex[j] = (hex[j] & 0xf0) | ((str[i + 1] + 10 - 'A') & 0x0f);
        else
            return ESP_FAIL;
    }
    return ESP_OK;
}

/*---------------------------------------------------------------------------------
                               thread_status
---------------------------------------------------------------------------------*/
#define CREATE_JSON_CHILD_TIEM(child_json, to_json, from_struct, type, element) \
    cJSON *child_json = cJSON_CreateObject();                                   \
    if (child_json)                                                             \
        cJSON_AddItemToObject(to_json, #element, child_json);

/**
 * @brief The function is to convert the struct of openthread status to json type
 *
 * @param[in] status A pointer of thread_avai_network_t, whichi points to the address of a status's instance
 * @return The cJSON type of openthread status.
 * @note return such as:
 * {
      "ipv6": {
        "link_local_address":	"fe80:0:0:0:78a1:d169:3527:868e",
        "local_address":		"fd3e:a1:664a:1:f495:113e:f919:d4e3",
        "mesh_local_address":	"fdee:c9fa:497b:93f4:72c:703b:d2d5:f53b",
        "mesh_local_prefix":	"fdee:c9fa:497b:93f4::/64"

        },
      "network":{
         "name": 			"OpenThread-b91e",
         "panid": 			"0xb91e",
         "partition_id": 	"337494241",
         "xpanid":		"487f:73e0::/25"
        },
      "information":{
          "version":     		 "3",
          "version_api":  	"29",
          "PSKc": 			"2226:bea9:8819:102e:bac2:4b16:776b:3189"
        },
      "rcp":{
            "channel":      "23",
            "state":        "leader",
            "EUI64":        "84f7:3a0:65:7a14:7f2a:840:7cc6:fd3f",
            "txpower":    "127",
         "version":"openthread-esp32/20f5e180ee-; esp32h2;  2022-0W (1262285) OPENTHREAD: Dropping unsupported mldv2
 record of type 6 6-14 06:24:58 UTC"
        },
      "wpan": {
            "service":      "associated"
            }
        }
 */

cJSON *ot_status_struct_convert2_json(openthread_status_t *status)
{
    cJSON *root = cJSON_CreateObject();

    CREATE_JSON_CHILD_TIEM(json_ipv6, root, status, thread_ipv6_status_t, ipv6);
    char address[OT_IP6_ADDRESS_STRING_SIZE];
    char prefix[OT_IP6_PREFIX_STRING_SIZE];
    otIp6AddressToString((const otIp6Address *)&status->ipv6.link_local_address, address, OT_IP6_ADDRESS_STRING_SIZE);
    cJSON_AddStringToObject(json_ipv6, "link_local_address", address);

    otIp6AddressToString((const otIp6Address *)&status->ipv6.local_address, address, OT_IP6_ADDRESS_STRING_SIZE);
    cJSON_AddStringToObject(json_ipv6, "local_address", address);

    otIp6AddressToString((const otIp6Address *)&status->ipv6.mesh_local_address, address, OT_IP6_ADDRESS_STRING_SIZE);
    cJSON_AddStringToObject(json_ipv6, "mesh_local_address", address);

    otIp6PrefixToString((const otIp6Prefix *)&status->ipv6.mesh_local_prefix, prefix, OT_IP6_PREFIX_STRING_SIZE);
    cJSON_AddStringToObject(json_ipv6, "mesh_local_prefix", prefix);

    char format[64];
    CREATE_JSON_CHILD_TIEM(json_network, root, status, thread_network_status_t, network);
    sprintf(format, "%s", status->network.name.m8);
    cJSON_AddStringToObject(json_network, "name", format);
    sprintf(format, "0x%x", status->network.panid);
    cJSON_AddStringToObject(json_network, "panid", format);
    sprintf(format, "%d", status->network.partition_id);
    cJSON_AddStringToObject(json_network, "partition_id", format);
    otIp6PrefixToString((const otIp6Prefix *)&status->network.xpanid, prefix, OT_IP6_PREFIX_STRING_SIZE);
    cJSON_AddStringToObject(json_network, "xpanid", prefix);

    CREATE_JSON_CHILD_TIEM(json_info, root, status, thread_information_status_t, information);
    sprintf(format, "%d", status->information.version);
    cJSON_AddStringToObject(json_info, "version", format);
    sprintf(format, "%d", status->information.version_api);
    cJSON_AddStringToObject(json_info, "version_api", format);
    otIp6AddressToString((const otIp6Address *)&status->information.PSKc, address, OT_IP6_ADDRESS_STRING_SIZE);
    cJSON_AddStringToObject(json_info, "PSKc", address);

    CREATE_JSON_CHILD_TIEM(json_rcp, root, status, thread_rcp_status_t, rcp);
    sprintf(format, "%d", status->rcp.channel);
    cJSON_AddStringToObject(json_rcp, "channel", format);
    cJSON_AddStringToObject(json_rcp, "state", otThreadDeviceRoleToString(status->rcp.state));
    otIp6AddressToString((const otIp6Address *)&status->rcp.EUI64, address, OT_IP6_ADDRESS_STRING_SIZE);
    cJSON_AddStringToObject(json_rcp, "EUI64", address);
    sprintf(format, "%d", status->rcp.txpower);
    cJSON_AddStringToObject(json_rcp, "txpower", format);
    cJSON_AddStringToObject(json_rcp, "version", status->rcp.version);

    CREATE_JSON_CHILD_TIEM(json_wpan, root, status, thread_wpan_status_t, wpan);
    cJSON_AddStringToObject(json_wpan, "service", status->wpan.service);

    return root;
}

/**
 * @brief The function is to convert the json type of openthread status to struct.
 *
 * @param[in] root The json type of openthread status
 * @return A struct of @param root.
 * @note the return value is created by malloc , which requires to fee
 */
openthread_status_t *ot_status_json_convert2_struct(cJSON *root)
{
    openthread_status_t *status = NULL;

    return status;
}

/**
 * @brief The functio is to free the memory of thread status object pointer.
 *
 * @param thread_status is a pointer of ot_status_t
 */
void free_ot_status(openthread_status_t *status)
{
    if (status->rcp.version != NULL)
        free(status->rcp.version);
}

/*----------------------------------------------------------------------
                       Scan Thread netWork
-----------------------------------------------------------------------*/
/**
 * @brief The function is to convert the struct of openthread availabel network to json type
 *
 * @param[in] network a pointer of thread_avai_network_t, whichi points to the address of a availabel network's instance
 * @return thread_avai_network_t struct  for @param root
 * @note return json:
 * {
    "id":           "1",
    "NetworkName":  "OpenThread-73e0",
    "ExtendedPanId": "dead00beef00cafe",
    "PanId":        "0x1234",
    "ExtAddress":   "3ac7652ac0c19d21",
    "Channel":      "21",
    "Rssi":         "-61",
    "LQI":          "0"
    }
 *
 */
cJSON *network_struct_convert2_json(thread_network_information_t *network)
{
    cJSON *root = cJSON_CreateObject();

    char format[64];
    sprintf(format, "%d", network->id);
    cJSON_AddStringToObject(root, "id", format);
    cJSON_AddStringToObject(root, "NetworkName", network->network_name.m8);

    if (hex_to_string(network->extended_panid.m8, format, sizeof(network->extended_panid.m8))) {
        ESP_LOGW(LIBRARY_TAG, "Fail to convert extended panid.");
        return root;
    }
    cJSON_AddStringToObject(root, "ExtendedPanId", format);

    sprintf(format, "0x%04x", network->panid);
    cJSON_AddStringToObject(root, "PanId", format);

    if (hex_to_string(network->extended_address.m8, format, sizeof(network->extended_address.m8))) {
        ESP_LOGW(LIBRARY_TAG, "Fail to convert extended address.");
        return root;
    }
    cJSON_AddStringToObject(root, "ExtAddress", format);

    sprintf(format, "%d", network->channel);
    cJSON_AddStringToObject(root, "Channel", format);

    sprintf(format, "%d", network->rssi);
    cJSON_AddStringToObject(root, "Rssi", format);

    sprintf(format, "%d", network->lqi);
    cJSON_AddStringToObject(root, "LQI", format);

    return root;
}

/**
 * @brief The function is to convert the json type of openthread availabel network to struct
 *
 * @param[in] root the json message of openthread availabel network
 * @return thread_avai_network_t struct  for @param root
 * @note the return value is created by malloc , which requires to fee
 */
thread_network_information_t network_json_convert2_struct(cJSON *root)
{
    thread_network_information_t network;

    cJSON *temp = cJSON_GetObjectItem(root, "id");
    if ((temp))
        network.id = temp->valueint;

    if ((temp = cJSON_GetObjectItem(root, "network_name")))
        memcpy(network.network_name.m8, temp->valuestring, strlen(temp->valuestring));

    if (!(temp = cJSON_GetObjectItem(root, "extended_panid"))) {
        if (string_to_hex(temp->valuestring, network.extended_panid.m8, OT_EXT_PAN_ID_SIZE)) {
            ESP_LOGW(LIBRARY_TAG, "Fail to convert extended_panid!");
            return network;
        }
    }

    if (!(temp = cJSON_GetObjectItem(root, "extended_address"))) {
        if (string_to_hex(temp->valuestring, network.extended_address.m8, OT_EXT_ADDRESS_SIZE)) {
            ESP_LOGW(LIBRARY_TAG, "Fail to convert extended_address!");
            return network;
        }
    }

    if ((temp = cJSON_GetObjectItem(root, "panid")))
        network.panid = temp->valueint;

    if ((temp = cJSON_GetObjectItem(root, "channel")))
        network.channel = temp->valueint;

    if ((temp = cJSON_GetObjectItem(root, "rssi")))
        network.rssi = temp->valueint;

    if ((temp = cJSON_GetObjectItem(root, "lqi")))
        network.lqi = temp->valueint;

    return network;
}

/**
 * @brief The function is to initalize the list of available thread network.
 *
 * @param[in] list A pointer represents the head of thead network list, which is required to point to actual address.
 * @return ESP_OK : succeed to initialize ,otherwise fial to initialize.
 * @note the ptr of list needs to malloc memeory before this
 */
esp_err_t init_available_thread_networks_list(thread_network_list_t *list)
{
    list->next = NULL;
    list->network = (thread_network_information_t *)malloc(sizeof(thread_network_information_t));
    if (list->network == NULL)
        return ESP_FAIL;
    return ESP_OK;
}

/**
 * @brief The function is to append available thread network to networksList.
 *
 * @param[in] list      A pointer of thread network list's head.
 * @param[in] network   A instance of thread_avai_network_t, which will be added to the end of @param list.
 * @return ESP_OK : succeed to add
 * @return ESP_FAIL : fial to add
 */
esp_err_t append_available_thread_networks_list(thread_network_list_t *list, thread_network_information_t network)
{
    if (list == NULL) {
        ESP_LOGW(LIBRARY_TAG, "The available network list is NULL.");
        return ESP_FAIL;
    }
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
            ESP_LOGW(LIBRARY_TAG, "Fail to create network node!");
            return ESP_FAIL;
        }
        head->next = node;
    } else {
        ESP_LOGW(LIBRARY_TAG, "Fail to create networklsit node!");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief The function is to destroy available thread network list.
 *
 * @param[in] list A pointer record the available network list's head.
 */
void destroy_available_thread_networks_list(thread_network_list_t *list)
{
    if (list == NULL)
        return;
    thread_network_list_t *pre = list;
    thread_network_list_t *next = list->next;
    while (next != NULL) {
        free(pre->network);
        free(pre);
        pre = next;
        next = pre->next;
    }
    free(pre->network);
    free(pre); // delete the end node
    return;
}

void free_available_network(thread_network_information_t *network)
{
    free(network);
}

/*----------------------------------------------------------------------
                       Form Thread netWork
-----------------------------------------------------------------------*/
/**
 * @brief The function is to convert to json type of network form parameter to struct.
 *
 * @param[in] content the cJSON type of network's paramter,
 *  such as,
 *  {"form":
 *      {   "network_name":"OpenThread-0x99",
 *          "extended_panid":"1111111122222222",
 *          "panid":"0x1234",
 *          "passphrase":"j01Nme",
 *          "key":"00112233445566778899aabbccddeeff",
 *          "channel":"15",
 *          "on_mesh_prefix":"fd11:22::",
 *          "default_route":"on"
 *      }
 *  }
 * @return struct of thread network form parameter
 */
thread_network_form_param_t form_param_json_convert2_struct(cJSON *content)
{
    thread_network_form_param_t param;
    param.check = FORM_ERROR_NONE;
    cJSON *form = cJSON_GetObjectItem(content, "form");
    if (!(form)) {
        param.check = FORM_ERROR_TYPE;
        return param;
    }

    cJSON *temp = cJSON_GetObjectItem(form, "network_name");
    if (!(temp) || strlen(temp->valuestring) > sizeof(otNetworkName)) {
        param.check = FORM_ERROR_NETWORK_NAME;
        return param;
    } else
        memcpy(param.network_name.m8, temp->valuestring,
               sizeof(otNetworkName) < strlen(temp->valuestring) + 1 ? sizeof(otNetworkName)
                                                                     : strlen(temp->valuestring) + 1);

    if (!(temp = cJSON_GetObjectItem(form, "channel"))) {
        param.check = FORM_ERROR_NETWORK_CHANNEL;
        return param;
    }
    param.channel = 0;
    while (*temp->valuestring != '\0') param.channel = param.channel * 10 + *(temp->valuestring++) - '0';
    if (param.channel < 11 || param.channel > 26) {
        param.check = FORM_ERROR_NETWORK_CHANNEL;
        return param;
    }

    if (!(temp = cJSON_GetObjectItem(form, "panid")) || sscanf(temp->valuestring, "0x%hx", &param.panid) != 1) {
        param.check = FORM_ERROR_NETWORK_PANID;
        return param;
    }

    if (!(temp = cJSON_GetObjectItem(form, "extended_panid")) || strlen(temp->valuestring) != OT_EXT_PAN_ID_SIZE * 2 ||
        string_to_hex(temp->valuestring, param.extended_panid.m8, OT_EXT_PAN_ID_SIZE)) {
        param.check = FORM_ERROR_NETWORK_EXTPANID;
        return param;
    }

    if ((temp = cJSON_GetObjectItem(form, "on_mesh_prefix"))) {
        param.on_mesh_prefix = (char *)malloc(strlen(temp->valuestring) + 5);
        memcpy(param.on_mesh_prefix, temp->valuestring, strlen(temp->valuestring) + 1);
    }

    if (!(temp = cJSON_GetObjectItem(form, "key")) || strlen(temp->valuestring) != OT_NETWORK_KEY_SIZE * 2 ||
        string_to_hex(temp->valuestring, param.key.m8, OT_NETWORK_KEY_SIZE)) {
        param.check = FORM_ERROR_NETWORK_KEY;
        return param;
    }

    if ((temp = cJSON_GetObjectItem(form, "passphrase"))) {
        param.passphrase = (char *)malloc(strlen(temp->valuestring) + 1);
        memcpy(param.passphrase, temp->valuestring, strlen(temp->valuestring) + 1);
    }

    temp = cJSON_GetObjectItem(form, "default_route");
    if (temp == NULL || temp->type == cJSON_NULL)
        param.default_route = false;
    else
        param.default_route = true;

    return param;
}

void free_network_form_param(thread_network_form_param_t *param)
{
    free(param->on_mesh_prefix);
    free(param->passphrase);
}
