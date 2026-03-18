cmake_minimum_required(VERSION 3.16)
project(App_GimbalMaster)

if(NOT BOARD STREQUAL "gimbal_master")
    message(STATUS "App_GimbalMaster: skipped for BOARD=${BOARD}, gimbal_master only")
    return()
endif()

set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../..)
include(${ROOT}/cmake/s300_model_metadata.cmake)
add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/WS2812 ${CMAKE_CURRENT_BINARY_DIR}/WS2812)
add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/BusServo ${CMAKE_CURRENT_BINARY_DIR}/BusServo)
set(KWS_DEMO_DIR ${ROOT}/Projects/Demo/KWS_Control_Protocol_Demo)
set(AUDIO_SHARED_DIR ${ROOT}/Projects/Demo/Audio_KWS_Demo/Src)
set(SHARED_DETECTION_DIR ${ROOT}/Projects/Demo/Algorithm_Detection_Demo)
set(DSP_BIN_DIR ${PROJECT_DIR}/model_bin)
set(KWS_GDB_TEMPLATE ${KWS_DEMO_DIR}/gdbinit.kws_control_protocol.gdb.in)
set(KWS_GDB_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.gimbal_master_kws.gdb)
set(MODEL_MANIFEST ${CMAKE_CURRENT_BINARY_DIR}/gimbal_master_model_manifest.txt)
set(MASTER_APP_LOG_LEVEL "INFO" CACHE STRING "Master app log level: WARN, INFO, DEBUG")
set(MASTER_SUBBOARD_WAIT_TIMEOUT_MS "3000" CACHE STRING "Milliseconds to wait for subboard RUNNING before local-KWS fallback")
set(MASTER_KWS_STATUS_LOG_STRIDE "0" CACHE STRING "Print every Nth KWS STATUS report; 0 disables periodic STATUS logs")
set(MASTER_KWS_HEARTBEAT_LOG_STRIDE "0" CACHE STRING "Print every Nth KWS heartbeat TX/RX; 0 disables periodic heartbeat logs")
set(MASTER_KWS_STREAM_STATS_LOG_INTERVAL "1024" CACHE STRING "Print KWS stream chunk/result statistics every N chunks; 0 disables periodic stream stats")
set(MASTER_GIMBAL_DEBUG_BOOT_DEMO "OFF" CACHE STRING "Enable gimbal boot demo inside App_GimbalMaster: ON or OFF")
set(MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS "2500" CACHE STRING "Milliseconds between App_GimbalMaster gimbal boot demo steps")

if(MASTER_KWS_STATUS_LOG_STRIDE STREQUAL "8" OR MASTER_KWS_STATUS_LOG_STRIDE STREQUAL "32")
    set(MASTER_KWS_STATUS_LOG_STRIDE "0" CACHE STRING "Print every Nth KWS STATUS report; 0 disables periodic STATUS logs" FORCE)
endif()

if(MASTER_KWS_HEARTBEAT_LOG_STRIDE STREQUAL "4" OR MASTER_KWS_HEARTBEAT_LOG_STRIDE STREQUAL "8")
    set(MASTER_KWS_HEARTBEAT_LOG_STRIDE "0" CACHE STRING "Print every Nth KWS heartbeat TX/RX; 0 disables periodic heartbeat logs" FORCE)
endif()

string(TOUPPER "${MASTER_APP_LOG_LEVEL}" MASTER_APP_LOG_LEVEL_NAME)
if(MASTER_APP_LOG_LEVEL_NAME STREQUAL "WARN")
    set(MASTER_APP_LOG_LEVEL_VALUE 1)
elseif(MASTER_APP_LOG_LEVEL_NAME STREQUAL "INFO")
    set(MASTER_APP_LOG_LEVEL_VALUE 2)
elseif(MASTER_APP_LOG_LEVEL_NAME STREQUAL "DEBUG")
    set(MASTER_APP_LOG_LEVEL_VALUE 3)
else()
    message(FATAL_ERROR "App_GimbalMaster: invalid MASTER_APP_LOG_LEVEL=${MASTER_APP_LOG_LEVEL}")
endif()

cmake_path(ABSOLUTE_PATH CMAKE_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
cmake_path(ABSOLUTE_PATH DSP_BIN_DIR BASE_DIRECTORY ${PROJECT_DIR} NORMALIZE OUTPUT_VARIABLE DSP_BIN_DIR_ABS)
set(KEYWORD_SPOTTING_MODEL_DIR ${ROOT_ABS}/Algorithm_Models/Keyword_Spotting)
set(APP_GIMBAL_MASTER_DSP_IMAGE_DIR ${KEYWORD_SPOTTING_MODEL_DIR})
set(APP_GIMBAL_MASTER_USE_LOCAL_MODEL FALSE)

if(EXISTS ${DSP_BIN_DIR}/model_dtcm_boot.bin AND
   EXISTS ${DSP_BIN_DIR}/model_ptcm_boot.bin)
    set(APP_GIMBAL_MASTER_DSP_IMAGE_DIR ${DSP_BIN_DIR})
    set(APP_GIMBAL_MASTER_USE_LOCAL_MODEL TRUE)
endif()

s300_load_model_metadata("${APP_GIMBAL_MASTER_DSP_IMAGE_DIR}" APP_GIMBAL_MASTER_MODEL)

if(APP_GIMBAL_MASTER_USE_LOCAL_MODEL AND NOT APP_GIMBAL_MASTER_MODEL_HAS_INFO)
    message(WARNING "App_GimbalMaster: local model_bin selected but model_info.json is missing, model version will be reported as Unknown")
endif()

file(GENERATE OUTPUT ${MODEL_MANIFEST}
    CONTENT
"model_dir=${APP_GIMBAL_MASTER_DSP_IMAGE_DIR}\nmodel_source=$<IF:$<BOOL:${APP_GIMBAL_MASTER_USE_LOCAL_MODEL}>,local_override,algorithm_models>\nmodel_name=${APP_GIMBAL_MASTER_MODEL_NAME}\nmodel_id=${APP_GIMBAL_MASTER_MODEL_ID}\nmodel_version=${APP_GIMBAL_MASTER_MODEL_VERSION}\nmodel_date=${APP_GIMBAL_MASTER_MODEL_DATE}\n")

if(BOARD STREQUAL "app_board" OR BOARD STREQUAL "gimbal_master")
    set(AUDIO_CODEC_DRIVERS es7210 es8311)
    message(STATUS "App_GimbalMaster: using ES7210+ES8311 codec for ${BOARD}")
else()
    set(AUDIO_CODEC_DRIVERS wm8978)
    message(STATUS "App_GimbalMaster: using WM8978 codec for ${BOARD}")
endif()

s300_add_executable(
    TARGET s300_gimbal_master
    SOURCES
        Src/main.c
        Src/app_action_dispatch.c
        Src/app_action_executor.c
        Src/app_card1_result_handler.c
        Src/app_card1_subboard.c
        Src/app_card2_result_handler.c
        Src/app_card2_subboard.c
        Src/app_card3_result_handler.c
        Src/app_card3_subboard.c
        Src/app_gimbal_control.c
        Src/app_gimbal_debug.c
        Src/app_media_control.c
        Src/app_runtime_state.c
        Src/app_status_light.c
        Src/app_fill_light.c
        Src/camera_ov5640.c
        Src/kws_control_stream.c
        Src/master_demo_app.c
        Src/master_detection_overlay.c
        ${AUDIO_SHARED_DIR}/audio_app.c
        ${AUDIO_SHARED_DIR}/audio_codec.c
    DRIVERS rcc gpio uart dma i2s i2c_soft mailbox mm psram ${AUDIO_CODEC_DRIVERS}
    INCLUDES . Inc ${KWS_DEMO_DIR}/Src ${AUDIO_SHARED_DIR} ${SHARED_DETECTION_DIR}/Inc ${CMAKE_SOURCE_DIR}/Projects/Common/Subboard_Protocol ${CMAKE_SOURCE_DIR}/Algorithm_Models/protocol
    LIBRARIES ws2812 bus_servo
    DBG_NAME gimbal_master
    DBG_SCRIPT ${KWS_GDB_SCRIPT}
    NO_IMAGE
)

if(BOARD STREQUAL "app_board" OR BOARD STREQUAL "gimbal_master")
    target_compile_definitions(s300_gimbal_master PRIVATE
        BOARD_AUDIO_CODEC_ES7210=1
        BOARD_AUDIO_CODEC_ES8311=1
        BOARD_AUDIO_ENABLE=1
    )
else()
    target_compile_definitions(s300_gimbal_master PRIVATE
        BOARD_AUDIO_CODEC_WM8978=1
        BOARD_AUDIO_ENABLE=1
    )
endif()

target_compile_definitions(s300_gimbal_master PRIVATE
    MASTER_LOG_LEVEL=${MASTER_APP_LOG_LEVEL_VALUE}
    MASTER_SUBBOARD_WAIT_TIMEOUT_MS=${MASTER_SUBBOARD_WAIT_TIMEOUT_MS}
    MASTER_KWS_STATUS_LOG_STRIDE=${MASTER_KWS_STATUS_LOG_STRIDE}
    MASTER_KWS_HEARTBEAT_LOG_STRIDE=${MASTER_KWS_HEARTBEAT_LOG_STRIDE}
    MASTER_KWS_STREAM_STATS_LOG_INTERVAL=${MASTER_KWS_STREAM_STATS_LOG_INTERVAL}
    $<$<STREQUAL:${MASTER_GIMBAL_DEBUG_BOOT_DEMO},ON>:MASTER_GIMBAL_DEBUG_BOOT_DEMO=1>
    MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS=${MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS}
    APP_GIMBAL_MASTER_MODEL_ID=\"${APP_GIMBAL_MASTER_MODEL_ID}\"
    APP_GIMBAL_MASTER_MODEL_VERSION=\"${APP_GIMBAL_MASTER_MODEL_VERSION}\"
    APP_GIMBAL_MASTER_MODEL_DATE=\"${APP_GIMBAL_MASTER_MODEL_DATE}\"
    APP_GIMBAL_MASTER_MODEL_DIR=\"${APP_GIMBAL_MASTER_DSP_IMAGE_DIR}\"
)

add_custom_command(TARGET s300_gimbal_master POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DDSP_BIN_DIR=${APP_GIMBAL_MASTER_DSP_IMAGE_DIR}
        -DTEMPLATE_FILE=${KWS_GDB_TEMPLATE}
        -DOUTPUT_FILE=${KWS_GDB_SCRIPT}
        -P ${KWS_DEMO_DIR}/generate_kws_control_protocol_gdb.cmake
    COMMENT "Refreshing App_GimbalMaster KWS GDB script from current DSP bins"
)

add_s300_image(s300_gimbal_master
    OUTPUT s300_gimbal_master.bin
    DSP_DIR ${APP_GIMBAL_MASTER_DSP_IMAGE_DIR}
)

message(STATUS "App_GimbalMaster: DSP image dir = ${APP_GIMBAL_MASTER_DSP_IMAGE_DIR}")
message(STATUS "App_GimbalMaster: model = ${APP_GIMBAL_MASTER_MODEL_ID} ${APP_GIMBAL_MASTER_MODEL_VERSION} (${APP_GIMBAL_MASTER_MODEL_DATE})")
message(STATUS "App_GimbalMaster: model manifest = ${MODEL_MANIFEST}")
message(STATUS "App_GimbalMaster: log level = ${MASTER_APP_LOG_LEVEL_NAME}")
message(STATUS "App_GimbalMaster: subboard wait timeout = ${MASTER_SUBBOARD_WAIT_TIMEOUT_MS} ms")
message(STATUS "App_GimbalMaster: KWS status log stride = ${MASTER_KWS_STATUS_LOG_STRIDE}")
message(STATUS "App_GimbalMaster: KWS heartbeat log stride = ${MASTER_KWS_HEARTBEAT_LOG_STRIDE}")
message(STATUS "App_GimbalMaster: KWS stream stats interval = ${MASTER_KWS_STREAM_STATS_LOG_INTERVAL}")
message(STATUS "App_GimbalMaster: gimbal boot demo = ${MASTER_GIMBAL_DEBUG_BOOT_DEMO}")
message(STATUS "App_GimbalMaster: gimbal boot demo interval = ${MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS} ms")
