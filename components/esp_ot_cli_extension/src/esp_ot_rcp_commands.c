/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_ota_commands.h"

#include <string.h>

#include "esp_check.h"
#include "esp_openthread_border_router.h"
#include "esp_ot_cli_extension.h"
#include "esp_rcp_update.h"
#include "openthread/cli.h"

#if CONFIG_OPENTHREAD_RCP_CLI
static esp_err_t join_args(char *out, size_t out_len, uint8_t start, uint8_t argc, char *argv[])
{
    size_t used = 0;
    out[0] = '\0';

    for (uint8_t i = start; i < argc; i++) {
        const char *part = argv[i] ? argv[i] : "";
        size_t plen = strlen(part);

        if (i > start) {
            if (used + 1 >= out_len) {
                return ESP_ERR_INVALID_SIZE;
            }
            out[used++] = ' ';
        }

        if (used + plen >= out_len) {
            return ESP_ERR_INVALID_SIZE;
        }

        memcpy(out + used, part, plen);
        used += plen;
    }

    out[used] = '\0';
    return ESP_OK;
}
#endif // CONFIG_OPENTHREAD_RCP_CLI

otError esp_openthread_process_rcp_command(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 0) {
        otCliOutputFormat("---otrcp parameter---\n");
        otCliOutputFormat("update               :    process updating the rcp\n");
#if CONFIG_OPENTHREAD_RCP_CLI
        otCliOutputFormat("<text>               :    send command to be run on rcp\n");
#endif
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("update the ot rcp    :    otrcp update\n");
#if CONFIG_OPENTHREAD_RCP_CLI
        otCliOutputFormat("run help cmd on rcp  :    otrcp help\n");
#endif
    } else if (strcmp(aArgs[0], "update") == 0) {
        otInstance *ins = esp_openthread_get_instance();
        ESP_RETURN_ON_FALSE(otThreadGetDeviceRole(ins) == OT_DEVICE_ROLE_DISABLED, OT_ERROR_INVALID_STATE,
                            OT_EXT_CLI_TAG, "Thread is not disabled");
        ESP_RETURN_ON_FALSE(!otIp6IsEnabled(ins), OT_ERROR_INVALID_STATE, OT_EXT_CLI_TAG,
                            "OT interface is not disabled");
        ESP_RETURN_ON_FALSE(esp_openthread_rcp_deinit() == ESP_OK, OT_ERROR_FAILED, OT_EXT_CLI_TAG,
                            "Fail to deinitialize RCP");
        if (esp_rcp_update() == ESP_OK) {
            esp_rcp_mark_image_verified(true);
        } else {
            esp_rcp_mark_image_verified(false);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_RETURN_ON_FALSE(esp_openthread_rcp_init() == ESP_OK, OT_ERROR_FAILED, OT_EXT_CLI_TAG,
                            "Fail to initialize RCP");
    } else {
#if CONFIG_OPENTHREAD_RCP_CLI
        char buf[256];
        ESP_RETURN_ON_ERROR(join_args(buf, sizeof(buf), 0, aArgsLength, aArgs), OT_EXT_CLI_TAG, "command too long");
        esp_openthread_rcp_send_command(buf);
#else
        otCliOutputFormat("invalid commands\n");
#endif
    }
    return OT_ERROR_NONE;
}
