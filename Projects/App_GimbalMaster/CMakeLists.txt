# =============================================================================
# App_GimbalMaster - 云台主控应用 (MVP0)
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(App_GimbalMaster)

if(NOT TARGET ws2812)
    add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/WS2812 ${CMAKE_CURRENT_BINARY_DIR}/WS2812)
endif()

set(FREERTOS_KERNEL_LOCAL_PATH "" CACHE PATH "Local path to FreeRTOS-Kernel (optional)")
set(FREERTOS_PORT GCC_ARM_CM4F CACHE STRING "FreeRTOS port")
set(FREERTOS_HEAP 4 CACHE STRING "FreeRTOS heap implementation (1..5)")
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Path to FreeRTOSConfig.h")

if(NOT TARGET freertos_kernel)
    if(FREERTOS_KERNEL_LOCAL_PATH AND EXISTS ${FREERTOS_KERNEL_LOCAL_PATH}/CMakeLists.txt)
        message(STATUS "Using FreeRTOS-Kernel from local path: ${FREERTOS_KERNEL_LOCAL_PATH}")
        add_subdirectory(${FREERTOS_KERNEL_LOCAL_PATH} ${CMAKE_CURRENT_BINARY_DIR}/freertos-kernel-local)
    else()
        include(FetchContent)
        FetchContent_Declare(
            freertos_kernel
            GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
            GIT_TAG V10.5.1
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(freertos_kernel)
    endif()

    target_include_directories(freertos_kernel PUBLIC
        ${CMAKE_SOURCE_DIR}/CMSIS/Device/PiMCHIP/S300/Include
        ${CMAKE_SOURCE_DIR}/CMSIS/Core/Include
    )
endif()

s300_add_executable(
    TARGET s300_gimbal_master
    SOURCES
        Src/main.c
        Src/app_log.c
        Src/track_state.c
        Src/track_events.c
        Src/task_state.c
        Src/task_command.c
        Src/task_heartbeat.c
        Src/task_kws.c
        ${CMAKE_SOURCE_DIR}/Projects/Demo/Audio_KWS_Demo/Src/audio_app.c
        ${CMAKE_SOURCE_DIR}/Projects/Demo/Audio_KWS_Demo/Src/audio_codec.c
    DRIVERS rcc gpio uart mailbox i2s dma i2c_soft es7210 es8311 mm
    INCLUDES . Inc ${CMAKE_SOURCE_DIR}/Algorithm_Models/protocol ${CMAKE_SOURCE_DIR}/Projects/Demo/Audio_KWS_Demo/Src
    LIBRARIES freertos_kernel ws2812
    DBG_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.keyword-spotting.gdb
    NO_IMAGE
)

target_compile_definitions(s300_gimbal_master PRIVATE
    BOARD_AUDIO_ENABLE=1
)

# =============================================================================
# KWS DSP 模型打包与调试加载
# =============================================================================
set(GDB_PORT 3333 CACHE STRING "GDB server port")

cmake_path(ABSOLUTE_PATH CMAKE_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(KWS_MODEL_DIR ${ROOT_ABS}/Algorithm_Models/Keyword_Spotting)

function(generate_gdb_restore_cmd IMAGE_FILE ADDR OUTPUT_VAR)
    if(EXISTS "${IMAGE_FILE}")
        file(SIZE "${IMAGE_FILE}" FILE_SIZE)
        cmake_path(NORMAL_PATH IMAGE_FILE OUTPUT_VARIABLE IMAGE_FILE_CLEAN)

        if(FILE_SIZE GREATER 0)
            set(${OUTPUT_VAR} "restore ${IMAGE_FILE_CLEAN} binary ${ADDR}" PARENT_SCOPE)
        else()
            set(${OUTPUT_VAR} "# ${IMAGE_FILE_CLEAN} is empty, skipping" PARENT_SCOPE)
        endif()
    else()
        set(${OUTPUT_VAR} "# ${IMAGE_FILE} not found, skipping" PARENT_SCOPE)
    endif()
endfunction()

set(MODEL_NAME "Keyword_Spotting")
set(MODEL_KEBAB "keyword-spotting")
set(MODEL_DIR ${KWS_MODEL_DIR})
set(PSRAM_BREAKPOINT_FUNC "dsp_firmware_load_point")

generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_dtcm_boot.bin" "0x44800000" GDB_RESTORE_DTCM)
generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_ptcm_boot.bin" "0x44A00000" GDB_RESTORE_PTCM)
generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_sram0_boot.bin" "0x44000000" GDB_RESTORE_SRAM0)
generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_sram1_boot.bin" "0x44040000" KWS_RESTORE_SRAM1)
set(GDB_RESTORE_PSRAM "${KWS_RESTORE_SRAM1}")

configure_file(
    ${CMAKE_SOURCE_DIR}/gdbinit.model.gdb.in
    ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    @ONLY
)

add_s300_image(s300_gimbal_master
    OUTPUT s300_gimbal_master.bin
    DSP_DIR ${KWS_MODEL_DIR}
)
