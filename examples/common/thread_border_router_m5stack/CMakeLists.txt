set(requires esp_ot_cli_extension openthread protocol_examples_common vfs esp_wifi esp_eth esp_rcp_update m5stack_core_s3)

if(CONFIG_OPENTHREAD_CLI_OTA)
    list(APPEND requires esp_http_client esp_br_http_ota)
endif()

set(extra_include_dirs
    $ENV{IDF_PATH}/components/openthread/private_include)

idf_component_register(SRC_DIRS "src" "image"
                       INCLUDE_DIRS "include" "private_include" "${extra_include_dirs}"
                       REQUIRES "${requires}"
)
