# =============================================================================
# Touch_Test Demo - 电容触摸屏 FT6X36 测试
# =============================================================================
# 验证触摸驱动是否正常工作
# 仅支持 app_board（应用板带触摸屏）
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(Touch_Test)

# 仅在 app_board 上编译此 Demo（其他板无触摸屏硬件）
if(NOT BOARD STREQUAL "app_board")
    message(STATUS "Touch_Test: Skipped (only supported on app_board)")
    return()
endif()

# ft6x36 -> i2c_soft -> gpio -> rcc (自动解析)
s300_add_executable(
    TARGET s300_touch_test
    SOURCES Src/main.c
    DRIVERS uart ft6x36
    DEFINES BOARD_TOUCH_ENABLE=1
    DBG_NAME touch
)
