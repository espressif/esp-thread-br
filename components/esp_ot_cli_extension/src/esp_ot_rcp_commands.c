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

otError esp_openthread_process_rcp_command(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 0) {
        otCliOutputFormat("---otrcp parameter---\n");
        otCliOutputFormat("update               :    process updating the rcp\n");
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("update the ot rcp    :    otrcp update\n");
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
        otCliOutputFormat("invalid commands\n");
    }
    return OT_ERROR_NONE;
}
