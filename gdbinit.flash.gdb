# =============================================================================
# S300 BSP - Flash 执行 GDB 初始化脚本
# =============================================================================
# 适用场景: Flash XIP 执行 (0x08010000)，用于 SBL/应用程序烧写后调试
# 使用方式: arm-none-eabi-gdb -q -ex "file <elf>" -ex "target extended-remote :3333" -x gdbinit.flash.gdb
# =============================================================================

# 基础配置
set pagination off
set confirm off
set remotetimeout 20

# 选择 Cortex-M4 核心
monitor targets ne005.m4

# 停止 + 等待稳定（不使用 soft_reset_halt，避免擦除 Flash）
monitor halt
monitor wait_halt 2000

# 加载程序到 Flash
load

# 设置 Flash 向量表: SP/PC from 0x08010000
set $sp = *(unsigned int*)0x08010000
set $pc = *(unsigned int*)0x08010004

# 配置 VTOR 指向 Flash 向量表
set {unsigned int}0xE000ED08 = 0x08010000

echo \n>>> Starting program from Flash (0x08010000)...\n
continue
