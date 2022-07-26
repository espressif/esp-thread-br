/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_ot_ota_commands.h"
#include "esp_ot_wifi_cmd.h"
#include "esp_spiffs.h"
#include "esp_vfs_eventfd.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

#include "border_router_launch.h"
#include "esp_br_web.h"

#define TAG "esp_ot_br"

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs", .partition_label = NULL, .max_files = 10, .format_if_mount_failed = false};
    return esp_vfs_spiffs_register(&conf);
}

void app_main(void)
{
    // Used eventfds:
    // * netif
    // * task queue
    // * border router
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    esp_openthread_platform_config_t platform_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    esp_rcp_update_config_t rcp_update_config = ESP_OPENTHREAD_RCP_UPDATE_CONFIG();
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(init_spiffs());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if CONFIG_OPENTHREAD_BR_AUTO_START
    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    esp_openthread_set_backbone_netif(get_example_netif());
#else
    esp_ot_wifi_netif_init();
    esp_openthread_set_backbone_netif(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
#endif

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp-ot-br"));
    esp_set_ota_server_cert((char *)server_cert_pem_start);
    launch_openthread_border_router(&platform_config, &rcp_update_config);

#if CONFIG_OPENTHREAD_BR_START_WEB
    esp_br_web_start("/spiffs");
#endif
}
