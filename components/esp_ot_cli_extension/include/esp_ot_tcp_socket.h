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

#define TCP_CLIENT_ADD_BIT BIT0
#define TCP_CLIENT_SEND_BIT BIT1
#define TCP_CLIENT_DELETE_BIT BIT2
#define TCP_CLIENT_CLOSE_BIT BIT3
#define TCP_SERVER_ADD_BIT BIT0
#define TCP_SERVER_SEND_BIT BIT1
#define TCP_SERVER_DELETE_BIT BIT2
#define TCP_SERVER_CLOSE_BIT BIT3

/**
 * @brief User command "tcpsockserver" process.
 *
 */
otError esp_ot_process_tcp_server(void *aContext, uint8_t aArgsLength, char *aArgs[]);

/**
 * @brief User command "tcpsockclient" process.
 *
 */
otError esp_ot_process_tcp_client(void *aContext, uint8_t aArgsLength, char *aArgs[]);

typedef struct tcp_server {
    int exist;
    int listen_sock;
    int connect_sock;
    int local_port;
    char local_ipaddr[128];
    char remote_ipaddr[128];
    char message[128];
} TCP_SERVER;

typedef struct tcp_client {
    int exist;
    int sock;
    int remote_port;
    char remote_ipaddr[128];
    char message[128];
} TCP_CLIENT;

#ifdef __cplusplus
}
#endif
