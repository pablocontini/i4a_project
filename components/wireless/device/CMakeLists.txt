idf_component_register(SRCS "device.c" "point_to_point.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi nvs_flash access_point station client server task_config config priority_manager node neighbor_manager)
