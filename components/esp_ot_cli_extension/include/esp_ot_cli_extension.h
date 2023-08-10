/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wi-Fi IPv6 address event declarations
 *
 */
typedef enum {
    WIFI_ADDRESS_EVENT_ADD_IP6,               /*!< Wi-Fi stack added IPv6 address */
    WIFI_ADDRESS_EVENT_REMOVE_IP6,            /*!< Wi-Fi stack removed IPv6 address */
    WIFI_ADDRESS_EVENT_MULTICAST_GROUP_JOIN,  /*!< Wi-Fi stack joined IPv6 multicast group */
    WIFI_ADDRESS_EVENT_MULTICAST_GROUP_LEAVE, /*!< Wi-Fi stack left IPv6 multicast group */
} esp_wifi_address_event_t;

/**
 * @brief Init the custom command.
 *
 */
void esp_cli_custom_command_init(void);

#define OT_EXT_CLI_TAG "ot_ext_cli"

#ifdef __cplusplus
}
#endif
