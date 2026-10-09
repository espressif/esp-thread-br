/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_system.h"
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#include "cJSON.h"
#include "esp_br_web.h"
#include "esp_br_web_api.h"
#include "esp_br_web_base.h"
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
#include "esp_br_wifi_config.h"
#endif
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_netif_glue.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "http_parser.h"
#include "mdns.h"
#include "protocol_examples_common.h"

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
#define FILE_CHUNK_SIZE 4096
#define WEB_TAG "obtr_web"

ESP_EVENT_DEFINE_BASE(ESP_BR_WEB_EVENT);

/*-----------------------------------------------------
Note: Http Server
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
    httpd_handle_t handle;   /* server handle, unique */
    http_server_data_t data; /* data */
    uint16_t port;           /* port */
} http_server_t;

static http_server_t s_server = {NULL, {"", ""}, 80}; /* the instance of server */

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
} request_url_t;

/*-----------------------------------------------------
Note: Http Server Thread REST API
-----------------------------------------------------*/
static esp_err_t esp_otbr_network_diagnostics_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_well_known_br_rest_get_handler(httpd_req_t *req);

static httpd_uri_t s_resource_handlers[] = {
    {
        .uri = ESP_OT_REST_API_WELL_KNOWN_ESP_BR_REST_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_well_known_br_rest_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_DIAGNOSTICS_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_network_diagnostics_get_handler,
        .user_ctx = NULL,
    },
};

/*----------------------------------------------------------------------
                    Thread REST HTTP handlers
----------------------------------------------------------------------*/
#define THREAD_REST_MAX_BODY (16 * 1024)
#define REST_METHOD(method) (UINT32_C(1) << (method))
#define REST_GET REST_METHOD(HTTP_GET)
#define REST_PUT REST_METHOD(HTTP_PUT)
#define REST_POST REST_METHOD(HTTP_POST)
#define REST_DELETE REST_METHOD(HTTP_DELETE)

typedef struct {
    const char *path;
    uint32_t methods;
    bool item;
} thread_rest_route_t;

static const thread_rest_route_t s_thread_rest_routes[] = {
    {"/node", REST_GET | REST_DELETE, false},
    {"/node/ba-id", REST_GET, false},
    {"/node/rloc", REST_GET, false},
    {"/node/rloc16", REST_GET, false},
    {"/node/state", REST_GET | REST_PUT, false},
    {"/node/ext-address", REST_GET, false},
    {"/node/network-name", REST_GET, false},
    {"/node/leader-data", REST_GET, false},
    {"/node/num-of-router", REST_GET, false},
    {"/node/ext-panid", REST_GET, false},
    {"/node/dataset/active", REST_GET | REST_PUT, false},
    {"/node/dataset/pending", REST_GET | REST_PUT, false},
    {"/node/commissioner/state", REST_GET | REST_PUT, false},
    {"/node/commissioner/joiner", REST_GET | REST_POST | REST_DELETE, false},
    {"/node/coprocessor/version", REST_GET, false},
    {"/node/ba-epskc/state", REST_GET | REST_PUT, false},
    {"/node/ba-epskc/key", REST_GET | REST_POST | REST_DELETE, false},
    {"/api/node", REST_GET, false},
    {"/api/actions", REST_GET | REST_POST | REST_DELETE, false},
    {"/api/actions/", REST_GET | REST_DELETE, true},
    {"/api/devices", REST_GET | REST_DELETE, false},
    {"/api/devices/", REST_GET | REST_DELETE, true},
    {"/api/diagnostics", REST_GET | REST_DELETE, false},
    {"/api/diagnostics/", REST_GET | REST_DELETE, true},
    {"/.well-known/thread/br-rest", REST_GET, false},
};

static const char *thread_rest_status_title(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 406:
        return "Not Acceptable";
    case 409:
        return "Conflict";
    case 412:
        return "Precondition Failed";
    case 413:
        return "Payload Too Large";
    case 415:
        return "Unsupported Media Type";
    case 422:
        return "Unprocessable Content";
    case 503:
        return "Service Unavailable";
    case 504:
        return "Gateway Timeout";
    case 507:
        return "Insufficient Storage";
    default:
        return "Internal Server Error";
    }
}

static esp_err_t thread_rest_send(httpd_req_t *req, esp_br_thread_rest_response_t *response, bool close)
{
    const char *title = thread_rest_status_title(response->status);
    char status[48];
    snprintf(status, sizeof(status), "%d %s", response->status, title);
    if (response->status >= 400) {
        cJSON_Delete(response->json);
        response->json = cJSON_CreateObject();
        cJSON_AddNumberToObject(response->json, "status", response->status);
        cJSON_AddStringToObject(response->json, "title", title);
        if (response->detail) {
            cJSON_AddStringToObject(response->json, "detail", response->detail);
        }
        response->content_type = "application/json";
    }
    char *json = response->json ? cJSON_PrintUnformatted(response->json) : NULL;
    const char *body = json ? json : response->text;
    if (response->json && !json) {
        strcpy(status, "500 Internal Server Error");
        body = "{\"status\":500,\"title\":\"Internal Server Error\"}";
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, response->content_type ? response->content_type : "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers",
                       "Origin, Accept, X-Requested-With, Content-Type, Access-Control-Request-Method, "
                       "Access-Control-Request-Headers, If-None-Match");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "DELETE, GET, OPTIONS, POST, PUT");
    if (close) {
        httpd_resp_set_hdr(req, "Connection", "close");
    }
    esp_err_t error = httpd_resp_send(req, req->method == HTTP_HEAD ? NULL : body,
                                      req->method == HTTP_HEAD || !body ? 0 : strlen(body));
    cJSON_free(json);
    cJSON_Delete(response->json);
    free(response->text);
    return close && error == ESP_OK ? ESP_FAIL : error;
}

static bool thread_rest_decode(char *text, bool query)
{
    char *write = text;
    for (char *read = text; *read; read++) {
        unsigned char value = *read;
        if (value == '%') {
            if (!read[1] || !read[2] || !isxdigit((unsigned char)read[1]) || !isxdigit((unsigned char)read[2])) {
                return false;
            }
            char pair[3] = {read[1], read[2], 0};
            value = strtoul(pair, NULL, 16);
            if (!value) {
                return false;
            }
            read += 2;
        } else if (query && value == '+') {
            value = ' ';
        }
        *write++ = value;
    }
    *write = 0;
    return true;
}

static char *thread_rest_header(httpd_req_t *req, const char *name)
{
    size_t length = httpd_req_get_hdr_value_len(req, name);
    char *header = calloc(length + 1, 1);
    if (header && length && httpd_req_get_hdr_value_str(req, name, header, length + 1) != ESP_OK) {
        free(header);
        header = NULL;
    }
    return header;
}

static cJSON *thread_rest_discovery(void)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *api = cJSON_AddObjectToObject(json, "api");
    cJSON_AddStringToObject(api, "version", ESP_OT_REST_API_VERSION);
    cJSON_AddStringToObject(api, "base", "/api/");
    cJSON *links = cJSON_AddArrayToObject(json, "links");
    static const char *const paths[] = {"/.well-known/thread/br-rest", "/api/node", "/api/actions", "/api/devices",
                                        "/api/diagnostics"};
    static const char *const relations[] = {"self", "node", "task", "device", "diagnostic"};
    for (unsigned i = 0; i < 5; i++) {
        cJSON *link = cJSON_CreateObject();
        cJSON_AddStringToObject(link, "href", paths[i]);
        cJSON_AddStringToObject(link, "rel", relations[i]);
        cJSON *types = cJSON_AddArrayToObject(link, "type");
        cJSON_AddItemToArray(types, cJSON_CreateString(i ? "application/vnd.api+json" : "application/json"));
        cJSON_AddItemToArray(links, link);
    }
    return json;
}

static esp_err_t esp_otbr_thread_rest_handler(httpd_req_t *req)
{
    esp_br_thread_rest_response_t response = {.status = 200, .content_type = "application/json"};
    esp_br_thread_rest_request_t request = {.method = req->method};
    char *uri = strdup(req->uri), *body = NULL, *accept = NULL, *content_type = NULL, *condition = NULL;
    cJSON *query = cJSON_CreateObject();
    bool close = false;
    if (!uri || !query) {
        response.status = 500;
        close = true;
        goto exit;
    }
    char *parameters = strchr(uri, '?');
    if (parameters) {
        *parameters++ = 0;
    }
    if (!thread_rest_decode(uri, false)) {
        response.status = 400;
        close = true;
        goto exit;
    }
    request.path = uri;
    unsigned parameter_count = 0;
    while (parameters && *parameters) {
        char *end = strchr(parameters, '&');
        if (end) {
            *end++ = 0;
        }
        char *value = strchr(parameters, '=');
        if (value) {
            *value++ = 0;
        } else {
            value = parameters + strlen(parameters);
        }
        if (++parameter_count >= 16 || !thread_rest_decode(parameters, true) || !thread_rest_decode(value, true)) {
            response.status = 400;
            close = true;
            goto exit;
        }
        cJSON_AddStringToObject(query, parameters, value);
        parameters = end;
    }
    request.query = query;
    const thread_rest_route_t *route = NULL;
    for (unsigned i = 0; i < sizeof(s_thread_rest_routes) / sizeof(s_thread_rest_routes[0]); i++) {
        const thread_rest_route_t *candidate = &s_thread_rest_routes[i];
        size_t length = strlen(candidate->path);
        if ((!candidate->item && !strcmp(uri, candidate->path)) ||
            (candidate->item && !strncmp(uri, candidate->path, length) && uri[length] && !strchr(uri + length, '/'))) {
            route = candidate;
            break;
        }
    }
    if (!route) {
        response.status = 404;
        goto exit;
    }
    char allow[48] = "";
    const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_OPTIONS};
    const char *names[] = {"GET", "POST", "PUT", "DELETE", "OPTIONS"};
    for (unsigned i = 0; i < 5; i++) {
        if (methods[i] == HTTP_OPTIONS || (route->methods & REST_METHOD(methods[i]))) {
            if (*allow) {
                strcat(allow, ", ");
            }
            strcat(allow, names[i]);
        }
    }
    if (req->method == HTTP_OPTIONS || !(route->methods & REST_METHOD(req->method))) {
        response.status = req->method == HTTP_OPTIONS ? 200 : 405;
        if (response.status == 405) {
            response.detail = "method not supported";
        }
        httpd_resp_set_hdr(req, "Allow", allow);
        /* Header values must remain live until httpd_resp_send(). */
        if (req->content_len) {
            close = true;
        }
        esp_err_t error = thread_rest_send(req, &response, close);
        free(uri);
        cJSON_Delete(query);
        return error;
    }
    if (req->content_len > THREAD_REST_MAX_BODY) {
        response.status = 413;
        close = true;
        goto exit;
    }
    body = calloc(req->content_len + 1, 1);
    accept = thread_rest_header(req, "Accept");
    content_type = thread_rest_header(req, "Content-Type");
    condition = thread_rest_header(req, "If-None-Match");
    if (!body || !accept || !content_type || !condition) {
        response.status = 500;
        close = true;
        goto exit;
    }
    for (size_t received = 0; received < req->content_len;) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) {
            response.status = 400;
            close = true;
            goto exit;
        }
        received += count;
    }
    if (memchr(body, 0, req->content_len)) {
        response.status = 400;
        close = true;
        goto exit;
    }
    request.body = body;
    request.accept = accept;
    request.content_type = content_type;
    request.if_none_match = condition;
    if (!strcmp(uri, "/.well-known/thread/br-rest")) {
        response.json = thread_rest_discovery();
    } else {
        esp_br_thread_rest_request(&request, &response);
    }
exit:
    if (response.status >= 400 && req->content_len && !body) {
        close = true;
    }
    bool reset = response.reset;
    esp_err_t error = thread_rest_send(req, &response, close);
    free(uri);
    free(body);
    free(accept);
    free(content_type);
    free(condition);
    cJSON_Delete(query);
    if (reset) {
        /* httpd_resp_send has submitted the response before rebooting. */
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }
    return error;
}

static esp_err_t esp_otbr_thread_rest_register(httpd_handle_t server)
{
    esp_err_t error = esp_br_thread_rest_init();
    if (error != ESP_OK) {
        return error;
    }
    static const char *const paths[] = {"/node", "/node/*", "/api/*", "/.well-known/thread/br-rest"};
    const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_OPTIONS, HTTP_HEAD, HTTP_PATCH};
    for (unsigned p = 0; p < 4; p++) {
        for (unsigned m = 0; m < 7; m++) {
            httpd_uri_t route = {.uri = paths[p], .method = methods[m], .handler = esp_otbr_thread_rest_handler};
            error = httpd_register_uri_handler(server, &route);
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    return ESP_OK;
}

/*-----------------------------------------------------
Note: Http Server WEB-GUI API
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
static esp_err_t esp_otbr_ping_post_handler(httpd_req_t *req);
static esp_err_t esp_otbr_ipaddr_get_handler(httpd_req_t *req);
static esp_err_t esp_otbr_add_ipaddr_post_handler(httpd_req_t *req);
static esp_err_t esp_otbr_delete_ipaddr_post_handler(httpd_req_t *req);

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
    {
        .uri = ESP_OT_REST_API_PING_PATH,
        .method = HTTP_POST,
        .handler = esp_otbr_ping_post_handler,
        .user_ctx = &s_server.data,
    },
    {
        .uri = ESP_OT_REST_API_IPADDR_PATH,
        .method = HTTP_GET,
        .handler = esp_otbr_ipaddr_get_handler,
        .user_ctx = NULL,
    },
    {
        .uri = ESP_OT_REST_API_ADD_IPADDR_PATH,
        .method = HTTP_POST,
        .handler = esp_otbr_add_ipaddr_post_handler,
        .user_ctx = &s_server.data,
    },
    {
        .uri = ESP_OT_REST_API_DELETE_IPADDR_PATH,
        .method = HTTP_POST,
        .handler = esp_otbr_delete_ipaddr_post_handler,
        .user_ctx = &s_server.data,
    },
};

/*-----------------------------------------------------
Note: Http Tools
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
    ESP_RETURN_ON_FALSE((server->handle && uris), ESP_ERR_INVALID_ARG, WEB_TAG, "Invalid argument");
    for (int i = 0; i < size; i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server->handle, &uris[i]), WEB_TAG,
                            "Failed to register %s for %d", uris[i].uri, i);
    }
    return ESP_OK;
}

static cJSON *httpd_request_convert2_json(httpd_req_t *req, int type)
{
    char *buf = ((http_server_data_t *)(req->user_ctx))->scratch;
    int received = 0;
    int cur_len = 0;
    int total_len = req->content_len;
    int end_len = total_len;
    if (type == cJSON_String) {
        cur_len = 1;
        total_len += 2;
        buf[0] = '\"';
        buf[total_len - 1] = '\"';
        end_len = total_len - 1;
    }

    if (total_len >= SCRATCH_BUFSIZE) /* Respond with 500 Internal Server Error */
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "The content of packet is too long");
        return NULL;
    }
    while (cur_len < end_len) {
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
    char *packet = cJSON_PrintUnformatted(root);
    ESP_RETURN_ON_FALSE(packet, ESP_FAIL, WEB_TAG, "Invalid Packet");
    ESP_LOGD(WEB_TAG, "Properties: %s\r\n", packet);
    ESP_RETURN_ON_FALSE(packet, ESP_FAIL, WEB_TAG, "Invalid Pesponse");
    ESP_GOTO_ON_ERROR(httpd_resp_set_type(req, ESP_OT_REST_CONTENT_TYPE_JSON), exit, WEB_TAG,
                      "Failed to set http type");
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr(req, packet), exit, WEB_TAG, "Failed to send http respond");
exit:
    cJSON_free(packet);
    return ret;
}

/*-----------------------------------------------------
Note: Openthread resource API implement
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

    /* Stream the JSON array in chunks to avoid allocating the entire
       serialized string in RAM at once (can be 30-50KB for large networks). */
    ESP_GOTO_ON_ERROR(httpd_resp_set_type(req, "application/json"), exit, WEB_TAG, "Failed to set content type");

    int array_size = cJSON_GetArraySize(response);
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr_chunk(req, "["), exit, WEB_TAG, "Failed to send chunk");

    for (int i = 0; i < array_size; i++) {
        cJSON *detached = cJSON_DetachItemFromArray(response, 0);
        char *chunk = cJSON_PrintUnformatted(detached);
        cJSON_Delete(detached);
        if (chunk) {
            if (i > 0) {
                ESP_GOTO_ON_ERROR(httpd_resp_sendstr_chunk(req, ","), exit, WEB_TAG, "Failed to send chunk");
            }
            esp_err_t send_err = httpd_resp_sendstr_chunk(req, chunk);
            cJSON_free(chunk);
            ESP_GOTO_ON_ERROR(send_err, exit, WEB_TAG, "Failed to send chunk");
        }
    }

    ESP_GOTO_ON_ERROR(httpd_resp_sendstr_chunk(req, "]"), exit, WEB_TAG, "Failed to send chunk");
    /* Signal end of chunked response */
    httpd_resp_sendstr_chunk(req, NULL);

exit:
    cJSON_Delete(response);
    return ret;
}

/**
 * @brief REST API discovery endpoint (RFC 8615 well-known URI). Returns the REST API's version and
 *        RFC 8288-style links to its entry points, so clients can discover them at runtime instead
 *        of hardcoding or probing endpoint paths.
 *
 * @param[in] req The request from http client.
 * @return
 *      -   ESP_OK   : On success
 *      -   ESP_FAIL : Failed to handle @param req
 */
static esp_err_t esp_otbr_well_known_br_rest_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *response = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(response, ESP_FAIL, WEB_TAG, "Failed to allocate well-known response");

    cJSON *api = cJSON_CreateObject();
    ESP_GOTO_ON_FALSE(api, ESP_FAIL, exit, WEB_TAG, "Failed to allocate well-known api object");
    cJSON_AddStringToObject(api, "version", ESP_OT_REST_API_VERSION);
    cJSON_AddStringToObject(api, "base", "/");
    cJSON_AddItemToObject(response, "api", api);

    cJSON *links = cJSON_AddArrayToObject(response, "links");
    static const struct {
        const char *href;
        const char *rel;
    } entry_points[] = {
        {ESP_OT_REST_API_WELL_KNOWN_ESP_BR_REST_PATH, "self"},
        {ESP_OT_REST_API_NODE_PATH, "node"},
        {ESP_OT_REST_API_DIAGNOSTICS_PATH, "diagnostic"},
    };
    for (size_t i = 0; i < sizeof(entry_points) / sizeof(entry_points[0]); i++) {
        cJSON *link = cJSON_CreateObject();
        cJSON_AddStringToObject(link, "href", entry_points[i].href);
        cJSON_AddStringToObject(link, "rel", entry_points[i].rel);
        cJSON *type = cJSON_AddArrayToObject(link, "type");
        cJSON_AddItemToArray(type, cJSON_CreateString(ESP_OT_REST_CONTENT_TYPE_JSON));
        cJSON_AddItemToArray(links, link);
    }

    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

/*-----------------------------------------------------
Note: Openthread WEB GUI API implement
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
    cJSON *request = httpd_request_convert2_json(req, cJSON_Object);
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
    ESP_LOGI(WEB_TAG, "Join request submitted, attaching...");
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
    cJSON *request = httpd_request_convert2_json(req, cJSON_Object);
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
    cJSON *request = httpd_request_convert2_json(req, cJSON_Object);
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
    cJSON *request = httpd_request_convert2_json(req, cJSON_Object);
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
    cJSON *request = httpd_request_convert2_json(req, cJSON_Object);
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
 * @brief The API handles a ping request to an IPv6 address.
 *
 * @param[in] req The request from http_client (JSON with "address", optional "count" and "size").
 * @return
 *      -   ESP_OK                      : On success
 *      -   ESP_FAIL                    : On failure
 */
static esp_err_t esp_otbr_ping_post_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *request = httpd_request_convert2_json(req, cJSON_Object);
    ESP_RETURN_ON_FALSE(request, ESP_FAIL, WEB_TAG, "Failed to parse the ping request");

    cJSON *result = handle_openthread_ping_request(request);
    cJSON *error = result ? cJSON_CreateNumber((double)OT_ERROR_NONE) : cJSON_CreateNumber((double)OT_ERROR_FAILED);
    cJSON *message = result ? cJSON_CreateString("Ping: Success") : cJSON_CreateString("Ping: Failure");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to response %s", req->uri);
    ESP_GOTO_ON_FALSE(result, ESP_FAIL, exit, WEB_TAG, "Failed to execute ping");
    ESP_LOGI(WEB_TAG, "<====================== Ping ==============================>");
    ESP_LOGI(WEB_TAG, "Ping completed");
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(request);
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_ipaddr_get_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *result = handle_openthread_ipaddr_list_request();
    cJSON *error = result ? cJSON_CreateNumber((double)OT_ERROR_NONE) : cJSON_CreateNumber((double)OT_ERROR_FAILED);
    cJSON *message = result ? cJSON_CreateString("OK") : cJSON_CreateString("Failed to list addresses");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to respond %s", req->uri);
exit:
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_add_ipaddr_post_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *request = httpd_request_convert2_json(req, cJSON_Object);
    ESP_RETURN_ON_FALSE(request, ESP_FAIL, WEB_TAG, "Failed to parse ipaddr add request");

    cJSON *result = handle_openthread_add_ipaddr_request(request);
    cJSON *status = cJSON_GetObjectItem(result, "status");
    bool ok = status && status->valuestring && strcmp(status->valuestring, "ok") == 0;
    cJSON *error = cJSON_CreateNumber(ok ? (double)OT_ERROR_NONE : (double)OT_ERROR_FAILED);
    cJSON *message = cJSON_CreateString(ok ? "Address added" : "Failed to add address");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to respond %s", req->uri);
exit:
    cJSON_Delete(request);
    cJSON_Delete(response);
    return ret;
}

static esp_err_t esp_otbr_delete_ipaddr_post_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    cJSON *request = httpd_request_convert2_json(req, cJSON_Object);
    ESP_RETURN_ON_FALSE(request, ESP_FAIL, WEB_TAG, "Failed to parse ipaddr delete request");

    cJSON *result = handle_openthread_delete_ipaddr_request(request);
    cJSON *status = cJSON_GetObjectItem(result, "status");
    bool ok = status && status->valuestring && strcmp(status->valuestring, "ok") == 0;
    cJSON *error = cJSON_CreateNumber(ok ? (double)OT_ERROR_NONE : (double)OT_ERROR_FAILED);
    cJSON *message = cJSON_CreateString(ok ? "Address removed" : "Failed to remove address");
    cJSON *response = pack_response(error, result, message);
    ESP_GOTO_ON_ERROR(httpd_send_packet(req, response), exit, WEB_TAG, "Failed to respond %s", req->uri);
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
    ESP_RETURN_ON_FALSE(result, ESP_FAIL, WEB_TAG, "Failed to get Thread Network Topology");

    /* Stream the wrapped JSON response in chunks to avoid allocating the
       entire serialized string in RAM at once (can be 30-50 KB for large networks).
       Format: {"error":0,"result":[<item>,<item>,...],"message":"Topology: Success"} */
    ESP_GOTO_ON_ERROR(httpd_resp_set_type(req, "application/json"), exit, WEB_TAG, "Failed to set content type");
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr_chunk(req, "{\"error\":0,\"result\":["), exit, WEB_TAG,
                      "Failed to send chunk");

    int array_size = cJSON_GetArraySize(result);
    for (int i = 0; i < array_size; i++) {
        cJSON *detached = cJSON_DetachItemFromArray(result, 0);
        char *chunk = cJSON_PrintUnformatted(detached);
        cJSON_Delete(detached);
        if (chunk) {
            if (i > 0) {
                ESP_GOTO_ON_ERROR(httpd_resp_sendstr_chunk(req, ","), exit, WEB_TAG, "Failed to send chunk");
            }
            esp_err_t send_err = httpd_resp_sendstr_chunk(req, chunk);
            cJSON_free(chunk);
            ESP_GOTO_ON_ERROR(send_err, exit, WEB_TAG, "Failed to send chunk");
        }
    }

    ESP_GOTO_ON_ERROR(httpd_resp_sendstr_chunk(req, "],\"message\":\"Topology: Success\"}"), exit, WEB_TAG,
                      "Failed to send chunk");
    httpd_resp_sendstr_chunk(req, NULL); /* end chunked response */

    ESP_LOGI(WEB_TAG, "<==================== Thread Topology =====================>");
    ESP_LOGI(WEB_TAG, "Thread diagnostic Tlv Complete.");
    ESP_LOGI(WEB_TAG, "<==========================================================>");
exit:
    cJSON_Delete(result);
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
Note: Handling for Client request
-----------------------------------------------------*/
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
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");
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
    ESP_LOGI(WEB_TAG, "Reading %s", path);

    FILE *fp = fopen(path, "r");
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, WEB_TAG, "Failed to open %s file", path);

    char buf[FILE_CHUNK_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (httpd_resp_send_chunk(req, buf, bytes_read) != ESP_OK) {
            fclose(fp);
            /* Abort chunked transfer on send error */
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
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
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=600");
    ESP_RETURN_ON_ERROR(httpd_resp_send_spiffs_file(req, path), WEB_TAG, "Failed to send index html file");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, NULL), WEB_TAG, "Failed to send http string chunk");
    return ESP_OK;
}

static esp_err_t style_css_get_handler(httpd_req_t *req, char *path)
{
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/css"), WEB_TAG, "Failed to set http text/css type");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=600");
    ESP_RETURN_ON_ERROR(httpd_resp_send_spiffs_file(req, path), WEB_TAG, "Failed to send css file");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, NULL), WEB_TAG, "Failed to send http string chunk");
    return ESP_OK;
}

static esp_err_t script_js_get_handler(httpd_req_t *req, char *path)
{
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/javascript"), WEB_TAG,
                        "Failed to set http application/javascript type");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=600");
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
static request_url_t parse_request_url_information(const char *uri, const struct http_parser_url *parse_url,
                                                   const char *base_path)
{
    request_url_t ret = {
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
 * @brief Check if a string ends with the given suffix.
 */
static bool str_ends_with(const char *str, const char *suffix)
{
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) {
        return false;
    }
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/**
 * @brief Verify and handle the client's default request, return corresponding file to client.
 *        Routes files by extension: .html, .css, .js are served from SPIFFS.
 *
 * @param[in] req The request of http client.
 * @return
 *      -   ESP_OK: on success
 *      -   ESP_FAIL: on failure.
 */
static esp_err_t default_urls_get_handler(httpd_req_t *req)
{
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
    // Check if this is a WiFi config request (when WiFi config mode is active)
    if (esp_br_wifi_config_is_active()) {
        // Let WiFi config server handle it
        return ESP_OK;
    }
#endif

    struct http_parser_url url;
    ESP_RETURN_ON_ERROR(http_parser_parse_url(req->uri, strlen(req->uri), 0, &url), WEB_TAG, "Failed to parse url");
    request_url_t info =
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

    /* Root path: serve index.html */
    if (strcmp(info.file_name, "/") == 0) {
        char index_path[FILEPATH_MAX_SIZE];
        strcpy(index_path, ((http_server_data_t *)req->user_ctx)->base_path);
        strcat(index_path, "/index.html");
        return index_html_get_handler(req, index_path);
    }

    /* Favicon: served from embedded binary */
    if (strcmp(info.file_name, "/favicon.ico") == 0) {
        return favicon_get_handler(req);
    }

    /* Extension-based routing for SPIFFS files */
    if (str_ends_with(info.file_name, ".html")) {
        return index_html_get_handler(req, info.file_path);
    } else if (str_ends_with(info.file_name, ".css")) {
        return style_css_get_handler(req, info.file_path);
    } else if (str_ends_with(info.file_name, ".js")) {
        return script_js_get_handler(req, info.file_path);
    }

    ESP_LOGE(WEB_TAG, "Failed to stat file : %s", info.file_path); /* Respond with 404 Not Found */
    return NOT_FOUND_handler(req);
}

#if CONFIG_SPIRAM
static void *ot_web_json_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void ot_web_json_free(void *ptr)
{
    heap_caps_free(ptr);
}
#endif

/*-----------------------------------------------------
Note: Server Start
-----------------------------------------------------*/
/**
 * @brief Create an HTTP server and register an accessible URI
 *
 * @param[in] base_path A string represents the basic path of http_server files.
 * @return
 *      -   ESP_OK: on success
 *      -   ESP_FAIL: on failure
 */
static httpd_handle_t *start_esp_br_http_server(const char *base_path)
{
    ESP_RETURN_ON_FALSE(base_path, NULL, WEB_TAG, "Invalid http server path");

#if CONFIG_SPIRAM
    cJSON_Hooks hooks;
    hooks.malloc_fn = ot_web_json_malloc;
    hooks.free_fn = ot_web_json_free;
    cJSON_InitHooks(&hooks);
#endif

    strlcpy(s_server.data.base_path, base_path, ESP_VFS_PATH_MAX + 1);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = (sizeof(s_resource_handlers) + sizeof(s_web_gui_handlers)) / sizeof(httpd_uri_t) + 30;
    config.max_resp_headers = (sizeof(s_resource_handlers) + sizeof(s_web_gui_handlers)) / sizeof(httpd_uri_t) + 30;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8 * 1024;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    s_server.port = config.server_port;

    esp_br_web_api_init();

    // start http_server
    ESP_RETURN_ON_FALSE(!httpd_start(&s_server.handle, &config), NULL, WEB_TAG, "Failed to start web server");

    httpd_uri_t default_uris_get = {.uri = "/*", // Match all URIs of type /path/to/file
                                    .method = HTTP_GET,
                                    .handler = default_urls_get_handler,
                                    .user_ctx = &s_server.data};

    if (esp_otbr_thread_rest_register(s_server.handle) != ESP_OK) {
        ESP_LOGE(WEB_TAG, "Failed to register Thread REST API");
        httpd_stop(s_server.handle);
        s_server.handle = NULL;
        return NULL;
    }
    httpd_server_register_http_uri(&s_server, s_resource_handlers, sizeof(s_resource_handlers) / sizeof(httpd_uri_t));
    httpd_server_register_http_uri(&s_server, s_web_gui_handlers, sizeof(s_web_gui_handlers) / sizeof(httpd_uri_t));
    httpd_register_uri_handler(s_server.handle, &default_uris_get);

    return s_server.handle;
}

void connect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data, const char *base_path)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    ESP_RETURN_ON_FALSE(server, , WEB_TAG, "Http server is invalid, failed to start it");
    ESP_LOGI(WEB_TAG, "Start the web server for Openthread Border Router");
    *server = (httpd_handle_t *)start_esp_br_http_server(base_path);
}

/*-----------------------------------------------------
Note: Server Stop
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

static atomic_bool is_br_web_server_started = ATOMIC_VAR_INIT(false);

bool esp_br_web_is_server_started(void)
{
    return atomic_load(&is_br_web_server_started);
}

static void log_web_server_address(const char *hostname)
{
    char hostname_buffer[MDNS_NAME_BUF_LEN] = {0};

    if (hostname || mdns_hostname_get(hostname_buffer) == ESP_OK) {
        hostname = hostname ? hostname : hostname_buffer;
        ESP_LOGI(WEB_TAG, "%s\r\n", "<========server address========>");
        ESP_LOGI(WEB_TAG, "http://%s.local\r\n", hostname);
        ESP_LOGI(WEB_TAG, "%s\r\n", "<================================>");
    } else {
        ESP_LOGW(WEB_TAG, "Cannot get the mDNS hostname, please init mDNS before Web UI");
    }
}

static void mdns_hostname_changed_callback(const char *hostname, void *arg)
{
    (void)arg;

    if (atomic_load(&is_br_web_server_started)) {
        log_web_server_address(hostname);
    }
}

static void start_web_server(const char *base_path)
{
    if (atomic_load(&is_br_web_server_started)) {
        return;
    }

    if (start_esp_br_http_server(base_path) != NULL) {
        atomic_store(&is_br_web_server_started, true);
        log_web_server_address(NULL);
        ESP_ERROR_CHECK(esp_event_post(ESP_BR_WEB_EVENT, ESP_BR_WEB_EVENT_SERVER_STARTED, NULL, 0, portMAX_DELAY));
    } else {
        ESP_LOGE(WEB_TAG, "Fail to start web server");
    }
}

static void handler_got_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
    // Don't start Thread BR web server if WiFi config mode is active
    if (esp_br_wifi_config_is_active()) {
        ESP_LOGI(WEB_TAG, "WiFi config mode is active, skipping Thread BR web server");
        return;
    }
#endif

    start_web_server(arg);
}

#if CONFIG_LWIP_IPV6
static void handler_got_ip6_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
    // Don't start Thread BR web server if WiFi config mode is active
    if (esp_br_wifi_config_is_active()) {
        ESP_LOGI(WEB_TAG, "WiFi config mode is active, skipping Thread BR web server");
        return;
    }
#endif

    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
    esp_ip6_addr_type_t ip6_type = esp_netif_ip6_get_addr_type(&event->ip6_info.ip);

    // IP_EVENT_GOT_IP6 is emitted for every esp-netif. Ignore the Thread and link-local addresses.
    if (event->esp_netif == esp_openthread_get_netif() || ip6_type == ESP_IP6_ADDR_IS_LINK_LOCAL) {
        return;
    }

    start_web_server(arg);
}
#endif

void esp_br_web_start(char *base_path)
{
    ESP_ERROR_CHECK(mdns_register_hostname_changed_callback(mdns_hostname_changed_callback, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handler_got_ip_event, base_path));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &handler_got_ip_event, base_path));
#if CONFIG_LWIP_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &handler_got_ip6_event, base_path));
#endif
}

/* 9-digit TAPs only span [0, 999999999], so this is safe to use as an invalid/sentinel value. */
#define EPSKC_TAP_INVALID 1000000000

static _Atomic uint32_t s_active_epskc_tap = EPSKC_TAP_INVALID;

void esp_br_web_epskc_set_active_tap(const char *tap)
{
    uint32_t value = EPSKC_TAP_INVALID;

    if (tap != NULL && strlen(tap) == 9) {
        value = (uint32_t)strtoul(tap, NULL, 10);
    }

    atomic_store(&s_active_epskc_tap, value);
}

bool esp_br_web_epskc_get_active_tap(char *tap_out, size_t tap_out_len)
{
    uint32_t value = atomic_load(&s_active_epskc_tap);

    if (value == EPSKC_TAP_INVALID || tap_out == NULL || tap_out_len < 10) {
        return false;
    }

    snprintf(tap_out, tap_out_len, "%09" PRIu32, value);
    return true;
}
