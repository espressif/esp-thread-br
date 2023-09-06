/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_ota_commands.h"

#include <string.h>

#include "esp_br_http_ota.h"
#include "esp_openthread_border_router.h"
#include "esp_rcp_update.h"
#include "openthread/cli.h"

static const char *s_server_cert = NULL;

static void print_help(void)
{
    otCliOutputFormat("rcp download ${server_url}");
}

otError esp_openthread_process_ota_command(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 0) {
        print_help();
    } else if (strcmp(aArgs[0], "download") == 0) {
        if (aArgsLength != 2) {
            print_help();
        } else {
            char *url = aArgs[1];
            esp_http_client_config_t config = {
                .url = url,
                .cert_pem = s_server_cert,
                .event_handler = NULL,
                .keep_alive_enable = true,
            };
            if (esp_br_http_ota(&config) != ESP_OK) {
                otCliOutputFormat("Failed to download image");
            }
        }
        esp_restart();
    } else if (strcmp(aArgs[0], "rcpupdate") == 0) {
        esp_openthread_rcp_deinit();
        if (esp_rcp_update() == ESP_OK) {
            esp_rcp_mark_image_verified(true);
        } else {
            esp_rcp_mark_image_verified(false);
        }
        esp_restart();
    } else {
        print_help();
    }
    return OT_ERROR_NONE;
}

void esp_set_ota_server_cert(const char *cert)
{
    s_server_cert = cert;
}
