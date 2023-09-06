/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_udp_socket.h"

#include "cc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_ot_cli_extension.h"
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/mld6.h"
#include "lwip/sockets.h"
#include "openthread/cli.h"

static EventGroupHandle_t udp_server_event_group;
static EventGroupHandle_t udp_client_event_group;

static void udp_server_receive_task(void *pvParameters)
{
    char rx_buffer[128];
    int len = 0;
    char addr_str[128];
    int port = 0;
    struct sockaddr_storage source_addr;
    UDP_SERVER *udp_server_member = (UDP_SERVER *)pvParameters;

    while (true) {
        socklen_t socklen = sizeof(source_addr);
        len = recvfrom(udp_server_member->sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr,
                       &socklen);
        if (len < 0) {
            ESP_LOGW(OT_EXT_CLI_TAG, "UDP server fail when receiving message");
        }
        if (len > 0) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
            port = ntohs(((struct sockaddr_in6 *)&source_addr)->sin6_port);
            ESP_LOGI(OT_EXT_CLI_TAG, "sock %d Received %d bytes from %s : %d", udp_server_member->sock, len, addr_str,
                     port);
            rx_buffer[len] = '\0';
            ESP_LOGI(OT_EXT_CLI_TAG, "%s", rx_buffer);
        }
        if (udp_server_member->exist == 0) {
            break;
        }
    }
    ESP_LOGI(OT_EXT_CLI_TAG, "UDP server receive task exiting");
    vTaskDelete(NULL);
}

static void udp_server_bind(UDP_SERVER *udp_server_member)
{
    esp_err_t ret = ESP_OK;
    int err = 0;
    int sock = -1;
    int err_flag = 0;

    struct sockaddr_in6 listen_addr = {0};

    inet6_aton(udp_server_member->local_ipaddr, &listen_addr.sin6_addr);
    inet6_ntoa_r((&listen_addr)->sin6_addr, udp_server_member->local_ipaddr,
                 sizeof(udp_server_member->local_ipaddr) - 1);
    listen_addr.sin6_family = AF_INET6;
    listen_addr.sin6_port = htons(udp_server_member->local_port);

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_IPV6);
    ESP_GOTO_ON_FALSE((sock >= 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "Unable to create socket: errno %d", errno);
    ESP_LOGI(OT_EXT_CLI_TAG, "Socket created");
    udp_server_member->sock = sock;
    err_flag = 1;

    err = bind(sock, (struct sockaddr *)&listen_addr, sizeof(struct sockaddr_in6));
    ESP_GOTO_ON_FALSE((err == 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "Socket unable to bind: errno %d, IPPROTO: %d", errno,
                      AF_INET6);
    ESP_LOGI(OT_EXT_CLI_TAG, "Socket bound, ipaddr %s, port %d", udp_server_member->local_ipaddr,
             udp_server_member->local_port);

    if (pdPASS != xTaskCreate(udp_server_receive_task, "udp_server_receive", 4096, udp_server_member, 4, NULL)) {
        err = -1;
    }
    ESP_GOTO_ON_FALSE((err == 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "The UDP server is unable to receive: errno %d",
                      errno);

exit:
    if (ret != ESP_OK) {
        if (err_flag) {
            shutdown(sock, 0);
            close(sock);
            udp_server_member->sock = -1;
        }
        ESP_LOGI(OT_EXT_CLI_TAG, "Fail to create a UDP server");
    } else {
        ESP_LOGI(OT_EXT_CLI_TAG, "Successfully created");
        udp_server_member->exist = 1;
    }
}

static void udp_server_send(UDP_SERVER *udp_server_member)
{
    struct sockaddr_in6 dest_addr = {0};
    int len = 0;

    inet6_aton(udp_server_member->messagesend.ipaddr, &dest_addr.sin6_addr);
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(udp_server_member->messagesend.port);
    ESP_LOGI(OT_EXT_CLI_TAG, "Sending to %s : %d", udp_server_member->messagesend.ipaddr,
             udp_server_member->messagesend.port);

    esp_err_t err = socket_bind_interface(udp_server_member->sock, &(udp_server_member->ifr));
    ESP_RETURN_ON_FALSE(err == ESP_OK, , OT_EXT_CLI_TAG, "Stop sending message");
    len = sendto(udp_server_member->sock, udp_server_member->messagesend.message,
                 strlen(udp_server_member->messagesend.message), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (len < 0) {
        ESP_LOGW(OT_EXT_CLI_TAG, "Fail to send message");
    }
}

static void udp_server_delete(UDP_SERVER *udp_server_member)
{
    udp_server_member->exist = 0;
    shutdown(udp_server_member->sock, 0);
    close(udp_server_member->sock);
    udp_server_member->sock = -1;
}

static void udp_socket_server_task(void *pvParameters)
{
    UDP_SERVER *udp_server_member = (UDP_SERVER *)pvParameters;

    while (true) {
        int bits = xEventGroupWaitBits(udp_server_event_group,
                                       UDP_SERVER_BIND_BIT | UDP_SERVER_SEND_BIT | UDP_SERVER_CLOSE_BIT, pdFALSE,
                                       pdFALSE, 10000 / portTICK_PERIOD_MS);
        int udp_event = bits & 0x0f;
        if (udp_event == UDP_SERVER_BIND_BIT) {
            xEventGroupClearBits(udp_server_event_group, UDP_SERVER_BIND_BIT);
            udp_server_bind(udp_server_member);
        } else if (udp_event == UDP_SERVER_SEND_BIT) {
            xEventGroupClearBits(udp_server_event_group, UDP_SERVER_SEND_BIT);
            udp_server_send(udp_server_member);
        } else if (udp_event == UDP_SERVER_CLOSE_BIT) {
            xEventGroupClearBits(udp_server_event_group, UDP_SERVER_CLOSE_BIT);
            udp_server_delete(udp_server_member);
            break;
        }
    }
    ESP_LOGI(OT_EXT_CLI_TAG, "Closed UDP server successfully");
    vEventGroupDelete(udp_server_event_group);
    vTaskDelete(NULL);
}

otError esp_ot_process_udp_server(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    static TaskHandle_t udp_server_handle = NULL;
    static UDP_SERVER udp_server_member = {0, -1, -1, "", {""}, {-1, "", ""}};

    if (aArgsLength == 0) {
        otCliOutputFormat("---udpsockserver parameter---\n");
        otCliOutputFormat("status                                   :     get UDP server status\n");
        otCliOutputFormat("open                                     :     open UDP server function\n");
        otCliOutputFormat("bind <port>                              :     create a UDP server with binding the port\n");
        otCliOutputFormat("send <ipaddr> <port> <message>           :     send a message to the UDP client\n");
        otCliOutputFormat("send <ipaddr> <port> <message> <if>      :     send a message to the UDP client via <if>\n");
        otCliOutputFormat("close                                    :     close UDP server\n");
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("get UDP server status                    :     udpsockserver status\n");
        otCliOutputFormat("open UDP server function                 :     udpsockserver open\n");
        otCliOutputFormat("create a UDP server                      :     udpsockserver bind 12345\n");
        otCliOutputFormat("send a message                           :     udpsockserver send "
                          "FDDE:AD00:BEEF:CAFE:FD14:30B6:CDA:8A95 51876 hello\n");
        otCliOutputFormat("send a message via Wi-Fi interface       :     udpsockserver send "
                          "FDDE:AD00:BEEF:CAFE:FD14:30B6:CDA:8A95 51876 hello st\n");
        otCliOutputFormat("send a message via OpenThread interface  :     udpsockserver send "
                          "FDDE:AD00:BEEF:CAFE:FD14:30B6:CDA:8A95 51876 hello ot\n");
        otCliOutputFormat("close UDP server                         :     udpsockserver close\n");
    } else if (strcmp(aArgs[0], "status") == 0) {
        if (udp_server_handle == NULL) {
            otCliOutputFormat("UDP server is not open\n");
            return OT_ERROR_NONE;
        }
        if (udp_server_member.exist == 0) {
            otCliOutputFormat("UDP server is not binded!\n");
            return OT_ERROR_NONE;
        }
        otCliOutputFormat("open\tlocal ipaddr: %s\tlocal port: %d\n", udp_server_member.local_ipaddr,
                          udp_server_member.local_port);
    } else if (strcmp(aArgs[0], "open") == 0) {
        if (udp_server_handle == NULL) {
            udp_server_event_group = xEventGroupCreate();
            ESP_RETURN_ON_FALSE(udp_server_event_group != NULL, OT_ERROR_FAILED, OT_EXT_CLI_TAG,
                                "Fail to open udp server");
            if (pdPASS !=
                xTaskCreate(udp_socket_server_task, "udp_socket_server", 4096, &udp_server_member, 4,
                            &udp_server_handle)) {
                udp_server_handle = NULL;
                vEventGroupDelete(udp_server_event_group);
                udp_server_event_group = NULL;
                ESP_LOGE(OT_EXT_CLI_TAG, "Fail to open udp server");
                return OT_ERROR_FAILED;
            }
        } else {
            otCliOutputFormat("Already!\n");
        }
    } else if (strcmp(aArgs[0], "bind") == 0) {
        if (udp_server_handle == NULL) {
            otCliOutputFormat("UDP server is not open.\n");
            return OT_ERROR_NONE;
        }
        if (udp_server_member.exist == 1) {
            otCliOutputFormat("UDP server exists.\n");
            return OT_ERROR_NONE;
        }
        if (aArgsLength != 2) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
            return OT_ERROR_INVALID_ARGS;
        }
        strncpy(udp_server_member.local_ipaddr, "::", sizeof(udp_server_member.local_ipaddr));
        udp_server_member.local_port = atoi(aArgs[1]);
        xEventGroupSetBits(udp_server_event_group, UDP_SERVER_BIND_BIT);
    } else if (strcmp(aArgs[0], "send") == 0) {
        if (udp_server_handle == NULL) {
            otCliOutputFormat("UDP server is not open.\n");
            return OT_ERROR_NONE;
        }
        if (udp_server_member.exist == 0) {
            otCliOutputFormat("UDP server is not binded!\n");
            return OT_ERROR_NONE;
        }
        if (aArgsLength != 4 && aArgsLength != 5) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
            return OT_ERROR_INVALID_ARGS;
        }
        strncpy(udp_server_member.messagesend.ipaddr, aArgs[1], sizeof(udp_server_member.messagesend.ipaddr));
        udp_server_member.messagesend.port = atoi(aArgs[2]);
        strncpy(udp_server_member.messagesend.message, aArgs[3], sizeof(udp_server_member.messagesend.message));
        strcpy(udp_server_member.ifr.ifr_name, "");
        if (aArgsLength == 5 && socket_get_netif_impl_name(aArgs[4], &(udp_server_member.ifr)) != ESP_OK) {
            otCliOutputFormat("invalid commands\n");
            return OT_ERROR_INVALID_ARGS;
        }
        xEventGroupSetBits(udp_server_event_group, UDP_SERVER_SEND_BIT);
    } else if (strcmp(aArgs[0], "close") == 0) {
        if (udp_server_handle == NULL) {
            otCliOutputFormat("UDP server is not open.\n");
            return OT_ERROR_NONE;
        }
        xEventGroupSetBits(udp_server_event_group, UDP_SERVER_CLOSE_BIT);
        udp_server_handle = NULL;
    } else {
        otCliOutputFormat("invalid commands\n");
    }
    return OT_ERROR_NONE;
}

static void udp_client_receive_task(void *pvParameters)
{
    char rx_buffer[128];
    int len = 0;
    char addr_str[128];
    int port = 0;
    struct sockaddr_storage source_addr;
    UDP_CLIENT *udp_client_member = (UDP_CLIENT *)pvParameters;

    while (true) {
        socklen_t socklen = sizeof(source_addr);
        len = recvfrom(udp_client_member->sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr,
                       &socklen);
        if (len < 0) {
            ESP_LOGW(OT_EXT_CLI_TAG, "UDP client fail when receiving message");
        }
        if (len > 0) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
            port = ntohs(((struct sockaddr_in6 *)&source_addr)->sin6_port);
            ESP_LOGI(OT_EXT_CLI_TAG, "sock %d Received %d bytes from %s : %d", udp_client_member->sock, len, addr_str,
                     port);
            rx_buffer[len] = '\0';
            ESP_LOGI(OT_EXT_CLI_TAG, "%s", rx_buffer);
        }
        if (udp_client_member->exist == 0) {
            break;
        }
    }
    ESP_LOGI(OT_EXT_CLI_TAG, "UDP client receive task exiting");
    vTaskDelete(NULL);
}

static void udp_client_send(UDP_CLIENT *udp_client_member)
{
    struct sockaddr_in6 dest_addr = {0};
    int len = 0;

    inet6_aton(udp_client_member->messagesend.ipaddr, &dest_addr.sin6_addr);
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(udp_client_member->messagesend.port);
    ESP_LOGI(OT_EXT_CLI_TAG, "Sending to %s : %d", udp_client_member->messagesend.ipaddr,
             udp_client_member->messagesend.port);
    esp_err_t err = socket_bind_interface(udp_client_member->sock, &(udp_client_member->ifr));
    ESP_RETURN_ON_FALSE(err == ESP_OK, , OT_EXT_CLI_TAG, "Stop sending message");
    len = sendto(udp_client_member->sock, udp_client_member->messagesend.message,
                 strlen(udp_client_member->messagesend.message), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (len < 0) {
        ESP_LOGW(OT_EXT_CLI_TAG, "Fail to send message");
    }
}

static void udp_client_delete(UDP_CLIENT *udp_client_member)
{
    udp_client_member->exist = 0;
    shutdown(udp_client_member->sock, 0);
    close(udp_client_member->sock);
    udp_client_member->sock = -1;
    udp_client_member->local_port = -1;
}

static void udp_socket_client_task(void *pvParameters)
{
    UDP_CLIENT *udp_client_member = (UDP_CLIENT *)pvParameters;

    esp_err_t ret = ESP_OK;
    int err = 0;
    int sock = -1;
    int err_flag = 0;
    struct sockaddr_in6 bind_addr = {0};

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_IPV6);
    ESP_GOTO_ON_FALSE((sock >= 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "Unable to create socket: errno %d", errno);
    ESP_LOGI(OT_EXT_CLI_TAG, "Socket created");
    udp_client_member->sock = sock;
    err_flag = 1;

    if (udp_client_member->local_port != -1) {
        inet6_aton(udp_client_member->local_ipaddr, &bind_addr.sin6_addr);
        inet6_ntoa_r((&bind_addr)->sin6_addr, udp_client_member->local_ipaddr,
                     sizeof(udp_client_member->local_ipaddr) - 1);
        bind_addr.sin6_family = AF_INET6;
        bind_addr.sin6_port = htons(udp_client_member->local_port);

        err = bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
        ESP_GOTO_ON_FALSE((err == 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGI(OT_EXT_CLI_TAG, "Socket bound, port %d", udp_client_member->local_port);
    }

    if (pdPASS != xTaskCreate(udp_client_receive_task, "udp_client_receive", 4096, udp_client_member, 4, NULL)) {
        err = -1;
    }
    ESP_GOTO_ON_FALSE((err == 0), ESP_FAIL, exit, OT_EXT_CLI_TAG, "The UDP client is unable to receive: errno %d",
                      errno);
    udp_client_member->exist = 1;
    ESP_LOGI(OT_EXT_CLI_TAG, "Successfully created");

    while (true) {
        int bits = xEventGroupWaitBits(udp_client_event_group, UDP_CLIENT_SEND_BIT | UDP_CLIENT_CLOSE_BIT, pdFALSE,
                                       pdFALSE, 10000 / portTICK_PERIOD_MS);
        int udp_event = bits & 0x0f;
        if (udp_event == UDP_CLIENT_SEND_BIT) {
            xEventGroupClearBits(udp_client_event_group, UDP_CLIENT_SEND_BIT);
            udp_client_send(udp_client_member);
        } else if (udp_event == UDP_CLIENT_CLOSE_BIT) {
            xEventGroupClearBits(udp_client_event_group, UDP_CLIENT_CLOSE_BIT);
            udp_client_delete(udp_client_member);
            break;
        }
    }
    ESP_LOGI(OT_EXT_CLI_TAG, "Closed UDP client successfully");

exit:
    if (ret != ESP_OK) {
        if (err_flag) {
            udp_client_member->sock = -1;
            shutdown(sock, 0);
            close(sock);
        }
        udp_client_member->local_port = -1;
        ESP_LOGI(OT_EXT_CLI_TAG, "Fail to create a UDP client");
    }
    vEventGroupDelete(udp_client_event_group);
    vTaskDelete(NULL);
}

otError esp_ot_process_udp_client(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    static TaskHandle_t udp_client_handle = NULL;
    static UDP_CLIENT udp_client_member = {0, -1, -1, "::", {""}, {-1, "", ""}};

    if (aArgsLength == 0) {
        otCliOutputFormat("---udpsockclient parameter---\n");
        otCliOutputFormat("status                                   :     get UDP client status\n");
        otCliOutputFormat("open <port>                              :     open UDP client function, create a UDP "
                          "client and bind a local port(optional)\n");
        otCliOutputFormat("send <ipaddr> <port> <message>           :     send a message to the UDP server\n");
        otCliOutputFormat("send <ipaddr> <port> <message> <if>      :     send a message to the UDP server via <if>\n");
        otCliOutputFormat("close                                    :     close UDP client\n");
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("get UDP client status                    :     udpsockclient status\n");
        otCliOutputFormat("create a UDP client without binding      :     udpsockclient open\n");
        otCliOutputFormat("create a UDP client with binding         :     udpsockclient open 12345\n");
        otCliOutputFormat("send a message                           :     udpsockclient send "
                          "FDDE:AD00:BEEF:CAFE:FD14:30B6:CDA:8A95 51876 hello\n");
        otCliOutputFormat("send a message via Wi-Fi interface       :     udpsockclient send "
                          "FDDE:AD00:BEEF:CAFE:FD14:30B6:CDA:8A95 51876 hello st\n");
        otCliOutputFormat("send a message via OpenThread interface  :     udpsockclient send "
                          "FDDE:AD00:BEEF:CAFE:FD14:30B6:CDA:8A95 51876 hello ot\n");
        otCliOutputFormat("close UDP client                         :     udpsockclient close\n");
    } else if (strcmp(aArgs[0], "status") == 0) {
        if (udp_client_handle == NULL) {
            otCliOutputFormat("UDP client is not open\n");
            return OT_ERROR_NONE;
        }
        if (udp_client_member.local_port != -1) {
            otCliOutputFormat("open\tlocal port: %d\n", udp_client_member.local_port);
        } else {
            otCliOutputFormat("open\tnot binded manually\n");
        }

    } else if (strcmp(aArgs[0], "open") == 0) {
        if (aArgsLength != 1 && aArgsLength != 2) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
            return OT_ERROR_INVALID_ARGS;
        }
        if (udp_client_handle == NULL) {
            if (aArgsLength == 2) {
                udp_client_member.local_port = atoi(aArgs[1]);
            }
            udp_client_event_group = xEventGroupCreate();
            ESP_RETURN_ON_FALSE(udp_client_event_group != NULL, OT_ERROR_FAILED, OT_EXT_CLI_TAG,
                                "Fail to open udp client");
            if (pdPASS !=
                xTaskCreate(udp_socket_client_task, "udp_socket_client", 4096, &udp_client_member, 4,
                            &udp_client_handle)) {
                udp_client_handle = NULL;
                udp_client_member.local_port = -1;
                vEventGroupDelete(udp_client_event_group);
                udp_client_event_group = NULL;
                ESP_LOGE(OT_EXT_CLI_TAG, "Fail to open udp client");
                return OT_ERROR_FAILED;
            }
        } else {
            otCliOutputFormat("Already!\n");
        }

    } else if (strcmp(aArgs[0], "bind") == 0) {
        if (udp_client_handle == NULL) {
            otCliOutputFormat("UDP client is not open.\n");
            return OT_ERROR_NONE;
        }
        if (udp_client_member.exist == 1) {
            otCliOutputFormat("UDP client exists.\n");
            return OT_ERROR_NONE;
        }
        if (aArgsLength != 2) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
            return OT_ERROR_INVALID_ARGS;
        }

    } else if (strcmp(aArgs[0], "send") == 0) {
        if (udp_client_handle == NULL) {
            otCliOutputFormat("UDP client is not open.\n");
            return OT_ERROR_NONE;
        }
        if (udp_client_member.exist == 0) {
            otCliOutputFormat("UDP client is not binded!\n");
            return OT_ERROR_NONE;
        }
        if (aArgsLength != 4 && aArgsLength != 5) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
            return OT_ERROR_INVALID_ARGS;
        }
        strncpy(udp_client_member.messagesend.ipaddr, aArgs[1], sizeof(udp_client_member.messagesend.ipaddr));
        udp_client_member.messagesend.port = atoi(aArgs[2]);
        strncpy(udp_client_member.messagesend.message, aArgs[3], sizeof(udp_client_member.messagesend.message));
        strcpy(udp_client_member.ifr.ifr_name, "");
        if (aArgsLength == 5 && socket_get_netif_impl_name(aArgs[4], &(udp_client_member.ifr)) != ESP_OK) {
            otCliOutputFormat("invalid commands\n");
            return OT_ERROR_INVALID_ARGS;
        }
        xEventGroupSetBits(udp_client_event_group, UDP_CLIENT_SEND_BIT);
    } else if (strcmp(aArgs[0], "close") == 0) {
        if (udp_client_handle == NULL) {
            otCliOutputFormat("UDP client is not open.\n");
            return OT_ERROR_NONE;
        }
        xEventGroupSetBits(udp_client_event_group, UDP_CLIENT_CLOSE_BIT);
        udp_client_handle = NULL;
    } else {
        otCliOutputFormat("invalid commands\n");
    }
    return OT_ERROR_NONE;
}
esp_err_t join_ip6_mcast(void *ctx)
{
    ip6_addr_t *group = ctx;
    struct netif *netif = esp_netif_get_netif_impl(esp_netif_get_handle_from_ifkey("OT_DEF"));
    return mld6_joingroup_netif(netif, group) == ERR_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t leave_ip6_mcast(void *ctx)
{
    ip6_addr_t *group = ctx;
    struct netif *netif = esp_netif_get_netif_impl(esp_netif_get_handle_from_ifkey("OT_DEF"));
    return mld6_leavegroup_netif(netif, group) == ERR_OK ? ESP_OK : ESP_FAIL;
}

otError esp_ot_process_mcast_group(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength != 2 || (strncmp(aArgs[0], "join", 4) != 0 && strncmp(aArgs[0], "leave", 5) != 0)) {
        ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments: mcast [join|leave] group_address");
        return OT_ERROR_INVALID_ARGS;
    }
    otError ret = OT_ERROR_NONE;
    esp_openthread_task_switching_lock_release();

    ip6_addr_t group;
    inet6_aton(aArgs[1], &group);
    if (strncmp(aArgs[0], "join", 4) == 0) {
        if (esp_netif_tcpip_exec(join_ip6_mcast, &group) != ESP_OK) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Failed to join group");
            ret = OT_ERROR_FAILED;
        }
    } else {
        if (esp_netif_tcpip_exec(leave_ip6_mcast, &group) != ESP_OK) {
            ESP_LOGE(OT_EXT_CLI_TAG, "Failed to leave group");
            ret = OT_ERROR_FAILED;
        }
    }
    esp_openthread_task_switching_lock_acquire(portMAX_DELAY);
    return ret;
}

esp_err_t socket_get_netif_impl_name(char *name_input, struct ifreq *ifr)
{
    char if_key[32] = "";
    if (strcmp(name_input, "st") == 0) {
        strcpy(if_key, _g_esp_netif_inherent_sta_config.if_key);
    } else if (strcmp(name_input, "ot") == 0) {
        strcpy(if_key, g_esp_netif_inherent_openthread_config.if_key);
    } else {
        return ESP_FAIL;
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(if_key);
    return esp_netif_get_netif_impl_name(netif, ifr->ifr_name);
}

esp_err_t socket_bind_interface(int sock, struct ifreq *ifr)
{
    ESP_RETURN_ON_FALSE(setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, ifr, sizeof(struct ifreq)) == 0, ESP_FAIL,
                        OT_EXT_CLI_TAG, "Failed to bind to interface");
    const char *log = (strlen(ifr->ifr_name) == 0) ? "Automatically select interface" : "Send from interface: ";
    ESP_LOGI(OT_EXT_CLI_TAG, "%s%s", log, ifr->ifr_name);
    return ESP_OK;
}