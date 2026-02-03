# =============================================================================
# Algorithm_Tracking_Demo - AI 追踪算法演示
# =============================================================================
# 本项目不使用 s300_add_executable() 的原因:
# 1. 需要动态遍历 Algorithm_Models/ 目录生成调试目标
# 2. 每个模型需要独立的 GDB 加载脚本和镜像目标
# =============================================================================

cmake_minimum_required(VERSION 3.16)

project(Algorithm_Tracking_Demo)

# Target
set(TARGET s300_algorithm_tracking_demo)

# Paths
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)

# 使用 s300_resolve_drivers 自动解析驱动依赖
s300_resolve_drivers(
    DRIVERS uart dma mm mailbox psram
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
    ${PROJECT_DIR}/Src/camera_ov5640.c
    ${PROJECT_DIR}/Src/face_tracker.c
    ${PROJECT_DIR}/Src/uart_cmd.c
    ${PROJECT_DIR}/Src/display_demo_app.c
)

set_source_files_properties(${S300_STARTUP_FILE} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})

# Link board support library and other dependencies
target_link_libraries(${TARGET}
    s300_board
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
    COMMENT "Generating binary, hex, disassembly and size info for Algorithm Tracking demo"
)

# Debug target
set(GDB_PORT 3333 CACHE STRING "GDB server port")
add_custom_target(dbg_algorithm_tracking_demo
    COMMENT "Launching GDB for ${TARGET} (SRAM: 0x20000000) (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${ROOT}/gdbinit.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

# Use cmake_path to get clean absolute paths without ".." components
cmake_path(ABSOLUTE_PATH ROOT NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(ALGORITHM_MODELS_DIR ${ROOT_ABS}/Algorithm_Models)

# Helper function to generate GDB restore commands if file exists
function(generate_gdb_restore_cmd IMAGE_FILE ADDR OUTPUT_VAR)
    if(EXISTS "${IMAGE_FILE}")
        file(SIZE "${IMAGE_FILE}" FILE_SIZE)

        # Normalize path to remove any ".." components
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
# e.g. Face_Detection -> dbg_mm_test_face-detection
file(GLOB ALG_MODELS RELATIVE ${ALGORITHM_MODELS_DIR} ${ALGORITHM_MODELS_DIR}/*)

foreach(MODEL_DIR ${ALG_MODELS})
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

        set(PSRAM_BREAKPOINT_FUNC "display_demo_app_init")

        # Configure model-specific GDB script
        configure_file(
            ${ROOT}/gdbinit.model.gdb.in
            ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
            @ONLY
        )

        add_custom_target(dbg_algorithm_tracking_${MODEL_KEBAB}
            COMMENT "Launching GDB for ${TARGET} with Algorithm Model: ${MODEL_DIR} [v${MODEL_VER}]"
            COMMAND arm-none-eabi-gdb -q
            -ex "file $<TARGET_FILE:${TARGET}>"
            -ex "target extended-remote :${GDB_PORT}"
            -x ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
            DEPENDS ${TARGET}
            USES_TERMINAL
        )

        # Generate Image Target
        set(IMAGE_OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/s300_algorithm_tracking_${MODEL_KEBAB}.bin")
        add_custom_target(img_algorithm_tracking_${MODEL_KEBAB}
            COMMENT "Generating Image for ${TARGET} with Algorithm Model: ${MODEL_DIR} -> ${IMAGE_OUTPUT_FILE}"
            COMMAND python3 ${ROOT}/tools/s300_image.py generate
            -o ${IMAGE_OUTPUT_FILE}
            --m4 ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
            --dsp ${MODEL_DIR_PATH}
            DEPENDS ${TARGET}
            USES_TERMINAL
        )
    endif()
endforeach()
