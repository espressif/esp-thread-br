/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "border_router_launch.h"

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

#define TAG "esp_ot_br"
#define RCP_VERSION_MAX_SIZE 100

static esp_openthread_platform_config_t s_openthread_platform_config;

#if CONFIG_OPENTHREAD_BR_AUTO_START

static int hex_digit_to_int(char hex)
{
    if ('A' <= hex && hex <= 'F') {
        return 10 + hex - 'A';
    }
    if ('a' <= hex && hex <= 'f') {
        return 10 + hex - 'a';
    }
    if ('0' <= hex && hex <= '9') {
        return hex - '0';
    }
    return -1;
}

static size_t hex_string_to_binary(const char *hex_string, uint8_t *buf, size_t buf_size)
{
    int num_char = strlen(hex_string);

    if (num_char != buf_size * 2) {
        return 0;
    }
    for (size_t i = 0; i < num_char; i += 2) {
        int digit0 = hex_digit_to_int(hex_string[i]);
        int digit1 = hex_digit_to_int(hex_string[i + 1]);

        if (digit0 < 0 || digit1 < 0) {
            return 0;
        }
        buf[i / 2] = (digit0 << 4) + digit1;
    }

    return buf_size;
}

static void create_config_network(otInstance *instance)
{
    otOperationalDataset dataset;

    if (otDatasetGetActive(instance, &dataset) == OT_ERROR_NONE) {
        ESP_LOGI(TAG, "Already has network, skip configuring OpenThread network.");
        return;
    }

    uint16_t network_name_len = strlen(CONFIG_OPENTHREAD_NETWORK_NAME);

    assert(network_name_len <= OT_NETWORK_NAME_MAX_SIZE);

    if (otDatasetCreateNewNetwork(instance, &dataset) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to create OpenThread network dataset.");
        abort();
    }
    dataset.mChannel = CONFIG_OPENTHREAD_NETWORK_CHANNEL;
    dataset.mComponents.mIsChannelPresent = true;
    dataset.mPanId = CONFIG_OPENTHREAD_NETWORK_PANID;
    dataset.mComponents.mIsPanIdPresent = true;
    memcpy(dataset.mNetworkName.m8, CONFIG_OPENTHREAD_NETWORK_NAME, network_name_len);
    dataset.mComponents.mIsNetworkNamePresent = true;
    if (hex_string_to_binary(CONFIG_OPENTHREAD_NETWORK_EXTPANID, dataset.mExtendedPanId.m8,
                             sizeof(dataset.mExtendedPanId.m8)) != sizeof(dataset.mExtendedPanId.m8)) {
        ESP_LOGE(TAG, "Cannot convert OpenThread extended pan id. Please double-check your config.");
        abort();
    }
    dataset.mComponents.mIsExtendedPanIdPresent = true;
    if (hex_string_to_binary(CONFIG_OPENTHREAD_NETWORK_MASTERKEY, dataset.mNetworkKey.m8,
                             sizeof(dataset.mNetworkKey.m8)) != sizeof(dataset.mNetworkKey.m8)) {
        ESP_LOGE(TAG, "Cannot convert OpenThread master key. Please double-check your config.");
        abort();
    }
    dataset.mComponents.mIsNetworkKeyPresent = true;
    if (hex_string_to_binary(CONFIG_OPENTHREAD_NETWORK_PSKC, dataset.mPskc.m8, sizeof(dataset.mPskc.m8)) !=
        sizeof(dataset.mPskc.m8)) {
        ESP_LOGE(TAG, "Cannot convert OpenThread pre-shared commissioner key. Please double-check your config.");
        abort();
    }
    dataset.mComponents.mIsPskcPresent = true;
    if (otDatasetSetActive(instance, &dataset) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set OpenThread active dataset.");
        abort();
    }
    return;
}

static void launch_openthread_network(otInstance *instance)
{
    if (otIp6SetEnabled(instance, true) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to enable OpenThread IP6 link");
        abort();
    }
    if (otThreadSetEnabled(instance, true) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to enable OpenThread");
        abort();
    }
    if (otBorderRouterRegister(instance) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to register border router.");
        abort();
    }
    otBackboneRouterSetEnabled(instance, true);
}

#endif // CONFIG_OPENTHREAD_BR_AUTO_START

static void update_rcp(void)
{
    // Deinit uart to transfer UART to the serial loader
    esp_openthread_rcp_deinit();
    if (esp_rcp_update() != ESP_OK) {
        esp_rcp_mark_image_verified(false);
    }
    esp_restart();
}

#if CONFIG_OPENTHREAD_BR_AUTO_UPDATE_RCP
static void try_update_ot_rcp(const esp_openthread_platform_config_t *config)
{
    char internal_rcp_version[RCP_VERSION_MAX_SIZE];
    const char *running_rcp_version = otPlatRadioGetVersionString(esp_openthread_get_instance());

    if (esp_rcp_load_version_in_storage(internal_rcp_version, sizeof(internal_rcp_version)) == ESP_OK) {
        ESP_LOGI(TAG, "Internal RCP Version: %s", internal_rcp_version);
        ESP_LOGI(TAG, "Running  RCP Version: %s", running_rcp_version);
        if (strcmp(internal_rcp_version, running_rcp_version)) {
            update_rcp();
        } else {
            esp_rcp_mark_image_verified(true);
        }
    } else {
        ESP_LOGI(TAG, "RCP firmware not found in storage, will reboot to try next image");
        esp_rcp_mark_image_verified(false);
        esp_restart();
    }
}
#endif // CONFIG_OPENTHREAD_BR_AUTO_UPDATE_RCP

static void rcp_failure_handler(void)
{
#if CONFIG_OPENTHREAD_BR_AUTO_UPDATE_RCP
    try_update_ot_rcp(&s_openthread_platform_config);
#endif // CONFIG_OPENTHREAD_BR_AUTO_UPDATE_RCP
    esp_rcp_reset();
}

static void ot_task_worker(void *ctx)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *openthread_netif = esp_netif_new(&cfg);

    assert(openthread_netif != NULL);

    // Initialize the OpenThread stack
    esp_openthread_register_rcp_failure_handler(rcp_failure_handler);
    ESP_ERROR_CHECK(esp_openthread_init(&s_openthread_platform_config));

    // Initialize border routing features
    esp_openthread_lock_acquire(portMAX_DELAY);
#if CONFIG_OPENTHREAD_BR_AUTO_UPDATE_RCP
    try_update_ot_rcp(&s_openthread_platform_config);
#endif
    ESP_ERROR_CHECK(esp_netif_attach(openthread_netif, esp_openthread_netif_glue_init(&s_openthread_platform_config)));

    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);
    esp_openthread_cli_init();
    ESP_ERROR_CHECK(esp_netif_set_default_netif(openthread_netif));
    esp_cli_custom_command_init();
#if CONFIG_OPENTHREAD_BR_AUTO_START
    ESP_ERROR_CHECK(esp_openthread_border_router_init());
    create_config_network(esp_openthread_get_instance());
    launch_openthread_network(esp_openthread_get_instance());
#endif // CONFIG_OPENTHREAD_BR_AUTO_START
    esp_openthread_lock_release();

    // Run the main loop
    esp_openthread_cli_create_task();
    esp_openthread_launch_mainloop();

    // Clean up
    esp_netif_destroy(openthread_netif);
    esp_openthread_netif_glue_deinit();
    esp_vfs_eventfd_unregister();
    esp_rcp_update_deinit();
    vTaskDelete(NULL);
}

void launch_openthread_border_router(const esp_openthread_platform_config_t *platform_config,
                                     const esp_rcp_update_config_t *update_config)
{
    s_openthread_platform_config = *platform_config;
    ESP_ERROR_CHECK(esp_rcp_update_init(update_config));
    xTaskCreate(ot_task_worker, "ot_br_main", 6144, xTaskGetCurrentTaskHandle(), 5, NULL);
}
