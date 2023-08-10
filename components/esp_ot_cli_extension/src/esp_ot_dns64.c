/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_dns64.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_openthread.h"
#include "esp_openthread_dns64.h"
#include "esp_ot_cli_extension.h"
#include "lwip/dns.h"
#include "openthread/cli.h"
#include "openthread/netdata.h"

#define DNS_SERVER_ALTERNATIVE_INDEX 1

static esp_err_t set_dns64(const ip4_addr_t *dns_server)
{
    ip_addr_t dns_server_addr = {};

    dns_server_addr.type = IPADDR_TYPE_V6;

    ESP_RETURN_ON_FALSE(esp_openthread_get_nat64_prefix(&dns_server_addr.u_addr.ip6) == ESP_OK, ESP_ERR_NOT_FOUND,
                        OT_EXT_CLI_TAG, "Cannot find NAT64 prefix");
    dns_server_addr.u_addr.ip6.addr[3] = dns_server->addr;
    dns_setserver(DNS_SERVER_ALTERNATIVE_INDEX, &dns_server_addr);

    ESP_RETURN_ON_ERROR(esp_event_post(OPENTHREAD_EVENT, OPENTHREAD_EVENT_SET_DNS_SERVER, NULL, 0, 0), OT_EXT_CLI_TAG,
                        "Failed to post OpenThread set DNS server event");
    return ESP_OK;
}

otError esp_openthread_process_dns64_server(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    ESP_RETURN_ON_FALSE(aArgsLength != 0, OT_ERROR_INVALID_ARGS, OT_EXT_CLI_TAG, "dns64server DNS_SERVER_URL");
    ip4_addr_t server_addr;

    ESP_RETURN_ON_FALSE(ip4addr_aton(aArgs[0], &server_addr) == 1, OT_ERROR_INVALID_ARGS, OT_EXT_CLI_TAG,
                        "Invalid DNS server");
    ESP_RETURN_ON_FALSE(set_dns64(&server_addr) == ESP_OK, OT_ERROR_INVALID_ARGS, OT_EXT_CLI_TAG,
                        "Failed to set DNS server");

    return OT_ERROR_NONE;
}
