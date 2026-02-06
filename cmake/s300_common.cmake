# =============================================================================
# S300 BSP Common CMake Functions
# =============================================================================
# 提供统一的项目配置函数，消除各 Demo/App 的重复代码
#
# 使用方式:
# include(${CMAKE_SOURCE_DIR}/cmake/s300_common.cmake)
# s300_add_executable(TARGET s300_xxx SOURCES main.c DRIVERS rcc gpio uart)
# =============================================================================

# 确保只被包含一次
if(DEFINED S300_COMMON_INCLUDED)
    return()
endif()

set(S300_COMMON_INCLUDED TRUE)

# =============================================================================
# 驱动模块定义 - 自动包含源文件和头文件路径
# =============================================================================
# 格式: DRIVER_<NAME>_SRCS, DRIVER_<NAME>_INCS, DRIVER_<NAME>_DEPS
# 新增驱动只需在这里添加定义

# SoC 驱动
set(DRIVER_rcc_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/RCC/Source/rcc.c)
set(DRIVER_rcc_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/RCC/Include)

set(DRIVER_gpio_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/GPIO/Source/gpio.c)
set(DRIVER_gpio_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/GPIO/Include)
set(DRIVER_gpio_DEPS rcc)

set(DRIVER_uart_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/UART/Source/uart.c)
set(DRIVER_uart_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/UART/Include)
set(DRIVER_uart_DEPS rcc gpio)

set(DRIVER_dma_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/DMA/Source/dma.c)
set(DRIVER_dma_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/DMA/Include)
set(DRIVER_dma_DEPS rcc)

set(DRIVER_i2c_soft_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/I2CSoft/Source/i2c_soft.c)
set(DRIVER_i2c_soft_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/I2CSoft/Include)
set(DRIVER_i2c_soft_DEPS gpio)

set(DRIVER_mailbox_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/MAILBOX/Source/mailbox.c)
set(DRIVER_mailbox_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/MAILBOX/Include)

set(DRIVER_psram_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/PSRAM/Source/psram.c)
set(DRIVER_psram_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/PSRAM/Include)

# QSPI 驱动 (Cadence 实现)
set(DRIVER_qspi_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/QSPI/Source/qspi_cadence.c)
set(DRIVER_qspi_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/QSPI/Include)

set(DRIVER_i2s_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/I2S/Source/i2s.c)
set(DRIVER_i2s_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/I2S/Include)
set(DRIVER_i2s_DEPS rcc gpio dma)

set(DRIVER_mm_SRCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/MM/Source/video.c)
set(DRIVER_mm_INCS ${CMAKE_SOURCE_DIR}/Drivers/SoC/MM/Include)
set(DRIVER_mm_DEPS ov5640)

# 外设驱动
set(DRIVER_ov5640_SRCS ${CMAKE_SOURCE_DIR}/Drivers/External/OV5640/Source/ov5640.c)
set(DRIVER_ov5640_INCS ${CMAKE_SOURCE_DIR}/Drivers/External/OV5640/Include)
set(DRIVER_ov5640_DEPS i2c_soft)

set(DRIVER_w25qxx_SRCS ${CMAKE_SOURCE_DIR}/Drivers/External/W25Qxx/Source/w25qxx.c)
set(DRIVER_w25qxx_INCS ${CMAKE_SOURCE_DIR}/Drivers/External/W25Qxx/Include)
set(DRIVER_w25qxx_DEPS qspi)

set(DRIVER_wm8978_SRCS ${CMAKE_SOURCE_DIR}/Drivers/External/WM8978/Source/wm8978.c)
set(DRIVER_wm8978_INCS ${CMAKE_SOURCE_DIR}/Drivers/External/WM8978/Include)
set(DRIVER_wm8978_DEPS i2c_soft i2s)

set(DRIVER_ft6x36_SRCS ${CMAKE_SOURCE_DIR}/Drivers/External/FT6X36/Source/ft6x36.c)
set(DRIVER_ft6x36_INCS ${CMAKE_SOURCE_DIR}/Drivers/External/FT6X36/Include)
set(DRIVER_ft6x36_DEPS i2c_soft)

set(DRIVER_es7210_SRCS ${CMAKE_SOURCE_DIR}/Drivers/External/ES7210/Source/es7210.c)
set(DRIVER_es7210_INCS ${CMAKE_SOURCE_DIR}/Drivers/External/ES7210/Include)
set(DRIVER_es7210_DEPS i2c_soft)

set(DRIVER_es8311_SRCS ${CMAKE_SOURCE_DIR}/Drivers/External/ES8311/Source/es8311.c)
set(DRIVER_es8311_INCS ${CMAKE_SOURCE_DIR}/Drivers/External/ES8311/Include)
set(DRIVER_es8311_DEPS i2c_soft)

# QMI8658A 六轴IMU传感器驱动
set(DRIVER_qmi8658a_SRCS ${CMAKE_SOURCE_DIR}/Drivers/External/QMI8658A/Source/qmi8658a.c)
set(DRIVER_qmi8658a_INCS ${CMAKE_SOURCE_DIR}/Drivers/External/QMI8658A/Include)
set(DRIVER_qmi8658a_DEPS i2c_soft)

# IMU 姿态解算库
set(DRIVER_imu_SRCS ${CMAKE_SOURCE_DIR}/Drivers/External/IMU/Source/imu.c)
set(DRIVER_imu_INCS ${CMAKE_SOURCE_DIR}/Drivers/External/IMU/Include)

# =============================================================================
# 核心路径定义
# =============================================================================
set(S300_DEVICE_DIR ${CMAKE_SOURCE_DIR}/CMSIS/Device/PiMCHIP/S300)
set(S300_CORE_INC ${CMAKE_SOURCE_DIR}/CMSIS/Core/Include)
set(S300_DRIVERS_DIR ${CMAKE_SOURCE_DIR}/Drivers)
set(S300_DEVICE_INC ${S300_DEVICE_DIR}/Include)
set(S300_STARTUP_FILE ${S300_DEVICE_DIR}/Source/GCC/startup_S300.S)
set(S300_SYSTEM_FILE ${S300_DEVICE_DIR}/Source/system_S300.c)
set(S300_DEFAULT_LD ${CMAKE_SOURCE_DIR}/ld/sram.ld)

# =============================================================================
# s300_add_executable - 统一的项目创建函数
# =============================================================================
# 用法:
# s300_add_executable(
# TARGET      s300_my_demo           # 目标名称
# SOURCES     main.c helper.c        # 项目源文件（相对于当前目录）
# DRIVERS     rcc gpio uart dma      # 需要的驱动模块
# INCLUDES    Inc                    # 额外的 include 目录（相对于当前目录）
# LIBRARIES   s300_video m           # 额外链接的库
# DEFINES     DEBUG=1 MY_MACRO       # 额外的编译定义
# LINKER_SCRIPT path/to/custom.ld    # 自定义链接脚本（可选）
# DBG_NAME    my_demo                # 调试目标后缀（可选，默认从 TARGET 推断）
# MM_ENABLE                          # 启用多媒体子系统（可选标志）
# NO_IMAGE                           # 不生成镜像目标（可选标志）
# )
function(s300_add_executable)
    set(options MM_ENABLE NO_IMAGE)
    set(oneValueArgs TARGET LINKER_SCRIPT DBG_NAME)
    set(multiValueArgs SOURCES DRIVERS INCLUDES LIBRARIES DEFINES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # 验证必需参数
    if(NOT ARG_TARGET)
        message(FATAL_ERROR "s300_add_executable: TARGET is required")
    endif()

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "s300_add_executable: SOURCES is required")
    endif()

    # 收集源文件
    set(ALL_SRCS
        ${S300_STARTUP_FILE}
        ${S300_SYSTEM_FILE}
    )

    # 添加项目源文件（转换为绝对路径）
    foreach(src ${ARG_SOURCES})
        if(IS_ABSOLUTE ${src})
            list(APPEND ALL_SRCS ${src})
        else()
            list(APPEND ALL_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/${src})
        endif()
    endforeach()

    # 收集 Include 路径
    set(ALL_INCS
        ${S300_DEVICE_INC}
        ${S300_CORE_INC}
    )

    # 添加项目 Include 目录
    foreach(inc ${ARG_INCLUDES})
        if(IS_ABSOLUTE ${inc})
            list(APPEND ALL_INCS ${inc})
        else()
            list(APPEND ALL_INCS ${CMAKE_CURRENT_SOURCE_DIR}/${inc})
        endif()
    endforeach()

    # 处理驱动模块 (包含依赖解析)
    set(_RESOLVED_DRIVERS "")
    set(_PENDING_DRIVERS ${ARG_DRIVERS})

    while(_PENDING_DRIVERS)
        list(POP_FRONT _PENDING_DRIVERS _DRV)

        if(_DRV IN_LIST _RESOLVED_DRIVERS)
            continue()
        endif()

        list(APPEND _RESOLVED_DRIVERS ${_DRV})

        # 添加依赖到待处理列表
        if(DEFINED DRIVER_${_DRV}_DEPS)
            list(APPEND _PENDING_DRIVERS ${DRIVER_${_DRV}_DEPS})
        endif()
    endwhile()

    foreach(drv ${_RESOLVED_DRIVERS})
        if(DEFINED DRIVER_${drv}_SRCS)
            list(APPEND ALL_SRCS ${DRIVER_${drv}_SRCS})
        endif()

        if(DEFINED DRIVER_${drv}_INCS)
            list(APPEND ALL_INCS ${DRIVER_${drv}_INCS})
        endif()

        if(NOT DEFINED DRIVER_${drv}_SRCS AND NOT DEFINED DRIVER_${drv}_INCS)
            message(WARNING "s300_add_executable: Unknown driver '${drv}'")
        endif()
    endforeach()

    # 设置 ASM 文件属性
    set_source_files_properties(${S300_STARTUP_FILE} PROPERTIES LANGUAGE ASM)

    # 创建可执行目标
    add_executable(${ARG_TARGET} ${ALL_SRCS})

    # 设置 Include 路径
    target_include_directories(${ARG_TARGET} PRIVATE ${ALL_INCS})

    # 链接板级支持库
    set(LINK_LIBS s300_board)

    if(ARG_LIBRARIES)
        list(APPEND LINK_LIBS ${ARG_LIBRARIES})
    endif()

    target_link_libraries(${ARG_TARGET} ${LINK_LIBS})

    # 编译定义
    if(ARG_MM_ENABLE)
        target_compile_definitions(${ARG_TARGET} PRIVATE BOARD_MM_ENABLE=1)
    endif()

    if(ARG_DEFINES)
        target_compile_definitions(${ARG_TARGET} PRIVATE ${ARG_DEFINES})
    endif()

    # 链接脚本
    if(ARG_LINKER_SCRIPT)
        if(IS_ABSOLUTE ${ARG_LINKER_SCRIPT})
            set(LD_SCRIPT ${ARG_LINKER_SCRIPT})
        else()
            set(LD_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_LINKER_SCRIPT})
        endif()
    else()
        set(LD_SCRIPT ${S300_DEFAULT_LD})
    endif()

    set_target_properties(${ARG_TARGET} PROPERTIES LINK_FLAGS "-T${LD_SCRIPT}")

    # Post-build: 生成 bin/hex/dis/size
    add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
        COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${ARG_TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET}.bin
        COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${ARG_TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET}.hex
        COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${ARG_TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET}.dis
        COMMAND arm-none-eabi-size $<TARGET_FILE:${ARG_TARGET}>
        COMMENT "Generating artifacts for ${ARG_TARGET}"
    )

    # 调试目标名称
    if(ARG_DBG_NAME)
        set(DBG_SUFFIX ${ARG_DBG_NAME})
    else()
        # 从 TARGET 名称推断（去掉 s300_ 前缀）
        string(REGEX REPLACE "^s300_" "" DBG_SUFFIX ${ARG_TARGET})
    endif()

    # Debug target
    set(GDB_PORT 3333 CACHE STRING "GDB server port")
    add_custom_target(dbg_${DBG_SUFFIX}
        COMMENT "Launching GDB for ${ARG_TARGET} (GDB_PORT=${GDB_PORT})"
        COMMAND arm-none-eabi-gdb -q
        -ex "file $<TARGET_FILE:${ARG_TARGET}>"
        -ex "target extended-remote :${GDB_PORT}"
        -x ${CMAKE_SOURCE_DIR}/gdbinit.gdb
        DEPENDS ${ARG_TARGET}
        USES_TERMINAL
    )

    # 镜像生成目标（除非指定 NO_IMAGE）
    if(NOT ARG_NO_IMAGE)
        add_s300_image(${ARG_TARGET})
    endif()

    message(STATUS "S300 Target: ${ARG_TARGET} (drivers: ${ARG_DRIVERS})")
endfunction()

# =============================================================================
# s300_resolve_drivers - 解析驱动依赖并输出源文件和头文件列表
# =============================================================================
# 用于复杂项目手动构建时自动解析驱动依赖
#
# 用法:
# s300_resolve_drivers(
# DRIVERS uart mm mailbox psram
# OUT_SRCS MY_DRIVER_SRCS
# OUT_INCS MY_DRIVER_INCS
# )
# 之后可使用 ${MY_DRIVER_SRCS} 和 ${MY_DRIVER_INCS}
function(s300_resolve_drivers)
    set(options "")
    set(oneValueArgs OUT_SRCS OUT_INCS)
    set(multiValueArgs DRIVERS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_DRIVERS)
        message(FATAL_ERROR "s300_resolve_drivers: DRIVERS is required")
    endif()

    # 解析驱动依赖
    set(_RESOLVED_DRIVERS "")
    set(_PENDING_DRIVERS ${ARG_DRIVERS})

    while(_PENDING_DRIVERS)
        list(POP_FRONT _PENDING_DRIVERS _DRV)

        if(_DRV IN_LIST _RESOLVED_DRIVERS)
            continue()
        endif()

        list(APPEND _RESOLVED_DRIVERS ${_DRV})

        if(DEFINED DRIVER_${_DRV}_DEPS)
            list(APPEND _PENDING_DRIVERS ${DRIVER_${_DRV}_DEPS})
        endif()
    endwhile()

    # 收集源文件和头文件
    set(_ALL_SRCS "")
    set(_ALL_INCS "")

    foreach(drv ${_RESOLVED_DRIVERS})
        if(DEFINED DRIVER_${drv}_SRCS)
            list(APPEND _ALL_SRCS ${DRIVER_${drv}_SRCS})
        endif()

        if(DEFINED DRIVER_${drv}_INCS)
            list(APPEND _ALL_INCS ${DRIVER_${drv}_INCS})
        endif()
    endforeach()

    # 输出到父作用域
    if(ARG_OUT_SRCS)
        set(${ARG_OUT_SRCS} ${_ALL_SRCS} PARENT_SCOPE)
    endif()

    if(ARG_OUT_INCS)
        set(${ARG_OUT_INCS} ${_ALL_INCS} PARENT_SCOPE)
    endif()
endfunction()

# =============================================================================
# 辅助宏：快速创建简单项目
# =============================================================================
# 用于只有一个 main.c 的简单 demo
macro(s300_simple_demo TARGET_NAME)
    s300_add_executable(
        TARGET ${TARGET_NAME}
        SOURCES Src/main.c
        DRIVERS rcc gpio uart
        ${ARGN}
    )
endmacro()
