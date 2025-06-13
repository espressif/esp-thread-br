/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

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
#include "mdns.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

#include "border_router_launch.h"
#include "border_router_m5stack.h"
#include "esp_br_web.h"

#if CONFIG_EXTERNAL_COEX_ENABLE
#include "esp_coexist.h"
#endif

#define TAG "esp_ot_br"

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static esp_err_t init_spiffs(void)
{
#if CONFIG_AUTO_UPDATE_RCP
    esp_vfs_spiffs_conf_t rcp_fw_conf = {.base_path = "/" CONFIG_RCP_PARTITION_NAME,
                                         .partition_label = CONFIG_RCP_PARTITION_NAME,
                                         .max_files = 10,
                                         .format_if_mount_failed = false};
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&rcp_fw_conf), TAG, "Failed to mount rcp firmware storage");
#endif
#if CONFIG_OPENTHREAD_BR_START_WEB
    esp_vfs_spiffs_conf_t web_server_conf = {
        .base_path = "/spiffs", .partition_label = "web_storage", .max_files = 10, .format_if_mount_failed = false};
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&web_server_conf), TAG, "Failed to mount web storage");
#endif
    return ESP_OK;
}

#if CONFIG_EXTERNAL_COEX_ENABLE
static void ot_br_external_coexist_init(void)
{
    esp_external_coex_gpio_set_t gpio_pin = ESP_OPENTHREAD_DEFAULT_EXTERNAL_COEX_CONFIG();
    esp_external_coex_set_work_mode(EXTERNAL_COEX_LEADER_ROLE);
    ESP_ERROR_CHECK(esp_enable_extern_coex_gpio_pin(CONFIG_EXTERNAL_COEX_WIRE_TYPE, gpio_pin));
}
#endif /* CONFIG_EXTERNAL_COEX_ENABLE */

void app_main(void)
{
    // Used eventfds:
    // * netif
    // * task queue
    // * border router
    size_t max_eventfd = 3;

#if CONFIG_OPENTHREAD_RADIO_SPINEL_SPI
    // * SpiSpinelInterface (The Spi Spinel Interface needs an eventfd.)
    max_eventfd++;
#endif
#if CONFIG_OPENTHREAD_RADIO_TREL
    // * TREL reception (The Thread Radio Encapsulation Link needs an eventfd for reception.)
    max_eventfd++;
#endif
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = max_eventfd,
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

#if CONFIG_EXAMPLE_CONNECT_ETHERNET || !CONFIG_EXAMPLE_CONNECT_WIFI
#error Ethernet is not supported on M5Stack. Please use Wi-Fi instead.
#endif

#if CONFIG_OPENTHREAD_RADIO_SPINEL_SPI
// TODO: Support configuring the M5Stack's Radio Spinel to work in SPI mode
#error Radio Spinel over SPI is not supported on M5Stack. Use UART instead.
#endif

#if CONFIG_EXTERNAL_COEX_ENABLE
    ot_br_external_coexist_init();
#endif // CONFIG_EXTERNAL_COEX_ENABLE

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp-ot-br"));
#if CONFIG_OPENTHREAD_CLI_OTA
    esp_set_ota_server_cert((char *)server_cert_pem_start);
#endif

#if CONFIG_OPENTHREAD_BR_START_WEB
    esp_br_web_start("/spiffs");
#endif

    launch_openthread_border_router(&platform_config, &rcp_update_config);
    ESP_ERROR_CHECK(border_router_m5stack_init());
}
