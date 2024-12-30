/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_dns64.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif_types.h"
#include "esp_openthread.h"
#include "esp_openthread_dns64.h"
#include "esp_openthread_netif_glue.h"
#include "esp_ot_cli_extension.h"
#include "lwip/dns.h"
#include "openthread/cli.h"
#include "openthread/netdata.h"

#define DNS_SERVER_ALTERNATIVE_INDEX 1

static esp_err_t set_dns64(esp_netif_dns_type_t dns_type, const ip4_addr_t *dns_server)
{
    esp_netif_t *openthread_netif = esp_openthread_get_netif();
    ESP_RETURN_ON_FALSE(openthread_netif, ESP_ERR_ESP_NETIF_IF_NOT_READY, OT_EXT_CLI_TAG,
                        "openthread netif is not initializd");
    ip6_addr_t dns_server_addr = {};
    ESP_RETURN_ON_ERROR(esp_openthread_get_nat64_prefix(&dns_server_addr), OT_EXT_CLI_TAG, "Cannot find NAT64 prefix");
    dns_server_addr.addr[3] = dns_server->addr;
    ESP_RETURN_ON_ERROR(esp_openthread_set_dnsserver_addr_with_type(dns_server_addr, dns_type), OT_EXT_CLI_TAG,
                        "Failed to set dns server address");
    ESP_RETURN_ON_ERROR(esp_event_post(OPENTHREAD_EVENT, OPENTHREAD_EVENT_SET_DNS_SERVER, NULL, 0, 0), OT_EXT_CLI_TAG,
                        "Failed to post OpenThread set DNS server event");
    return ESP_OK;
}

static esp_netif_dns_type_t parse_dns_type(const char *type)
{
    if (strcmp(type, "main") == 0) {
        return ESP_NETIF_DNS_MAIN;
    }
    if (strcmp(type, "backup") == 0) {
        return ESP_NETIF_DNS_BACKUP;
    }
    if (strcmp(type, "fallback") == 0) {
        return ESP_NETIF_DNS_FALLBACK;
    }
    return ESP_NETIF_DNS_MAX;
}

otError esp_openthread_process_dns64_server(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    (void)(aContext);
    if (aArgsLength == 0) {
        otCliOutputFormat("---dns64server command parameter---\n");
        otCliOutputFormat("dns64server <dns_server_addr> [dns_type]\n");
        otCliOutputFormat("<dns_server_addr> should be an IPv4 address\n");
        otCliOutputFormat(
            "[dns_type] can be 'main', 'backup', or 'fallback'.If not specified, it defaults to 'backup'\n");
    } else {
        ESP_RETURN_ON_FALSE(aArgsLength == 2 || aArgsLength == 1, OT_ERROR_INVALID_ARGS, OT_EXT_CLI_TAG,
                            "Invalid parameters");
        ip4_addr_t server_addr;
        ESP_RETURN_ON_FALSE(ip4addr_aton(aArgs[0], &server_addr) == 1, OT_ERROR_INVALID_ARGS, OT_EXT_CLI_TAG,
                            "Invalid DNS server");
        esp_netif_dns_type_t dns_type = ESP_NETIF_DNS_BACKUP;
        if (aArgsLength == 2) {
            dns_type = parse_dns_type(aArgs[1]);
            ESP_RETURN_ON_FALSE(dns_type != ESP_NETIF_DNS_MAX, OT_ERROR_INVALID_ARGS, OT_EXT_CLI_TAG,
                                "Invalid DNS type");
        }
        ESP_RETURN_ON_FALSE(set_dns64(dns_type, &server_addr) == ESP_OK, OT_ERROR_INVALID_ARGS, OT_EXT_CLI_TAG,
                            "Failed to set DNS server");
    }

    return OT_ERROR_NONE;
}
