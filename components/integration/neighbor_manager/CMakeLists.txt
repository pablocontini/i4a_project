idf_component_register(
    SRCS "src/registry.c" "src/neighbor_manager.c" "src/blocks.c" "src/block_store.c"
    INCLUDE_DIRS "include"
    REQUIRES ring_share os esp_timer task_config nvs_flash
)
