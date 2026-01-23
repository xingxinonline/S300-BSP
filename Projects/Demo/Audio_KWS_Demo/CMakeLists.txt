# =============================================================================
# Audio_KWS_Demo - 音频采集与关键词检测 Demo
# =============================================================================
# 功能：使用 I2S + WM8978 进行音频采集与播放，配合 DSP 进行 KWS 检测
# 参考自 gitlab/feature/i2s-kws-audio-processing 分支
#
# DSP 通信协议 (共享内存):
# - 0x44040000: dsp_in_block  - 音频输入数据 (CM4 -> DSP)
# - 0x44040200: dsp_out_block - 音频输出数据 (DSP -> CM4)
# - 0x44040400: kws_cmd       - KWS 识别结果
# - 0x44040430: dsp_ready_flag
# - 0x44040434: input_ready_flag
# - 0x44040438: output_ready_flag
#
# 本项目不使用 s300_add_executable() 的原因:
# 1. 需要生成包含 DSP 模型的 GDB 调试脚本
# 2. 使用 gdbinit.model.gdb.in 模板配置模型加载
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(Audio_KWS_Demo)

set(TARGET s300_audio_kws_demo)

# Paths
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)

# 使用 s300_resolve_drivers 自动解析驱动依赖
s300_resolve_drivers(
    DRIVERS rcc gpio uart dma i2s i2c_soft wm8978
    OUT_SRCS DRIVER_SRCS
    OUT_INCS DRIVER_INCS
)

include_directories(
    ${S300_DEVICE_DIR}/Include
    ${S300_CORE_INC}
    ${DRIVER_INCS}
)

# Sources
set(SRCS
    ${S300_STARTUP_FILE}
    ${S300_SYSTEM_FILE}
    ${DRIVER_SRCS}
    ${PROJECT_DIR}/Src/main.c
    ${PROJECT_DIR}/Src/audio_app.c
)

set_source_files_properties(${S300_STARTUP_FILE} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})
target_link_libraries(${TARGET} s300_board)
set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-T${S300_DEFAULT_LD}")

# Post build artifacts
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dis
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMENT "Generating artifacts for ${TARGET}"
)

set(GDB_PORT 3333 CACHE STRING "GDB server port")

# =============================================================================
# DSP Model: Algorithm_Models/Keyword_Spotting
# =============================================================================
cmake_path(ABSOLUTE_PATH ROOT NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(KWS_MODEL_DIR ${ROOT_ABS}/Algorithm_Models/Keyword_Spotting)

# Helper: generate GDB restore command
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

# KWS 模型加载地址 (参考 model_info.json):
# dtcm:  0x44800000
# ptcm:  0x44A00000
# sram0: 0x44000000  (空文件)
# sram1: 0x44040000  (1KB, DSP 初始化数据)
#
# KWS 与其他模型不同：必须在 DSP 复位后加载固件
# 所以把 DTCM/PTCM/SRAM1 放到断点处加载，而不是程序开始前

# 程序开始前不加载任何固件
set(GDB_RESTORE_DTCM "# KWS: DTCM loaded at breakpoint")
set(GDB_RESTORE_PTCM "# KWS: PTCM loaded at breakpoint")
set(GDB_RESTORE_SRAM0 "# KWS: SRAM0 empty")

# 在断点处加载所有 DSP 固件
generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_dtcm_boot.bin" "0x44800000" KWS_RESTORE_DTCM)
generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_ptcm_boot.bin" "0x44A00000" KWS_RESTORE_PTCM)
generate_gdb_restore_cmd("${KWS_MODEL_DIR}/model_sram1_boot.bin" "0x44040000" KWS_RESTORE_SRAM1)
set(GDB_RESTORE_PSRAM "${KWS_RESTORE_DTCM}\n  ${KWS_RESTORE_PTCM}\n  ${KWS_RESTORE_SRAM1}")

set(MODEL_NAME "Keyword_Spotting")
set(MODEL_KEBAB "keyword-spotting")
set(MODEL_DIR ${KWS_MODEL_DIR})

# GDB 断点设置在 dsp_firmware_load_point，DSP 复位后加载固件
set(PSRAM_BREAKPOINT_FUNC "dsp_firmware_load_point")

configure_file(
    ${ROOT}/gdbinit.model.gdb.in
    ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    @ONLY
)

# Debug target with KWS model (默认调试目标)
add_custom_target(dbg_audio_kws_demo
    COMMENT "Launching GDB for ${TARGET} with KWS model (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

# S300 Image with KWS model
add_s300_image(${TARGET}
    OUTPUT s300_audio_kws_demo.bin
    DSP_DIR ${KWS_MODEL_DIR}
)

message(STATUS "Audio_KWS_Demo: Model dir = ${KWS_MODEL_DIR}")
