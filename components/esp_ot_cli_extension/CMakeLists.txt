set(srcs    "src/esp_ot_cli_extension.c")

if(CONFIG_OPENTHREAD_CLI_IPERF)
    list(APPEND srcs   "src/esp_ot_iperf.c")
endif()

if(CONFIG_OPENTHREAD_CLI_SOCKET)
    list(APPEND srcs    "src/esp_ot_tcp_socket.c"
                        "src/esp_ot_udp_socket.c")
endif()

if(CONFIG_OPENTHREAD_CLI_WIFI)
    list(APPEND srcs   "src/esp_ot_wifi_cmd.c")
endif()

if(CONFIG_OPENTHREAD_CLI_OTA)
    list(APPEND srcs   "src/esp_ot_ota_commands.c")
endif()

set(include "include")
idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${include}"
                    REQUIRES lwip openthread iperf esp_netif esp_wifi)

if (CONFIG_OPENTHREAD_CLI_OTA)
    idf_component_optional_requires(PUBLIC esp_br_http_ota)
endif()

