/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_br_web.h"
#include "cJSON.h"
#include "esp_br_web_api.h"
#include "esp_br_web_base.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "http_parser.h"
#include "protocol_examples_common.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "openthread/dataset.h"
#include "openthread/error.h"
#include "openthread/ip6.h"
#include "openthread/platform/radio.h"
#include "openthread/thread.h"
#include "openthread/thread_ftd.h"

#define MAX_FILE_SIZE (200 * 1024) // 200 KB
#define MAX_FILE_SIZE_STR "200KB"
#define SCRATCH_BUFSIZE 1024 /* Scratch buffer size */
#define VFS_PATH_MAXNUM 15
#define SERVER_IPV4_LEN 16
#define FILE_CHUNK_SIZE 1024
#define WEB_TAG "obtr_web"

/*-----------------------------------------------------
 Note：Http Server
-----------------------------------------------------*/
/**
 * @brief The basic configuration for http_server
 */
typedef struct http_server_data {
    char base_path[ESP_VFS_PATH_MAX + 1]; /* the storaged file path */
    char scratch[SCRATCH_BUFSIZE];        /* scratch buffer for temporary storage during file transfer */
} http_server_data_t;

/**
 * @brief The basic information for http_server
 */
typedef struct http_server {
    httpd_handle_t handle;    /* server handle, unique */
    http_server_data_t data;  /* data */
    char ip[SERVER_IPV4_LEN]; /* ip */
    uint16_t port;            /* port */
} http_server_t;

static http_server_t s_server = {NULL, {"", ""}, "", 80}; /* the instance of server */

/**
 * @brief The basic parameter definition for parsing url
 */
#define PROTLOCOL_MAX_SIZE 12
#define FILENAME_MAX_SIZE 64
#define FILEPATH_MAX_SIZE (FILENAME_MAX_SIZE + ESP_VFS_PATH_MAX)
typedef struct request_url {
    char protocol[PROTLOCOL_MAX_SIZE];
    uint16_t port;
    char file_name[FILENAME_MAX_SIZE];
    char file_path[FILEPATH_MAX_SIZE];
} reqeust_url_t;

/*-----------------------------------------------------
 Note：Http Server Thread REST API
-----------------------------------------------------*/
static esp_err_t esp_otbr_network_diagnostics_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_delete_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_rloc_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_rloc16_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_state_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_extaddress_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_network_name_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_leader_data_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_number_of_router_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_extpanid_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_baid_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_active_dataset_tlv_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_node_dataset_active_get_handler(httpd_req_t *req);

static httpd_uri_t s_resource_handlers[] = {
    {
        .uri = ESP_OT_REST_API_DIAGNOSTICS_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_diagnostics_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_PATH,
        .method = HTTP_DELETE,
        .handler = esp_otbr_network_node_delete_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_RLOC_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_rloc_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_RLOC16_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_rloc16_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_STATE_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_state_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_EXTADDRESS_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_extaddress_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_NETWORKNAME_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_network_name_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_LEADERDATA_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_leader_data_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_NUMBEROFROUTER_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_number_of_router_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_EXTPANID_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_extpanid_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_BORDERAGENTID_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_baid_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_ACTIVE_DATASET_TLVS_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_active_dataset_tlv_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_DATASET_ACTIVE_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_node_dataset_active_get_handler,
        .user_ctx = NULL,
    },
};

/*-----------------------------------------------------
 Note：Http Server WEB-GUI API
-----------------------------------------------------*/
static esp_err_t esp_otbr_network_properties_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_available_networks_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_join_post_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_form_post_handler(httpd_req_t *req);
static esp_err_t esp_otbr_add_network_prefix_post_handler(httpd_req_t *req);
static esp_err_t esp_otbr_delete_network_prefix_post_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_commission_post_handler(httpd_req_t *req);
static esp_err_t esp_otbr_network_topology_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_current_node_get_handler(httpd_req_t *req);

static httpd_uri_t s_web_gui_handlers[] = {
    {
        .uri = ESP_OT_REST_API_PROPERTIES_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_properties_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_AVAILABLE_NETWORK_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_available_networks_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_JOIN_NETWORK_PATH,
        .method = HTTP_POST,
        .handler = esp_otbr_network_join_post_handler,
        .user_ctx = &s_server.data,
    },
    {
        .uri = ESP_OT_REST_API_FORM_NETWORK_PATH,
        .method = HTTP_POST,
        .handler = esp_otbr_network_form_post_handler,
        .user_ctx = &s_server.data,
    },
    {
        .uri = ESP_OT_REST_API_ADD_NETWORK_PREFIX_PATH,
        .method = HTTP_POST,
        .handler = esp_otbr_add_network_prefix_post_handler,
        .user_ctx = &s_server.data,
    },
    {
        .uri = ESP_OT_REST_API_DELETE_NETWORK_PREFIX_PATH,
        .method = HTTP_POST,
        .handler = esp_otbr_delete_network_prefix_post_handler,
        .user_ctx = &s_server.data,
    },
    {
        .uri = ESP_OT_REST_API_COMMISSION_PATH,
        .method = HTTP_POST,
        .handler = esp_otbr_network_commission_post_handler,
        .user_ctx = &s_server.data,
    },
    {
        .uri = ESP_OT_REST_API_TOPOLOGY_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_topology_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_NODE_INFORMATION_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_current_node_get_handler,
        .user_ctx = NULL,
    },
};

/*-----------------------------------------------------
 Note：Http Tools
-----------------------------------------------------*/
static cJSON *pack_response(cJSON *error, cJSON *result, cJSON *message)
{
    if (!error || !result || !message) {
        if (error) {
            cJSON_Delete(error);
        }
        if (result) {
            cJSON_Delete(result);
        }
        if (message) {
            cJSON_Delete(message);
        }
        ESP_LOGE(WEB_TAG, "Failed to pack response json");
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "error", error);
    cJSON_AddItemToObject(root, "result", result);
    cJSON_AddItemToObject(root, "message", message);
    return root;
}

static cJSON *resource_status(char *error, char *msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "ErrorCode", cJSON_CreateString(error));
    cJSON_AddItemToObject(root, "ErrorMessage", cJSON_CreateString(msg));
    return root;
}

static esp_err_t httpd_server_register_http_uri(const http_server_t *server, httpd_uri_t *uris, uint8_t size)
{
    ESP_RETURN_ON_FALSE((server->handle && uris), ESP_ERR_INVALID_ARG, WEB_TAG, "Invalid arguement");
    for (int i = 0; i < size; i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server->handle, &uris[i]), WEB_TAG,
                            "Failed to register %s for %d", uris[i].uri, i);
    }
    return ESP_OK;
}

static cJSON *httpd_request_convert2_json(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((http_server_data_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) /* Respond with 500 Internal Server Error */
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "The content of packet is too long");
        return NULL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) /* Respond with 500 Internal Server Error */
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal Server Error[500]");
            return NULL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    return cJSON_Parse(buf);
}

static esp_err_t httpd_send_packet(httpd_req_t *req, cJSON *root)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(root, ESP_FAIL, WEB_TAG, "Invalid Argument");
    char *packet = cJSON_Print(root);
    ESP_RETURN_ON_FALSE(packet, ESP_FAIL, WEB_TAG, "Invalid Packet");
    ESP_LOGD(WEB_TAG, "Properties: %s\r\n", packet);
    ESP_RETURN_ON_FALSE(packet, ESP_FAIL, WEB_TAG, "Invalid Response");
    ESP_GOTO_ON_ERROR(httpd_resp_set_type(req, "application/json"), exit, WEB_TAG, "Failed to set http type");
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr(req, packet), exit, WEB_TAG, "Failed to send http respond");
exit:
    cJSON_free(packet);
    return ret;
}

static esp_err_t httpd_send_plain_text_packet(httpd_req_t *req, char *packet)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(packet, ESP_FAIL, WEB_TAG, "Invalid Argument");
    ESP_RETURN_ON_FALSE(packet, ESP_FAIL, WEB_TAG, "Invalid Packet");
    ESP_LOGD(WEB_TAG, "Properties: %s\r\n", packet);
    ESP_RETURN_ON_FALSE(packet, ESP_FAIL, WEB_TAG, "Invalid Response");
    ESP_GOTO_ON_ERROR(httpd_resp_set_type(req, "text/plain"), exit, WEB_TAG, "Failed to set http type");
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr(req, packet), exit, WEB_TAG, "Failed to send http respond");
exit:
    free(packet);
    return ret;
}

/*-----------------------------------------------------
 Note：Openthread resource API implement
-----------------------------------------------------*/
/**
 * @brief These APIs would collect corresponding information from Thread network and send to @param req.
 *
 * @param[in] req The request from http client.
 * @return
 *      -   ESP_OK   : On success
 *      -   ESP_FAIL : Failed to handle @param req
 *      -   ESP_ERR_INVALID_ARG         : Null request pointer
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 */

static esp_err_t esp_otbr_network_diagnostics_get_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_FALSE(req, ESP_FAIL, WEB_TAG, "Failed to parse the diagnostics of http request");
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_network_diagnostics_request();
    ESP_RETURN_ON_FALSE(response, ESP_FAIL, WEB_TAG, "Failed to handle openthread diagnostics request");
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_get_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_FALSE(req, ESP_FAIL, WEB_TAG, "Failed to parse the node information of http request");
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_information_request();
    ESP_RETURN_ON_FALSE(response, ESP_FAIL, WEB_TAG, "Failed to handle openthread diagnostics request");
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_delete_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_FALSE(req, ESP_FAIL, WEB_TAG, "Failed to parse the node information of http request");
    esp_err_t ret = ESP_OK;
    otError error = handle_ot_resource_node_delete_information_request();
    if (error == OT_ERROR_NONE) {
        httpd_resp_set_status(req, HTTPD_200);
    } else if (error == OT_ERROR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
    } else {
        httpd_resp_set_status(req, HTTPD_500);
    }
    ESP_GOTO_ON_ERROR(httpd_resp_send(req, NULL, 0), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    return ret;
}

static esp_err_t esp_otbr_network_node_rloc_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_rloc_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_rloc16_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_rloc16_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_state_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_state_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_extaddress_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_extaddress_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_network_name_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_network_name_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_leader_data_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_leader_data_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_number_of_router_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_numofrouter_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_extpanid_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_extpanid_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_baid_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_baid_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_active_dataset_tlv_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = handle_ot_resource_node_active_dataset_tlv_request();
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_node_dataset_active_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    char *response = handle_ot_resource_node_active_dataset_tlv_request_plain_text();
    ESP_GOTO_ON_ERROR(httpd_send_plain_text_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    return ret;
}
/*-----------------------------------------------------
 Note：Openthread WEB GUI API implement
-----------------------------------------------------*/
/**
 * @brief The API would collect the properties of Thread network and send to @param req.
 *
 * @param[in] req The request from http client.
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_INVALID_ARG         : Null request pointer
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 */
static esp_err_t esp_otbr_network_properties_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *result = handle_openthread_network_properties_request(); /* encode json package */
    cJSON *error = result ? cJSON_CreateNumber((double)OT_ERROR_NONE) : cJSON_CreateNumber((double)OT_ERROR_FAILED);
    cJSON *message = result ? cJSON_CreateString("Properties: Success") : cJSON_CreateString("Properties: Failure");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(result, ESP_FAIL, exit, WEB_TAG, "Failed to Get Thread network properties");
    ESP_LOGI(WEB_TAG, "<================= OpenThread Properties ==================>");
    ESP_LOGI(WEB_TAG, "Collection Complete !");
    ESP_LOGI(WEB_TAG, "<==========================================================>");

exit:
    cJSON_Delete(response);
    return ret;
}

/**
 * @brief The API would discover the available thread network, packs and sends it to @param req.
 *
 * @param[in] req The request for http_client.
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 *      -   ESP_FAILED                  : Null request pointer.
 */
static esp_err_t esp_otbr_available_networks_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *result = handle_openthread_available_network_request();
    cJSON *error = result ? cJSON_CreateNumber((double)OT_ERROR_NONE) : cJSON_CreateNumber((double)OT_ERROR_FAILED);
    cJSON *message = result ? cJSON_CreateString("Networks: Success") : cJSON_CreateString("Networks: Failure");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(result, ESP_FAIL, exit, WEB_TAG, "Failed to Discover Thread available networks");
    ESP_LOGI(WEB_TAG, "<================== Available Network =====================>");
    ESP_LOGI(WEB_TAG, "Discover Completed !");
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(response);
    return ret;
}

/**
 * @brief The API provides an entry for you to join the Thread network by the @param req.
 *
 * @param[in] req The request for http_client.
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 *      -   ESP_FAILED                  : Null request pointer.
 */
static esp_err_t esp_otbr_network_join_post_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *request = httpd_request_convert2_json(req);
    ESP_RETURN_ON_FALSE(request, ESP_FAIL, WEB_TAG, "Failed to parse the JOIN package");

    cJSON *join_log = cJSON_CreateString("Known");
    otError err = handle_openthread_join_network_request(request, join_log);
    cJSON *error = cJSON_CreateNumber((double)err);
    cJSON *result = err ? cJSON_CreateString("failed") : cJSON_CreateString("successful");
    cJSON *message = join_log;
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(!err, ESP_FAIL, exit, WEB_TAG, "Failed to JOIN openthread network");
    ESP_LOGI(WEB_TAG, "<================== Join Thread Network ====================>");
    ESP_LOGI(WEB_TAG, "Join successfully !"); /* successfully */
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(request);
    cJSON_Delete(response);
    return ret;
}

/**
 * @brief The API provides an entry to form a Thread network by the @param req.
 *
 * @param[in] req The request from http_client.
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 *      -   ESP_FAILED                  : Null request pointer
 */
static esp_err_t esp_otbr_network_form_post_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *request = httpd_request_convert2_json(req);
    ESP_RETURN_ON_FALSE(request, ESP_FAIL, WEB_TAG, "Failed to parse the FORM package");

    cJSON *form_log = cJSON_CreateString("Known");
    otError err = handle_openthread_form_network_request(request, form_log);
    cJSON *error = cJSON_CreateNumber((double)err);
    cJSON *result = err ? cJSON_CreateString("failed") : cJSON_CreateString("successful");
    cJSON *message = form_log;
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(!err, ESP_FAIL, exit, WEB_TAG, "Failed to form Thread network");
    ESP_LOGI(WEB_TAG, "<================== Form Thread Network ===================>");
    ESP_LOGI(WEB_TAG, "Form network successfully"); /* form successfully */
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(request);
    cJSON_Delete(response);
    return ret;
}

/**
 * @brief The API provides an entry to add network prefix to Thread node by the @param req.
 *
 * @param[in] req The request from http_client.
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 *      -   ESP_FAILED                  : Null request pointer
 */
static esp_err_t esp_otbr_add_network_prefix_post_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *request = httpd_request_convert2_json(req);
    ESP_RETURN_ON_FALSE(request, ESP_FAIL, WEB_TAG, "Failed to parse the add prefix package");
    otError err = handle_openthread_add_network_prefix_request(request);
    cJSON *error = cJSON_CreateNumber((double)err);
    cJSON *result = err ? cJSON_CreateString("failed") : cJSON_CreateString("successful");
    cJSON *message = err ? cJSON_CreateString("Add Prefix: Failure") : cJSON_CreateString("Add Prefix: Success");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(!err, ESP_FAIL, exit, WEB_TAG, "Failed to ADD Thread network prefix");
    ESP_LOGI(WEB_TAG, "<================== Add Network Prefix ====================>");
    ESP_LOGI(WEB_TAG, "On mesh prefix: %s", cJSON_GetStringValue(cJSON_GetObjectItem(request, "prefix")));
    ESP_LOGI(WEB_TAG, "Add mesh prefix successfully"); /* successfully */
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(request);
    cJSON_Delete(response);
    return ret;
}

/**
 * @brief The API provides an entry to delete network prefix from Thread node by the @param req.
 *
 * @param[in] req The request from http_client.
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 *      -   ESP_FAILED                  : Null request pointer
 */
static esp_err_t esp_otbr_delete_network_prefix_post_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *request = httpd_request_convert2_json(req);
    ESP_RETURN_ON_FALSE(request, ESP_FAIL, WEB_TAG, "Failed to parse the delete prefix package");

    otError err = handle_openthread_delete_network_prefix_request(request);
    cJSON *error = cJSON_CreateNumber((double)err);
    cJSON *result = err ? cJSON_CreateString("failed") : cJSON_CreateString("successful");
    cJSON *message = err ? cJSON_CreateString("Delete Prefix: Failure") : cJSON_CreateString("Delete Prefix: Success");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(!err, ESP_FAIL, exit, WEB_TAG, "Failed to DELETE Thread network prefix");
    ESP_LOGI(WEB_TAG, "<================= Delete Network Prefix ===================>");
    ESP_LOGI(WEB_TAG, "On mesh prefix: %s", cJSON_GetStringValue(cJSON_GetObjectItem(request, "prefix")));
    ESP_LOGI(WEB_TAG, "Delete mesh prefix successfully"); /* successfully */
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(request);
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_network_commission_post_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *request = httpd_request_convert2_json(req);
    ESP_RETURN_ON_FALSE(request, ESP_FAIL, WEB_TAG, "Failed to parse the add prefix package");

    otError err = handle_openthread_network_commission_request(request);
    cJSON *error = err ? cJSON_CreateNumber((double)err) : cJSON_CreateNumber((double)err);
    cJSON *result = err ? cJSON_CreateString("failed") : cJSON_CreateString("successful");
    cJSON *message = err ? cJSON_CreateString("Commissioner: Failure") : cJSON_CreateString("Commissioner: Process");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(!err, ESP_FAIL, exit, WEB_TAG, "Failed to commsission thread network");
    ESP_LOGI(WEB_TAG, "<================== Thread Commission =====================>");
    ESP_LOGI(WEB_TAG, "Thread commission successfully"); /* successfully */
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(request);
    cJSON_Delete(response);
    return ret;
}

/**
 * @brief The API provides an entry to collect the topology of Thread node, packs and sends it to @param req.
 *
 * @param[in] req The request from http_client.
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 *      -   ESP_FAILED                  : Null request pointer
 */
static esp_err_t esp_otbr_network_topology_get_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_FALSE(req, ESP_FAIL, WEB_TAG, "Failed to parse the diagnostics of http request");
    esp_err_t ret = ESP_OK;
    cJSON *result = handle_ot_resource_network_diagnostics_request();
    cJSON *error = result ? cJSON_CreateNumber((double)OT_ERROR_NONE) : cJSON_CreateNumber((double)OT_ERROR_FAILED);
    cJSON *message = result ? cJSON_CreateString("Topology: Success") : cJSON_CreateString("Topology: Failure");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(result, ESP_FAIL, exit, WEB_TAG, "Failed to get Thread Network Topology");
    ESP_LOGI(WEB_TAG, "<==================== Thread Topology =====================>");
    ESP_LOGI(WEB_TAG, "Thread diagnostic Tlv Complete.");
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(response);
    return ret;
}

/**
 * @brief The API provides an entry to collect the information of Thread node, packs and sends it to @param req.
 *
 * @param[in] req The request from http_client.
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 *      -   ESP_FAILED                  : Null request pointer
 */
static esp_err_t esp_otbr_current_node_get_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_FALSE(req, ESP_FAIL, WEB_TAG, "Failed to parse the node information of http request");
    esp_err_t ret = ESP_OK;
    cJSON *result = handle_ot_resource_node_information_request();
    cJSON *error = result ? cJSON_CreateNumber((double)OT_ERROR_NONE) : cJSON_CreateNumber((double)OT_ERROR_FAILED);
    cJSON *message = result ? cJSON_CreateString("Get Node: Success") : cJSON_CreateString("Get Node: Failure");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(result, ESP_FAIL, exit, WEB_TAG, "Failed to get current thread node information");
    ESP_LOGI(WEB_TAG, "<=================== Node Information =====================>");
    ESP_LOGI(WEB_TAG, "Extraction Complete");
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(response);
    return ret;
}

/*-----------------------------------------------------
 Note：Handling for Client request
-----------------------------------------------------*/
/**
 * @brief Provide a blank html to client. when client accesses a error link, the function will be
 * call.
 *
 * @param[in] req The request from client's browser
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_INVALID_ARG         : Null request pointer
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 */
static esp_err_t blank_html_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = resource_status("404", "404 Not Found");
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);

exit:
    cJSON_Delete(response);
    return ret;
}
static esp_err_t NOT_FOUND_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = resource_status("404", "404 Not Found");
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);

exit:
    cJSON_Delete(response);
    return ret;
}

/**
 * @brief Provide the favicon for GUI.
 *
 * @param[in] req The request from client's browser.
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_ERR_INVALID_ARG         : Null request pointer
 *      -   ESP_ERR_HTTPD_RESP_HDR      : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND     : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ   : Invalid request
 */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[] asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);

    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "image/x-icon"), WEB_TAG, "Failed to set http respond type");
    ESP_RETURN_ON_ERROR(httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size), WEB_TAG,
                        "Failed to send http respond");
    return ESP_OK;
}

/**
 * @brief Provide a method to send message to @param req client accord to @param path.
 *
 * @param[in] req  The request from the client
 * @param[in] path The path of file which will be sent.
 * @return
 *      -   ESP_OK: on success
 *      -   ESP_FAIL: on failure
 */
static esp_err_t httpd_resp_send_spiffs_file(httpd_req_t *req, char *path)
{
    ESP_LOGI(WEB_TAG, "-------------------------------------------");
    ESP_LOGI(WEB_TAG, "Reading %s", path);

    FILE *fp = fopen(path, "r"); // Open and read file

    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, WEB_TAG, "Failed to open %s file", path);

    char buf[FILE_CHUNK_SIZE]; // the size of chunk
    while (!feof(fp) && !ferror(fp)) {
        fread(buf, FILE_CHUNK_SIZE - 1, 1, fp);
        buf[FILE_CHUNK_SIZE - 1] = '\0';
        httpd_resp_sendstr_chunk(req, buf);
        memset(buf, 0, sizeof(buf));
    };
    return fclose(fp) == 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Provide the index.html for GUI,when the client login the web.
 *
 * @param[in] req is the request from client's browser.
 * @param[in] path points to the index.hrml path.
 * @return
 *      -   ESP_OK : On success
 *      -   ESP_ERR_INVALID_ARG : Null request pointer
 *      -   ESP_ERR_HTTPD_RESP_HDR    : Essential headers are too large for internal buffer
 *      -   ESP_ERR_HTTPD_RESP_SEND   : Error in raw send
 *      -   ESP_ERR_HTTPD_INVALID_REQ : Invalid request
 */
static esp_err_t index_html_get_handler(httpd_req_t *req, char *path)
{
    ESP_RETURN_ON_ERROR(httpd_resp_send_spiffs_file(req, path), WEB_TAG, "Failed to send index html file");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, NULL), WEB_TAG, "Failed to send http string chunk");
    return ESP_OK;
}

static esp_err_t style_css_get_handler(httpd_req_t *req, char *path)
{
    // send content-type："text/css" in http-header
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/css"), WEB_TAG, "Failed to set http text/css type");
    ESP_RETURN_ON_ERROR(httpd_resp_send_spiffs_file(req, path), WEB_TAG, "Failed to send css file");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, NULL), WEB_TAG, "Failed to send http string chunk");
    return ESP_OK;
}

static esp_err_t script_js_get_handler(httpd_req_t *req, char *path)
{
    // send content-type："application/javascript" in http-header
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/javascript"), WEB_TAG,
                        "Failed to set http application/javascript type");
    ESP_RETURN_ON_ERROR(httpd_resp_send_spiffs_file(req, path), WEB_TAG, "Failed to send js file");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, NULL), WEB_TAG, "Failed to send http string chunk");
    return ESP_OK;
}

/**
 * @brief Parse @param parse_url to obtain valid information for returning
 *
 * @param[in] uri       A pointer points the request uri from client
 * @param[in] parse_url A pointer points to the result structure for @function "http_parser_parse_url()".
 * @param[in] base_path A pointer points the base files path.
 * @return the valid information from @param parse_url
 */
static reqeust_url_t parse_request_url_information(const char *uri, const struct http_parser_url *parse_url,
                                                   const char *base_path)
{
    reqeust_url_t ret = {
        .protocol = "http",
        .port = 80,
        .file_name = "",
        .file_path = "",
    };
    ret.port = parse_url->port;
    if ((parse_url->field_set & (1 << UF_SCHEMA)) != 0 && PROTLOCOL_MAX_SIZE > parse_url->field_data[UF_SCHEMA].len) {
        memcpy(ret.protocol, uri + parse_url->field_data[UF_SCHEMA].off, parse_url->field_data[UF_SCHEMA].len);
        ret.protocol[parse_url->field_data[UF_SCHEMA].len] = '\0';
    }

    if ((parse_url->field_set & (1 << UF_PATH)) != 0 && FILENAME_MAX_SIZE > parse_url->field_data[UF_PATH].len) {
        memcpy(ret.file_name, uri + parse_url->field_data[UF_PATH].off, parse_url->field_data[UF_PATH].len);
        ret.file_name[parse_url->field_data[UF_PATH].len] = '\0';
        memcpy(ret.file_path, base_path, strlen(base_path));
        strcat(ret.file_path, ret.file_name);
    }
    return ret;
}

/**
 * @brief Verify and handle the client's default request, return corrresponding file to client.
 *
 * @param[in] req The request of http client.
 * @return
 *      -   ESP_OK: on success
 *      -   ESP_FAIL: on failure.
 */
static esp_err_t default_urls_get_handler(httpd_req_t *req)
{
    struct http_parser_url url;
    ESP_RETURN_ON_ERROR(http_parser_parse_url(req->uri, strlen(req->uri), 0, &url), WEB_TAG, "Failed to parse url");
    reqeust_url_t info =
        parse_request_url_information(req->uri, &url, ((http_server_data_t *)req->user_ctx)->base_path);

    ESP_LOGI(WEB_TAG, "-------------------------------------------");
    ESP_LOGI(WEB_TAG, "%s", info.file_name);
    if (!strcmp(info.file_name, "")) // check the filename.
    {
        ESP_LOGE(WEB_TAG, "Filename is too long or url error"); /* Respond with 500 Internal Server Error */
        ESP_RETURN_ON_ERROR(
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename is too long or url error"), WEB_TAG,
            "Failed to send error code");
        return ESP_FAIL;
    }
    if (strcmp(info.file_name, "/") == 0) {
        return blank_html_get_handler(req);
    } else if (strcmp(info.file_name, "/index.html") == 0) {
        return index_html_get_handler(req, info.file_path);
    } else if (strcmp(info.file_name, "/static/style.css") == 0) {
        return style_css_get_handler(req, info.file_path);
    } else if (strcmp(info.file_name, "/static/restful.js") == 0) {
        return script_js_get_handler(req, info.file_path);
    } else if (strcmp(info.file_name, "/static/bootstrap.min.css") == 0) {
        return script_js_get_handler(req, info.file_path);
    } else if (strcmp(info.file_name, "/favicon.ico") == 0) {
        return favicon_get_handler(req);
    } else {
        ESP_LOGE(WEB_TAG, "Failed to stat file : %s", info.file_path); /* Respond with 404 Not Found */
        return NOT_FOUND_handler(req);
    }
    return ESP_OK;
}

/*-----------------------------------------------------
 Note：Server Start
-----------------------------------------------------*/
/**
 * @brief Create an HTTP server and register an accessible URI
 *
 * @param[in] base_path A string represents the basic path of http_server files.
 * @param[in] host_ip   A IPvr4 address provided by the connected wifi, recorded by s_server.ip
 * @return
 *      -   ESP_OK: on success
 *      -   ESP_FAIL: on failure
 */
static httpd_handle_t *start_esp_br_http_server(const char *base_path, const char *host_ip)
{
    ESP_RETURN_ON_FALSE(base_path, NULL, WEB_TAG, "Invalid http server path");

    strcpy(s_server.ip, host_ip);
    strlcpy(s_server.data.base_path, base_path, ESP_VFS_PATH_MAX + 1);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = (sizeof(s_resource_handlers) + sizeof(s_web_gui_handlers)) / sizeof(httpd_uri_t) + 2;
    config.max_resp_headers = (sizeof(s_resource_handlers) + sizeof(s_web_gui_handlers)) / sizeof(httpd_uri_t) + 2;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8 * 1024;
    s_server.port = config.server_port;

    // start http_server
    ESP_RETURN_ON_FALSE(!httpd_start(&s_server.handle, &config), NULL, WEB_TAG, "Failed to start web server");

    httpd_uri_t default_uris_get = {.uri = "/*", // Match all URIs of type /path/to/file
                                    .method = HTTP_GET,
                                    .handler = default_urls_get_handler,
                                    .user_ctx = &s_server.data};

    httpd_server_register_http_uri(&s_server, s_resource_handlers, sizeof(s_resource_handlers) / sizeof(httpd_uri_t));
    httpd_server_register_http_uri(&s_server, s_web_gui_handlers, sizeof(s_web_gui_handlers) / sizeof(httpd_uri_t));
    httpd_register_uri_handler(s_server.handle, &default_uris_get);

    // Show the login address in the console
    ESP_LOGI(WEB_TAG, "%s\r\n", "<=======================server start========================>");
    ESP_LOGI(WEB_TAG, "http://%s:%d/index.html\r\n", s_server.ip, s_server.port);
    ESP_LOGI(WEB_TAG, "%s\r\n", "<===========================================================>");

    return s_server.handle;
}

void connect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data, const char *base_path)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    ESP_RETURN_ON_FALSE(server, , WEB_TAG, "Http server is invalid, failed to start it");
    ESP_LOGI(WEB_TAG, "Start the web server for Openthread Border Router");
    *server = (httpd_handle_t *)start_esp_br_http_server(base_path, s_server.ip);
}

/*-----------------------------------------------------
 Note：Server Stop
-----------------------------------------------------*/
void stop_httpserver(httpd_handle_t server)
{
    httpd_stop(server); // Stop the httpd server
}

void disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    http_server_data_t *data = (http_server_data_t *)event_data;
    if (data) {
        free(data);
        data = NULL;
    }
    httpd_handle_t *server = (httpd_handle_t *)arg;
    ESP_RETURN_ON_FALSE(server, , WEB_TAG, "Web server is valid, failed to stop it");
    ESP_LOGI(WEB_TAG, "Stop the web server for Openthread Border Router");
    stop_httpserver(*server);
    *server = NULL;
}

/*-----------------------------------------------------
 Note：FreeRTOS task
-----------------------------------------------------*/
static void ot_task_br_web(void *arg)
{
    esp_netif_t *netif = esp_openthread_get_backbone_netif();
    esp_netif_ip_info_t ip_info;
    char ipv4_address[SERVER_IPV4_LEN];
    esp_netif_get_ip_info(netif, &ip_info);
    sprintf((char *)ipv4_address, IPSTR, IP2STR(&ip_info.ip));
    start_esp_br_http_server((const char *)arg, (char *)ipv4_address);
    vTaskDelete(NULL);
}

void esp_br_web_start(char *base_path)
{
    xTaskCreate(ot_task_br_web, "ot_task_br_web", 4096, base_path, 4, NULL);
}
