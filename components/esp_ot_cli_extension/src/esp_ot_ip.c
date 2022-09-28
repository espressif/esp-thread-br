/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_ip.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread_netif_glue.h"
#include "esp_ot_cli_extension.h"
#include "stdlib.h"
#include "string.h"
#include "freertos/portmacro.h"
#include "lwip/inet.h"
#include "lwip/netif.h"
#include "openthread/cli.h"

static void print_ip_address(void)
{
    struct netif *netif;
    NETIF_FOREACH(netif)
    {
        otCliOutputFormat("netif: %c%c\n", netif->name[0], netif->name[1]);
        if (netif_is_up(netif) && netif_is_link_up(netif)) {
            for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
                if (netif->ip6_addr_state[i]) {
                    otCliOutputFormat("%c%c inet6: %s %d\n", netif->name[0], netif->name[1],
                                      inet6_ntoa(*ip_2_ip6(&netif->ip6_addr[i])), netif->ip6_addr_state[i]);
                }
            }
        } else {
            otCliOutputFormat("down\n");
        }
    }
}

bool is_netif_exist(const char *if_name)
{
    struct netif *netif;
    NETIF_FOREACH(netif)
    {
        if (netif_is_up(netif) && netif_is_link_up(netif)) {
            if (if_name[0] == netif->name[0] && if_name[1] == netif->name[1]) {
                return true;
            }
        }
    }
    otCliOutputFormat("no such netif: %s\n", if_name);
    return false;
}

void esp_lwip_add_del_ip(ip_event_add_ip6_t *add_addr, const char *if_name, bool is_add)
{
    if (!is_netif_exist(if_name)) {
        return;
    }
    bool is_multicast = (add_addr->addr.addr[0] & 0xff) == 0xff;
    uint8_t type = ((uint8_t)is_multicast << 1) + (uint8_t)is_add;

    // TODO: Support adding or deleting addresses of other interface, such as wifi interface.
    if (strcmp(if_name, "ot") == 0) {
        switch (type) {
        case UNICAST_DEL:
            esp_event_post(OPENTHREAD_EVENT, OPENTHREAD_EVENT_LOST_IP6, add_addr, sizeof(*add_addr), portMAX_DELAY);
            break;
        case UNICAST_ADD:
            esp_event_post(OPENTHREAD_EVENT, OPENTHREAD_EVENT_GOT_IP6, add_addr, sizeof(*add_addr), portMAX_DELAY);
            break;
        case MULTICAST_DEL:
            esp_event_post(OPENTHREAD_EVENT, OPENTHREAD_EVENT_MULTICAST_GROUP_LEAVE, add_addr, sizeof(*add_addr), 0);
            break;
        case MULTICAST_ADD:
            esp_event_post(OPENTHREAD_EVENT, OPENTHREAD_EVENT_MULTICAST_GROUP_JOIN, add_addr, sizeof(*add_addr), 0);
            break;
        default:
            break;
        }
    } else {
        otCliOutputFormat("cannot add address onto: %s\n", if_name);
    }
}

otError esp_ot_process_ip(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    (void)(aContext);
    if (aArgsLength == 0) {
        otCliOutputFormat("---ip parameter---\n");
        otCliOutputFormat("-p                         :     print all ip on each interface of lwip\n");
        otCliOutputFormat("-a <ifaddr> dev <ifname>   :     add an address onto an interface of lwip\n");
        otCliOutputFormat("-d <ifaddr> dev <ifname>   :     delete an address from an interface of lwip\n");
    } else {
        if (strcmp(aArgs[0], "-p") == 0) {
            print_ip_address();
        } else if (strcmp(aArgs[0], "-a") == 0 && strcmp(aArgs[2], "dev") == 0) {
            char dest_addr[128];
            strncpy(dest_addr, aArgs[1], strlen(aArgs[1]) + 1);
            ip_event_add_ip6_t add_addr;
            inet6_aton(dest_addr, &add_addr.addr);
            esp_lwip_add_del_ip(&add_addr, aArgs[3], true);
        } else if (strcmp(aArgs[0], "-d") == 0 && strcmp(aArgs[2], "dev") == 0) {
            char dest_addr[128];
            strncpy(dest_addr, aArgs[1], strlen(aArgs[1]));
            ip_event_add_ip6_t add_addr;
            inet6_aton(dest_addr, &add_addr.addr);
            esp_lwip_add_del_ip(&add_addr, aArgs[3], false);
        } else {
            otCliOutputFormat("Invalid args\n");
            return OT_ERROR_INVALID_ARGS;
        }
    }
    return OT_ERROR_NONE;
}
