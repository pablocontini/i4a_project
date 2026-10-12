idf_component_register(
    SRCS "src/discovery_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES ring_share neighbor_manager os esp_timer esp_hw_support task_config
)
