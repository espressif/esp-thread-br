/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_ot_wifi_cmd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

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
#include "example_common_private.h"
#include "nvs.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "openthread/cli.h"

#ifndef EXAMPLE_WIFI_SCAN_METHOD
#if CONFIG_EXAMPLE_WIFI_SCAN_METHOD_FAST
#define EXAMPLE_WIFI_SCAN_METHOD WIFI_FAST_SCAN
#elif CONFIG_EXAMPLE_WIFI_SCAN_METHOD_ALL_CHANNEL
#define EXAMPLE_WIFI_SCAN_METHOD WIFI_ALL_CHANNEL_SCAN
#endif
#endif

#ifndef EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD
#if CONFIG_EXAMPLE_WIFI_CONNECT_AP_BY_SIGNAL
#define EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#elif CONFIG_EXAMPLE_WIFI_CONNECT_AP_BY_SECURITY
#define EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SECURITY
#endif
#endif

#ifndef EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD
#if CONFIG_EXAMPLE_WIFI_AUTH_OPEN
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_EXAMPLE_WIFI_AUTH_WEP
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA2_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA_WPA2_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA2_ENTERPRISE
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_ENTERPRISE
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA3_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA2_WPA3_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WAPI_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif
#endif

static bool s_wifi_initialized = false;
static esp_ot_wifi_state_t s_wifi_state = OT_WIFI_DISCONNECTED;
static bool s_border_router_initialized = false;
static bool s_wifi_handler_registered = false;
static uint16_t wifi_conn_retry_nums = 0;
const char wifi_state_string[3][20] = {"disconnected", "connected", "reconnecting"};
static nvs_handle_t s_wifi_config_nvs_handle = 0;
#define SSIDKEY "ssid"
#define PASSWORDKEY "password"
#define SSIDMAXLEN 32
#define PASSWORDMAXLEN 64
ESP_EVENT_DEFINE_BASE(WIFI_ADDRESS_EVENT);

static void handle_wifi_addr_init(void)
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

static void handler_on_wifi_disconnect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    wifi_conn_retry_nums++;
    if (wifi_conn_retry_nums > CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY) {
        wifi_conn_retry_nums = 0;
        s_wifi_state = OT_WIFI_DISCONNECTED;
    } else {
        s_wifi_state = OT_WIFI_RECONNECTING;
    }
}

static void handler_on_wifi_connect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    wifi_conn_retry_nums = 0;
    s_wifi_state = OT_WIFI_CONNECTED;
}

static esp_err_t wifi_join(const char *ssid, const char *password)
{
    ESP_LOGI(OT_EXT_CLI_TAG, "Start example_connect");

    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = "",
                .password = "",
                .scan_method = EXAMPLE_WIFI_SCAN_METHOD,
                .sort_method = EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD,
                .threshold.rssi = CONFIG_EXAMPLE_WIFI_SCAN_RSSI_THRESHOLD,
                .threshold.authmode = EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            },
    };

    uint8_t ssid_len = strnlen(ssid, SSIDMAXLEN);
    uint8_t password_len = strnlen(password, PASSWORDMAXLEN);
    if (ssid_len < SSIDMAXLEN && password_len < PASSWORDMAXLEN) {
        memcpy(wifi_config.sta.ssid, (void *)ssid, ssid_len + 1);
        memcpy(wifi_config.sta.password, (void *)password, password_len + 1);
    } else {
        ESP_LOGE(OT_EXT_CLI_TAG, "Invalid ssid or password");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = example_wifi_sta_do_connect(wifi_config, true);
    return err;
}

static esp_err_t wifi_config_print(void)
{
    ESP_RETURN_ON_FALSE((s_wifi_config_nvs_handle != 0), ESP_FAIL, OT_EXT_CLI_TAG, "wifi_config NVS handle is invalid");
    char ssid_str[SSIDMAXLEN] = {0};

    if (esp_ot_wifi_config_get_ssid(ssid_str) != ESP_OK) {
        otCliOutputFormat("None Wi-Fi configurations\n");
        return ESP_FAIL;
    } else {
        otCliOutputFormat("Wi-Fi SSID: %s\n", ssid_str);
        char password_str[PASSWORDMAXLEN] = {0};
        esp_ot_wifi_config_get_password(password_str);
        if (strlen(password_str) > 0) {
            otCliOutputFormat("Wi-Fi password: %s\n", password_str);
        } else {
            otCliOutputFormat("None password\n", password_str);
        }
    }
    return ESP_OK;
}

void esp_ot_wifi_border_router_init_flag_set(bool initialized)
{
    s_border_router_initialized = initialized;
}

esp_err_t esp_ot_wifi_connect(const char *ssid, const char *password)
{
    if (!s_wifi_initialized) {
        example_wifi_start();
#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE && CONFIG_OPENTHREAD_RADIO_NATIVE
        ESP_ERROR_CHECK(esp_coex_wifi_i154_enable());
#endif
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
        handle_wifi_addr_init();
        s_wifi_initialized = true;
    }

    if (!s_wifi_handler_registered) {
        ESP_ERROR_CHECK(
            esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &handler_on_wifi_disconnect, NULL));
        ESP_ERROR_CHECK(
            esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &handler_on_wifi_connect, NULL));
        s_wifi_handler_registered = true;
    }

    esp_err_t err = wifi_join(ssid, password);
    if (err == ESP_OK) {
        char old_ssid[SSIDMAXLEN] = "";
        char old_password[PASSWORDMAXLEN] = "";
        esp_ot_wifi_config_get_ssid(old_ssid);
        esp_ot_wifi_config_get_password(old_password);
        if (strcmp(ssid, old_ssid) != 0 || strcmp(password, old_password) != 0) {
            if (esp_ot_wifi_config_set_ssid(ssid) != ESP_OK || esp_ot_wifi_config_set_password(password) != ESP_OK) {
                ESP_LOGE(OT_EXT_CLI_TAG, "Fail to save wifi ssid and password");
                assert(0);
            }
        }
    }
    return err;
}

esp_err_t esp_ot_wifi_disconnect(void)
{
    if (!s_wifi_initialized || s_wifi_state != OT_WIFI_CONNECTED) {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &handler_on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &handler_on_wifi_connect));
    s_wifi_handler_registered = false;
    s_wifi_state = OT_WIFI_DISCONNECTED;
    esp_err_t err = example_wifi_sta_do_disconnect();
    return err;
}

otError esp_ot_process_wifi_cmd(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    char ssid[SSIDMAXLEN] = "";
    char psk[PASSWORDMAXLEN] = "";
    (void)(aContext);
    if (aArgsLength == 0) {
        otCliOutputFormat("---wifi parameter---\n");
        otCliOutputFormat(
            "connect -s <ssid> -p <psk>               :       connect to a wifi network with an ssid and a psk\n");
        otCliOutputFormat("connect -s <ssid>                        :       connect to a wifi network with an ssid\n");
        otCliOutputFormat("disconnect                               :       wifi disconnect\n");
        otCliOutputFormat(
            "state                                    :       get wifi state, disconnect, connect or reconnecting\n");
        otCliOutputFormat("mac <role>                               :       get mac address of wifi netif, <role> can "
                          "be 'sta' or 'ap'\n");
        otCliOutputFormat("config                                   :       get stored wifi configurations\n");
        otCliOutputFormat("config clear                             :       clear stored wifi configurations\n");
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
        otCliOutputFormat("get mac address of Wi-Fi station         :       wifi mac sta\n");
        otCliOutputFormat("get mac address of Wi-Fi soft-AP         :       wifi mac ap\n");
        otCliOutputFormat("get stored wifi configurations           :       wifi config\n");
        otCliOutputFormat("clear stored wifi configurations         :       wifi config clear\n");
    } else if (strcmp(aArgs[0], "connect") == 0) {
        if (s_wifi_state != OT_WIFI_DISCONNECTED) {
            if (s_wifi_state == OT_WIFI_CONNECTED) {
                otCliOutputFormat("wifi has already connected!\n");
            } else if (s_wifi_state == OT_WIFI_RECONNECTING) {
                otCliOutputFormat("Command invalid because WiFi is reconnecting!\n");
            } else {
                otCliOutputFormat("Invalid wifi state\n");
            }
            return OT_ERROR_NONE;
        }

        for (int i = 1; i < aArgsLength; i++) {
            if (strcmp(aArgs[i], "-s") == 0) {
                i++;
                if (i < aArgsLength && strlen(aArgs[i]) < SSIDMAXLEN) {
                    strcpy(ssid, aArgs[i]);
                    otCliOutputFormat("ssid: %s\n", ssid);
                } else {
                    otCliOutputFormat("Wrong format of ssid\n");
                    return OT_ERROR_INVALID_ARGS;
                }
            } else if (strcmp(aArgs[i], "-p") == 0) {
                i++;
                if (i < aArgsLength && strlen(aArgs[i]) < PASSWORDMAXLEN) {
                    strcpy(psk, aArgs[i]);
                    otCliOutputFormat("psk: %s\n", psk);
                } else {
                    otCliOutputFormat("Wrong format of password\n");
                    return OT_ERROR_INVALID_ARGS;
                }
            }
        }
        if (strcmp(ssid, "") == 0) {
            otCliOutputFormat("None ssid\n");
            return OT_ERROR_INVALID_ARGS;
        }
        esp_openthread_task_switching_lock_release();
        esp_err_t error = esp_ot_wifi_connect(ssid, psk);
        esp_openthread_task_switching_lock_acquire(portMAX_DELAY);
        if (error == ESP_OK) {
            if (!s_border_router_initialized) {
                esp_openthread_set_backbone_netif(get_example_netif());
                if (esp_openthread_border_router_init() != ESP_OK) {
                    ESP_LOGE(OT_EXT_CLI_TAG, "Fail to initialize border router");
                }
                s_border_router_initialized = true;
            }
            otCliOutputFormat("wifi sta is connected successfully\n");
        } else {
            ESP_LOGW(OT_EXT_CLI_TAG, "Connection time out, please check your ssid & password, then retry");
            otCliOutputFormat("wifi sta connection is failed\n");
        }
    } else if (strcmp(aArgs[0], "state") == 0) {
        otCliOutputFormat("%s\n", wifi_state_string[s_wifi_state]);
    } else if (strcmp(aArgs[0], "disconnect") == 0) {
        if (s_wifi_state) {
            esp_openthread_task_switching_lock_release();
            esp_err_t error = esp_ot_wifi_disconnect();
            esp_openthread_task_switching_lock_acquire(portMAX_DELAY);
            if (error == ESP_OK) {
                otCliOutputFormat("disconnect wifi\n");
            } else {
                ESP_LOGE(OT_EXT_CLI_TAG, "Fail to disconnect wifi");
            }
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
    } else if (strcmp(aArgs[0], "config") == 0) {
        if (aArgsLength == 1) {
            wifi_config_print();
        } else {
            if (strcmp(aArgs[1], "clear") == 0) {
                esp_ot_wifi_config_clear();
            } else {
                otCliOutputFormat("invalid arguments: %s\n", aArgs[1]);
                return OT_ERROR_INVALID_ARGS;
            }
        }
    } else {
        otCliOutputFormat("invalid commands\n");
    }
    return OT_ERROR_NONE;
}

esp_err_t esp_ot_wifi_config_init(void)
{
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &s_wifi_config_nvs_handle);
    ESP_RETURN_ON_ERROR(err, OT_EXT_CLI_TAG, "Failed to open wifi_config NVS namespace (0x%x)", err);
    return ESP_OK;
}

esp_err_t esp_ot_wifi_config_set_ssid(const char *ssid)
{
    ESP_RETURN_ON_FALSE((s_wifi_config_nvs_handle != 0), ESP_FAIL, OT_EXT_CLI_TAG, "wifi_config NVS handle is invalid");
    ESP_RETURN_ON_ERROR(nvs_set_str(s_wifi_config_nvs_handle, SSIDKEY, ssid), OT_EXT_CLI_TAG, "No buffers for ssid");
    ESP_RETURN_ON_ERROR(nvs_commit(s_wifi_config_nvs_handle), OT_EXT_CLI_TAG,
                        "wifi_config NVS handle shut down when setting ssid");
    return ESP_OK;
}

esp_err_t esp_ot_wifi_config_get_ssid(char *ssid)
{
    ESP_RETURN_ON_FALSE((sizeof(ssid) < SSIDMAXLEN), ESP_FAIL, OT_EXT_CLI_TAG,
                        "The buffer size is not enough to get ssid");

    size_t length = SSIDMAXLEN;
    ESP_RETURN_ON_FALSE((s_wifi_config_nvs_handle != 0), ESP_FAIL, OT_EXT_CLI_TAG, "wifi_config NVS handle is invalid");
    if (nvs_get_str(s_wifi_config_nvs_handle, SSIDKEY, ssid, &length) != ESP_OK) {
        ESP_LOGI(OT_EXT_CLI_TAG, "None wifi ssid in nvs");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t esp_ot_wifi_config_set_password(const char *password)
{
    ESP_RETURN_ON_FALSE((s_wifi_config_nvs_handle != 0), ESP_FAIL, OT_EXT_CLI_TAG, "wifi_config NVS handle is invalid");
    ESP_RETURN_ON_ERROR(nvs_set_str(s_wifi_config_nvs_handle, PASSWORDKEY, password), OT_EXT_CLI_TAG,
                        "No buffers for password");
    ESP_RETURN_ON_ERROR(nvs_commit(s_wifi_config_nvs_handle), OT_EXT_CLI_TAG,
                        "wifi_config NVS handle shut down when setting password");
    return ESP_OK;
}

esp_err_t esp_ot_wifi_config_get_password(char *password)
{
    ESP_RETURN_ON_FALSE((sizeof(password) < PASSWORDMAXLEN), ESP_FAIL, OT_EXT_CLI_TAG,
                        "The buffer size is not enough to get password");
    size_t length = PASSWORDMAXLEN;
    ESP_RETURN_ON_FALSE((s_wifi_config_nvs_handle != 0), ESP_FAIL, OT_EXT_CLI_TAG, "wifi_config NVS handle is invalid");
    if (nvs_get_str(s_wifi_config_nvs_handle, PASSWORDKEY, password, &length) != ESP_OK) {
        ESP_LOGI(OT_EXT_CLI_TAG, "None wifi password in nvs");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t esp_ot_wifi_config_clear(void)
{
    ESP_RETURN_ON_FALSE((s_wifi_config_nvs_handle != 0), ESP_FAIL, OT_EXT_CLI_TAG, "wifi_config NVS handle is invalid");
    ESP_RETURN_ON_ERROR(nvs_erase_all(s_wifi_config_nvs_handle), OT_EXT_CLI_TAG, "Fail to clear wifi configurations");
    return ESP_OK;
}

esp_ot_wifi_state_t esp_ot_wifi_state_get(void)
{
    return s_wifi_state;
}