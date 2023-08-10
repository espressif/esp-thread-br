/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_tcp_socket.h"

#include "esp_check.h"
#include "esp_err.h"

#include "esp_log.h"
#include "esp_ot_cli_extension.h"
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "openthread/cli.h"

static EventGroupHandle_t tcp_client_event_group;
static EventGroupHandle_t tcp_server_event_group;

static void tcp_client_receive_task(void *pvParameters)
{
    char rx_buffer[128];
    int len = 0;
    TCP_CLIENT *tcp_client_member = (TCP_CLIENT *)pvParameters;
    int receive_error_nums = 0;
    int set_exit = 0;

    while (true) {
        len = recv(tcp_client_member->sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            receive_error_nums++;
            if (receive_error_nums >= 5 && !set_exit && tcp_client_member->sock != -1) {
                set_exit = 1;
                ESP_LOGW(OT_EXT_CLI_TAG, "TCP client fail when receiving message");
                xEventGroupSetBits(tcp_client_event_group, TCP_CLIENT_DELETE_BIT);
            }
        }
        if (len > 0) {
            receive_error_nums = 0;
            ESP_LOGI(OT_EXT_CLI_TAG, "sock %d Received %d bytes from %s", tcp_client_member->sock, len,
                     tcp_client_member->remote_ipaddr);
            rx_buffer[len] = '\0';
            ESP_LOGI(OT_EXT_CLI_TAG, "%s", rx_buffer);
        }
        if (tcp_client_member->exist == 0 && tcp_client_member->sock == -1) {
            break;
        }
    }
    ESP_LOGI(OT_EXT_CLI_TAG, "TCP client receive task exiting");
    vTaskDelete(NULL);
}

static void tcp_client_add(TCP_CLIENT *tcp_client_member)
{
    esp_err_t ret = ESP_OK;
    int err = 0;
    int err_flag = 0;

    struct sockaddr_in6 dest_addr = {0};

    inet6_aton(tcp_client_member->remote_ipaddr, &dest_addr.sin6_addr);
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(tcp_client_member->remote_port);

    inet_ntop(AF_INET6, &dest_addr.sin6_addr, tcp_client_member->remote_ipaddr,
              sizeof(tcp_client_member->remote_ipaddr));
    tcp_client_member->remote_port = tcp_client_member->remote_port;

    tcp_client_member->sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_IPV6);
    err_flag = 1;

    ESP_GOTO_ON_FALSE((tcp_client_member->sock >= 0), ESP_FAIL, exit, OT_EXT_CLI_TAG,
                      "Unable to create socket: errno %d", errno);

    ESP_LOGI(OT_EXT_CLI_TAG, "Socket created, connecting to %s:%d", tcp_client_member->remote_ipaddr,
             tcp_client_member->remote_port);
    err = connect(tcp_client_member->sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
    ESP_GOTO_ON_FALSE((err == 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "Socket unable to connect: errno %d", errno);
    ESP_LOGI(OT_EXT_CLI_TAG, "Successfully connected");

    if (pdPASS != xTaskCreate(tcp_client_receive_task, "tcp_client_receive", 4096, tcp_client_member, 4, NULL)) {
        err = -1;
    }
    ESP_GOTO_ON_FALSE((err == 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "The TCP client is unable to receive: errno %d",
                      errno);

exit:
    if (ret != ESP_OK) {
        if (err_flag) {
            shutdown(tcp_client_member->sock, 0);
            close(tcp_client_member->sock);
            tcp_client_member->sock = -1;
        }
        ESP_LOGI(OT_EXT_CLI_TAG, "Fail to create TCP client");
    } else {
        tcp_client_member->exist = 1;
    }
}

static void tcp_client_send(TCP_CLIENT *tcp_client_member)
{
    int len = 0;
    len = send(tcp_client_member->sock, tcp_client_member->message, strlen(tcp_client_member->message), 0);
    if (len < 0) {
        ESP_LOGI(OT_EXT_CLI_TAG, "Fail to send message");
    }
}

static void tcp_client_delete(TCP_CLIENT *tcp_client_member)
{
    ESP_LOGI(OT_EXT_CLI_TAG, "TCP client is disconnecting with %s", tcp_client_member->remote_ipaddr);
    tcp_client_member->exist = 0;
    shutdown(tcp_client_member->sock, 0);
    close(tcp_client_member->sock);
    tcp_client_member->sock = -1;
}

static void tcp_socket_client_task(void *pvParameters)
{
    TCP_CLIENT *tcp_client_member = (TCP_CLIENT *)pvParameters;

    while (true) {
        int bits =
            xEventGroupWaitBits(tcp_client_event_group,
                                TCP_CLIENT_ADD_BIT | TCP_CLIENT_SEND_BIT | TCP_CLIENT_DELETE_BIT | TCP_CLIENT_CLOSE_BIT,
                                pdFALSE, pdFALSE, 10000 / portTICK_PERIOD_MS);
        int tcp_event = bits & 0x0f;
        if (tcp_event == TCP_CLIENT_ADD_BIT) {
            xEventGroupClearBits(tcp_client_event_group, TCP_CLIENT_ADD_BIT);
            tcp_client_add(tcp_client_member);
        } else if (tcp_event == TCP_CLIENT_SEND_BIT) {
            xEventGroupClearBits(tcp_client_event_group, TCP_CLIENT_SEND_BIT);
            tcp_client_send(tcp_client_member);
        } else if (tcp_event == TCP_CLIENT_DELETE_BIT) {
            xEventGroupClearBits(tcp_client_event_group, TCP_CLIENT_DELETE_BIT);
            tcp_client_delete(tcp_client_member);
        } else if (tcp_event == TCP_CLIENT_CLOSE_BIT) {
            xEventGroupClearBits(tcp_client_event_group, TCP_CLIENT_CLOSE_BIT);
            if (tcp_client_member->exist == 1) {
                tcp_client_delete(tcp_client_member);
            }
            break;
        }
    }
    ESP_LOGI(OT_EXT_CLI_TAG, "Closed TCP client successfully");
    vEventGroupDelete(tcp_client_event_group);
    vTaskDelete(NULL);
}

otError esp_ot_process_tcp_client(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    static TaskHandle_t tcp_client_handle = NULL;
    static TCP_CLIENT tcp_client_member = {0, -1, -1, "", ""};

    if (aArgsLength == 0) {
        otCliOutputFormat("---tcpsockclient parameter---\n");
        otCliOutputFormat("status                     :     get TCP client status\n");
        otCliOutputFormat("open                       :     open TCP client function\n");
        otCliOutputFormat("connect <ipaddr> <port>    :     create a TCP client and connect the server\n");
        otCliOutputFormat("send <message>             :     send a message to the TCP server\n");
        otCliOutputFormat("close                      :     close TCP client \n");
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("get TCP client status      :     tcpsockclient status\n");
        otCliOutputFormat("open TCP client function   :     tcpsockclient open\n");
        otCliOutputFormat("create a TCP client        :     tcpsockclient connect fd81:984a:b59d:2::c0a8:0166 12345\n");
        otCliOutputFormat("send a message             :     tcpsockclient send hello\n");
        otCliOutputFormat("close TCP client           :     tcpsockclient close\n");
    } else if (strcmp(aArgs[0], "status") == 0) {
        if (tcp_client_handle == NULL) {
            otCliOutputFormat("TCP client is not open\n");
            return OT_ERROR_NONE;
        }
        if (tcp_client_member.exist == 0) {
            otCliOutputFormat("None TCP client!\n");
            return OT_ERROR_NONE;
        }
        otCliOutputFormat("connected\tremote ipaddr: %s\n", tcp_client_member.remote_ipaddr);
    } else if (strcmp(aArgs[0], "open") == 0) {
        if (tcp_client_handle == NULL) {
            tcp_client_event_group = xEventGroupCreate();
            ESP_RETURN_ON_FALSE(tcp_client_event_group != NULL, OT_ERROR_FAILED, OT_EXT_CLI_TAG,
                                "Fail to open tcp client");
            if (pdPASS !=
                xTaskCreate(tcp_socket_client_task, "tcp_socket_client", 4096, &tcp_client_member, 4,
                            &tcp_client_handle)) {
                tcp_client_handle = NULL;
                vEventGroupDelete(tcp_client_event_group);
                tcp_client_event_group = NULL;
                ESP_LOGE(OT_EXT_CLI_TAG, "Fail to open tcp client");
                return OT_ERROR_FAILED;
            }
        } else {
            otCliOutputFormat("Already!\n");
        }
    } else if (strcmp(aArgs[0], "connect") == 0) {
        if (tcp_client_handle == NULL) {
            otCliOutputFormat("TCP client is not open\n");
            return OT_ERROR_NONE;
        }
        if (tcp_client_member.exist == 1) {
            otCliOutputFormat("TCP client exists.\n");
            return OT_ERROR_NONE;
        }
        if (aArgsLength != 3) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
            return OT_ERROR_INVALID_ARGS;
        }
        strncpy(tcp_client_member.remote_ipaddr, aArgs[1], sizeof(tcp_client_member.remote_ipaddr));
        tcp_client_member.remote_port = atoi(aArgs[2]);
        xEventGroupSetBits(tcp_client_event_group, TCP_CLIENT_ADD_BIT);
    } else if (strcmp(aArgs[0], "send") == 0) {
        if (tcp_client_handle == NULL) {
            otCliOutputFormat("TCP client is not open\n");
            return OT_ERROR_NONE;
        }
        if (tcp_client_member.exist == 0) {
            otCliOutputFormat("None TCP client!\n");
            return OT_ERROR_NONE;
        }
        if (aArgsLength != 2) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
            return OT_ERROR_INVALID_ARGS;
        }
        strncpy(tcp_client_member.message, aArgs[1], sizeof(tcp_client_member.message));
        xEventGroupSetBits(tcp_client_event_group, TCP_CLIENT_SEND_BIT);
    } else if (strcmp(aArgs[0], "close") == 0) {
        if (tcp_client_handle == NULL) {
            otCliOutputFormat("TCP client is not open\n");
            return OT_ERROR_NONE;
        }
        xEventGroupSetBits(tcp_client_event_group, TCP_CLIENT_CLOSE_BIT);
        tcp_client_handle = NULL;
    } else {
        otCliOutputFormat("invalid commands\n");
    }
    return OT_ERROR_NONE;
}

static void tcp_server_task(void *pvParameters)
{
    int connect_sock = -1;
    char rx_buffer[128];
    int len = 0;
    char addr_str[128];
    struct sockaddr_storage source_addr;
    TCP_SERVER *tcp_server_member = (TCP_SERVER *)pvParameters;
    int receive_error_nums = 0;
    int set_exit = 0;

    socklen_t addr_len = sizeof(source_addr);
    connect_sock = accept(tcp_server_member->listen_sock, (struct sockaddr *)&source_addr, &addr_len);
    if (connect_sock < 0) {
        ESP_LOGE(OT_EXT_CLI_TAG, "Unable to accept connection: errno %d", errno);
        if (tcp_server_member->listen_sock != -1) {
            xEventGroupSetBits(tcp_server_event_group, TCP_SERVER_DELETE_BIT);
        }
        while (true) {
            if (tcp_server_member->exist == 0 && tcp_server_member->listen_sock == -1) {
                break;
            }
        }
    } else {
        tcp_server_member->connect_sock = connect_sock;
        inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        ESP_LOGI(OT_EXT_CLI_TAG, "Socket accepted ip address: %s", addr_str);
        strncpy(tcp_server_member->remote_ipaddr, addr_str, strlen(addr_str) + 1);
        while (true) {
            len = recv(connect_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len < 0) {
                receive_error_nums++;
                if (receive_error_nums >= 5 && !set_exit && tcp_server_member->connect_sock != -1) {
                    ESP_LOGW(OT_EXT_CLI_TAG, "TCP server fail when receiving message");
                    xEventGroupSetBits(tcp_server_event_group, TCP_SERVER_DELETE_BIT);
                    set_exit = 1;
                }
            }
            if (len > 0) {
                receive_error_nums = 0;
                ESP_LOGI(OT_EXT_CLI_TAG, "sock %d Received %d bytes from %s", connect_sock, len, addr_str);
                rx_buffer[len] = '\0';
                ESP_LOGI(OT_EXT_CLI_TAG, "%s", rx_buffer);
            }
            if (tcp_server_member->exist == 0 && tcp_server_member->connect_sock == -1) {
                break;
            }
        }
    }
    ESP_LOGI(OT_EXT_CLI_TAG, "TCP server receive task exiting");
    vTaskDelete(NULL);
}

static void tcp_server_add(TCP_SERVER *tcp_server_member)
{
    esp_err_t ret = ESP_OK;
    int err = 0;
    int listen_sock = -1;
    int err_flag = 0;

    struct sockaddr_in6 listen_addr = {0};

    inet6_aton(tcp_server_member->local_ipaddr, &listen_addr.sin6_addr);
    listen_addr.sin6_family = AF_INET6;
    listen_addr.sin6_port = htons(tcp_server_member->local_port);

    listen_sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_IPV6);
    ESP_GOTO_ON_FALSE((listen_sock >= 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "Unable to create socket: errno %d", errno);
    ESP_LOGI(OT_EXT_CLI_TAG, "Socket created");
    tcp_server_member->listen_sock = listen_sock;
    err_flag = 1;

    err = bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(struct sockaddr_in6));
    ESP_GOTO_ON_FALSE((err == 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "Socket unable to bind: errno %d, IPPROTO: %d", errno,
                      AF_INET6);
    ESP_LOGI(OT_EXT_CLI_TAG, "Socket bound, port %d", tcp_server_member->local_port);

    err = listen(listen_sock, 1);
    ESP_GOTO_ON_FALSE((err == 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "Error occurred during listen: errno %d", errno);
    ESP_LOGI(OT_EXT_CLI_TAG, "Socket listening");

    if (pdPASS != xTaskCreate(tcp_server_task, "tcp_server_receive", 4096, tcp_server_member, 4, NULL)) {
        err = -1;
    }
    ESP_GOTO_ON_FALSE((err == 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "The TCP server is unable to accept: errno %d",
                      errno);

exit:
    if (ret != ESP_OK) {
        if (err_flag) {
            shutdown(listen_sock, 0);
            close(listen_sock);
            tcp_server_member->listen_sock = -1;
        }
        ESP_LOGI(OT_EXT_CLI_TAG, "Fail to create a TCP server");
    } else {
        ESP_LOGI(OT_EXT_CLI_TAG, "Successfully created");
        tcp_server_member->exist = 1;
    }
}

static void tcp_server_send(TCP_SERVER *tcp_server_member)
{
    int len = 0;
    len = send(tcp_server_member->connect_sock, tcp_server_member->message, strlen(tcp_server_member->message), 0);
    if (len < 0) {
        ESP_LOGI(OT_EXT_CLI_TAG, "Fail to send message");
    }
}

static void tcp_server_delete(TCP_SERVER *tcp_server_member)
{
    tcp_server_member->exist = 0;
    int listen_sock = tcp_server_member->listen_sock;
    int connect_sock = tcp_server_member->connect_sock;
    tcp_server_member->listen_sock = -1;
    tcp_server_member->connect_sock = -1;
    shutdown(listen_sock, 0);
    close(listen_sock);
    tcp_server_member->listen_sock = -1;
    if (connect_sock >= 0) {
        shutdown(connect_sock, 0);
        close(connect_sock);
        ESP_LOGI(OT_EXT_CLI_TAG, "TCP server is disconnecting with %s", tcp_server_member->remote_ipaddr);
    }
}

static void tcp_socket_server_task(void *pvParameters)
{
    TCP_SERVER *tcp_server_member = (TCP_SERVER *)pvParameters;

    while (true) {
        int bits =
            xEventGroupWaitBits(tcp_server_event_group,
                                TCP_SERVER_ADD_BIT | TCP_SERVER_SEND_BIT | TCP_SERVER_DELETE_BIT | TCP_SERVER_CLOSE_BIT,
                                pdFALSE, pdFALSE, 10000 / portTICK_PERIOD_MS);
        int tcp_event = bits & 0x0f;
        if (tcp_event == TCP_SERVER_ADD_BIT) {
            xEventGroupClearBits(tcp_server_event_group, TCP_SERVER_ADD_BIT);
            tcp_server_add(tcp_server_member);
        } else if (tcp_event == TCP_SERVER_SEND_BIT) {
            xEventGroupClearBits(tcp_server_event_group, TCP_SERVER_SEND_BIT);
            tcp_server_send(tcp_server_member);
        } else if (tcp_event == TCP_SERVER_DELETE_BIT) {
            xEventGroupClearBits(tcp_server_event_group, TCP_SERVER_DELETE_BIT);
            tcp_server_delete(tcp_server_member);
        } else if (tcp_event == TCP_SERVER_CLOSE_BIT) {
            xEventGroupClearBits(tcp_server_event_group, TCP_SERVER_CLOSE_BIT);
            if (tcp_server_member->exist == 1) {
                tcp_server_delete(tcp_server_member);
            }
            break;
        }
    }
    ESP_LOGI(OT_EXT_CLI_TAG, "Closed TCP server successfully");
    vEventGroupDelete(tcp_server_event_group);
    vTaskDelete(NULL);
}

otError esp_ot_process_tcp_server(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    static TaskHandle_t tcp_server_handle = NULL;
    static TCP_SERVER tcp_server_member = {0, -1, -1, -1, "", "", ""};

    if (aArgsLength == 0) {
        otCliOutputFormat("---tcpsockserver parameter---\n");
        otCliOutputFormat("status                     :     get TCP server status\n");
        otCliOutputFormat("open                       :     open TCP server function\n");
        otCliOutputFormat("bind <ipaddr> <port>       :     create a TCP server with binding the ipaddr and port\n");
        otCliOutputFormat("send <message>             :     send a message to the TCP client\n");
        otCliOutputFormat("close                      :     close TCP server\n");
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("get TCP server status      :     tcpsockserver status\n");
        otCliOutputFormat("open TCP server function   :     tcpsockserver open\n");
        otCliOutputFormat("create a TCP server        :     tcpsockserver bind :: 12345\n");
        otCliOutputFormat("send a message             :     tcpsockserver send hello\n");
        otCliOutputFormat("close TCP server           :     tcpsockserver close\n");
    } else if (strcmp(aArgs[0], "status") == 0) {
        if (tcp_server_handle == NULL) {
            otCliOutputFormat("TCP server is not open.\n");
            return OT_ERROR_NONE;
        }
        if (tcp_server_member.exist == 0) {
            otCliOutputFormat("None TCP server!\n");
            return OT_ERROR_NONE;
        }
        if (tcp_server_member.connect_sock != -1) {
            otCliOutputFormat("connected\tremote ipaddr: %s\n", tcp_server_member.remote_ipaddr);
        } else {
            otCliOutputFormat("disconnected\n");
        }
    } else if (strcmp(aArgs[0], "open") == 0) {
        if (tcp_server_handle == NULL) {
            tcp_server_event_group = xEventGroupCreate();
            ESP_RETURN_ON_FALSE(tcp_server_event_group != NULL, OT_ERROR_FAILED, OT_EXT_CLI_TAG,
                                "Fail to open tcp server");
            if (pdPASS !=
                xTaskCreate(tcp_socket_server_task, "tcp_socket_server", 4096, &tcp_server_member, 4,
                            &tcp_server_handle)) {
                tcp_server_handle = NULL;
                vEventGroupDelete(tcp_server_event_group);
                tcp_server_event_group = NULL;
                ESP_LOGE(OT_EXT_CLI_TAG, "Fail to open tcp server");
                return OT_ERROR_FAILED;
            }
        } else {
            otCliOutputFormat("Already!\n");
        }
    } else if (strcmp(aArgs[0], "bind") == 0) {
        if (tcp_server_handle == NULL) {
            otCliOutputFormat("TCP server is not open.\n");
            return OT_ERROR_NONE;
        }
        if (tcp_server_member.exist == 1) {
            otCliOutputFormat("TCP server exists.\n");
            return OT_ERROR_NONE;
        }
        if (aArgsLength != 3) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
            return OT_ERROR_INVALID_ARGS;
        }
        strncpy(tcp_server_member.local_ipaddr, aArgs[1], sizeof(tcp_server_member.local_ipaddr));
        tcp_server_member.local_port = atoi(aArgs[2]);
        xEventGroupSetBits(tcp_server_event_group, TCP_SERVER_ADD_BIT);
    } else if (strcmp(aArgs[0], "send") == 0) {
        if (tcp_server_handle == NULL) {
            otCliOutputFormat("TCP server is not open.\n");
            return OT_ERROR_NONE;
        }
        if (tcp_server_member.exist == 0) {
            otCliOutputFormat("None TCP server.\n");
            return OT_ERROR_NONE;
        }
        if (aArgsLength != 2) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
            return OT_ERROR_INVALID_ARGS;
        }
        strncpy(tcp_server_member.message, aArgs[1], sizeof(tcp_server_member.message));
        xEventGroupSetBits(tcp_server_event_group, TCP_SERVER_SEND_BIT);
    } else if (strcmp(aArgs[0], "close") == 0) {
        if (tcp_server_handle == NULL) {
            otCliOutputFormat("TCP server is not open.\n");
            return OT_ERROR_NONE;
        }
        xEventGroupSetBits(tcp_server_event_group, TCP_SERVER_CLOSE_BIT);
        tcp_server_handle = NULL;
    } else {
        otCliOutputFormat("invalid commands\n");
    }
    return OT_ERROR_NONE;
}
