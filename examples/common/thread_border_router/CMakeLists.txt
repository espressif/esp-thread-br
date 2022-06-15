idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES
                        border_router_board
                        esp_http_client
                        esp_rcp_http_download
                        esp_rcp_update
                        esp-serial-flasher
                        esp_ot_cli_extension
                        openthread
)
