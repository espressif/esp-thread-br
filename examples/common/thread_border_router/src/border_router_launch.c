/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "border_router_launch.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_cli.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_cli_extension.h"
#include "esp_rcp_update.h"
#include "esp_vfs_eventfd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "openthread/backbone_router_ftd.h"
#include "openthread/border_router.h"
#include "openthread/cli.h"
#include "openthread/dataset_ftd.h"
#include "openthread/error.h"
#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/logging.h"
#include "openthread/platform/radio.h"
#include "openthread/tasklet.h"
#include "openthread/thread_ftd.h"

#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"

#if CONFIG_OPENTHREAD_CLI_WIFI
#include "esp_ot_wifi_cmd.h"
#endif

#if CONFIG_OPENTHREAD_BR_AUTO_START
#include "esp_wifi.h"
#include "example_common_private.h"
#include "protocol_examples_common.h"
#endif

#define TAG "esp_ot_br"
#define RCP_VERSION_MAX_SIZE 100

static esp_openthread_platform_config_t s_openthread_platform_config;

#if CONFIG_AUTO_UPDATE_RCP
static void update_rcp(void)
{
    // Deinit uart to transfer UART to the serial loader
    esp_openthread_rcp_deinit();
    if (esp_rcp_update() == ESP_OK) {
        esp_rcp_mark_image_verified(true);
    } else {
        esp_rcp_mark_image_verified(false);
    }
    esp_restart();
}

static void try_update_ot_rcp(const esp_openthread_platform_config_t *config)
{
    char internal_rcp_version[RCP_VERSION_MAX_SIZE];
    const char *running_rcp_version = otPlatRadioGetVersionString(esp_openthread_get_instance());

    if (esp_rcp_load_version_in_storage(internal_rcp_version, sizeof(internal_rcp_version)) == ESP_OK) {
        ESP_LOGI(TAG, "Internal RCP Version: %s", internal_rcp_version);
        ESP_LOGI(TAG, "Running  RCP Version: %s", running_rcp_version);
        if (strcmp(internal_rcp_version, running_rcp_version) == 0) {
            esp_rcp_mark_image_verified(true);
        } else {
            update_rcp();
        }
    } else {
        ESP_LOGI(TAG, "RCP firmware not found in storage, will reboot to try next image");
        esp_rcp_mark_image_verified(false);
        esp_restart();
    }
}
#endif // CONFIG_AUTO_UPDATE_RCP

static void rcp_failure_handler(void)
{
#if CONFIG_AUTO_UPDATE_RCP
    esp_rcp_mark_image_unusable();
    char internal_rcp_version[RCP_VERSION_MAX_SIZE];
    if (esp_rcp_load_version_in_storage(internal_rcp_version, sizeof(internal_rcp_version)) == ESP_OK) {
        ESP_LOGI(TAG, "Internal RCP Version: %s", internal_rcp_version);
        update_rcp();
    } else {
        ESP_LOGI(TAG, "RCP firmware not found in storage, will reboot to try next image");
        esp_rcp_mark_image_verified(false);
        esp_restart();
    }
#endif
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *) event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            log_error_if_nonzero("HTTP status code", data->error_handle.esp_ws_handshake_status_code);
            if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",
                                     data->error_handle.esp_transport_sock_errno);
            }
            break;
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
            ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
            if (data->op_code == 0x08 && data->data_len == 2) {
                ESP_LOGW(TAG, "Received closed message with code=%d", 256 * data->data_ptr[0] + data->data_ptr[1]);
            } else {
                ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *) data->data_ptr);
            }

            // If received data contains json structure it succeed to parse
            ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len,
                     data->data_len, data->payload_offset);

            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
            log_error_if_nonzero("HTTP status code", data->error_handle.esp_ws_handshake_status_code);
            if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",
                                     data->error_handle.esp_transport_sock_errno);
            }
            break;
    }
}

static void websocket_client_example_worker(void *ctx)
{
    struct ifreq if_name = {0};
    esp_netif_get_netif_impl_name(get_example_netif(), if_name.ifr_name);
    ESP_LOGI(TAG, "Network interface name: %s", if_name.ifr_name);

    esp_websocket_client_config_t websocket_cfg = {};

    websocket_cfg.uri = "wss://echo.websocket.events";
    websocket_cfg.if_name = &if_name;

    // Using certificate bundle as default server certificate source
    websocket_cfg.crt_bundle_attach = esp_crt_bundle_attach;

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    esp_websocket_client_start(client);

    while (1) {
        if (esp_websocket_client_is_connected(client)) {
            size_t buffer_len = 50;
            uint8_t *buffer = malloc(buffer_len);

            if (buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate buffer");
            } else {
                memset(buffer, 0, buffer_len);

                int bytes_sent = esp_websocket_client_send_bin(client, (const char *) buffer, buffer_len, portMAX_DELAY);

                if (bytes_sent == buffer_len) {
                    ESP_LOGI(TAG, "Successfully sent message");
                } else if (bytes_sent >= 0) {
                    ESP_LOGE(TAG, "Only sent %d of %d bytes of message", bytes_sent, buffer_len);
                } else {
                    ESP_LOGE(TAG, "Failed to send message");
                }

                free(buffer);
            }
        }

        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

static void ot_br_init(void *ctx)
{
#if CONFIG_OPENTHREAD_CLI_WIFI
    ESP_ERROR_CHECK(esp_ot_wifi_config_init());
#endif
#if CONFIG_OPENTHREAD_BR_AUTO_START
#if CONFIG_EXAMPLE_CONNECT_WIFI || CONFIG_EXAMPLE_CONNECT_ETHERNET
    bool wifi_or_ethernet_connected = false;
#else
#error No backbone netif!
#endif
#if CONFIG_EXAMPLE_CONNECT_WIFI
    char wifi_ssid[32] = "";
    char wifi_password[64] = "";
    if (esp_ot_wifi_config_get_ssid(wifi_ssid) == ESP_OK) {
        ESP_LOGI(TAG, "use the Wi-Fi config from NVS");
        esp_ot_wifi_config_get_password(wifi_password);
    } else {
        ESP_LOGI(TAG, "use the Wi-Fi config from Kconfig");
        strcpy(wifi_ssid, CONFIG_EXAMPLE_WIFI_SSID);
        strcpy(wifi_password, CONFIG_EXAMPLE_WIFI_PASSWORD);
    }
    if (esp_ot_wifi_connect(wifi_ssid, wifi_password) == ESP_OK) {
        wifi_or_ethernet_connected = true;
    } else {
        ESP_LOGE(TAG, "Fail to connect to Wi-Fi, please try again manually");
    }
#endif
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(example_ethernet_connect());
    wifi_or_ethernet_connected = true;
#endif
    if (wifi_or_ethernet_connected) {
        esp_openthread_lock_acquire(portMAX_DELAY);
        esp_openthread_set_backbone_netif(get_example_netif());
        ESP_ERROR_CHECK(esp_openthread_border_router_init());
#if CONFIG_EXAMPLE_CONNECT_WIFI
        esp_ot_wifi_border_router_init_flag_set(true);
#endif
        otOperationalDatasetTlvs dataset;
        otError error = otDatasetGetActiveTlvs(esp_openthread_get_instance(), &dataset);
        ESP_ERROR_CHECK(esp_openthread_auto_start((error == OT_ERROR_NONE) ? &dataset : NULL));
        esp_openthread_lock_release();
    } else {
        ESP_LOGE(TAG, "Auto-start mode failed, please try to start manually");
    }
#endif // CONFIG_OPENTHREAD_BR_AUTO_START

    xTaskCreate(websocket_client_example_worker, "websocket_client_example_worker", 6144, NULL, 4, NULL);

    vTaskDelete(NULL);
}

static void ot_task_worker(void *ctx)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *openthread_netif = esp_netif_new(&cfg);

    assert(openthread_netif != NULL);

    // Initialize the OpenThread stack
    esp_openthread_register_rcp_failure_handler(rcp_failure_handler);
    esp_openthread_set_compatibility_error_callback(rcp_failure_handler);
    ESP_ERROR_CHECK(esp_openthread_init(&s_openthread_platform_config));
#if CONFIG_AUTO_UPDATE_RCP
    try_update_ot_rcp(&s_openthread_platform_config);
#endif
    // Initialize border routing features
    esp_openthread_lock_acquire(portMAX_DELAY);
    ESP_ERROR_CHECK(esp_netif_attach(openthread_netif, esp_openthread_netif_glue_init(&s_openthread_platform_config)));
#if CONFIG_OPENTHREAD_LOG_LEVEL_DYNAMIC
    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);
#endif
    esp_openthread_cli_init();
    esp_cli_custom_command_init();
    esp_openthread_cli_create_task();
    esp_openthread_lock_release();

    xTaskCreate(ot_br_init, "ot_br_init", 6144, NULL, 4, NULL);
    // Run the main loop
    esp_openthread_launch_mainloop();

    // Clean up
    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);
    esp_vfs_eventfd_unregister();
    esp_rcp_update_deinit();
    vTaskDelete(NULL);
}

void launch_openthread_border_router(const esp_openthread_platform_config_t *platform_config,
                                     const esp_rcp_update_config_t *update_config)
{
    s_openthread_platform_config = *platform_config;

#if CONFIG_AUTO_UPDATE_RCP
    ESP_ERROR_CHECK(esp_rcp_update_init(update_config));
#else
    OT_UNUSED_VARIABLE(update_config);
#endif

    xTaskCreate(ot_task_worker, "ot_br_main", 8192, xTaskGetCurrentTaskHandle(), 5, NULL);
}
