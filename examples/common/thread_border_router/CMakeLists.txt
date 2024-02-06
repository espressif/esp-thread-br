set(requires esp_ot_cli_extension openthread protocol_examples_common vfs esp_wifi)

if(CONFIG_OPENTHREAD_CLI_OTA)
    list(APPEND requires esp_http_client esp_br_http_ota)
endif()

if(CONFIG_OPENTHREAD_BR_AUTO_UPDATE_RCP)
    list(APPEND requires esp_rcp_update esp-serial-flasher)
endif()

idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES "${requires}"
)
