cmake_minimum_required(VERSION 3.16)
project(KWS_CM4_Demo)

set(TARGET s300_kws_cm4_demo)

set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)
set(AUDIO_SHARED_DIR ${ROOT}/Projects/Demo/Audio_KWS_Demo/Src)

if(BOARD STREQUAL "app_board" OR BOARD STREQUAL "gimbal_master")
    set(AUDIO_CODEC_DRIVERS es7210 es8311)
    message(STATUS "KWS_CM4_Demo: Using ES7210+ES8311 codec for ${BOARD}")
else()
    set(AUDIO_CODEC_DRIVERS wm8978)
    message(STATUS "KWS_CM4_Demo: Using WM8978 codec for ${BOARD}")
endif()

s300_resolve_drivers(
    DRIVERS rcc gpio uart dma i2s i2c_soft ${AUDIO_CODEC_DRIVERS}
    OUT_SRCS DRIVER_SRCS
    OUT_INCS DRIVER_INCS
)

include_directories(
    ${S300_DEVICE_DIR}/Include
    ${S300_CORE_INC}
    ${DRIVER_INCS}
    ${AUDIO_SHARED_DIR}
)

set(SRCS
    ${S300_STARTUP_FILE}
    ${S300_SYSTEM_FILE}
    ${DRIVER_SRCS}
    ${PROJECT_DIR}/Src/main.c
    ${PROJECT_DIR}/Src/kws_dsp.c
    ${AUDIO_SHARED_DIR}/audio_app.c
    ${AUDIO_SHARED_DIR}/audio_codec.c
)

set_source_files_properties(${S300_STARTUP_FILE} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})
target_link_libraries(${TARGET} s300_board)
set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-T${S300_DEFAULT_LD}")

if(BOARD STREQUAL "app_board" OR BOARD STREQUAL "gimbal_master")
    target_compile_definitions(${TARGET} PRIVATE
        BOARD_AUDIO_CODEC_ES7210=1
        BOARD_AUDIO_CODEC_ES8311=1
        BOARD_AUDIO_ENABLE=1
    )
else()
    target_compile_definitions(${TARGET} PRIVATE
        BOARD_AUDIO_CODEC_WM8978=1
        BOARD_AUDIO_ENABLE=1
    )
endif()

add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dis
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMENT "Generating artifacts for ${TARGET}"
)

set(GDB_PORT 3333 CACHE STRING "GDB server port")

cmake_path(ABSOLUTE_PATH ROOT NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
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

set(GDB_RESTORE_DTCM "# KWS: DTCM loaded at breakpoint")
set(GDB_RESTORE_PTCM "# KWS: PTCM loaded at breakpoint")
set(GDB_RESTORE_SRAM0 "# KWS: SRAM0 empty")

generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_dtcm_boot.bin" "0x44800000" KWS_RESTORE_DTCM)
generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_ptcm_boot.bin" "0x44A00000" KWS_RESTORE_PTCM)
generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_sram0_boot.bin" "0x44000000" KWS_RESTORE_SRAM0)
generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_sram1_boot.bin" "0x44040000" KWS_RESTORE_SRAM1)
set(GDB_RESTORE_PSRAM "${KWS_RESTORE_DTCM}\n  ${KWS_RESTORE_PTCM}\n  ${KWS_RESTORE_SRAM1}")

set(MODEL_NAME "Keyword_Spotting")
set(MODEL_KEBAB "keyword-spotting")
set(MODEL_DIR ${KWS_MODEL_DIR})
set(PSRAM_BREAKPOINT_FUNC "dsp_firmware_load_point")

configure_file(
    ${ROOT}/gdbinit.model.gdb.in
    ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    @ONLY
)

add_custom_target(dbg_kws_cm4_demo
    COMMENT "Launching GDB for ${TARGET} with KWS model (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

add_s300_image(${TARGET}
    OUTPUT s300_kws_cm4_demo.bin
    DSP_DIR ${KWS_MODEL_DIR}
)

message(STATUS "KWS_CM4_Demo: Model dir = ${KWS_MODEL_DIR}")
