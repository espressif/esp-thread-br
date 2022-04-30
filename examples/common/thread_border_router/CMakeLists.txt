idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES 
                        esp_http_client
                        esp_rcp_http_download
                        esp_rcp_update
                        esp-serial-flasher 
                        extension_command
                        openthread 
)
