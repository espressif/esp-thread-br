/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_ot_iperf.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ot_cli_extension.h"
#include "iperf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/inet.h"
#include "openthread/cli.h"

static char s_dest_ip_addr[50];

otError esp_ot_process_iperf(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    (void)(aContext);
    (void)(aArgsLength);
    iperf_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    int client_flag = 0;
    // set iperf default flag: tcp server
    IPERF_FLAG_SET(cfg.flag, IPERF_FLAG_TCP);
    IPERF_FLAG_SET(cfg.flag, IPERF_FLAG_SERVER);
    cfg.time = IPERF_DEFAULT_TIME;
    cfg.type = IPERF_IP_TYPE_IPV4;
    cfg.len_send_buf = OT_IPERF_DEFAULT_LEN;
    cfg.format = KBITS_PER_SEC;
    if (aArgsLength == 0) {
        otCliOutputFormat("---iperf parameter---\n");
        otCliOutputFormat("-V                  :     use IPV6 address\n");
        otCliOutputFormat("-s                  :     server mode, only receive\n");
        otCliOutputFormat("-u                  :     upd mode\n");
        otCliOutputFormat("-c <addr>           :     client mode, only transmit\n");
        otCliOutputFormat("-i <interval>       :     seconds between periodic bandwidth reports\n");
        otCliOutputFormat("-t <time>           :     time in seconds to transmit for (default 30 secs)\n");
        otCliOutputFormat("-p <port>           :     server port to listen on/connect to\n");
        otCliOutputFormat("-l <len_send_buf>   :     the lenth of send buffer\n");
        otCliOutputFormat("-f <output_format>  :     the output format of the report (Mbit/sec, Kbit/sec, bit/sec; "
                          "default Mbit/sec)\n");
        otCliOutputFormat("---example---\n");
        otCliOutputFormat("create a tcp server :     iperf -V -s -i 3 -p 5001 -t 60 -f M\n");
        otCliOutputFormat("create a udp client :     iperf -V -c <addr> -u -i 3 -t 60 -p 5001 -l 512 -f B\n");
    } else {
        for (int i = 0; i < aArgsLength; i++) {
            if (strcmp(aArgs[i], "-c") == 0) {
                IPERF_FLAG_SET(cfg.flag, IPERF_FLAG_CLIENT);
                IPERF_FLAG_CLR(cfg.flag, IPERF_FLAG_SERVER);
                i++;
                strcpy(s_dest_ip_addr, aArgs[i]);
                client_flag = 1;
            } else if (strcmp(aArgs[i], "-s") == 0) {
                IPERF_FLAG_SET(cfg.flag, IPERF_FLAG_SERVER);
                IPERF_FLAG_CLR(cfg.flag, IPERF_FLAG_CLIENT);
            } else if (strcmp(aArgs[i], "-V") == 0) {
                cfg.type = IPERF_IP_TYPE_IPV6;
            } else if (strcmp(aArgs[i], "-u") == 0) {
                IPERF_FLAG_SET(cfg.flag, IPERF_FLAG_UDP);
                IPERF_FLAG_CLR(cfg.flag, IPERF_FLAG_TCP);
            } else if (strcmp(aArgs[i], "-p") == 0) {
                i++;
                if (cfg.flag & IPERF_FLAG_SERVER) {
                    cfg.sport = atoi(aArgs[i]);
                    cfg.dport = IPERF_DEFAULT_PORT;
                } else {
                    cfg.sport = IPERF_DEFAULT_PORT;
                    cfg.dport = atoi(aArgs[i]);
                }
                otCliOutputFormat("dp:%d\n", cfg.dport);
                otCliOutputFormat("sp:%d\n", cfg.sport);
            } else if (strcmp(aArgs[i], "-i") == 0) {
                i++;
                if (atoi(aArgs[i]) < 0) {
                    cfg.interval = IPERF_DEFAULT_INTERVAL;
                } else {
                    cfg.interval = atoi(aArgs[i]);
                }
                otCliOutputFormat("i:%d\n", cfg.interval);
            } else if (strcmp(aArgs[i], "-t") == 0) {
                i++;
                cfg.time = atoi(aArgs[i]);
                if (cfg.time <= cfg.interval) {
                    cfg.time = cfg.interval;
                }
                otCliOutputFormat("t:%d\n", cfg.time);
            } else if (strcmp(aArgs[i], "-l") == 0) {
                i++;
                if (atoi(aArgs[i]) <= 0) {
                    ESP_LOGE(OT_EXT_CLI_TAG, "Invalid arguments.");
                } else {
                    cfg.len_send_buf = atoi(aArgs[i]);
                }
            } else if (strcmp(aArgs[i], "-a") == 0) {
                iperf_stop();
                return OT_ERROR_NONE;
            } else if (strcmp(aArgs[i], "-f") == 0) {
                i++;
                const char *unit[3] = {"M", "K", "B"};
                if (i >= aArgsLength) {
                    return OT_ERROR_INVALID_ARGS;
                } else {
                    for (uint8_t idx = 0; idx <= 3; idx++) {
                        if (idx == 3) {
                            return OT_ERROR_INVALID_ARGS;
                        }
                        if (strcmp(aArgs[i], unit[idx]) == 0) {
                            cfg.format = idx;
                            break;
                        }
                    }
                }
                otCliOutputFormat("f:%sbit/s\n", strcmp(unit[cfg.format], "B") == 0 ? "\0" : unit[cfg.format]);
            }
        }
        if (client_flag) {
            if (cfg.type == IPERF_IP_TYPE_IPV4) {
                cfg.destination_ip4 = inet_addr(s_dest_ip_addr);
                char ip_addr[50];
                strncpy(ip_addr, inet_ntoa(cfg.destination_ip4), sizeof(ip_addr));
                otCliOutputFormat("ip:%s\n", ip_addr);
            } else {
                cfg.destination_ip6 = s_dest_ip_addr;
                otCliOutputFormat("ip:%s\n", cfg.destination_ip6);
            }
        }
        iperf_start(&cfg);
    }
    return OT_ERROR_NONE;
}
