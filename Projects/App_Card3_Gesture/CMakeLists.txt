cmake_minimum_required(VERSION 3.16)
project(App_Card3_Gesture)

if(NOT BOARD STREQUAL "gimbal_node")
    message(STATUS "App_Card3_Gesture: skipped for BOARD=${BOARD}, gimbal_node only")
    return()
endif()

set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../..)
set(SUBBOARD_COMMON_DIR ${ROOT}/Projects/Common/Subboard_App_Common)
include(${ROOT}/cmake/s300_model_metadata.cmake)
set(DSP_BIN_DIR ${PROJECT_DIR}/model_bin)
set(SUBBOARD_GDB_TEMPLATE ${PROJECT_DIR}/gdbinit.card3_gesture.gdb.in)
set(SUBBOARD_GDB_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.card3_gesture.gdb)
set(SUBBOARD_GDB_GENERATOR ${PROJECT_DIR}/generate_card3_gesture_gdb.cmake)
set(MODEL_MANIFEST ${CMAKE_CURRENT_BINARY_DIR}/card3_gesture_model_manifest.txt)
set(CARD3_APP_LOG_LEVEL "INFO" CACHE STRING "Card3 app log level: WARN, INFO, DEBUG")

string(TOUPPER "${CARD3_APP_LOG_LEVEL}" CARD3_APP_LOG_LEVEL_NAME)
if(CARD3_APP_LOG_LEVEL_NAME STREQUAL "WARN")
    set(CARD3_APP_LOG_LEVEL_VALUE 1)
elseif(CARD3_APP_LOG_LEVEL_NAME STREQUAL "INFO")
    set(CARD3_APP_LOG_LEVEL_VALUE 2)
elseif(CARD3_APP_LOG_LEVEL_NAME STREQUAL "DEBUG")
    set(CARD3_APP_LOG_LEVEL_VALUE 3)
else()
    message(FATAL_ERROR "App_Card3_Gesture: invalid CARD3_APP_LOG_LEVEL=${CARD3_APP_LOG_LEVEL}")
endif()

cmake_path(ABSOLUTE_PATH CMAKE_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(HAND_GESTURE_MODEL_DIR ${ROOT_ABS}/Algorithm_Models/Hand_Gesture)
set(APP_CARD3_DSP_IMAGE_DIR ${HAND_GESTURE_MODEL_DIR})
set(APP_CARD3_USE_LOCAL_MODEL FALSE)

if(EXISTS ${DSP_BIN_DIR}/model_dtcm_boot.bin AND
   EXISTS ${DSP_BIN_DIR}/model_ptcm_boot.bin)
    set(APP_CARD3_DSP_IMAGE_DIR ${DSP_BIN_DIR})
    set(APP_CARD3_USE_LOCAL_MODEL TRUE)
endif()

s300_load_model_metadata("${APP_CARD3_DSP_IMAGE_DIR}" APP_CARD3_MODEL)

if(APP_CARD3_USE_LOCAL_MODEL AND NOT APP_CARD3_MODEL_HAS_INFO)
    message(WARNING "App_Card3_Gesture: local model_bin selected but model_info.json is missing, model version will be reported as Unknown")
endif()

file(GENERATE OUTPUT ${MODEL_MANIFEST}
    CONTENT
"model_dir=${APP_CARD3_DSP_IMAGE_DIR}\nmodel_source=$<IF:$<BOOL:${APP_CARD3_USE_LOCAL_MODEL}>,local_override,algorithm_models>\nmodel_name=${APP_CARD3_MODEL_NAME}\nmodel_id=${APP_CARD3_MODEL_ID}\nmodel_version=${APP_CARD3_MODEL_VERSION}\nmodel_date=${APP_CARD3_MODEL_DATE}\n")

s300_add_executable(
    TARGET s300_card3_gesture
    SOURCES
        Src/main.c
        ${SUBBOARD_COMMON_DIR}/Src/subboard_mm_app.c
        ${SUBBOARD_COMMON_DIR}/Src/subboard_mm_app_runtime.c
        ${SUBBOARD_COMMON_DIR}/Src/subboard_mm_app_commands.c
        ${SUBBOARD_COMMON_DIR}/Src/subboard_dsp_ctrl.c
        ${SUBBOARD_COMMON_DIR}/Src/subboard_dsp_control_plane.c
        ${SUBBOARD_COMMON_DIR}/Src/subboard_dsp_mailbox.c
        ${SUBBOARD_COMMON_DIR}/Src/subboard_startup_i2c.c
        ${SUBBOARD_COMMON_DIR}/Src/subboard_detection_adapter.c
    DRIVERS rcc gpio uart mm mailbox psram i2c
    INCLUDES . Inc ${SUBBOARD_COMMON_DIR}/Inc ${CMAKE_SOURCE_DIR}/Projects/Common/Subboard_Protocol ${CMAKE_SOURCE_DIR}/Algorithm_Models/protocol
    DBG_SCRIPT ${SUBBOARD_GDB_SCRIPT}
    NO_IMAGE
    MM_ENABLE
)

target_compile_definitions(s300_card3_gesture PRIVATE
    BOARD_MM_ENABLE=1
    SUBBOARD_LOG_LEVEL=${CARD3_APP_LOG_LEVEL_VALUE}
)

add_custom_command(TARGET s300_card3_gesture POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DDSP_BIN_DIR=${APP_CARD3_DSP_IMAGE_DIR}
        -DTEMPLATE_FILE=${SUBBOARD_GDB_TEMPLATE}
        -DOUTPUT_FILE=${SUBBOARD_GDB_SCRIPT}
        -P ${SUBBOARD_GDB_GENERATOR}
    COMMENT "Refreshing App_Card3_Gesture GDB script from current DSP bins"
)

add_s300_image(s300_card3_gesture
    OUTPUT s300_card3_gesture.bin
    DSP_DIR ${APP_CARD3_DSP_IMAGE_DIR}
)

add_custom_target(dbg_card3
    DEPENDS dbg_card3_gesture
)

message(STATUS "App_Card3_Gesture: DSP image dir = ${APP_CARD3_DSP_IMAGE_DIR}")
message(STATUS "App_Card3_Gesture: model = ${APP_CARD3_MODEL_ID} ${APP_CARD3_MODEL_VERSION} (${APP_CARD3_MODEL_DATE})")
message(STATUS "App_Card3_Gesture: model manifest = ${MODEL_MANIFEST}")
message(STATUS "App_Card3_Gesture: log level = ${CARD3_APP_LOG_LEVEL_NAME}")