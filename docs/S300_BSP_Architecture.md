# S300 BSP架构设计

## 1. 概述

S300 BSP遵循CMSIS标准，为S300芯片提供硬件抽象层和开发支持。

设计目标：
- CMSIS兼容，提供标准化设备支持
- 分层清晰：Core/Device → Drivers → Boards → Projects
- 支持多板型配置和外设扩展

### 1.1 芯片资源

S300 (PiMCHIP) 规格：
- CPU: ARM Cortex-M4 @ 200MHz, FPU, DSP
- SRAM0: 8KB (0x10000000)
- SRAM1: 384KB (0x20000000) - 主要运行区域
- PSRAM: 8MB W956x8MBYA (OSPI, 0x90000000) - 扩展数据存储
- Flash: 4KB ROMBOOT (固化) + 16MB W25Q128 (QSPI)
- 外设: UART×4, SPI×3, I2C×2, TIMER×8, DMA, GPIO等

## 2. 目录结构

```
S300_BSP/
├── CMSIS/                    # CMSIS标准支持
│   ├── Core/Include/         # ARM核心头文件
│   └── Device/PiMCHIP/S300/  # S300设备支持
├── Drivers/                  # 硬件驱动
│   ├── SoC/                  # 片上外设驱动
│   └── External/             # 外部器件驱动
├── Boards/                   # 板级配置
├── Projects/                 # 示例项目
├── ld/                       # 链接脚本
└── docs/                     # 文档
```

### 2.1 分层设计

- **Layer 1 - CMSIS Core**: ARM标准接口，中断管理
- **Layer 2 - Device**: 寄存器定义，系统初始化，中断向量表
- **Layer 3 - Driver**: 片上外设和外部器件驱动
- **Layer 4 - Board**: 板级配置，引脚映射
- **Layer 5 - Application**: 示例程序

## 3. 启动架构

当前开发阶段使用GDB直接加载到SRAM调试：

```
GDB调试:  GDB加载ELF到SRAM → 调试执行
```

### 3.1 内存映射

SRAM布局 (384KB @ 0x20000000):
```
+-------------+ 0x20000000
| .text       |
| .rodata     |
| .data       |
| .bss        |
| heap ↓      |
|             |
| stack ↑     |
+-------------+ 0x20060000
```

SRAM0 (8KB @ 0x10000000): 备用，可用于DMA缓冲

## 4. 驱动架构

驱动采用统一的设计模式：配置结构体 + 句柄 + API函数。

### 4.1 驱动API示例

```c
// UART初始化示例
s300_uart_config_t config = {
    .baudrate = 115200,
    .word_length = 8,
    .stop_bits = 1,
    .parity = 0,
};
s300_uart_init(UART3, &config);
s300_uart_transmit(UART3, data, len);
```

### 4.2 错误码定义

```c
typedef enum {
    S300_OK = 0,
    S300_ERROR = -1,
    S300_ERROR_INVALID_PARAM = -2,
    S300_ERROR_TIMEOUT = -3,
    S300_ERROR_BUSY = -4,
} s300_status_t;
```

## 5. 板级配置

board.h定义板级引脚和参数：

```c
#define BOARD_DEBUG_UART        UART3
#define BOARD_DEBUG_BAUDRATE    115200
#define BOARD_LED1_PIN          GPIO_PIN_13
#define BOARD_LED1_PORT         GPIOC
#define BOARD_SYSCLK_FREQ       200000000
```

board.c实现初始化：

```c
int board_init(void)
{
    s300_rcc_config_sysclk(S300_RCC_SYSCLK_HSE, BOARD_SYSCLK_FREQ);
    // GPIO、UART初始化...
    return 0;
}
```

## 6. 构建

使用CMake + Ninja构建：

```bash
mkdir build && cd build
cmake -G Ninja ..
ninja
```

编译选项：
- CPU: `-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16`
- 优化: `-O2 -ffunction-sections -fdata-sections`

## 7. 调试

### 7.1 dbg_xxx调试目标

每个Demo都有`dbg_xxx` CMake目标，一键启动GDB调试：

```bash
# 先启动OpenOCD
openocd -f s300_openocd.cfg

# 调试示例（另开终端）
ninja dbg_uart3_echo        # UART回显
ninja dbg_freertos_shell    # FreeRTOS Shell
ninja dbg_dma_uart3_tx      # DMA发送
ninja dbg_psram             # PSRAM测试
ninja dbg_w25qxx_test       # Flash测试
```

dbg_xxx执行流程：
1. 编译项目（如有变更）
2. 启动arm-none-eabi-gdb
3. 连接OpenOCD（默认端口3333）
4. 执行gdbinit.gdb：halt → soft_reset → load → 设置SP/PC/VTOR

### 7.2 gdbinit.gdb

通用GDB初始化脚本，用于SRAM调试：

```gdb
monitor targets ne005.m4     # 选择M4核心
monitor halt
monitor soft_reset_halt
load                         # 加载ELF到SRAM
set $sp = *(unsigned int*)0x20000000
set $pc = *(unsigned int*)0x20000004
set {unsigned int}0xE000ED08 = 0x20000000  # VTOR
```

### 7.3 printf重定向

printf重定向到UART：
```c
int _write(int file, char *ptr, int len) {
    s300_uart_transmit(BOARD_DEBUG_UART, (uint8_t*)ptr, len);
    return len;
}
```

## 相关文档

- [DMA使用指南](S300_DMA_LLI_Guide.md)
- [编码规范](coding_style_cn.md)
