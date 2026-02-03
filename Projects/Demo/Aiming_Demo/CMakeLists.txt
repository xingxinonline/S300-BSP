# =============================================================================
# Aiming_Demo - 视觉指向演示 (摄像头固定模式)
# =============================================================================
# 与 Tracking_Demo 的区别:
# - Tracking: 摄像头在云台上，目标右 → 云台左转 (追踪)
# - Aiming:   摄像头固定，目标右 → 云台右转 (指向)
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(Aiming_Demo)

# Target
set(TARGET s300_aiming_demo)

# Paths
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)

# 检查 bus_servo 库是否已存在
if(NOT TARGET bus_servo)
    add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/BusServo ${CMAKE_CURRENT_BINARY_DIR}/BusServo)
endif()

# 驱动依赖
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

    # 复用 Tracking_Demo 的 gimbal_ctrl
    ${ROOT}/Projects/Demo/Tracking_Demo/Inc
)

add_compile_definitions(BOARD_MM_ENABLE=1)

# Sources
set(SRCS
    ${S300_STARTUP_FILE}
    ${S300_SYSTEM_FILE}
    ${DRIVER_SRCS}
    ${PROJECT_DIR}/Src/main.c
    ${PROJECT_DIR}/Src/aiming_ctrl.c

    # 复用 Tracking_Demo 的 gimbal_ctrl
    ${ROOT}/Projects/Demo/Tracking_Demo/Src/gimbal_ctrl.c

    # 复用 Algorithm_Detection_Demo 的摄像头驱动
    ${ROOT}/Projects/Demo/Algorithm_Detection_Demo/Src/camera_ov5640.c
)

include_directories(
    ${ROOT}/Projects/Demo/Algorithm_Detection_Demo/Inc
)

set_source_files_properties(${S300_STARTUP_FILE} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})

target_link_libraries(${TARGET}
    s300_board
    bus_servo
    m
)

set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-T${S300_DEFAULT_LD}")

# Post build
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dis
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMENT "Generating binary, hex, disassembly and size info for Aiming demo"
)

# Debug target
set(GDB_PORT 3333 CACHE STRING "GDB server port")
add_custom_target(dbg_aiming_demo
    COMMENT "Launching GDB for ${TARGET} (SRAM: 0x20000000)"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${ROOT}/gdbinit.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

# =============================================================================
# 模型加载调试目标 (只支持人脸和人形检测)
# =============================================================================
cmake_path(ABSOLUTE_PATH ROOT NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(ALGORITHM_MODELS_DIR ${ROOT_ABS}/Algorithm_Models)

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

set(AIMING_SUPPORTED_MODELS "Face_Detection" "Human_Detection")

foreach(MODEL_DIR ${AIMING_SUPPORTED_MODELS})
    if(IS_DIRECTORY ${ALGORITHM_MODELS_DIR}/${MODEL_DIR})
        execute_process(
            COMMAND python3 ${ROOT}/tools/get_model_version.py ${ALGORITHM_MODELS_DIR}/${MODEL_DIR}
            OUTPUT_VARIABLE MODEL_VER
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        if("${MODEL_VER}" STREQUAL "")
            set(MODEL_VER "Unknown")
        endif()

        string(TOLOWER ${MODEL_DIR} MODEL_LOWER)
        string(REPLACE "_" "-" MODEL_KEBAB ${MODEL_LOWER})

        set(MODEL_NAME ${MODEL_DIR})
        set(MODEL_DIR_PATH ${ALGORITHM_MODELS_DIR}/${MODEL_DIR})

        generate_gdb_restore_cmd("${MODEL_DIR_PATH}/model_dtcm_boot.bin" "0x44800000" GDB_RESTORE_DTCM)
        generate_gdb_restore_cmd("${MODEL_DIR_PATH}/model_ptcm_boot.bin" "0x44A00000" GDB_RESTORE_PTCM)
        generate_gdb_restore_cmd("${MODEL_DIR_PATH}/model_sram0_boot.bin" "0x44000000" GDB_RESTORE_SRAM0)
        generate_gdb_restore_cmd("${MODEL_DIR_PATH}/model_psram_boot.bin" "0x80000000" GDB_RESTORE_PSRAM)

        set(PSRAM_BREAKPOINT_FUNC "video_subsystem_init")

        configure_file(
            ${ROOT}/gdbinit.model.gdb.in
            ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.aiming.${MODEL_KEBAB}.gdb
            @ONLY
        )

        add_custom_target(dbg_aiming_${MODEL_KEBAB}
            COMMENT "Launching GDB for Aiming Demo with Algorithm Model: ${MODEL_DIR} [v${MODEL_VER}]"
            COMMAND arm-none-eabi-gdb -q
            -ex "file $<TARGET_FILE:${TARGET}>"
            -ex "target extended-remote :${GDB_PORT}"
            -x ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.aiming.${MODEL_KEBAB}.gdb
            DEPENDS ${TARGET}
            USES_TERMINAL
        )

        message(STATUS "Aiming_Demo: Created debug target dbg_aiming_${MODEL_KEBAB} [v${MODEL_VER}]")
    endif()
endforeach()

message(STATUS "Aiming_Demo configured (fixed camera, gimbal aiming mode)")
