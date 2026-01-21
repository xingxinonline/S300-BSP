# =============================================================================
# Face_Recognition_Demo - 人脸识别演示
# =============================================================================
# 本项目不使用 s300_add_executable() 的原因:
# 1. 需要生成包含 DSP 模型的 GDB 调试脚本
# 2. 使用 gdbinit.model.gdb.in 模板配置模型加载
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(Face_Recognition_Demo)

set(TARGET s300_face_recognition_demo)

# Paths
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)

# 使用 s300_resolve_drivers 自动解析驱动依赖
# uart -> rcc, gpio
# mm -> ov5640 -> i2c_soft -> gpio -> rcc
s300_resolve_drivers(
    DRIVERS uart mm mailbox psram
    OUT_SRCS DRIVER_SRCS
    OUT_INCS DRIVER_INCS
)

include_directories(
    ${S300_DEVICE_DIR}/Include
    ${S300_CORE_INC}
    ${DRIVER_INCS}
    ${PROJECT_DIR}/Inc
)

add_compile_definitions(BOARD_MM_ENABLE=1)

# Sources
set(SRCS
    ${S300_STARTUP_FILE}
    ${S300_SYSTEM_FILE}
    ${DRIVER_SRCS}
    ${PROJECT_DIR}/Src/main.c
    ${PROJECT_DIR}/Src/camera_ov5640.c
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

# Debug target (no model)
set(GDB_PORT 3333 CACHE STRING "GDB server port")
add_custom_target(dbg_face_recognition_demo
    COMMENT "Launching GDB for ${TARGET} (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${ROOT}/gdbinit.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

# DSP Model: Algorithm_Models/Face_Recognition
cmake_path(ABSOLUTE_PATH ROOT NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(FACE_RECOGNITION_DIR ${ROOT_ABS}/Algorithm_Models/Face_Recognition)

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

generate_gdb_restore_cmd("${FACE_RECOGNITION_DIR}/model_dtcm_boot.bin" "0x44800000" GDB_RESTORE_DTCM)
generate_gdb_restore_cmd("${FACE_RECOGNITION_DIR}/model_ptcm_boot.bin" "0x44A00000" GDB_RESTORE_PTCM)
generate_gdb_restore_cmd("${FACE_RECOGNITION_DIR}/model_sram0_boot.bin" "0x44000000" GDB_RESTORE_SRAM0)
generate_gdb_restore_cmd("${FACE_RECOGNITION_DIR}/model_psram_boot.bin" "0x80000000" GDB_RESTORE_PSRAM)

set(MODEL_NAME "Face_Recognition")
set(MODEL_KEBAB "face-recognition")
set(MODEL_DIR ${FACE_RECOGNITION_DIR})
set(PSRAM_BREAKPOINT_FUNC "init_mailbox")

configure_file(
    ${ROOT}/gdbinit.model.gdb.in
    ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    @ONLY
)

# Debug target with model
add_custom_target(dbg_face_recognition_demo_model
    COMMENT "Launching GDB for ${TARGET} with Face_Recognition model (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

# S300 Image with Face Recognition model
add_s300_image(${TARGET}
    OUTPUT s300_face_recognition_demo.bin
    DSP_DIR ${FACE_RECOGNITION_DIR}
)
