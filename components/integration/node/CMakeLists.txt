idf_component_register(SRCS "node.c"
                    INCLUDE_DIRS "."
                    REQUIRES config device ring_link callbacks channel_manager reset_manager internal_messages traffic info_manager remote_control task_config config_portal portal_settings wifi_credentials)

if(DEFINED I4A_HOUSE_NETWORK_NAME)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "I4A_HOUSE_NETWORK_NAME=\"${I4A_HOUSE_NETWORK_NAME}\"")
endif()


