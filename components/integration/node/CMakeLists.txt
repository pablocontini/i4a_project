idf_component_register(SRCS "node.c" "offer_policy.c"
                    INCLUDE_DIRS "."
                    REQUIRES config device client node_ota ring_link callbacks channel_manager reset_manager internal_messages traffic info_manager remote_control task_config discovery_manager neighbor_manager wifi_credentials config_portal portal_settings)



if(DEFINED I4A_HOUSE_SSID)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "I4A_HOUSE_SSID=\"${I4A_HOUSE_SSID}\"")
endif()
