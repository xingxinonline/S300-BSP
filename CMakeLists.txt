cmake_minimum_required(VERSION 3.16)

# Set the toolchain for ARM Cortex-M4
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Specify the cross compiler
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_C_COMPILER arm-none-eabi-gcc.exe)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++.exe)
    set(CMAKE_ASM_COMPILER arm-none-eabi-gcc.exe)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
    set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
else()
    message(FATAL_ERROR "Unsupported host system: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

# Since it's bare-metal, don't try to link executables during compiler tests
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Set the project name
project(S300_BSP)

# =============================================================================
# Board Selection (Multi-board Support)
# =============================================================================
# Usage: cmake -DBOARD=<board_name> ...
# Available boards: generic_evb, ...  (add new boards under Boards/)
set(BOARD "generic_evb" CACHE STRING "Target board name (e.g., generic_evb)")
message(STATUS "Building for board: ${BOARD}")

# Set compiler flags
# Debug level: -g1 (minimal, fast loading) | -g2 (standard) | -g3 (full, slow loading)
# Use -g1 for faster GDB symbol loading, -g3 for detailed macro debugging
set(DEBUG_LEVEL "-g1" CACHE STRING "Debug information level (-g1=minimal/fast, -g2=standard, -g3=full/slow)")
set(CMAKE_C_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -ffunction-sections -fdata-sections -fno-builtin -Wall -Wextra ${DEBUG_LEVEL} -gdwarf-2")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS}")

# Enable ASM language
enable_language(ASM)

# Set linker flags
set(CMAKE_EXE_LINKER_FLAGS "-nostartfiles -Wl,--gc-sections -specs=nosys.specs")

# =============================================================================
# 版本信息配置 (每次构建自动更新)
# =============================================================================
set(S300_VERSION_MAJOR 1)
set(S300_VERSION_MINOR 0)
set(S300_VERSION_PATCH 0)

# 获取构建时间戳 (CMake 配置时)
string(TIMESTAMP S300_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

# 获取 Git 信息
find_package(Git QUIET)

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE S300_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE S300_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(S300_GIT_BRANCH "unknown")
    set(S300_GIT_COMMIT "unknown")
endif()

# 生成 version.h 到构建目录
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/version.h.in
    ${CMAKE_BINARY_DIR}/generated/version.h
    @ONLY
)

# 添加到包含路径
include_directories(${CMAKE_BINARY_DIR}/generated)

# ===========================================================================
# 强制每次构建重编译 version.c 的机制:
# 生成一个 build_timestamp.h 头文件，每次构建都更新其内容（包含当前时间）
# version.c 依赖此头文件，因此每次构建都会被重新编译
# ===========================================================================
set(BUILD_TIMESTAMP_HEADER "${CMAKE_BINARY_DIR}/generated/build_timestamp.h")

# 添加 CMake 脚本来生成时间戳头文件
file(WRITE ${CMAKE_BINARY_DIR}/update_timestamp.cmake
    "string(TIMESTAMP CURRENT_TIME \"%Y-%m-%d %H:%M:%S\")
file(WRITE \"${BUILD_TIMESTAMP_HEADER}\"
\"// Auto-generated file - DO NOT EDIT
#ifndef BUILD_TIMESTAMP_H
#define BUILD_TIMESTAMP_H
#define BUILD_TIMESTAMP_STR \\\"$\{CURRENT_TIME\}\\\"
#endif
\")
")

add_custom_target(s300_update_timestamp ALL
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/update_timestamp.cmake
    BYPRODUCTS ${BUILD_TIMESTAMP_HEADER}
    COMMENT "Updating build timestamp header"
)

# Add subdirectories
add_subdirectory(CMSIS)
add_subdirectory(Boards)

# =============================================================================
# 包含公共 CMake 函数
# =============================================================================
# 提供 s300_add_executable() 等便捷函数，简化项目配置
include(${CMAKE_SOURCE_DIR}/cmake/s300_common.cmake)

# =============================================================================
# 全局镜像目标收集器
# =============================================================================
# 用于收集所有注册的镜像目标，最终创建统一的 `images` 目标
set(S300_IMAGE_TARGETS "" CACHE INTERNAL "List of all image targets")

# Helper function to add image generation target
# Usage: add_s300_image(target_name [M0_BIN path] [DSP_DIR path] [OUTPUT name] [DSP_PRO hex] [IMG_TARGET name])
# Note: 输出文件名会自动追加板型后缀，例如 demo.bin -> demo_generic_evb.bin
#
# 使用方式:
#   ninja img_<target>     - 生成单个镜像
#   ninja images           - 生成所有已注册的镜像
#
# IMG_TARGET: 自定义镜像目标名称（用于同一个 TARGET 生成多个不同模型的镜像）
function(add_s300_image TARGET)
    set(options)
    set(oneValueArgs M0_BIN DSP_DIR OUTPUT DSP_PRO IMG_TARGET)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_OUTPUT)
        set(ARG_OUTPUT ${TARGET}.bin)
    endif()

    # 自定义镜像目标名称，默认为 img_${TARGET}
    if(ARG_IMG_TARGET)
        set(IMG_TARGET_NAME img_${ARG_IMG_TARGET})
    else()
        set(IMG_TARGET_NAME img_${TARGET})
    endif()

    # 自动追加板型后缀到输出文件名 (demo.bin -> demo_${BOARD}.bin)
    string(REGEX REPLACE "\\.bin$" "_${BOARD}.bin" ARG_OUTPUT "${ARG_OUTPUT}")

    # 使用生成的 .bin 文件而不是 .elf 文件
    get_target_property(TARGET_DIR ${TARGET} BINARY_DIR)
    set(M4_BIN_FILE ${TARGET_DIR}/${TARGET}.bin)

    set(CMD_ARGS generate -o ${CMAKE_BINARY_DIR}/${ARG_OUTPUT} --m4 ${M4_BIN_FILE})

    # 如果指定了 M0 bin 文件
    if(ARG_M0_BIN)
        list(APPEND CMD_ARGS --m0 ${ARG_M0_BIN})
    endif()

    # 如果指定了 DSP 目录
    if(ARG_DSP_DIR)
        list(APPEND CMD_ARGS --dsp ${ARG_DSP_DIR})
    endif()

    # 如果指定了 DSP Pro 值
    if(ARG_DSP_PRO)
        list(APPEND CMD_ARGS --dsp-pro ${ARG_DSP_PRO})
    endif()

    add_custom_target(${IMG_TARGET_NAME}
        COMMAND python s300_image.py ${CMD_ARGS}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools
        DEPENDS ${TARGET}
        COMMENT "Generating S300 image: ${ARG_OUTPUT}"
    )

    # 注册到全局镜像目标列表
    set(S300_IMAGE_TARGETS ${S300_IMAGE_TARGETS} ${IMG_TARGET_NAME} CACHE INTERNAL "List of all image targets")
endfunction()

add_subdirectory(Projects)

# =============================================================================
# 统一镜像生成目标
# =============================================================================
# 生成所有已注册的镜像: ninja images
if(S300_IMAGE_TARGETS)
    add_custom_target(images
        DEPENDS ${S300_IMAGE_TARGETS}
        COMMENT "Generating all S300 images for board: ${BOARD}"
    )
    message(STATUS "Registered image targets: ${S300_IMAGE_TARGETS}")
endif()

# Add debug target (equivalent to make dbg)
# GDB port can be customized; default OpenOCD/J-Link is :3333
set(GDB_PORT 3333 CACHE STRING "GDB server port")

if(TARGET s300_rbl_minimal)
    add_custom_target(dbg_rbl
        COMMENT "Launching GDB for s300_rbl_minimal (RBL - SRAM: 0x20000000) (GDB_PORT=${GDB_PORT})"
        COMMAND arm-none-eabi-gdb -q
        -ex "file $<TARGET_FILE:s300_rbl_minimal>"
        -ex "target extended-remote :${GDB_PORT}"
        -x ${CMAKE_SOURCE_DIR}/gdbinit.gdb
        DEPENDS s300_rbl_minimal
        USES_TERMINAL
    )
endif()

if(TARGET s300_sbl_minimal)
    add_custom_target(dbg_sbl
        COMMENT "Launching GDB for s300_sbl_minimal (SBL - Flash: 0x08010000) (GDB_PORT=${GDB_PORT})"
        COMMAND arm-none-eabi-gdb -q
        -ex "file $<TARGET_FILE:s300_sbl_minimal>"
        -ex "target extended-remote :${GDB_PORT}"
        -x ${CMAKE_SOURCE_DIR}/gdbinit.flash.gdb
        DEPENDS s300_sbl_minimal
        USES_TERMINAL
    )
endif()

if(TARGET s300_rbl_minimal)
    add_custom_target(dbg DEPENDS dbg_rbl)
elseif(TARGET s300_sbl_minimal)
    add_custom_target(dbg DEPENDS dbg_sbl)
elseif(TARGET s300_display_demo)
    add_custom_target(dbg DEPENDS dbg_display)
elseif(TARGET s300_uart3_echo)
    add_custom_target(dbg DEPENDS dbg_uart3_echo)
elseif(TARGET s300_dma_uart3_tx)
    add_custom_target(dbg DEPENDS dbg_dma_uart3_tx)
elseif(TARGET s300_dma_uart3_rx)
    add_custom_target(dbg DEPENDS dbg_dma_uart3_rx)
elseif(TARGET s300_dma_ram2ram)
    add_custom_target(dbg DEPENDS dbg_dma_ram2ram)
elseif(TARGET s300_dma_interrupt)
    add_custom_target(dbg DEPENDS dbg_dma_interrupt)
elseif(TARGET s300_freertos_shell)
    add_custom_target(dbg DEPENDS dbg_freertos_shell)
endif()

# Set the default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()