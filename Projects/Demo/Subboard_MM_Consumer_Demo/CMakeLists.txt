cmake_minimum_required(VERSION 3.16)
project(Subboard_MM_Consumer_Demo)

if(NOT BOARD STREQUAL "gimbal_node")
    message(STATUS "Subboard_MM_Consumer_Demo: skipped for BOARD=${BOARD}, gimbal_node only")
    return()
endif()

set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)
set(DSP_BIN_DIR ${PROJECT_DIR}/model_bin)
set(SUBBOARD_GDB_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/gdbinit.subboard_mm_consumer.gdb.in)
set(SUBBOARD_GDB_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.subboard_mm_consumer.gdb)
set(SUBBOARD_MM_CONSUMER_LOG_LEVEL "INFO" CACHE STRING "Subboard MM consumer log level: WARN, INFO, DEBUG")

string(TOUPPER "${SUBBOARD_MM_CONSUMER_LOG_LEVEL}" SUBBOARD_MM_CONSUMER_LOG_LEVEL_NAME)
if(SUBBOARD_MM_CONSUMER_LOG_LEVEL_NAME STREQUAL "WARN")
    set(SUBBOARD_MM_CONSUMER_LOG_LEVEL_VALUE 1)
elseif(SUBBOARD_MM_CONSUMER_LOG_LEVEL_NAME STREQUAL "INFO")
    set(SUBBOARD_MM_CONSUMER_LOG_LEVEL_VALUE 2)
elseif(SUBBOARD_MM_CONSUMER_LOG_LEVEL_NAME STREQUAL "DEBUG")
    set(SUBBOARD_MM_CONSUMER_LOG_LEVEL_VALUE 3)
else()
    message(FATAL_ERROR "Subboard_MM_Consumer_Demo: invalid SUBBOARD_MM_CONSUMER_LOG_LEVEL=${SUBBOARD_MM_CONSUMER_LOG_LEVEL}")
endif()

s300_add_executable(
    TARGET s300_subboard_mm_consumer_demo
    SOURCES
        Src/main.c
        Src/subboard_mm_app.c
        Src/subboard_mm_app_runtime.c
        Src/subboard_mm_app_commands.c
        Src/subboard_dsp_ctrl.c
        Src/subboard_dsp_control_plane.c
        Src/subboard_detection_adapter.c
        Src/subboard_dsp_mailbox.c
        Src/subboard_startup_i2c.c
    DRIVERS rcc gpio uart mm psram i2c mailbox
    INCLUDES . Inc ${CMAKE_SOURCE_DIR}/Projects/Demo/Subboard_Bringup_Common ${CMAKE_SOURCE_DIR}/Algorithm_Models/protocol
    DBG_NAME subboard_mm_consumer
    DBG_SCRIPT ${SUBBOARD_GDB_SCRIPT}
    MM_ENABLE
    NO_IMAGE
)

target_compile_definitions(s300_subboard_mm_consumer_demo PRIVATE
    BOARD_MM_ENABLE=1
    SUBBOARD_LOG_LEVEL=${SUBBOARD_MM_CONSUMER_LOG_LEVEL_VALUE}
)

cmake_path(ABSOLUTE_PATH CMAKE_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(FACE_DETECTION_MODEL_DIR ${ROOT_ABS}/Algorithm_Models/Face_Detection)

set(SUBBOARD_DSP_IMAGE_DIR ${FACE_DETECTION_MODEL_DIR})
if(EXISTS ${DSP_BIN_DIR}/model_dtcm_boot.bin AND
   EXISTS ${DSP_BIN_DIR}/model_ptcm_boot.bin)
    set(SUBBOARD_DSP_IMAGE_DIR ${DSP_BIN_DIR})
endif()

add_custom_command(TARGET s300_subboard_mm_consumer_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DDSP_BIN_DIR=${SUBBOARD_DSP_IMAGE_DIR}
        -DTEMPLATE_FILE=${SUBBOARD_GDB_TEMPLATE}
        -DOUTPUT_FILE=${SUBBOARD_GDB_SCRIPT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_subboard_mm_consumer_gdb.cmake
    COMMENT "Refreshing Subboard_MM_Consumer_Demo GDB script from current DSP bins"
)

add_s300_image(s300_subboard_mm_consumer_demo
    OUTPUT s300_subboard_mm_consumer_demo.bin
    DSP_DIR ${SUBBOARD_DSP_IMAGE_DIR}
)

message(STATUS "Subboard_MM_Consumer_Demo: DSP image dir = ${SUBBOARD_DSP_IMAGE_DIR}")
message(STATUS "Subboard_MM_Consumer_Demo: log level = ${SUBBOARD_MM_CONSUMER_LOG_LEVEL_NAME}")