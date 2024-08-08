/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_ota_commands.h"

#include <string.h>

#include "esp_br_http_ota.h"
#include "esp_check.h"
#include "esp_openthread_border_router.h"
#include "esp_ot_cli_extension.h"
#include "freertos/idf_additions.h"
#include "openthread/cli.h"

static const char *s_server_cert = NULL;

static void print_help(void)
{
    otCliOutputFormat("rcp download ${server_url}\n");
}

static void ota_image_download_task(void *ctx)
{
    char *url = (char *)ctx;
    esp_err_t err = ESP_OK;
    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = s_server_cert,
        .event_handler = NULL,
        .keep_alive_enable = true,
    };
    err = esp_br_http_ota(&config);
    free(url);
    if (err != ESP_OK) {
        otCliOutputFormat("Failed to download image");
    } else {
        // OTA succeed, restart.
        esp_restart();
    }
    vTaskDelete(NULL);
}

otError esp_openthread_process_ota_command(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 0) {
        print_help();
    } else if (strcmp(aArgs[0], "download") == 0) {
        if (aArgsLength != 2) {
            print_help();
        } else {
            char *url = strdup(aArgs[1]);
            if (!url) {
                return OT_ERROR_NO_BUFS;
            }
            xTaskCreate(ota_image_download_task, "ota_image_download", 3072, url, 5, NULL);
        }
    } else {
        print_help();
    }
    return OT_ERROR_NONE;
}

void esp_set_ota_server_cert(const char *cert)
{
    s_server_cert = cert;
}
