/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi Configuration and SoftAP support for ESP Thread Border Router
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "esp_br_web.h"
#include "esp_br_wifi_config.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/ip_addr.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define WIFI_CONFIG_TAG "wifi_config"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_SCAN_DONE_BIT BIT2
#define WIFI_CONFIGURED_BIT BIT3
#define DNS_PORT 53
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64

static bool s_wifi_config_mode = false;
static esp_netif_t *s_ap_netif = NULL;
static httpd_handle_t s_wifi_config_server = NULL;
static EventGroupHandle_t s_wifi_event_group = NULL;
static wifi_ap_record_t *s_ap_records = NULL;
static uint16_t s_ap_count = 0;
static TaskHandle_t s_dns_task_handle = NULL;
static SemaphoreHandle_t s_dns_task_semaphore = NULL;
static int s_dns_socket = -1;
static esp_event_handler_instance_t s_wifi_event_handler_instance = NULL;
static char s_softap_ssid[32] = "";
static char s_configured_ssid[32] = "";
static char s_configured_password[64] = "";

// Embedded HTML files (will be added via SPIFFS)
extern const char wifi_configuration_html_start[] asm("_binary_wifi_configuration_html_start");

static esp_err_t wifi_config_dns_server_start(void);
static void wifi_config_dns_server_stop(void);
static void wifi_config_dns_server_task(void *arg);
static esp_err_t wifi_config_start_softap(void);
static void wifi_config_stop_softap(void);
static esp_err_t wifi_config_start_webserver(void);
static void wifi_config_stop_webserver(void);
static void wifi_config_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// DNS Server implementation
static esp_err_t wifi_config_dns_server_start(void)
{
    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_socket < 0) {
        ESP_LOGE(WIFI_CONFIG_TAG, "Failed to create DNS socket");
        return ESP_FAIL;
    }

    // Set socket to reuse address
    int opt = 1;
    setsockopt(s_dns_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set socket to non-blocking mode
    int flags = fcntl(s_dns_socket, F_GETFL, 0);
    fcntl(s_dns_socket, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DNS_PORT);

    if (bind(s_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(WIFI_CONFIG_TAG, "Failed to bind DNS port %d", DNS_PORT);
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_FAIL;
    }

    // Create semaphore for task synchronization
    s_dns_task_semaphore = xSemaphoreCreateBinary();
    if (!s_dns_task_semaphore) {
        ESP_LOGE(WIFI_CONFIG_TAG, "Failed to create DNS task semaphore");
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(wifi_config_dns_server_task, "dns_server", 4096, NULL, 5, &s_dns_task_handle) != pdPASS) {
        ESP_LOGE(WIFI_CONFIG_TAG, "Failed to create DNS server task");
        vSemaphoreDelete(s_dns_task_semaphore);
        s_dns_task_semaphore = NULL;
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(WIFI_CONFIG_TAG, "DNS server started");
    return ESP_OK;
}

static void wifi_config_dns_server_stop(void)
{
    if (s_dns_task_handle && s_dns_task_semaphore) {
        TaskHandle_t task_handle = s_dns_task_handle;
        // Wait for task to signal completion via semaphore
        if (xSemaphoreTake(s_dns_task_semaphore, pdMS_TO_TICKS(1000)) != pdTRUE) {
            // Timeout: task didn't exit in time, force delete
            ESP_LOGW(WIFI_CONFIG_TAG, "DNS task did not exit in time, forcing deletion");
            if (s_dns_task_handle) {
                vTaskDelete(task_handle);
                s_dns_task_handle = NULL;
            }
        }
    }

    // Clean up semaphore
    if (s_dns_task_semaphore) {
        vSemaphoreDelete(s_dns_task_semaphore);
        s_dns_task_semaphore = NULL;
    }

    if (s_dns_socket >= 0) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    ESP_LOGI(WIFI_CONFIG_TAG, "DNS server stopped");
}

static void wifi_config_dns_server_task(void *arg)
{
    // Store gateway IP address locally to avoid accessing potentially invalid pointer
    esp_ip4_addr_t gateway_addr;
    IP4_ADDR(&gateway_addr, 192, 168, 4, 1);

    char buffer[512];

    while (s_dns_task_handle != NULL) {
        if (s_dns_socket < 0) {
            break;
        }

        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int len = recvfrom(s_dns_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(WIFI_CONFIG_TAG, "DNS recvfrom failed, errno=%d", errno);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Check minimum DNS query length (12 bytes header)
        if (len < 12) {
            continue;
        }

        // Check buffer size to prevent overflow
        if (len + 16 > sizeof(buffer)) {
            continue;
        }

        // Simple DNS response: point all queries to gateway IP
        buffer[2] |= 0x80; // Set response flag (QR = 1)
        buffer[3] |= 0x80; // Set Recursion Available (RA = 1)
        buffer[7] = 1;     // Set answer count to 1

        // Add answer section
        memcpy(&buffer[len], "\xc0\x0c", 2); // Name pointer to question name
        len += 2;
        memcpy(&buffer[len], "\x00\x01\x00\x01\x00\x00\x00\x1c\x00\x04", 10); // Type A, class IN, TTL 28, data length 4
        len += 10;
        memcpy(&buffer[len], &gateway_addr.addr, 4); // Gateway IP
        len += 4;

        int sent = sendto(s_dns_socket, buffer, len, 0, (struct sockaddr *)&client_addr, client_addr_len);
        if (sent < 0) {
            ESP_LOGE(WIFI_CONFIG_TAG, "DNS sendto failed, errno=%d", errno);
        }
    }
    // Signal completion via semaphore before clearing task handle
    if (s_dns_task_semaphore) {
        xSemaphoreGive(s_dns_task_semaphore);
    }
    // Clear task handle before exiting to signal completion
    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

// SoftAP implementation
static esp_err_t wifi_config_start_softap(void)
{
    esp_err_t ret = ESP_OK;
    uint8_t mac[6];

    // Create AP netif
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(WIFI_CONFIG_TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }

    // Set AP IP address to 192.168.4.1
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    esp_netif_dhcps_stop(s_ap_netif);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    esp_netif_dhcps_start(s_ap_netif);

    // Start DNS server
    wifi_config_dns_server_start();

    // Get MAC address for SSID
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));

    // Generate SSID: ESP-ThreadBR-XXXX
    snprintf(s_softap_ssid, sizeof(s_softap_ssid), "ESP-ThreadBR-%02X%02X", mac[4], mac[5]);

    // Configure WiFi AP
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.ap.ssid, s_softap_ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid[sizeof(wifi_config.ap.ssid) - 1] = '\0';
    wifi_config.ap.ssid_len = strlen(s_softap_ssid);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.beacon_interval = 100;  // Set beacon interval to 100ms for better discoverability

    // Initialize WiFi if not already initialized
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(WIFI_CONFIG_TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_config_wifi_event_handler,
                                                        NULL, &s_wifi_event_handler_instance));

    // Set WiFi mode and start AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_CONFIG_TAG, "SoftAP started with SSID: %s", s_softap_ssid);

    return ESP_OK;
}

static void wifi_config_stop_softap(void)
{
    // Stop DNS server first (this may take some time)
    wifi_config_dns_server_stop();

    // Unregister event handlers before stopping WiFi
    if (s_wifi_event_handler_instance) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_handler_instance);
        s_wifi_event_handler_instance = NULL;
    }

    // Destroy AP netif before stopping WiFi
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }

    // Stop WiFi - this will stop both AP and STA if in APSTA mode
    // Note: We don't change mode here, just stop WiFi. The caller will set mode to STA if needed.
    esp_wifi_stop();

    ESP_LOGI(WIFI_CONFIG_TAG, "SoftAP stopped");
}

// WiFi event handlers
static void wifi_config_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id) {
    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(WIFI_CONFIG_TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(WIFI_CONFIG_TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
        break;
    }
    case WIFI_EVENT_STA_CONNECTED:
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        break;
    case WIFI_EVENT_SCAN_DONE: {
        wifi_event_sta_scan_done_t *event = (wifi_event_sta_scan_done_t *)event_data;
        ESP_LOGI(WIFI_CONFIG_TAG, "Scan done, number: %d", event->number);

        if (s_ap_records) {
            free(s_ap_records);
            s_ap_records = NULL;
        }
        s_ap_count = event->number;
        if (s_ap_count > 0) {
            s_ap_records = malloc(sizeof(wifi_ap_record_t) * s_ap_count);
            if (s_ap_records) {
                esp_wifi_scan_get_ap_records(&s_ap_count, s_ap_records);
            }
        }

        // Signal scan completion
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
        }
        break;
    }
    default:
        // Unhandled event, ignore
        break;
    }
}

// HTTP handlers for WiFi configuration
static esp_err_t wifi_config_index_handler(httpd_req_t *req)
{
    // Try to read from SPIFFS first, fallback to embedded
    FILE *fp = fopen("/spiffs/wifi_configuration.html", "r");
    if (fp) {
        char buf[1024];
        size_t read_len;
        while ((read_len = fread(buf, 1, sizeof(buf) - 1, fp)) > 0) {
            buf[read_len] = '\0';
            httpd_resp_sendstr_chunk(req, buf);
        }
        fclose(fp);
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
    }

    // Fallback to embedded HTML
    httpd_resp_send(req, wifi_configuration_html_start, strlen(wifi_configuration_html_start));
    return ESP_OK;
}

static esp_err_t wifi_config_scan_handler(httpd_req_t *req)
{
    // Ensure event group exists
    assert(s_wifi_event_group);

    // Clear scan done bit before starting scan
    xEventGroupClearBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);

    // Trigger a new scan
    wifi_scan_config_t scan_config = {.ssid = NULL,
                                      .bssid = NULL,
                                      .channel = 0,
                                      .show_hidden = true,
                                      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                                      .scan_time = {.active = {.min = 100, .max = 300}}};

    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
    if (ret != ESP_OK) {
        ESP_LOGE(WIFI_CONFIG_TAG, "Failed to start scan: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\":\"Failed to start scan\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Wait for scan to complete using event group (max 10 seconds)
    EventBits_t bits =
        xEventGroupWaitBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WIFI_SCAN_DONE_BIT)) {
        ESP_LOGW(WIFI_CONFIG_TAG, "Scan timeout after 5 seconds");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\":\"Scan timeout\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *aps = cJSON_CreateArray();

    // Add scanned APs
    for (int i = 0; i < s_ap_count && s_ap_records; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char *)s_ap_records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", s_ap_records[i].rssi);
        cJSON_AddNumberToObject(ap, "authmode", s_ap_records[i].authmode);
        cJSON_AddItemToArray(aps, ap);
    }

    cJSON_AddItemToObject(root, "aps", aps);

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t wifi_config_submit_handler(httpd_req_t *req)
{
    char *buf = NULL;
    size_t buf_len = req->content_len;

    if (buf_len > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }

    buf = malloc(buf_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate memory");
        return ESP_FAIL;
    }

    int recv_len = httpd_req_recv(req, buf, buf_len);
    if (recv_len <= 0) {
        free(buf);
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to receive request");
        }
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    buf = NULL;
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    cJSON *password_item = cJSON_GetObjectItemCaseSensitive(json, "password");

    if (!cJSON_IsString(ssid_item) || !ssid_item->valuestring || strlen(ssid_item->valuestring) >= MAX_SSID_LEN + 1) {
        cJSON_Delete(json);
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid SSID\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    const char *ssid = ssid_item->valuestring;
    const char *password = NULL;
    if (cJSON_IsString(password_item) && password_item->valuestring &&
        strlen(password_item->valuestring) < MAX_PASSWORD_LEN + 1) {
        password = password_item->valuestring;
    }

    // Store configured WiFi credentials in memory (will be saved to NVS by caller)
    strncpy(s_configured_ssid, ssid, sizeof(s_configured_ssid) - 1);
    s_configured_ssid[sizeof(s_configured_ssid) - 1] = '\0';
    if (password && strlen(password) > 0) {
        strncpy(s_configured_password, password, sizeof(s_configured_password) - 1);
        s_configured_password[sizeof(s_configured_password) - 1] = '\0';
    } else {
        s_configured_password[0] = '\0';
    }
    // Signal configuration completion
    if (s_wifi_event_group) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONFIGURED_BIT);
    }

    ESP_LOGI(WIFI_CONFIG_TAG, "WiFi configuration received: SSID=%s", s_configured_ssid);

    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t wifi_config_icon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Captive portal handler
static esp_err_t wifi_config_captive_portal_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Start WiFi configuration Web server
static esp_err_t wifi_config_start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 15;
    config.send_wait_timeout = 15;

    ESP_RETURN_ON_ERROR(httpd_start(&s_wifi_config_server, &config), WIFI_CONFIG_TAG,
                        "Failed to start WiFi config server");

    // Register URI handlers
    httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = wifi_config_index_handler, .user_ctx = NULL};
    httpd_register_uri_handler(s_wifi_config_server, &index_uri);

    httpd_uri_t scan_uri = {.uri = "/scan", .method = HTTP_GET, .handler = wifi_config_scan_handler, .user_ctx = NULL};
    httpd_register_uri_handler(s_wifi_config_server, &scan_uri);

    httpd_uri_t submit_uri = {
        .uri = "/submit", .method = HTTP_POST, .handler = wifi_config_submit_handler, .user_ctx = NULL};
    httpd_register_uri_handler(s_wifi_config_server, &submit_uri);

    // Register icon handlers to avoid 404 warnings
    const char *icon_urls[] = {"/favicon.ico", "/apple-touch-icon.png", "/apple-touch-icon-precomposed.png",
                               "/apple-touch-icon-120x120.png", "/apple-touch-icon-120x120-precomposed.png"};

    for (int i = 0; i < sizeof(icon_urls) / sizeof(icon_urls[0]); i++) {
        httpd_uri_t icon_uri = {
            .uri = icon_urls[i], .method = HTTP_GET, .handler = wifi_config_icon_handler, .user_ctx = NULL};
        httpd_register_uri_handler(s_wifi_config_server, &icon_uri);
    }

    // Register captive portal URLs
    const char *captive_urls[] = {"/hotspot-detect.html",      "/generate_204", "/mobile/status.php",
                                  "/check_network_status.txt", "/ncsi.txt",     "/fwlink/",
                                  "/connectivity-check.html",  "/success.txt",  "/portal.html",
                                  "/library/test/success.html"};

    for (int i = 0; i < sizeof(captive_urls) / sizeof(captive_urls[0]); i++) {
        httpd_uri_t captive_uri = {.uri = captive_urls[i],
                                   .method = HTTP_GET,
                                   .handler = wifi_config_captive_portal_handler,
                                   .user_ctx = NULL};
        httpd_register_uri_handler(s_wifi_config_server, &captive_uri);
    }

    ESP_LOGI(WIFI_CONFIG_TAG, "WiFi configuration Web server started");
    return ESP_OK;
}

static void wifi_config_stop_webserver(void)
{
    if (s_wifi_config_server) {
        httpd_stop(s_wifi_config_server);
        s_wifi_config_server = NULL;
    }
}

esp_err_t esp_br_wifi_config_start(void)
{
    if (s_wifi_config_mode) {
        ESP_LOGW(WIFI_CONFIG_TAG, "WiFi config mode already started");
        return ESP_OK;
    }

    // Create event group (for WiFi connection status if needed)
    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
        if (!s_wifi_event_group) {
            ESP_LOGE(WIFI_CONFIG_TAG, "Failed to create event group");
            return ESP_ERR_NO_MEM;
        }
    }

    // Start SoftAP
    esp_err_t ret = wifi_config_start_softap();
    if (ret != ESP_OK) {
        ESP_LOGE(WIFI_CONFIG_TAG, "Failed to start SoftAP");
        if (s_wifi_event_group) {
            vEventGroupDelete(s_wifi_event_group);
            s_wifi_event_group = NULL;
        }
        return ret;
    }

    // Start Web server
    ret = wifi_config_start_webserver();
    if (ret != ESP_OK) {
        ESP_LOGE(WIFI_CONFIG_TAG, "Failed to start Web server");
        // Rollback: stop SoftAP
        wifi_config_stop_softap();
        if (s_wifi_event_group) {
            vEventGroupDelete(s_wifi_event_group);
            s_wifi_event_group = NULL;
        }
        return ret;
    }

    s_wifi_config_mode = true;
    ESP_LOGI(WIFI_CONFIG_TAG, "WiFi configuration mode started");
    ESP_LOGI(WIFI_CONFIG_TAG, "Access web interface at: http://192.168.4.1");

    return ESP_OK;
}

esp_err_t esp_br_wifi_config_stop(void)
{
    if (!s_wifi_config_mode) {
        return ESP_OK;
    }

    wifi_config_stop_webserver();
    wifi_config_stop_softap();

    if (s_ap_records) {
        free(s_ap_records);
        s_ap_records = NULL;
    }

    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    s_wifi_config_mode = false;
    // Clear configured WiFi info when stopping
    s_configured_ssid[0] = '\0';
    s_configured_password[0] = '\0';
    // Clear configured bit when stopping
    if (s_wifi_event_group) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONFIGURED_BIT);
    }
    ESP_LOGI(WIFI_CONFIG_TAG, "WiFi configuration mode stopped");
    return ESP_OK;
}

esp_err_t esp_br_wifi_config_get_configured_wifi(char *ssid, size_t ssid_len, char *password, size_t password_len,
                                                 uint32_t timeout_ms)
{
    if (!s_wifi_config_mode) {
        return ESP_ERR_INVALID_STATE;
    }

    assert(s_wifi_event_group);

    // If not yet configured, wait for configuration event
    if (s_configured_ssid[0] == '\0') {
        // Clear configured bit before waiting
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONFIGURED_BIT);

        // Wait for configuration event
        TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONFIGURED_BIT, pdTRUE, pdFALSE, timeout_ticks);

        if (!(bits & WIFI_CONFIGURED_BIT)) {
            return ESP_ERR_TIMEOUT;
        }
    }

    // Copy configured WiFi credentials to output buffers
    if (ssid && ssid_len > 0) {
        strncpy(ssid, s_configured_ssid, ssid_len - 1);
        ssid[ssid_len - 1] = '\0';
    }

    if (password && password_len > 0) {
        strncpy(password, s_configured_password, password_len - 1);
        password[password_len - 1] = '\0';
    }

    return ESP_OK;
}

bool esp_br_wifi_config_is_active(void)
{
    return s_wifi_config_mode;
}


esp_err_t esp_br_wifi_config_get_softap_info(char *ssid, size_t ssid_len, char *ip_addr, size_t ip_addr_len)
{
    if (!s_wifi_config_mode) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ssid && ssid_len) {
        strncpy(ssid, s_softap_ssid, ssid_len - 1);
        ssid[ssid_len - 1] = '\0';
    }

    if (ip_addr && ip_addr_len > 0 && s_ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
            snprintf(ip_addr, ip_addr_len, IPSTR, IP2STR(&ip_info.ip));
        } else {
            // Fallback to default IP
            strncpy(ip_addr, "192.168.4.1", ip_addr_len - 1);
            ip_addr[ip_addr_len - 1] = '\0';
        }
    }

    return ESP_OK;
}

