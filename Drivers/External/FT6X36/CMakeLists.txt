# FT6X36 Capacitive Touch Controller Driver
# 电容触摸屏驱动 (FT6236/FT6336 系列)

add_library(ft6x36 STATIC
    ft6x36.c
)

target_include_directories(ft6x36 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ft6x36 PUBLIC
    i2c_soft
)
