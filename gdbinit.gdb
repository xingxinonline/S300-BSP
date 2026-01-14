# =============================================================================
# S300 BSP - 通用 GDB 初始化脚本
# =============================================================================
# 适用场景: SRAM 执行 (0x20000000)，覆盖 99% 的调试场景
# 使用方式: arm-none-eabi-gdb -q -ex "file <elf>" -ex "target extended-remote :3333" -x gdbinit.gdb
# =============================================================================

# 基础配置
set pagination off
set confirm off
set remotetimeout 20

# 选择 Cortex-M4 核心 (避免误触 M0 AON)
monitor targets ne005.m4

# 停止 + 软复位 + 等待稳定
monitor halt
monitor soft_reset_halt
monitor wait_halt 2000

# 加载程序到 SRAM
load

# 设置 SRAM 向量表: SP/PC from 0x20000000
set $sp = *(unsigned int*)0x20000000
set $pc = *(unsigned int*)0x20000004

# 配置 VTOR 指向 SRAM 向量表
set {unsigned int}0xE000ED08 = 0x20000000

echo \n>>> Starting program from SRAM (0x20000000)...\n
continue
