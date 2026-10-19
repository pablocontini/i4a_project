idf_component_register(SRCS "main.c" "capture_diagnostics.c"
                       INCLUDE_DIRS "."
                       REQUIRES node callbacks internal_messages ring_share routing routing_config shared_state siblings sync wireless routing_hooks task_config info_manager node_ota neighbor_manager spi ring_link)

idf_component_get_property(lwip lwip COMPONENT_LIB)
target_compile_options(${lwip} PRIVATE "-I${CMAKE_SOURCE_DIR}/../components/internal/lwip_custom_hooks")
target_compile_definitions(${lwip} PRIVATE "-DESP_IDF_LWIP_HOOK_FILENAME=\"lwip_custom_hooks.h\"")


