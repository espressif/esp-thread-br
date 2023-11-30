set(srcs    "src/esp_ot_cli_extension.c"
            "src/esp_ot_curl.c"
            "src/esp_ot_heap_diag.c"
            "src/esp_ot_ip.c"
            "src/esp_ot_iperf.c"
            "src/esp_ot_loglevel.c"
            "src/esp_ot_tcp_socket.c"
            "src/esp_ot_udp_socket.c")

if(CONFIG_OPENTHREAD_CLI_WIFI)
    list(APPEND srcs   "src/esp_ot_wifi_cmd.c")
endif()

if(CONFIG_OPENTHREAD_CLI_OTA)
    list(APPEND srcs   "src/esp_ot_ota_commands.c")
endif()

if(CONFIG_OPENTHREAD_DNS64_CLIENT)
    list(APPEND srcs   "src/esp_ot_dns64.c")
endif()

if(CONFIG_OPENTHREAD_NVS_DIAG)
    list(APPEND srcs   "src/esp_ot_nvs_diag.c")
endif()

set(include "include")
idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${include}"
                    PRIV_REQUIRES lwip openthread iperf esp_netif esp_wifi http_parser esp_http_client esp_coex heap mbedtls nvs_flash)

if (CONFIG_OPENTHREAD_CLI_OTA)
    idf_component_optional_requires(PUBLIC esp_br_http_ota)
endif()

