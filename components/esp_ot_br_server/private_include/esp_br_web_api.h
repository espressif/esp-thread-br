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
#include "esp_br_web_base.h"
#include "esp_http_server.h"
#include "openthread/error.h"

/*---------------------------------------------------------------------
            ESP Thread Border Router Wer Server REST API
----------------------------------------------------------------------*/
/* HTTP GET */
#define ESP_OT_REST_API_DIAGNOSTICS_PATH "/diagnostics"
#define ESP_OT_REST_API_NODE_PATH "/node"
#define ESP_OT_REST_API_NODE_RLOC_PATH "/node/rloc"
#define ESP_OT_REST_API_NODE_RLOC16_PATH "/node/rloc16"
#define ESP_OT_REST_API_NODE_STATE_PATH "/node/state"
#define ESP_OT_REST_API_NODE_EXTADDRESS_PATH "/node/ext-address"
#define ESP_OT_REST_API_NODE_NETWORKNAME_PATH "/node/network-name"
#define ESP_OT_REST_API_NODE_LEADERDATA_PATH "/node/leader-data"
#define ESP_OT_REST_API_NODE_NUMBEROFROUTER_PATH "/node/num-of-router"
#define ESP_OT_REST_API_NODE_EXTPANID_PATH "/node/ext-panid"
#define ESP_OT_REST_API_NODE_BORDERAGENTID_PATH "/node/ba-id"
#define ESP_OT_REST_API_NODE_DATASET_ACTIVE_PATH "/node/dataset/active"
#define ESP_OT_REST_API_NODE_DATASET_PENDING_PATH "/node/dataset/pending"
#define ESP_OT_REST_API_PROPERTIES_PATH "/get_properties"
#define ESP_OT_REST_API_AVAILABLE_NETWORK_PATH "/available_network"
#define ESP_OT_REST_API_NODE_INFORMATION_PATH "/node_information"
#define ESP_OT_REST_API_TOPOLOGY_PATH "/topology"
/* HTTP POST */
#define ESP_OT_REST_API_JOIN_NETWORK_PATH "/join_network"
#define ESP_OT_REST_API_FORM_NETWORK_PATH "/form_network"
#define ESP_OT_REST_API_ADD_NETWORK_PREFIX_PATH "/add_prefix"
#define ESP_OT_REST_API_DELETE_NETWORK_PREFIX_PATH "/delete_prefix"
/* To implement in the future */
#define ESP_OT_REST_API_COMMISSION_PATH "/commission"
#define ESP_OT_REST_API_NETWORK "/networks"
#define ESP_OT_REST_API_NETWORK_CURRENT "/networks/current"
#define ESP_OT_REST_API_NETWORK_CURRENT_COMMISSION "/networks/commission"
#define ESP_OT_REST_API_NETWORK_CURRENT_PREFIX "/networks/current/prefix"

/*---------------------------------------------------------------------
                            Implement
----------------------------------------------------------------------*/
/**
 * @brief Provides an entry to obtain the Thread device's node information
 *
 * @return The cJSON object of Thread network node information
 */
cJSON *handle_ot_resource_node_information_request(void);

/**
 * @brief Provides an entry to delete the Thread device's node information
 *
 * @return
 *      -   OT_ERROR_NONE   :   On success.
 *      -   Other           :   Fail to delete the node information .
 */
otError handle_ot_resource_node_delete_information_request(void);

/**
 * @brief Provide a entry to collect the Thread network topology message.
 *
 * @return The cJSON object of diagnostics
 */
cJSON *handle_ot_resource_network_diagnostics_request(void);

/**
 * @brief Provide an entry to get current Thread node rloc
 *
 * @return The cJSON object of Thread node rloc
 */
cJSON *handle_ot_resource_node_rloc_request(void);

/**
 * @brief Provide an entry to get current Thread node rloc16
 *
 * @return The cJSON object of Thread node rloc16
 */
cJSON *handle_ot_resource_node_rloc16_request(void);

/**
 * @brief Provide an entry to get current Thread node state
 *
 * @return The cJSON object of Thread node state
 */
cJSON *handle_ot_resource_node_state_request(void);

/**
 * @brief Provide an entry to get current Thread node extended address
 *
 * @return The cJSON object of Thread node extended address
 */
cJSON *handle_ot_resource_node_extaddress_request(void);

/**
 * @brief Provide an entry to get current Thread network name
 *
 * @return The cJSON object of Thread node network name
 */
cJSON *handle_ot_resource_node_network_name_request(void);

/**
 * @brief Provide an entry to get current Thread leader data
 *
 * @return The cJSON object of Thread node leader data
 */
cJSON *handle_ot_resource_node_leader_data_request(void);

/**
 * @brief Provide an entry to get Thread router number
 *
 * @return The cJSON object of Thread  number of router
 */
cJSON *handle_ot_resource_node_numofrouter_request(void);

/**
 * @brief Provide an entry to get Thread extended panid
 *
 * @return The cJSON object of Thread extended panid
 */
cJSON *handle_ot_resource_node_extpanid_request(void);

/**
 * @brief Provide an entry to get Thread border agent id
 *
 * @return The cJSON object of border agent id
 */
cJSON *handle_ot_resource_node_baid_request(void);

/**
 * @brief Handle the Thread dataset get @param request and provide @param log
 *
 * @param [in] request  A cJSON format from http request for getting dataset.
 * @param [out] log     A cJSON String type to record the result of getting dataset.
 *
 * @return              The cJSON object of Thread dataset
 */
cJSON *handle_ot_resource_node_get_dataset_request(const cJSON *request, cJSON *log);

/**
 * @brief Handle the Thread state configuration @param request
 *
 * @param [in] request  A cJSON format from http request for state configuration.
 *
 * @return
 *      -   OT_ERROR_NONE           :   On success.
 *      -   OT_ERROR_INVALID_ARGS   :   The @param request is invalid.
 *      -   OT_ERROR_INVALID_STATE  :   Current device status does not allow state configuration.
 */
otError handle_ot_resource_node_state_put_request(cJSON *request);

/**
 * @brief Handle the Thread dataset set @param request and provide @param log
 *
 * @param [in] request  A cJSON format from http request for setting dataset.
 * @param [out] log     A cJSON String type to record the result of setting dataset.
 *
 */
void handle_ot_resource_node_set_dataset_request(const cJSON *request, cJSON *log);

/**
 * @brief Handle the Thread network formation @param request and provide @param log
 *
 * @param [in] request  A cJSON format from http request for forming network.
 * @param [out] log     A cJSON String type to record the result of network formation.
 *
 * @return
 *      -   OT_ERROR_NONE           :   On success.
 *      -   OT_ERROR_INVALID_ARGS   :   The @param request or @param log is invalid.
 *      -   OT_ERROR_INVALID_STATE  :   The network interface was not not up.
 *      -   OT_ERROR_NO_BUFS        :   Insufficient buffer space to set the Active Operational Dataset.
 *      -   OT_ERROR_NOT_IMPLEMENTED:   The platform does not implement settings functionality.
 *      -   OT_ERROR_FAILED         :   Failed to form network.
 */
otError handle_openthread_form_network_request(const cJSON *request, cJSON *log);

/**
 * @brief Handle the Thread network join @param request and provide @param log
 *
 * @param[in] request   A cJSON format from the request of http.
 * @param[out] log      A cJSON string type to record the result of joining network.
 *
 * @return
 *      -   OT_ERROR_NONE           :   On success.
 *      -   OT_ERROR_INVALID_ARGS   :   The @param request or @param log is invalid.
 *      -   OT_ERROR_INVALID_STATE  :   The network interface was not not up.
 *      -   OT_ERROR_NO_BUFS        :   Insufficient buffer space to set the Active Operational Dataset.
 *      -   OT_ERROR_NOT_IMPLEMENTED:   The platform does not implement settings functionality.
 *      -   OT_ERROR_FAILED         :   Failed to join network.
 */
otError handle_openthread_join_network_request(const cJSON *request, cJSON *log);

/**
 * @brief Add prefix to Thread Network interface.
 *
 * @param[in] request   The prefix cJSON format from http.
 * @return
 *      -   OT_ERROR_NONE           :   On success.
 *      -   OT_ERROR_INVALID_ARGS   :   Invalid @param request.
 *      -   OT_ERROR_NOT_FOUND      :   Could not find the Border Router entry.
 */
otError handle_openthread_add_network_prefix_request(const cJSON *request);

/**
 * @brief Delete prefix from Thread Network interface.
 *
 * @param[in] request   The request from http.
 * @return
 *      -   OT_ERROR_NONE           :   On success.
 *      -   OT_ERROR_INVALID_ARGS   :   Invalid @param request.
 *      -   OT_ERROR_NOT_FOUND      :   Could not find the Border Router entry.
 */
otError handle_openthread_delete_network_prefix_request(const cJSON *request);

/**
 * @brief Commission Thread network by @param request.
 *
 * @param[in] request   The request from http
 * @return
 *      -   OT_ERROR_NONE           :   Successfully started the Commissioner service.
 *      -   OT_ERROR_ALREADY        :   Commissioner is already started.
 *      -   OT_ERROR_INVALID_STATE  :   Device is not currently attached to a network.
 *      -   OT_ERROR_NO_BUFS        :   No buffers available to add the Joiner.
 *      -   OT_ERROR_INVALID_ARGS   :   @p aDiscerner or @p aPskd is invalid.
 *      -   OT_ERROR_INVALID_STATE  :   The commissioner is not active.
 */
otError handle_openthread_network_commission_request(const cJSON *request);

/**
 * @brief Provide an entry to discover Thread available network.
 *
 * @return the cJSON format of the available Thread networks.
 */
cJSON *handle_openthread_available_network_request(void);

/**
 * @brief Provides an entry to obtain and pack the openthread properties.
 *
 * @return The cJSON Object type of Thread network properties.
 */
cJSON *handle_openthread_network_properties_request(void);

#ifdef __cplusplus
}
#endif
