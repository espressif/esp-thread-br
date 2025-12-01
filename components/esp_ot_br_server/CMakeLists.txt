set(requires mdns fatfs spiffs esp_eth nvs_flash freertos openthread esp_http_server protocol_examples_common esp_wifi)

# For IDF < 6.0, use IDF's built-in json component; for IDF >= 6.0 use managed cJSON component through idf_component.yml
if("${IDF_VERSION_MAJOR}" VERSION_LESS "6")
    list(APPEND requires json)
endif()

idf_component_register(
    SRC_DIRS src
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS private_include
    PRIV_REQUIRES ${requires}
    EMBED_FILES "favicon.ico"
    EMBED_TXTFILES "frontend/wifi_configuration.html"
)

spiffs_create_partition_image(web_storage ${CMAKE_CURRENT_SOURCE_DIR}/frontend FLASH_IN_PROJECT)
