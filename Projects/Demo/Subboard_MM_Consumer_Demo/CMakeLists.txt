cmake_minimum_required(VERSION 3.16)
project(Subboard_MM_Consumer_Demo)

if(NOT BOARD STREQUAL "gimbal_node")
    message(STATUS "Subboard_MM_Consumer_Demo: skipped for BOARD=${BOARD}, gimbal_node only")
    return()
endif()

s300_add_executable(
    TARGET s300_subboard_mm_consumer_demo
    SOURCES
        Src/main.c
        Src/subboard_startup_i2c.c
    DRIVERS rcc gpio uart mm psram i2c
    INCLUDES . Inc ${CMAKE_SOURCE_DIR}/Projects/Demo/Subboard_Bringup_Common
    DBG_NAME subboard_mm_consumer
    MM_ENABLE
)

target_compile_definitions(s300_subboard_mm_consumer_demo PRIVATE
    BOARD_MM_ENABLE=1
)