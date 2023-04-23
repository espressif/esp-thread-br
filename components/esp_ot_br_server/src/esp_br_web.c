/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_br_web.h"
#include "cJSON.h"
#include "esp_br_web_package.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_openthread_border_router.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "http_parser.h"
#include "protocol_examples_common.h"
#include <stdio.h>
#include <string.h>

#define MAX_FILE_SIZE (200 * 1024) // 200 KB
#define MAX_FILE_SIZE_STR "200KB"
#define SCRATCH_BUFSIZE 8192 /* Scratch buffer size */
#define VFS_PATH_MAXNUM 15
#define MAX_WS_QUEUE_SIZE 5
#define SERVER_IPV4_LEN 16
#define FILE_CHUNK_SIZE 1024
#define WEB_TAG "OTBR_WEB"
#define SPIFFS_TAG "SPIFFS"

/**
 * @brief some information for http_server
 */
typedef struct http_server {
    httpd_handle_t handle;    /* Server handle, unique */
    char ip[SERVER_IPV4_LEN]; /* ip */
    uint16_t port;            /* port */
} http_server_t;
static http_server_t g_server = {NULL, "", 80}; /* the instance of server */
/**
 * @brief some configure for http_server
 */
typedef struct http_server_data {
    char base_path[ESP_VFS_PATH_MAX + 1]; /* Base path of file storage */
    char scratch[SCRATCH_BUFSIZE];        /* Scratch buffer for temporary storage during file transfer */
} http_server_data_t;

/**
 * @brief some parameter for parsing url
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
 Note：REST API Configure
-----------------------------------------------------*/
/**
 * @brief The function is to convert client's http request message to cSJON type.
 *
 * @param[in] req A request from http client.
 * @return the json message
 * @note return result needs to free.
 */
static cJSON *http_request_convert2_json(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((http_server_data_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) /* Respond with 500 Internal Server Error */
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return NULL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) /* Respond with 500 Internal Server Error */
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return NULL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    cJSON *root = cJSON_Parse(buf); /* need to free. */
    if (verify_ot_server_package(root) == ESP_OK)
        return root;
    else
        return NULL;
}

/*-----------------------------------------------------
 Note：Openthread Discover
-----------------------------------------------------*/
/**
 * @brief The function is to handle a client's request which requires to get the available thread networks by 'HTTP_GET'
 * way.
 *
 * @param[in] req the request for http_client
 * @return ESP_OK: on success, ESP_FAIL: on failure
 */
static esp_err_t thread_scan_networks_get_handler(httpd_req_t *req)
{
    ESP_LOGI(WEB_TAG, "<==================== Thread Discover ======================>");
    ESP_LOGI(WEB_TAG, "Start to discover network!");
    ESP_LOGI(WEB_TAG, "<==========================================================>");

    cJSON *json_ret = encode_available_thread_networks_package(); /* encode the package */
    const char *send_msg = cJSON_Print(json_ret);

    ESP_LOGD(WEB_TAG, "send: %s\r\n", send_msg);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, send_msg);

    cJSON_free((void *)send_msg);
    cJSON_Delete(json_ret);
    return ESP_OK;
}

/*-----------------------------------------------------
 Note：Openthread Join
-----------------------------------------------------*/
/**
 * @brief The function is to handle a request from client, which requires Thread to join a network.
 *
 * @param[in] req The request for http_client.
 * @return ESP_OK: ESP_OK: on success, ESP_FAIL: on failure.
 */
static esp_err_t thread_join_network_post_handler(httpd_req_t *req)
{
    cJSON *root = http_request_convert2_json(req); /* obtain the json for network which client wants to join */
    if (!root) {
        ESP_LOGW(WEB_TAG, "parsing package error.");
        return ESP_FAIL;
    }
    char *output = cJSON_Print(root);
    ESP_LOGD(WEB_TAG, "%s", output);
    cJSON_Delete(root);
    cJSON_free(output);
    return ESP_OK;
}

/*-----------------------------------------------------
 Note：Openthread Form
-----------------------------------------------------*/
/**
 * @brief The function is to handle a request from client, which requires Thread to form a Network.
 *
 * @param[in] req The request for http_client.
 * @return ESP_OK: ESP_OK: on success, ESP_FAIL: on failure.
 */
static esp_err_t thread_form_network_post_handler(httpd_req_t *req)
{
    cJSON *root = http_request_convert2_json(req);
    cJSON *content = cJSON_GetObjectItem(root, "content");

    char *output = cJSON_Print(root);
    ESP_LOGD(WEB_TAG, "received: %s", output);
    cJSON_free(output);

    if (!content) {
        ESP_LOGW(WEB_TAG, "parsing package error.");
        return ESP_FAIL;
    }

    cJSON *response = form_openthread_network(content);
    const char *send_msg = cJSON_Print(response);
    httpd_resp_sendstr(req, send_msg);

    ESP_LOGD(WEB_TAG, "send: %s", send_msg);

    cJSON_free(root);
    cJSON_free((void *)send_msg);
    cJSON_Delete(response);
    return ESP_OK;
}

/*-----------------------------------------------------
 Note：Openthread Status
-----------------------------------------------------*/
/**
 * @brief The function is to response the requirement from client by "HTTP_GET" which need to get thread status.
 *
 * @param[in] req The request for http_client.
 * @return ESP_OK: ESP_OK: on success, ESP_FAIL: on failure.
 */
static esp_err_t thread_status_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *ret = encode_thread_status_package(); /* encode json package */
    const char *send_msg = cJSON_Print(ret);

    ESP_LOGD(WEB_TAG, "send: %s\r\n", send_msg);

    httpd_resp_sendstr(req, send_msg);

    free((void *)send_msg);
    cJSON_Delete(ret);

    return ESP_OK;
}

/*-----------------------------------------------------
 Note：Handling for Client request
-----------------------------------------------------*/
/**
 * @brief The function is to return a blank html to client. when client accesses a error link, the function will be
 * call.
 *
 * @param[in] req the request from client's browser
 * @return ESP_OK: on success, ESP_FAIL: on failure
 */
static esp_err_t blank_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0); /* Response body can be empty */
}

/**
 * @brief The function is to provide the favicon for GUI.
 *
 * @param[in] req the request from client's browser.
 * @return ESP_OK: on success, ESP_FAIL: on failure.
 */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[] asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon"); /* notice resp type to browser*/
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
    return ESP_OK;
}

/**
 * @brief The function provide a method to send message to @param req client accord to @param path.
 *
 * @param[in] req  The request from the client
 * @param[in] path A string represents the path of file which you want to send.
 * @return Whether the message is sent or not
 */
static esp_err_t httpd_resp_send_spiffs_file(httpd_req_t *req, char *path)
{
    ESP_LOGI(WEB_TAG, "-------------------------------------------");
    ESP_LOGI(SPIFFS_TAG, "Reading %s", path);

    FILE *fp = fopen(path, "r"); // Open for reading file
    if (fp == NULL) {
        ESP_LOGE(SPIFFS_TAG, "Failed to open file.");
        return ESP_FAIL;
    }
    char buf[FILE_CHUNK_SIZE]; // the size of chunk
    while (!feof(fp) && !ferror(fp)) {
        fread(buf, FILE_CHUNK_SIZE - 1, 1, fp);
        buf[FILE_CHUNK_SIZE - 1] = '\0';
        httpd_resp_sendstr_chunk(req, buf);
        memset(buf, 0, sizeof(buf));
    }
    fclose(fp);
    return ESP_OK;
}

/**
 * @brief The function is to provide the index.html for GUI,when the client login the web.
 *
 * @param[in] req the request from client's browser.
 * @param[in] path A string pointer points to the index.hrml path.
 * @return ESP_OK: on success, ESP_FAIL: on failure
 */
static esp_err_t index_html_get_handler(httpd_req_t *req, char *path)
{
    ESP_RETURN_ON_ERROR(httpd_resp_send_spiffs_file(req, path), WEB_TAG, "Fail to get html file.");
    httpd_resp_sendstr_chunk(req, NULL); // end
    return ESP_OK;
}

static esp_err_t style_css_get_handler(httpd_req_t *req, char *path)
{
    httpd_resp_set_type(req, "text/css"); // send content-type："text/css" in http-header
    ESP_RETURN_ON_ERROR(httpd_resp_send_spiffs_file(req, path), WEB_TAG, "Fail to get style file.");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t script_js_get_handler(httpd_req_t *req, char *path)
{
    httpd_resp_set_type(req,
                        "application/javascript"); // send content-type："application/javascript" in http-header
    ESP_RETURN_ON_ERROR(httpd_resp_send_spiffs_file(req, path), WEB_TAG, "Fail to get script file.");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/**
 * @brief The function is to parse @param parse_url to obtain vaild information for returning
 *
 * @param[in] uri       A string points the request uri from client
 * @param[in] parse_url A pointer point to the result structure for @function "http_parser_parse_url()".
 * @param[in] base_path A string pointer point the base files path.
 * @return the vaild information from @param parse_url
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
 * @brief The function is to verify and handle the client's default request, return corrresponding file to client.
 *
 * @param[in] req A request of client's.
 * @return ESP_OK: on success, ESP_FAIL: on failure.
 */
static esp_err_t default_urls_get_handler(httpd_req_t *req)
{
    struct http_parser_url url;
    ESP_RETURN_ON_ERROR(http_parser_parse_url(req->uri, strlen(req->uri), 0, &url), WEB_TAG, "Fail to parse url.");
    reqeust_url_t info =
        parse_request_url_information(req->uri, &url, ((http_server_data_t *)req->user_ctx)->base_path);

    ESP_LOGI(WEB_TAG, "-------------------------------------------");
    ESP_LOGI(WEB_TAG, "%s", info.file_name);
    if (!strcmp(info.file_name, "")) // check the filename.
    {
        ESP_LOGE(WEB_TAG, "Filename is too long or url error."); /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename is too long or url error.");
        return ESP_FAIL;
    }
    if (strcmp(info.file_name, "/") == 0)
        return blank_html_get_handler(req);
    else if (strcmp(info.file_name, "/index.html") == 0)
        return index_html_get_handler(req, info.file_path);
    else if (strcmp(info.file_name, "/static/style.css") == 0)
        return style_css_get_handler(req, info.file_path);
    else if (strcmp(info.file_name, "/static/restful.js") == 0)
        return script_js_get_handler(req, info.file_path);
    else if (strcmp(info.file_name, "/favicon.ico") == 0)
        return favicon_get_handler(req);
    else {
        ESP_LOGE(WEB_TAG, "Failed to stat file : %s", info.file_path); /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*-----------------------------------------------------
 Note：Server Start
-----------------------------------------------------*/
/**
 * @brief The function is to create an HTTP server and register an accessible URI
 *
 * @param[in] base_path A string represents the basic path of http_server files.
 * @param[in] host_ip A IPvr4 address provided by the connected wifi, recorded by g_server.ip
 * @return ESP_OK: on success, ESP_FAIL: on failure
 */
static esp_err_t start_esp_br_http_server(const char *base_path, const char *host_ip)
{
    ESP_RETURN_ON_ERROR(base_path == NULL, WEB_TAG, "The base path is empty.");

    strcpy(g_server.ip, host_ip);
    static http_server_data_t *server_data = NULL;

    // avoid to create server twice
    if (server_data) {
        ESP_LOGE(WEB_TAG, "Http server already started!");
        return ESP_ERR_INVALID_STATE;
    }

    server_data = calloc(
        1, sizeof(http_server_data_t)); /* malloc the buffer of server, when stop server, the memory needs to free */
    if (!server_data) {
        ESP_LOGE(WEB_TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(
        server_data->base_path, base_path,
        sizeof(server_data->base_path)); /* take the path of web_server in flash as the root directory of the server */

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Use the URI wildcard matching function to allow the same handler to respond to multiple different target URIs
    // that match the wildcard scheme
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 10240; // expand the stack size to fit file transmission
    g_server.port = config.server_port;
    // start http_server
    if (httpd_start(&g_server.handle, &config) != ESP_OK) {
        ESP_LOGE(WEB_TAG, "Failed to start file server!");
        return ESP_FAIL;
    }
    httpd_uri_t default_uris_get = {.uri = "/*", // Match all URIs of type /path/to/file
                                    .method = HTTP_GET,
                                    .handler = default_urls_get_handler,
                                    .user_ctx = server_data};
    httpd_uri_t thread_status_uris_get = {.uri = "/thread/api/status", // match function thread status
                                          .method = HTTP_GET,
                                          .handler = thread_status_get_handler,
                                          .user_ctx = server_data};
    httpd_uri_t thread_scan_network_uris_get = {.uri = "/thread/api/scan",
                                                .method = HTTP_GET,
                                                .handler = thread_scan_networks_get_handler,
                                                .user_ctx = server_data};
    httpd_uri_t thread_join_network_uris_post = {.uri = "/thread/api/scan/join",
                                                 .method = HTTP_POST,
                                                 .handler = thread_join_network_post_handler,
                                                 .user_ctx = server_data};
    httpd_uri_t thread_form_network_uris_post = {.uri = "/thread/api/form",
                                                 .method = HTTP_POST,
                                                 .handler = thread_form_network_post_handler,
                                                 .user_ctx = server_data};
    // httpd_register_uri_handler(g_server.handle, &webSocket);            //care for the order
    httpd_register_uri_handler(g_server.handle, &thread_status_uris_get);
    httpd_register_uri_handler(g_server.handle, &thread_scan_network_uris_get);
    httpd_register_uri_handler(g_server.handle, &thread_join_network_uris_post);
    httpd_register_uri_handler(g_server.handle, &thread_form_network_uris_post);
    httpd_register_uri_handler(g_server.handle, &default_uris_get);
    // tip: login address
    ESP_LOGI(WEB_TAG, "%s\r\n", "<=======================server start========================>");
    ESP_LOGI(WEB_TAG, "http://%s:%d/index.html\r\n", g_server.ip, g_server.port);
    ESP_LOGI(WEB_TAG, "%s\r\n", "<===========================================================>");
    return ESP_OK;
}

void connect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data, const char *base_path)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server == NULL) {
        ESP_LOGI(WEB_TAG, "Starting webserver");
        *server = (httpd_handle_t *)start_esp_br_http_server(base_path, g_server.ip);
    }
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
    if (*server) {
        ESP_LOGI(WEB_TAG, "Stopping httpserver");
        stop_httpserver(*server);
        *server = NULL;
    }
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
