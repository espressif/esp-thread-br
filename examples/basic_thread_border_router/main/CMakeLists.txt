idf_component_register(SRCS "esp_ot_br.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
                       )

if(CONFIG_OPENTHREAD_BR_AUTO_UPDATE_RCP)
    add_custom_target(rcp_image_generation ALL
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../create_example_spiffs.py
        -b $ENV{IDF_PATH}/examples/openthread/ot_rcp/build
        -t ${CMAKE_CURRENT_BINARY_DIR}/spiffs_image/ot_rcp_0
        )

    spiffs_create_partition_image(rcp_fw ${CMAKE_CURRENT_BINARY_DIR}/spiffs_image FLASH_IN_PROJECT
                                  DEPENDS rcp_image_generation)
endif()
