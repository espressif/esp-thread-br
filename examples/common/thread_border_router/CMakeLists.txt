set(requires esp_ot_cli_extension openthread protocol_examples_common vfs esp_wifi esp_eth esp_rcp_update)

if(CONFIG_OPENTHREAD_CLI_OTA)
    list(APPEND requires esp_http_client esp_br_http_ota)
endif()

idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES "${requires}"
)
