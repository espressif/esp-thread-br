#include "esp_rcp_update_commands.h"

#include <string.h>

#include "esp_openthread_border_router.h"
#include "esp_rcp_http_download.h"
#include "esp_rcp_update.h"
#include "openthread/cli.h"

static const char *s_server_cert = NULL;

static void print_help(void)
{
    otCliOutputFormat("rcp download ${server_url}");
}

void esp_openthread_process_rcp_command(void *aContext, uint8_t aArgsLength, char *aArgs[])
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
            if (esp_rcp_download_image(&config, esp_rcp_get_firmware_dir()) != ESP_OK) {
                otCliOutputFormat("Failed to download image");
            }
        }
    } else if (strcmp(aArgs[0], "apply") == 0) {
        esp_openthread_rcp_deinit();
        esp_rcp_update();
        esp_restart();
    } else {
        print_help();
    }
}

void esp_set_rcp_server_cert(const char *cert)
{
    s_server_cert = cert;
}
