cmake_minimum_required(VERSION 3.16)
project(Subboard_Video_Bringup_Master_Demo)

if(NOT BOARD STREQUAL "gimbal_master")
    message(STATUS "Subboard_Video_Bringup_Master_Demo: skipped for BOARD=${BOARD}, gimbal_master only")
    return()
endif()

set(SHARED_DETECTION_DIR ${CMAKE_SOURCE_DIR}/Projects/Demo/Algorithm_Detection_Demo)

s300_add_executable(
    TARGET s300_subboard_video_bringup_master_demo
    SOURCES
        Src/main.c
        Src/master_demo_app.c
        Src/master_detection_overlay.c
        ${SHARED_DETECTION_DIR}/Src/camera_ov5640.c
    DRIVERS rcc gpio uart mm i2c_soft psram
    INCLUDES . Inc ${SHARED_DETECTION_DIR}/Inc ${CMAKE_SOURCE_DIR}/Projects/Demo/Subboard_Bringup_Common
    DBG_NAME subboard_video_bringup_master
    MM_ENABLE
)

target_compile_definitions(s300_subboard_video_bringup_master_demo PRIVATE
    BOARD_MM_ENABLE=1
)