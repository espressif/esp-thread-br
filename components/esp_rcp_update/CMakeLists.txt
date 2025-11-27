# Starting from esp-idf v5.3, the GPIO and UART drivers are moved to separate components
if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
    set (priv_requires "esp_driver_gpio" "esp_driver_uart")
else()
    set (priv_requires "driver")
endif()

set(exclude_srcs "")

if(NOT CONFIG_OPENTHREAD_ENABLED OR NOT CONFIG_AUTO_UPDATE_RCP)
    list(APPEND exclude_srcs "src/esp_ot_rcp_update.c")
endif()

idf_component_register(SRC_DIRS src
                       EXCLUDE_SRCS ${exclude_srcs}
                       INCLUDE_DIRS include
                       REQUIRES esp-serial-flasher nvs_flash
                       PRIV_REQUIRES ${priv_requires})

if(CONFIG_OPENTHREAD_ENABLED)
    idf_component_optional_requires(PRIVATE openthread)
endif()

idf_build_get_property(python PYTHON)
if(CONFIG_AUTO_UPDATE_RCP)
add_custom_target(rcp_image_generation ALL
    COMMAND ${python} ${CMAKE_CURRENT_SOURCE_DIR}/create_ota_image.py
    --rcp-build-dir ${CONFIG_RCP_SRC_DIR}
    --target-file ${CMAKE_CURRENT_BINARY_DIR}/spiffs_image/ot_rcp_0/rcp_image
    )

spiffs_create_partition_image(${CONFIG_RCP_PARTITION_NAME} ${CMAKE_CURRENT_BINARY_DIR}/spiffs_image FLASH_IN_PROJECT
                                DEPENDS rcp_image_generation)
endif()

if(CONFIG_CREATE_OTA_IMAGE_WITH_RCP_FW)
    idf_build_get_property(elf_name EXECUTABLE_NAME GENERATOR_EXPRESSION)
    add_custom_command(OUTPUT ${build_dir}/ota_with_rcp_image
        COMMAND ${python} ${CMAKE_CURRENT_SOURCE_DIR}/create_ota_image.py
        --rcp-build-dir ${CONFIG_RCP_SRC_DIR}
        --target-file ${build_dir}/ota_with_rcp_image
        --br-firmware "${build_dir}/${elf_name}.bin"
        DEPENDS "${build_dir}/.bin_timestamp"
        )

    add_custom_target(gen_ota_image ALL DEPENDS ${build_dir}/ota_with_rcp_image)
    add_dependencies(gen_ota_image gen_project_binary)
endif()
