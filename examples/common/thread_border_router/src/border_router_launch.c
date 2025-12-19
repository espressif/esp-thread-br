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
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
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
#include "ot_examples_common.h"

#if !CONFIG_EXAMPLE_CONNECT_WIFI && !CONFIG_EXAMPLE_CONNECT_ETHERNET
#error No backbone netif!
#endif

#define TAG "esp_ot_br"

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
    if (error != OT_ERROR_NONE) {
        // No existing dataset, create a random one
        otOperationalDataset new_dataset;
        error = otDatasetCreateNewNetwork(esp_openthread_get_instance(), &new_dataset);
        assert(error == OT_ERROR_NONE);

        // Set network name to ESP-BR-<MAC address>
        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_BASE) == ESP_OK) {
            char network_name[OT_NETWORK_NAME_MAX_SIZE + 1];
            snprintf(network_name, sizeof(network_name), "ESP-BR-%02X%02X", mac[4], mac[5]);
            memcpy(new_dataset.mNetworkName.m8, network_name, strlen(network_name) + 1);
            new_dataset.mComponents.mIsNetworkNamePresent = true;
        }
        otDatasetConvertToTlvs(&new_dataset, &dataset);
        ESP_LOGI(TAG, "Created new random Thread dataset");
    }
    ESP_ERROR_CHECK(esp_openthread_auto_start(&dataset));
    esp_openthread_lock_release();

    vTaskDelete(NULL);
}
#endif // CONFIG_OPENTHREAD_BR_AUTO_START

void launch_openthread_border_router(const esp_openthread_config_t *config,
                                     const esp_rcp_update_config_t *update_config)
{
#if CONFIG_OPENTHREAD_CLI
    ot_console_start();
#endif

#if CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
    ot_external_coexist_init();
#endif

#if CONFIG_AUTO_UPDATE_RCP
    ESP_ERROR_CHECK(esp_rcp_update_init(update_config));
    esp_ot_register_rcp_handler();
#else
    OT_UNUSED_VARIABLE(update_config);
#endif

    ESP_ERROR_CHECK(esp_openthread_start(config));
#if CONFIG_AUTO_UPDATE_RCP
    esp_ot_update_rcp_if_different();
#endif
#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif
#if CONFIG_OPENTHREAD_BR_AUTO_START
    xTaskCreate(ot_br_init, "ot_br_init", 6144, NULL, 4, NULL);
#endif
}
