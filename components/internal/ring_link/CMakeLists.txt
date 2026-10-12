idf_component_register(SRCS 
     "ring_link.c"
    INCLUDE_DIRS "include"
    REQUIRES spi link_diagnostics ring_link_lowlevel ring_link_internal ring_link_netif task_config
)
