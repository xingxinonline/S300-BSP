# =============================================================================
# LVGL_Gesture_Progress_Bar - 手势进度条演示
# =============================================================================
# 本项目不使用 s300_add_executable() 的原因:
#   1. 需要 FetchContent 集成 LVGL
#   2. 需要自定义多模型 GDB 调试目标
# =============================================================================

cmake_minimum_required(VERSION 3.16)

project(LVGL_Gesture_Progress_Bar)

# Target
set(TARGET s300_gesture_progress_demo)

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
    ${S300_BOARD_DIR}
    ${PROJECT_DIR}/Inc
    ${ROOT}/Algorithm_Models/protocol
)

# Integrate LVGL v9.4
# 检查 lvgl 目标是否已存在（可能由其他项目创建）
if(NOT TARGET lvgl)
    set(LVGL_LOCAL_PATH "" CACHE PATH "Local path to LVGL sources (if set, FetchContent is skipped)")
    set(LVGL_VENDOR_DIR ${PROJECT_DIR}/third_party/lvgl/lvgl)

    # Point LVGL to our local config header
    set(LV_CONF_PATH ${PROJECT_DIR}/third_party/lvgl/lv_conf.h CACHE STRING "Path to lv_conf.h" FORCE)

    # Configure LVGL options to keep footprint small on MCU (must be set before adding lvgl)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "Use ThorVG internal" FORCE)
    set(LV_BUILD_CONF_PATH ${LV_CONF_PATH} CACHE PATH "Path to lv_conf.h file" FORCE)

    if(EXISTS ${LVGL_VENDOR_DIR}/CMakeLists.txt AND EXISTS ${LVGL_VENDOR_DIR}/lvgl.h)
        message(STATUS "Using LVGL from vendor directory: ${LVGL_VENDOR_DIR}")
        add_subdirectory(${LVGL_VENDOR_DIR} ${CMAKE_CURRENT_BINARY_DIR}/lvgl-vendor-build)
    elseif(LVGL_LOCAL_PATH)
        message(STATUS "Using LVGL from local path: ${LVGL_LOCAL_PATH}")
        add_subdirectory(${LVGL_LOCAL_PATH} ${CMAKE_CURRENT_BINARY_DIR}/lvgl-local-build)
    else()
        include(FetchContent)
        FetchContent_Declare(
            lvgl
            GIT_REPOSITORY https://github.com/lvgl/lvgl.git
            GIT_TAG v9.4.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(lvgl)
    endif()

    # Ensure lvgl sees LV_CONF_PATH
    if(TARGET lvgl)
        target_compile_definitions(lvgl PUBLIC LV_CONF_PATH="${LV_CONF_PATH}")
    endif()
endif()

# Sources
set(SRCS
    ${S300_STARTUP_FILE}
    ${S300_BOARD_DIR}/board.c
    ${S300_BOARD_COMMON_DIR}/retarget.c
    ${S300_BOARD_COMMON_DIR}/syscalls.c
    ${S300_SYSTEM_FILE}
    ${DRIVER_SRCS}
    ${PROJECT_DIR}/Src/main.c
    ${PROJECT_DIR}/Src/ui_display.c
    ${PROJECT_DIR}/Src/app_mailbox.c
    ${PROJECT_DIR}/Src/camera_ov5640.c
)

set_source_files_properties(${S300_STARTUP_FILE} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})

# Linker script
set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-T${S300_DEFAULT_LD}")

# Link libraries (including drivers and LVGL)
target_link_libraries(${TARGET} PRIVATE
    lvgl
)

# Post build artifacts (bin/hex/disasm/size)
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dis
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMENT "Generating binary, hex, disassembly and size info"
)

# Use Algorithm_Models/Hand_Gesture for DSP firmware (Gesture Progress Demo default)
cmake_path(ABSOLUTE_PATH ROOT NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(HAND_GESTURE_DIR ${ROOT_ABS}/Algorithm_Models/Hand_Gesture)

# Helper function to generate GDB restore command for a binary image
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

# Generate restore commands for Hand_Gesture model
generate_gdb_restore_cmd("${HAND_GESTURE_DIR}/model_dtcm_boot.bin" "0x44800000" GDB_RESTORE_DTCM)
generate_gdb_restore_cmd("${HAND_GESTURE_DIR}/model_ptcm_boot.bin" "0x44A00000" GDB_RESTORE_PTCM)
generate_gdb_restore_cmd("${HAND_GESTURE_DIR}/model_sram0_boot.bin" "0x44000000" GDB_RESTORE_SRAM0)
generate_gdb_restore_cmd("${HAND_GESTURE_DIR}/model_psram_boot.bin" "0x80000000" GDB_RESTORE_PSRAM)

# Use shared model GDB template
set(MODEL_NAME "Hand_Gesture")
set(MODEL_KEBAB "hand-gesture")
set(MODEL_DIR ${HAND_GESTURE_DIR})
set(PSRAM_BREAKPOINT_FUNC "main")

configure_file(
    ${ROOT}/gdbinit.model.gdb.in
    ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    @ONLY
)

# Debug target with Hand_Gesture model preloaded
add_custom_target(dbg_gesture_demo_hand_gesture
    COMMENT "Launching GDB for ${TARGET} with Hand_Gesture model (SRAM: 0x20000000) (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

# Legacy target alias
add_custom_target(dbg_gesture_demo
    COMMENT "Launching GDB for ${TARGET} with default DSP images (Hand Gesture)"
    COMMAND ${CMAKE_COMMAND} --build . --target dbg_gesture_demo_hand_gesture
    DEPENDS dbg_gesture_demo_hand_gesture
) # 生成 S300 ROMBOOT 镜像 (含手势识别模型)
add_s300_image(${TARGET}
    OUTPUT s300_gesture_progress_demo.bin
    DSP_DIR ${HAND_GESTURE_DIR}
)

