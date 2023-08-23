/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_loglevel.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ot_cli_extension.h"
#include "string.h"
#include <stdlib.h>
#include "openthread/cli.h"
#include "openthread/error.h"

esp_err_t ext_loglevel_set(const char *tag, const char *level)
{
    int levelset = atoi(level);
    if (levelset < ESP_LOG_NONE || levelset > ESP_LOG_VERBOSE || (levelset == ESP_LOG_NONE && strcmp(level, "0"))) {
        otCliOutputFormat("A wrong log level \"%s\"\n", level);
        return ESP_FAIL;
    }
    esp_log_level_set(tag, (esp_log_level_t)levelset);
    return ESP_OK;
}

otError esp_ot_process_logset(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    (void)(aContext);
    if (aArgsLength == 0) {
        otCliOutputFormat("---loglevel---\n");
        otCliOutputFormat("set <TAG> <level>                :       set log level of the <TAG> to <level>\n");
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("loglevel set * 3                 :       set log level of all the tags to INFO\n");
        otCliOutputFormat("loglevel set OPENTHREAD 0        :       set log level of OpenThread to None\n");
        otCliOutputFormat("loglevel set wifi 4              :       set log level of Wi-Fi to DEBUG\n");
        otCliOutputFormat("----Note----\n");
        otCliOutputFormat(
            "Support 6 levels         :       0(NONE), 1(ERROR), 2(WARN), 3(INFO), 4(DEBUG), 5(VERBOSE)\n");
        otCliOutputFormat("The log level of the tags cannot be bigger than the maximum log level\n");
        otCliOutputFormat("You can set the maximum log level via:\n");
        otCliOutputFormat("idf.py menuconfig >  Component config---> Log output---> Maximum log verbosity\n");
    } else {
        if (strcmp(aArgs[0], "set") == 0 && aArgsLength == 3) {
            ESP_RETURN_ON_FALSE(ext_loglevel_set(aArgs[1], aArgs[2]) == ESP_OK, OT_ERROR_INVALID_ARGS, OT_EXT_CLI_TAG,
                                "Failed to set log level");
        } else {
            return OT_ERROR_INVALID_ARGS;
        }
    }
    return OT_ERROR_NONE;
}
