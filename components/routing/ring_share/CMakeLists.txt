idf_component_register(
    SRCS "src/ring_share.c"
    INCLUDE_DIRS "include"
    REQUIRES os siblings esp_timer ring_link_lowlevel
)
