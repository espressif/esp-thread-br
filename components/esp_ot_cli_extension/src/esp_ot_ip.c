/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_ip.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_ot_cli_extension.h"
#include "esp_ot_wifi_cmd.h"
#include "stdlib.h"
#include "string.h"
#include "freertos/portmacro.h"
#include "lwip/inet.h"
#include "lwip/mld6.h"
#include "lwip/netif.h"
#include "openthread/cli.h"

#if CONFIG_OPENTHREAD_CLI_WIFI
ESP_EVENT_DECLARE_BASE(WIFI_ADDRESS_EVENT);
#endif

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
            struct mld_group *group = netif_mld6_data(netif);
            while (group != NULL) {
                otCliOutputFormat("%c%c inet6: %s\n", netif->name[0], netif->name[1], inet6_ntoa(group->group_address));
                group = group->next;
            }
            otCliOutputFormat("\n");
        } else {
            otCliOutputFormat("down\n\n");
        }
    }
}

bool is_netif_exist(const char *if_name)
{
    if (strlen(if_name) != 2) {
        otCliOutputFormat("no such netif: %s\n", if_name);
        return false;
    }
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
#if CONFIG_OPENTHREAD_CLI_WIFI
    } else if (strcmp(if_name, "st") == 0) {
        switch (type) {
        case UNICAST_DEL:
            esp_event_post(WIFI_ADDRESS_EVENT, WIFI_ADDRESS_EVENT_REMOVE_IP6, add_addr, sizeof(*add_addr),
                           portMAX_DELAY);
            break;
        case UNICAST_ADD:
            esp_event_post(WIFI_ADDRESS_EVENT, WIFI_ADDRESS_EVENT_ADD_IP6, add_addr, sizeof(*add_addr), portMAX_DELAY);
            break;
        case MULTICAST_DEL:
            esp_event_post(WIFI_ADDRESS_EVENT, WIFI_ADDRESS_EVENT_MULTICAST_GROUP_LEAVE, add_addr, sizeof(*add_addr),
                           0);
            break;
        case MULTICAST_ADD:
            esp_event_post(WIFI_ADDRESS_EVENT, WIFI_ADDRESS_EVENT_MULTICAST_GROUP_JOIN, add_addr, sizeof(*add_addr), 0);
            break;
        default:
            break;
        }
#endif
    } else {
        otCliOutputFormat("Not support this interface: %s\n", if_name);
    }
}

otError esp_ot_process_ip(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 0) {
        otCliOutputFormat("---ip parameter---\n");
        otCliOutputFormat("print                    :     print all ip on each interface of lwip\n");
        otCliOutputFormat("add <ifname> <ifaddr>    :     add an address onto an interface of lwip\n");
        otCliOutputFormat("del <ifname> <ifaddr>    :     delete an address from an interface of lwip\n");
    } else {
        if (strcmp(aArgs[0], "print") == 0) {
            print_ip_address();
        } else if (strcmp(aArgs[0], "add") == 0) {
            if (aArgsLength != 3) {
                return OT_ERROR_INVALID_ARGS;
            }
            char dest_addr[128];
            strncpy(dest_addr, aArgs[2], strlen(aArgs[2]) + 1);
            ip_event_add_ip6_t add_addr;
            inet6_aton(dest_addr, &add_addr.addr);
            esp_lwip_add_del_ip(&add_addr, aArgs[1], true);
        } else if (strcmp(aArgs[0], "del") == 0) {
            if (aArgsLength != 3) {
                return OT_ERROR_INVALID_ARGS;
            }
            char dest_addr[128];
            strncpy(dest_addr, aArgs[2], strlen(aArgs[2]));
            ip_event_add_ip6_t add_addr;
            inet6_aton(dest_addr, &add_addr.addr);
            esp_lwip_add_del_ip(&add_addr, aArgs[1], false);
        } else {
            otCliOutputFormat("Invalid args\n");
            return OT_ERROR_INVALID_ARGS;
        }
    }
    return OT_ERROR_NONE;
}
