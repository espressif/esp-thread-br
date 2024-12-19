/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_br_lib_compati_check.h"
#include "lwip/netif.h"

#define LOAD_AND_TEST_NETIF_MEMBER(a, out_result)                                 \
    do {                                                                          \
        extern bool esp_openthread_check_netif_##a##_offset(size_t offset);       \
        if (esp_openthread_check_netif_##a##_offset(offsetof(struct netif, a))) { \
            ESP_LOGI("OT_TEST", "The offset `%s` checking passed.", #a);          \
        } else {                                                                  \
            out_result = false;                                                   \
        }                                                                         \
    } while (0)

otError esp_openthread_process_br_lib_compatibility_check(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    bool test_result = true;
    LOAD_AND_TEST_NETIF_MEMBER(ip_addr, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(netmask, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(gw, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(ip6_addr, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(ip6_addr_state, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(ip6_addr_valid_life, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(ip6_addr_pref_life, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(input, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(output, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(linkoutput, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(status_callback, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(state, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(hostname, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(hwaddr, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(hwaddr_len, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(flags, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(name, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(num, test_result);
    LOAD_AND_TEST_NETIF_MEMBER(ip6_autoconfig_enabled, test_result);
    extern bool esp_openthread_check_netif_size(size_t netif_t);
    if (esp_openthread_check_netif_size(sizeof(struct netif))) {
        ESP_LOGI("OT_TEST", "The size of netif struct checking passed.");
    } else {
        test_result = false;
    }
    if (test_result) {
        ESP_LOGI("OT_TEST", "The br library compatibility checking passed.");
    } else {
        ESP_LOGE("OT_TEST", "The br library compatibility checking failed.");
    }
    return OT_ERROR_NONE;
}
