cmake_minimum_required(VERSION 3.16)

project(Display_Demo)

# Target
set(TARGET s300_display_demo)

# Paths
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)
set(DEVICE_DIR ${ROOT}/CMSIS/Device/PiMCHIP/S300)
set(LD_SCRIPT ${ROOT}/ld/sram.ld)

# Includes
include_directories(
    ${DEVICE_DIR}/Include
    ${ROOT}/CMSIS/Core/Include
    ${ROOT}/Drivers/SoC/RCC/Include
    ${ROOT}/Drivers/SoC/GPIO/Include
    ${ROOT}/Drivers/SoC/UART/Include
    ${ROOT}/Drivers/SoC/DMA/Include
    ${ROOT}/Drivers/SoC/I2CSoft/Include
    ${ROOT}/Drivers/SoC/MAILBOX/Include
    ${ROOT}/Drivers/External/OV5640/Include
    ${ROOT}/Drivers/SoC/MM/Include
    ${ROOT}/Drivers/SoC/PSRAM/Include
    ${PROJECT_DIR}/Inc
)

# Integrate LVGL v9.4 (online via FetchContent or offline via local path)
# Option: provide a local LVGL source tree to avoid network access
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

# Board feature enable macros (启用多媒体子系统)
add_compile_definitions(
    BOARD_MM_ENABLE=1
)

# Sources (board files provided by s300_board library)
set(SRCS
    ${DEVICE_DIR}/Source/GCC/startup_S300.S
    ${ROOT}/CMSIS/Device/PiMCHIP/S300/Source/system_S300.c
    ${ROOT}/Drivers/SoC/RCC/Source/rcc.c
    ${ROOT}/Drivers/SoC/GPIO/Source/gpio.c
    ${ROOT}/Drivers/SoC/UART/Source/uart.c
    ${ROOT}/Drivers/SoC/DMA/Source/dma.c
    ${ROOT}/Drivers/SoC/I2CSoft/Source/i2c_soft.c
    ${ROOT}/Drivers/External/OV5640/Source/ov5640.c
    ${PROJECT_DIR}/Src/main.c
    ${PROJECT_DIR}/Src/ui_display.c
    ${PROJECT_DIR}/Src/eyes.c
    ${PROJECT_DIR}/Src/camera_ov5640.c
    ${PROJECT_DIR}/Src/face_tracker.c
    ${PROJECT_DIR}/Src/uart_cmd.c
    ${PROJECT_DIR}/Src/display_demo_app.c
)

# Auto-include all generated image assets
file(GLOB GEN_IMG_SRCS ${PROJECT_DIR}/Assets/generated/*.c)
list(APPEND SRCS ${GEN_IMG_SRCS})
set_source_files_properties(${DEVICE_DIR}/Source/GCC/startup_S300.S PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})

# Link board support library and other dependencies
target_link_libraries(${TARGET}
    s300_board
    s300_video
    s300_mailbox
    s300_psram
    lvgl
)

# Linker script
set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-T${LD_SCRIPT}")

# Post build artifacts (bin/hex/disasm/size)
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dis
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMENT "Generating binary, hex, disassembly and size info for display demo"
)

# Debug target: 启动 GDB 调试当前 demo（沿用 RBL 的 gdbinit 脚本）
set(GDB_PORT 3333 CACHE STRING "GDB server port")
add_custom_target(dbg_display
    COMMENT "Launching GDB for ${TARGET} (SRAM: 0x20000000) (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${ROOT}/gdbinit.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

# Use Algorithm_Models/Face_Detection for DSP firmware
cmake_path(ABSOLUTE_PATH ROOT NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(FACE_DETECTION_DIR ${ROOT_ABS}/Algorithm_Models/Face_Detection)

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

# Generate restore commands for Face_Detection model
generate_gdb_restore_cmd("${FACE_DETECTION_DIR}/model_dtcm_boot.bin" "0x44800000" GDB_RESTORE_DTCM)
generate_gdb_restore_cmd("${FACE_DETECTION_DIR}/model_ptcm_boot.bin" "0x44A00000" GDB_RESTORE_PTCM)
generate_gdb_restore_cmd("${FACE_DETECTION_DIR}/model_sram0_boot.bin" "0x44000000" GDB_RESTORE_SRAM0)
generate_gdb_restore_cmd("${FACE_DETECTION_DIR}/model_psram_boot.bin" "0x80000000" GDB_RESTORE_PSRAM)

# Use shared model GDB template
set(MODEL_NAME "Face_Detection")
set(MODEL_KEBAB "face-detection")
set(MODEL_DIR ${FACE_DETECTION_DIR})
set(PSRAM_BREAKPOINT_FUNC "display_demo_app_init")

configure_file(
    ${ROOT}/gdbinit.model.gdb.in
    ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    @ONLY
)

# Debug target with Face_Detection model preloaded
add_custom_target(dbg_display_face-detection
    COMMENT "Launching GDB for ${TARGET} with Face_Detection model (SRAM: 0x20000000) (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)

# 生成 S300 ROMBOOT 镜像 (带人脸检测模型)
add_s300_image(${TARGET}
    OUTPUT s300_display_face_detection.bin
    DSP_DIR ${ROOT}/Algorithm_Models/Face_Detection
)
