/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "esp_coexist.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_net_stack.h"
#include "esp_netif_types.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "esp_ot_cli_extension.h"
#include "esp_wifi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "openthread/cli.h"

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_IP4_BIT = BIT0;
const int CONNECTED_IP6_BIT = BIT1;
static bool s_wifi_connected = false;
static uint32_t s_wifi_disconnect_delay_ms = 0;
ESP_EVENT_DEFINE_BASE(WIFI_ADDRESS_EVENT);

void esp_ot_wifi_netif_init(void)
{
    esp_netif_t *esp_netif = esp_netif_create_default_wifi_sta();
    assert(esp_netif);
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_disconnect_delay_ms) {
            vTaskDelay(s_wifi_disconnect_delay_ms / portTICK_PERIOD_MS);
            s_wifi_disconnect_delay_ms = 0;
        }
        esp_wifi_connect();
        otCliOutputFormat("wifi reconnecting...\n");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, CONNECTED_IP4_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        esp_netif_create_ip6_linklocal(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
        xEventGroupSetBits(wifi_event_group, CONNECTED_IP6_BIT);
    }
}

void handle_wifi_addr_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_ADDRESS_EVENT, WIFI_ADDRESS_EVENT_ADD_IP6,
                                               esp_netif_action_add_ip6_address,
                                               esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_ADDRESS_EVENT, WIFI_ADDRESS_EVENT_REMOVE_IP6,
                                               esp_netif_action_remove_ip6_address,
                                               esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_ADDRESS_EVENT, WIFI_ADDRESS_EVENT_MULTICAST_GROUP_JOIN,
                                               esp_netif_action_join_ip6_multicast_group,
                                               esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_ADDRESS_EVENT, WIFI_ADDRESS_EVENT_MULTICAST_GROUP_LEAVE,
                                               esp_netif_action_leave_ip6_multicast_group,
                                               esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")));
}

static void wifi_join(const char *ssid, const char *psk)
{
    static bool s_initialized = false;
    if (!s_initialized) {
        wifi_event_group = xEventGroupCreate();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        handle_wifi_addr_init();

#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE && CONFIG_OPENTHREAD_RADIO_NATIVE
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
        ESP_ERROR_CHECK(esp_coex_wifi_i154_enable());
#else
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
#endif
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &event_handler, NULL));
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_wifi_set_mode(WIFI_MODE_NULL);
        s_initialized = true;
    }
    esp_wifi_start();
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (psk) {
        strncpy((char *)wifi_config.sta.password, psk, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();

    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_IP4_BIT | CONNECTED_IP6_BIT, pdFALSE, pdTRUE,
                                   10000 / portTICK_PERIOD_MS);

    if (((bits & CONNECTED_IP4_BIT) != 0) && ((bits & CONNECTED_IP6_BIT) != 0)) {
        s_wifi_connected = true;
        xEventGroupClearBits(wifi_event_group, CONNECTED_IP4_BIT);
        xEventGroupClearBits(wifi_event_group, CONNECTED_IP6_BIT);
        esp_openthread_lock_acquire(portMAX_DELAY);
        esp_openthread_border_router_init();
        esp_openthread_lock_release();
        otCliOutputFormat("wifi sta is connected successfully\n");
    } else {
        esp_wifi_disconnect();
        esp_wifi_stop();
        ESP_LOGW("", "Connection time out, please check your ssid & password, then retry.");
        otCliOutputFormat("wifi sta connection is failed\n");
    }
}

otError esp_ot_process_wifi_cmd(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    char ssid[100] = "";
    char psk[100] = "";
    (void)(aContext);
    if (aArgsLength == 0) {
        otCliOutputFormat("---wifi parameter---\n");
        otCliOutputFormat(
            "connect -s <ssid> -p <psk>               :       connect to a wifi network with an ssid and a psk\n");
        otCliOutputFormat("connect -s <ssid>                        :       connect to a wifi network with an ssid\n");
        otCliOutputFormat("disconnect                               :       wifi disconnect once, only for test\n");
        otCliOutputFormat("disconnect <delay>                       :       wifi disconnect, and reconnect after "
                          "delay(ms), only for test\n");
        otCliOutputFormat("state                                    :       get wifi state, disconnect or connect\n");
        otCliOutputFormat("mac <role>                               :       get mac address of wifi netif, <role> can "
                          "be 'sta' or 'ap'\n");
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("join a wifi:\n");
        otCliOutputFormat("ssid: threadcertAP\n");
        otCliOutputFormat(
            "psk: threadcertAP                        :       wifi connect -s threadcertAP -p threadcertAP\n");
        otCliOutputFormat("join a wifi:\n");
        otCliOutputFormat("ssid: threadAP\n");
        otCliOutputFormat("does not have a psk                      :       wifi connect -s threadAP\n");
        otCliOutputFormat("get wifi state                           :       wifi state\n");
        otCliOutputFormat("wifi disconnect once                     :       wifi disconnect\n");
        otCliOutputFormat("wifi disconnec, and reconnect after 2s   :       wifi disconnect 2000\n");
        otCliOutputFormat("get mac address of WiFi station          :       wifi mac sta\n");
        otCliOutputFormat("get mac address of WiFi soft-AP          :       wifi mac ap\n");
    } else if (strcmp(aArgs[0], "connect") == 0) {
        for (int i = 1; i < aArgsLength; i++) {
            if (strcmp(aArgs[i], "-s") == 0) {
                i++;
                strcpy(ssid, aArgs[i]);
                otCliOutputFormat("ssid: %s\n", ssid);
            } else if (strcmp(aArgs[i], "-p") == 0) {
                i++;
                strcpy(psk, aArgs[i]);
                otCliOutputFormat("psk: %s\n", psk);
            }
        }
        if (!s_wifi_connected) {
            wifi_join(ssid, psk);
        } else {
            otCliOutputFormat("wifi has already connected\n");
        }
    } else if (strcmp(aArgs[0], "state") == 0) {
        if (s_wifi_connected) {
            otCliOutputFormat("connected\n");
        } else {
            otCliOutputFormat("disconnected\n");
        }
    } else if (strcmp(aArgs[0], "disconnect") == 0) {
        if (aArgsLength == 2) {
            s_wifi_disconnect_delay_ms = atoi(aArgs[1]);
            otCliOutputFormat("wifi reconnect delay in ms: %d\n", s_wifi_disconnect_delay_ms);
        } else if (aArgsLength > 2) {
            otCliOutputFormat("invalid commands\n");
        }
        if (s_wifi_connected) {
            esp_wifi_disconnect();
        } else {
            otCliOutputFormat("wifi is not ready, please connect wifi first\n");
        }
    } else if (strcmp(aArgs[0], "mac") == 0) {
        uint8_t mac[6];
        if (aArgsLength != 2) {
            return OT_ERROR_INVALID_ARGS;
        }
        esp_err_t error = ESP_OK;
        if (strcmp(aArgs[1], "sta") == 0) {
            error = esp_read_mac(mac, ESP_MAC_WIFI_STA);
        } else if (strcmp(aArgs[1], "ap") == 0) {
            error = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        } else {
            otCliOutputFormat("invalid arguments: %s\n", aArgs[1]);
            return OT_ERROR_INVALID_ARGS;
        }
        if (error == ESP_OK) {
            for (int i = 0; i < 5; i++) {
                otCliOutputFormat("%02x:", mac[i]);
            }
            otCliOutputFormat("%02x\n", mac[5]);
        } else {
            otCliOutputFormat("Fail to get the mac address\n");
        }
    } else {
        otCliOutputFormat("invalid commands\n");
    }
    return OT_ERROR_NONE;
}
