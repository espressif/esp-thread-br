/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <openthread/error.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UDP_CLIENT_SEND_BIT BIT0
#define UDP_CLIENT_CLOSE_BIT BIT1
#define UDP_SERVER_BIND_BIT BIT0
#define UDP_SERVER_SEND_BIT BIT1
#define UDP_SERVER_CLOSE_BIT BIT2

/**
 * @brief User command "mcast" process.
 *
 */
otError esp_ot_process_mcast_group(void *aContext, uint8_t aArgsLength, char *aArgs[]);

/**
 * @brief User command "udpsockserver" process.
 *
 */
otError esp_ot_process_udp_server(void *aContext, uint8_t aArgsLength, char *aArgs[]);

/**
 * @brief User command "udpsockclient" process.
 *
 */
otError esp_ot_process_udp_client(void *aContext, uint8_t aArgsLength, char *aArgs[]);

typedef struct send_meaasge {
    int port;
    char ipaddr[128];
    char message[128];
} SEND_MESSAGE;

typedef struct udp_server {
    int exist;
    int sock;
    int local_port;
    char local_ipaddr[128];
    SEND_MESSAGE messagesend;
} UDP_SERVER;

typedef struct udp_client {
    int exist;
    int sock;
    int local_port;
    char local_ipaddr[128];
    SEND_MESSAGE messagesend;
} UDP_CLIENT;

#ifdef __cplusplus
}
#endif
