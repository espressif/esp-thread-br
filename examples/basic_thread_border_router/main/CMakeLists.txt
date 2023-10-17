idf_component_register(SRCS "esp_ot_br.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
                       )

idf_build_get_property(python PYTHON)
if(CONFIG_OPENTHREAD_BR_AUTO_UPDATE_RCP)
    add_custom_target(rcp_image_generation ALL
        COMMAND ${python} ${CMAKE_CURRENT_SOURCE_DIR}/../create_ota_image.py
        --rcp-build-dir $ENV{IDF_PATH}/examples/openthread/ot_rcp/build
        --target-file ${CMAKE_CURRENT_BINARY_DIR}/spiffs_image/ot_rcp_0/rcp_image
        )

    spiffs_create_partition_image(rcp_fw ${CMAKE_CURRENT_BINARY_DIR}/spiffs_image FLASH_IN_PROJECT
                                  DEPENDS rcp_image_generation)

endif()

if(CONFIG_OPENTHREAD_BR_CREATE_OTA_IMAGE)
    add_custom_command(OUTPUT ${build_dir}/br_ota_image
        COMMAND ${python} ${CMAKE_CURRENT_SOURCE_DIR}/../create_ota_image.py
        --rcp-build-dir $ENV{IDF_PATH}/examples/openthread/ot_rcp/build
        --target-file ${build_dir}/br_ota_image
        --br-firmware "${build_dir}/${elf_name}.bin"
        DEPENDS "${build_dir}/.bin_timestamp"
        )
        
    add_custom_target(gen_ota_image ALL DEPENDS ${build_dir}/br_ota_image)
    add_dependencies(gen_ota_image gen_project_binary)
endif()
