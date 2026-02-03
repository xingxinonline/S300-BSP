# =============================================================================
# Tracking_Demo - 智能云台目标追踪演示
# =============================================================================
# 本项目集成:
# 1. OV5640 摄像头采集
# 2. DSP 人脸/人形检测 (通过邮箱通信)
# 3. PID 控制的总线舵机云台跟踪
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(Tracking_Demo)

# Target
set(TARGET s300_tracking_demo)

# Paths
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)

# 检查 bus_servo 库是否已存在，避免重复添加
if(NOT TARGET bus_servo)
    add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/BusServo ${CMAKE_CURRENT_BINARY_DIR}/BusServo)
endif()

# 使用 s300_resolve_drivers 自动解析驱动依赖
s300_resolve_drivers(
    DRIVERS uart dma mm mailbox psram gpio rcc
    OUT_SRCS DRIVER_SRCS
    OUT_INCS DRIVER_INCS
)

include_directories(
    ${S300_DEVICE_DIR}/Include
    ${S300_CORE_INC}
    ${DRIVER_INCS}
    ${PROJECT_DIR}/Inc
    ${ROOT}/Algorithm_Models/protocol
)

add_compile_definitions(BOARD_MM_ENABLE=1)

# Sources
set(SRCS
    ${S300_STARTUP_FILE}
    ${S300_SYSTEM_FILE}
    ${DRIVER_SRCS}
    ${PROJECT_DIR}/Src/main.c
    ${PROJECT_DIR}/Src/gimbal_ctrl.c
    ${PROJECT_DIR}/Src/target_tracker.c

    # 复用 Algorithm_Detection_Demo 的摄像头驱动
    ${ROOT}/Projects/Demo/Algorithm_Detection_Demo/Src/camera_ov5640.c
)

# 添加 Algorithm_Detection_Demo 的 Include 目录 (for camera_ov5640.h)
include_directories(
    ${ROOT}/Projects/Demo/Algorithm_Detection_Demo/Inc
)

set_source_files_properties(${S300_STARTUP_FILE} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})

# Link board support library and other dependencies
target_link_libraries(${TARGET}
    s300_board
    bus_servo
    m
)

# Linker script
set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-T${S300_DEFAULT_LD}")

# Post build artifacts (bin/hex/disasm/size)
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dis
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMENT "Generating binary, hex, disassembly and size info for Tracking demo"
)

# Debug target (SRAM mode)
set(GDB_PORT 3333 CACHE STRING "GDB server port")
add_custom_target(dbg_tracking_demo
    COMMENT "Launching GDB for ${TARGET} (SRAM: 0x20000000) (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${ROOT}/gdbinit.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

# =============================================================================
# 模型加载调试目标
# =============================================================================
# 使用 Algorithm_Models 目录中的模型
cmake_path(ABSOLUTE_PATH ROOT NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(ALGORITHM_MODELS_DIR ${ROOT_ABS}/Algorithm_Models)

# Helper function to generate GDB restore commands if file exists
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

# Auto-generate debug targets for each algorithm model
# 只有人脸检测和人形检测适用于跟踪 Demo
set(TRACKING_SUPPORTED_MODELS "Face_Detection" "Human_Detection")

foreach(MODEL_DIR ${TRACKING_SUPPORTED_MODELS})
    if(IS_DIRECTORY ${ALGORITHM_MODELS_DIR}/${MODEL_DIR})
        # Try to get version info
        execute_process(
            COMMAND python3 ${ROOT}/tools/get_model_version.py ${ALGORITHM_MODELS_DIR}/${MODEL_DIR}
            OUTPUT_VARIABLE MODEL_VER
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        if("${MODEL_VER}" STREQUAL "")
            set(MODEL_VER "Unknown")
        endif()

        # Convert Face_Detection to face-detection
        string(TOLOWER ${MODEL_DIR} MODEL_LOWER)
        string(REPLACE "_" "-" MODEL_KEBAB ${MODEL_LOWER})

        # Set model-specific variables for template
        set(MODEL_NAME ${MODEL_DIR})
        set(MODEL_DIR_PATH ${ALGORITHM_MODELS_DIR}/${MODEL_DIR})

        # Generate restore commands for this model
        generate_gdb_restore_cmd("${MODEL_DIR_PATH}/model_dtcm_boot.bin" "0x44800000" GDB_RESTORE_DTCM)
        generate_gdb_restore_cmd("${MODEL_DIR_PATH}/model_ptcm_boot.bin" "0x44A00000" GDB_RESTORE_PTCM)
        generate_gdb_restore_cmd("${MODEL_DIR_PATH}/model_sram0_boot.bin" "0x44000000" GDB_RESTORE_SRAM0)
        generate_gdb_restore_cmd("${MODEL_DIR_PATH}/model_psram_boot.bin" "0x80000000" GDB_RESTORE_PSRAM)

        set(PSRAM_BREAKPOINT_FUNC "video_subsystem_init")

        # Configure model-specific GDB script
        configure_file(
            ${ROOT}/gdbinit.model.gdb.in
            ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.tracking.${MODEL_KEBAB}.gdb
            @ONLY
        )

        add_custom_target(dbg_tracking_${MODEL_KEBAB}
            COMMENT "Launching GDB for Tracking Demo with Algorithm Model: ${MODEL_DIR} [v${MODEL_VER}]"
            COMMAND arm-none-eabi-gdb -q
            -ex "file $<TARGET_FILE:${TARGET}>"
            -ex "target extended-remote :${GDB_PORT}"
            -x ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.tracking.${MODEL_KEBAB}.gdb
            DEPENDS ${TARGET}
            USES_TERMINAL
        )

        message(STATUS "Tracking_Demo: Created debug target dbg_tracking_${MODEL_KEBAB} [v${MODEL_VER}]")
    endif()
endforeach()

message(STATUS "Tracking_Demo configured with bus servo gimbal control")
