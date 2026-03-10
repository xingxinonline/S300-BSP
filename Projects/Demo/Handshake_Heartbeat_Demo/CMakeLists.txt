# =============================================================================
# Handshake_Heartbeat_Demo - CM4 与 DSP 握手及心跳通信演示
# =============================================================================
#
# 功能说明:
#   - 演示 CM4 和 DSP 之间的握手流程
#   - CM4 由 SysTick 定时器触发打印 Heartbeat
#   - DSP 由 CM4 Mailbox 消息触发打印 Heartbeat
#
# 使用方法:
#   1. 将 DSP 编译好的 bin 文件放到 dsp_bin/ 目录
#   2. 重新配置 CMake
#   3. 运行 ninja dbg_handshake_hb 启动调试 (自动加载 DSP bin)
#   4. 观察串口输出，验证握手和 Heartbeat 功能
#
# DSP bin 文件:
#   - dsp_bin/model_ptcm_boot.bin  (DSP 程序代码, 加载到 0x44A00000)
#   - dsp_bin/model_dtcm_boot.bin  (DSP 数据段, 加载到 0x44800000)
#
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(Handshake_Heartbeat_Demo)

# DSP bin 目录
set(DSP_BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dsp_bin)

# =============================================================================
# 生成 GDB restore 命令的辅助函数
# =============================================================================
function(generate_gdb_restore_cmd IMAGE_FILE ADDR OUTPUT_VAR)
    if(EXISTS ${IMAGE_FILE})
        # 将路径中的反斜杠转换为正斜杠 (GDB 兼容)
        file(TO_CMAKE_PATH "${IMAGE_FILE}" IMAGE_FILE_CLEAN)
        set(${OUTPUT_VAR} "restore ${IMAGE_FILE_CLEAN} binary ${ADDR}" PARENT_SCOPE)
        message(STATUS "  DSP bin: ${IMAGE_FILE} -> ${ADDR}")
    else()
        set(${OUTPUT_VAR} "# ${IMAGE_FILE} not found" PARENT_SCOPE)
        message(STATUS "  DSP bin: ${IMAGE_FILE} (NOT FOUND)")
    endif()
endfunction()

# =============================================================================
# 检测 DSP bin 文件并生成 GDB restore 命令
# =============================================================================
message(STATUS "Handshake_Heartbeat_Demo: Checking DSP bin files...")

generate_gdb_restore_cmd("${DSP_BIN_DIR}/model_dtcm_boot.bin" "0x44800000" GDB_RESTORE_DTCM)
generate_gdb_restore_cmd("${DSP_BIN_DIR}/model_ptcm_boot.bin" "0x44A00000" GDB_RESTORE_PTCM)

# =============================================================================
# 生成 GDB 脚本
# =============================================================================
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/gdbinit.handshake.gdb.in
    ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.handshake.gdb
    @ONLY
)

# =============================================================================
# 主目标
# =============================================================================
s300_add_executable(
    TARGET s300_handshake_heartbeat
    SOURCES Src/main.c
    DRIVERS rcc gpio uart mailbox
    DBG_NAME handshake_hb
    DBG_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.handshake.gdb
    NO_IMAGE
)

# 添加协议头文件目录
target_include_directories(s300_handshake_heartbeat PRIVATE
    ${CMAKE_SOURCE_DIR}/Algorithm_Models/protocol
)
