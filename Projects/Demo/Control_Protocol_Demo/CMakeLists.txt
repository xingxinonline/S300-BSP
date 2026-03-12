# =============================================================================
# Control_Protocol_Demo - CM4 与 DSP 控制面协议测试 Demo
# =============================================================================

# 使用方法:
#   1. 将本 Demo 对应的 DSP bin 文件放到 model_bin/ 目录
#   2. 运行 ninja dbg_control_protocol 启动调试 (自动加载本 Demo 的 DSP bin)
#
# DSP bin 文件:
#   - model_bin/model_ptcm_boot.bin  (DSP 程序代码, 加载到 0x44A00000)
#   - model_bin/model_dtcm_boot.bin  (DSP 数据段, 加载到 0x44800000)

cmake_minimum_required(VERSION 3.16)
project(Control_Protocol_Demo)

set(DSP_BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/model_bin)
set(CONTROL_PROTOCOL_GDB_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.control_protocol.gdb)

s300_add_executable(
    TARGET s300_control_protocol_demo
    SOURCES Src/main.c
    DRIVERS rcc gpio uart mailbox
    DBG_NAME control_protocol
    DBG_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.control_protocol.gdb
    NO_IMAGE
)

target_include_directories(s300_control_protocol_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/Algorithm_Models/protocol
)

add_custom_command(TARGET s300_control_protocol_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DDSP_BIN_DIR=${DSP_BIN_DIR}
        -DTEMPLATE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/gdbinit.control_protocol.gdb.in
        -DOUTPUT_FILE=${CONTROL_PROTOCOL_GDB_SCRIPT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_control_protocol_gdb.cmake
    COMMENT "Refreshing Control_Protocol_Demo GDB script from current DSP bins"
)

add_s300_image(s300_control_protocol_demo
    OUTPUT s300_control_protocol_demo.bin
    DSP_DIR ${DSP_BIN_DIR}
)