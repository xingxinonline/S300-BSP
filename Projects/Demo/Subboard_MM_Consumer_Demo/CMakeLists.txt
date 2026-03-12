cmake_minimum_required(VERSION 3.16)
project(Subboard_MM_Consumer_Demo)

if(NOT BOARD STREQUAL "gimbal_node")
    message(STATUS "Subboard_MM_Consumer_Demo: skipped for BOARD=${BOARD}, gimbal_node only")
    return()
endif()

set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DSP_BIN_DIR ${PROJECT_DIR}/model_bin)

s300_add_executable(
    TARGET s300_subboard_mm_consumer_demo
    SOURCES
        Src/main.c
        Src/subboard_dsp_ctrl.c
        Src/subboard_startup_i2c.c
    DRIVERS rcc gpio uart mm psram i2c mailbox
    INCLUDES . Inc ${CMAKE_SOURCE_DIR}/Projects/Demo/Subboard_Bringup_Common ${CMAKE_SOURCE_DIR}/Algorithm_Models/protocol
    DBG_NAME subboard_mm_consumer
    MM_ENABLE
    NO_IMAGE
)

target_compile_definitions(s300_subboard_mm_consumer_demo PRIVATE
    BOARD_MM_ENABLE=1
)

cmake_path(ABSOLUTE_PATH CMAKE_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(FACE_DETECTION_MODEL_DIR ${ROOT_ABS}/Algorithm_Models/Face_Detection)

set(SUBBOARD_DSP_IMAGE_DIR ${FACE_DETECTION_MODEL_DIR})
if(EXISTS ${DSP_BIN_DIR}/model_dtcm_boot.bin AND
   EXISTS ${DSP_BIN_DIR}/model_ptcm_boot.bin)
    set(SUBBOARD_DSP_IMAGE_DIR ${DSP_BIN_DIR})
endif()

add_s300_image(s300_subboard_mm_consumer_demo
    OUTPUT s300_subboard_mm_consumer_demo.bin
    DSP_DIR ${SUBBOARD_DSP_IMAGE_DIR}
)

message(STATUS "Subboard_MM_Consumer_Demo: DSP image dir = ${SUBBOARD_DSP_IMAGE_DIR}")