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

#if CONFIG_OPENTHREAD_BR_AUTO_START
#include "esp_wifi.h"
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
    try_update_ot_rcp(&s_openthread_platform_config);
#endif // CONFIG_AUTO_UPDATE_RCP
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
#if CONFIG_AUTO_UPDATE_RCP
    try_update_ot_rcp(&s_openthread_platform_config);
#endif
    ESP_ERROR_CHECK(esp_netif_attach(openthread_netif, esp_openthread_netif_glue_init(&s_openthread_platform_config)));

#if CONFIG_OPENTHREAD_LOG_LEVEL_DYNAMIC
    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);
#endif

    esp_openthread_cli_init();
    ESP_ERROR_CHECK(esp_netif_set_default_netif(openthread_netif));
    esp_cli_custom_command_init();
#if CONFIG_OPENTHREAD_BR_AUTO_START
#if !CONFIG_EXAMPLE_CONNECT_WIFI && !CONFIG_EXAMPLE_CONNECT_ETHERNET
#error No backbone netif!
#endif
    esp_openthread_lock_release();
    ESP_ERROR_CHECK(example_connect());
#if CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
#endif
    esp_openthread_lock_acquire(portMAX_DELAY);
    esp_openthread_set_backbone_netif(get_example_netif());
    ESP_ERROR_CHECK(esp_openthread_border_router_init());
    otOperationalDatasetTlvs dataset;
    otError error = otDatasetGetActiveTlvs(esp_openthread_get_instance(), &dataset);
    ESP_ERROR_CHECK(esp_openthread_auto_start((error == OT_ERROR_NONE) ? &dataset : NULL));
#endif // CONFIG_OPENTHREAD_BR_AUTO_START
    esp_openthread_lock_release();

    // Run the main loop
    esp_openthread_cli_create_task();
    esp_openthread_launch_mainloop();

    // Clean up
    esp_netif_destroy(openthread_netif);
    esp_vfs_eventfd_unregister();
    esp_openthread_netif_glue_deinit();
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
