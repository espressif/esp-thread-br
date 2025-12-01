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
#include "esp_ot_rcp_update.h"
#include "esp_rcp_update.h"
#include "esp_vfs_eventfd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
#include "esp_br_wifi_config.h"
#endif
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

#if CONFIG_OPENTHREAD_CLI_WIFI
#include "esp_ot_wifi_cmd.h"
#endif

#if CONFIG_OPENTHREAD_BR_AUTO_START
#include "esp_wifi.h"
#include "example_common_private.h"
#include "protocol_examples_common.h"
#endif

#if !CONFIG_EXAMPLE_CONNECT_WIFI && !CONFIG_EXAMPLE_CONNECT_ETHERNET
#error No backbone netif!
#endif

#define TAG "esp_ot_br"

static esp_openthread_platform_config_t s_openthread_platform_config;

#if CONFIG_EXAMPLE_CONNECT_WIFI && CONFIG_OPENTHREAD_BR_AUTO_START
/**
 * @brief Save Wi-Fi configuration to NVS and connect
 *
 * @param ssid Wi-Fi SSID
 * @param password Wi-Fi password (can be NULL for open network)
 * @return true if connection successful, false otherwise
 */
static bool wifi_config_save_and_connect(const char *ssid, const char *password)
{
    if (esp_ot_wifi_connect(ssid, password) == ESP_OK) {
        ESP_LOGI(TAG, "Connected to Wi-Fi: %s", ssid);

        // Save to NVS
        if (esp_ot_wifi_config_set_ssid(ssid) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save Wi-Fi SSID to NVS");
            return false;
        }

        if (password && strlen(password) > 0) {
            if (esp_ot_wifi_config_set_password(password) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save Wi-Fi password to NVS");
                return false;
            }
        } else {
            // Clear password for open network
            if (esp_ot_wifi_config_set_password("") != ESP_OK) {
                ESP_LOGE(TAG, "Failed to clear Wi-Fi password in NVS");
                return false;
            }
        }
        return true;
    }
    return false;
}
#endif

#if CONFIG_OPENTHREAD_BR_AUTO_START
static void ot_br_init(void *ctx)
{
#if CONFIG_EXAMPLE_CONNECT_WIFI
    // Wi-Fi connection mode - two ways to get Wi-Fi parameters:
    // 1. CONFIG_OPENTHREAD_BR_SOFTAP_SETUP: via SoftAP Web interface
    // 2. CONFIG_EXAMPLE_WIFI_SSID: from Kconfig
    char wifi_ssid[32] = "";
    char wifi_password[64] = "";
    bool has_nvs_wifi_config = false;

    // Check if Wi-Fi configuration exists in NVS
    if (esp_ot_wifi_config_get_ssid(wifi_ssid) == ESP_OK) {
        esp_ot_wifi_config_get_password(wifi_password);
        has_nvs_wifi_config = true;
    }

    if (has_nvs_wifi_config && esp_ot_wifi_connect(wifi_ssid, wifi_password) == ESP_OK) {
        ESP_LOGI(TAG, "Connected to Wi-Fi: %s", wifi_ssid);
    } else {
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
        // SoftAP Wi-Fi configuration mode
        esp_br_wifi_config_start();

        // Wait for user to configure Wi-Fi via web interface (wait forever)
        esp_br_wifi_config_get_configured_wifi(wifi_ssid, sizeof(wifi_ssid), wifi_password, sizeof(wifi_password), 0);

        // Stop SoftAP mode
        esp_br_wifi_config_stop();
#else
        // Standard Wi-Fi connection mode - get from Kconfig
        strncpy(wifi_ssid, CONFIG_EXAMPLE_WIFI_SSID, sizeof(wifi_ssid) - 1);
        wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
        strncpy(wifi_password, CONFIG_EXAMPLE_WIFI_PASSWORD, sizeof(wifi_password) - 1);
        wifi_password[sizeof(wifi_password) - 1] = '\0';
#endif

        // Connect to Wi-Fi and save to NVS
        if (!wifi_config_save_and_connect(wifi_ssid, wifi_password)) {
            // In auto-start mode, Wi-Fi must be configured and connected before initializing the OpenThread stack
            ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", wifi_ssid);
            ESP_LOGE(TAG, "Rebooting ... to try again");
            esp_restart();
        }
    }
#elif CONFIG_EXAMPLE_CONNECT_ETHERNET
    // Ethernet connection mode
    ESP_ERROR_CHECK(example_ethernet_connect());
#endif // CONFIG_EXAMPLE_CONNECT_WIFI || CONFIG_EXAMPLE_CONNECT_ETHERNET

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

    vTaskDelete(NULL);
}
#endif // CONFIG_OPENTHREAD_BR_AUTO_START

static void ot_task_worker(void *ctx)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *openthread_netif = esp_netif_new(&cfg);

    assert(openthread_netif != NULL);

    // Initialize the OpenThread stack
#if CONFIG_AUTO_UPDATE_RCP
    esp_ot_register_rcp_handler();
#endif
    ESP_ERROR_CHECK(esp_openthread_init(&s_openthread_platform_config));
#if CONFIG_AUTO_UPDATE_RCP
    esp_ot_update_rcp_if_different();
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

#if CONFIG_OPENTHREAD_BR_AUTO_START
    xTaskCreate(ot_br_init, "ot_br_init", 6144, NULL, 4, NULL);
#endif

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
