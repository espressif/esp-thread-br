idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES
                        esp_http_client
                        esp_br_http_ota
                        esp_rcp_update
                        esp-serial-flasher
                        esp_ot_cli_extension
                        openthread
                        vfs
)
