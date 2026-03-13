cmake_minimum_required(VERSION 3.16)
project(App_Card1_HumanDetection)

if(NOT BOARD STREQUAL "gimbal_node")
    message(STATUS "App_Card1_HumanDetection: skipped for BOARD=${BOARD}, gimbal_node only")
    return()
endif()

set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../..)
include(${ROOT}/cmake/s300_model_metadata.cmake)
set(DSP_BIN_DIR ${PROJECT_DIR}/model_bin)
set(SUBBOARD_GDB_TEMPLATE ${PROJECT_DIR}/gdbinit.card1_human_detection.gdb.in)
set(SUBBOARD_GDB_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.card1_human_detection.gdb)
set(SUBBOARD_GDB_GENERATOR ${PROJECT_DIR}/generate_card1_human_detection_gdb.cmake)
set(MODEL_MANIFEST ${CMAKE_CURRENT_BINARY_DIR}/card1_human_detection_model_manifest.txt)
set(CARD1_APP_LOG_LEVEL "INFO" CACHE STRING "Card1 app log level: WARN, INFO, DEBUG")

string(TOUPPER "${CARD1_APP_LOG_LEVEL}" CARD1_APP_LOG_LEVEL_NAME)
if(CARD1_APP_LOG_LEVEL_NAME STREQUAL "WARN")
    set(CARD1_APP_LOG_LEVEL_VALUE 1)
elseif(CARD1_APP_LOG_LEVEL_NAME STREQUAL "INFO")
    set(CARD1_APP_LOG_LEVEL_VALUE 2)
elseif(CARD1_APP_LOG_LEVEL_NAME STREQUAL "DEBUG")
    set(CARD1_APP_LOG_LEVEL_VALUE 3)
else()
    message(FATAL_ERROR "App_Card1_HumanDetection: invalid CARD1_APP_LOG_LEVEL=${CARD1_APP_LOG_LEVEL}")
endif()

cmake_path(ABSOLUTE_PATH CMAKE_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(HUMAN_DETECTION_MODEL_DIR ${ROOT_ABS}/Algorithm_Models/Human_Detection)
set(APP_CARD1_DSP_IMAGE_DIR ${HUMAN_DETECTION_MODEL_DIR})
set(APP_CARD1_USE_LOCAL_MODEL FALSE)

if(EXISTS ${DSP_BIN_DIR}/model_dtcm_boot.bin AND
   EXISTS ${DSP_BIN_DIR}/model_ptcm_boot.bin)
    set(APP_CARD1_DSP_IMAGE_DIR ${DSP_BIN_DIR})
    set(APP_CARD1_USE_LOCAL_MODEL TRUE)
endif()

s300_load_model_metadata("${APP_CARD1_DSP_IMAGE_DIR}" APP_CARD1_MODEL)

if(APP_CARD1_USE_LOCAL_MODEL AND NOT APP_CARD1_MODEL_HAS_INFO)
    message(WARNING "App_Card1_HumanDetection: local model_bin selected but model_info.json is missing, model version will be reported as Unknown")
endif()

file(GENERATE OUTPUT ${MODEL_MANIFEST}
    CONTENT
"model_dir=${APP_CARD1_DSP_IMAGE_DIR}\nmodel_source=$<IF:$<BOOL:${APP_CARD1_USE_LOCAL_MODEL}>,local_override,algorithm_models>\nmodel_name=${APP_CARD1_MODEL_NAME}\nmodel_id=${APP_CARD1_MODEL_ID}\nmodel_version=${APP_CARD1_MODEL_VERSION}\nmodel_date=${APP_CARD1_MODEL_DATE}\n")

s300_add_executable(
    TARGET s300_card1_human_detection
    SOURCES
        Src/main.c
        Src/subboard_mm_app.c
        Src/subboard_mm_app_runtime.c
        Src/subboard_mm_app_commands.c
        Src/subboard_dsp_ctrl.c
        Src/subboard_dsp_control_plane.c
        Src/subboard_dsp_mailbox.c
        Src/subboard_startup_i2c.c
        Src/subboard_detection_adapter.c
    DRIVERS rcc gpio uart mm mailbox psram i2c
    INCLUDES . Inc ${CMAKE_SOURCE_DIR}/Projects/Demo/Subboard_Bringup_Common ${CMAKE_SOURCE_DIR}/Algorithm_Models/protocol
    DBG_SCRIPT ${SUBBOARD_GDB_SCRIPT}
    NO_IMAGE
    MM_ENABLE
)

target_compile_definitions(s300_card1_human_detection PRIVATE
    BOARD_MM_ENABLE=1
    SUBBOARD_LOG_LEVEL=${CARD1_APP_LOG_LEVEL_VALUE}
)

add_custom_command(TARGET s300_card1_human_detection POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DDSP_BIN_DIR=${APP_CARD1_DSP_IMAGE_DIR}
        -DTEMPLATE_FILE=${SUBBOARD_GDB_TEMPLATE}
        -DOUTPUT_FILE=${SUBBOARD_GDB_SCRIPT}
        -P ${SUBBOARD_GDB_GENERATOR}
    COMMENT "Refreshing App_Card1_HumanDetection GDB script from current DSP bins"
)

add_s300_image(s300_card1_human_detection
    OUTPUT s300_card1_human_detection.bin
    DSP_DIR ${APP_CARD1_DSP_IMAGE_DIR}
)

add_custom_target(dbg_card1
    DEPENDS dbg_card1_human_detection
)

message(STATUS "App_Card1_HumanDetection: DSP image dir = ${APP_CARD1_DSP_IMAGE_DIR}")
message(STATUS "App_Card1_HumanDetection: model = ${APP_CARD1_MODEL_ID} ${APP_CARD1_MODEL_VERSION} (${APP_CARD1_MODEL_DATE})")
message(STATUS "App_Card1_HumanDetection: model manifest = ${MODEL_MANIFEST}")
message(STATUS "App_Card1_HumanDetection: log level = ${CARD1_APP_LOG_LEVEL_NAME}")
