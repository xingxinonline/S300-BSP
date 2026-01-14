# S300 BSP

PiMCHIP S300 (ARM Cortex-M4) 板级支持包，基于CMSIS标准。

## 特性

- CMSIS兼容，符合ARM标准规范
- GDB直接加载到SRAM调试
- 支持GCC + OpenOCD + ST-Link/J-Link工具链
- 丰富的外设驱动和Demo示例

## 目录结构

```
S300_BSP/
├── CMSIS/              # CMSIS标准实现
│   ├── Core/Include/   # ARM核心头文件
│   └── Device/PiMCHIP/ # S300设备定义
├── Drivers/            # 硬件驱动
│   ├── SoC/            # 片上外设 (UART/GPIO/DMA/QSPI等)
│   └── External/       # 外部器件 (OV5640/W25Qxx/WM8978)
├── Boards/             # 板级配置
├── Projects/           # 示例项目
│   ├── App_HelloWorld/
│   └── Demo/
├── ld/                 # 链接脚本
├── tools/              # PC端工具
└── docs/               # 文档
```

## 快速开始

### 环境要求

- ARM GNU Toolchain (arm-none-eabi-gcc)
- CMake + Ninja
- OpenOCD + ST-Link/J-Link

### 构建

```bash
mkdir build && cd build
cmake -G Ninja ..
ninja
```

### 调试

每个项目都有对应的`dbg_xxx`目标，自动启动GDB连接OpenOCD：

```bash
# 先启动OpenOCD（另开终端）
openocd -f s300_openocd.cfg

# 调试UART3_Echo示例
ninja dbg_uart3_echo

# 调试FreeRTOS_Shell
ninja dbg_freertos_shell

# 调试DMA传输
ninja dbg_dma_uart3_tx
```

`dbg_xxx`会自动完成：加载ELF → 连接GDB Server → 执行gdbinit.gdb初始化 → 进入调试

## 文档

- [BSP架构设计](docs/S300_BSP_Architecture.md)
- [DMA使用指南](docs/S300_DMA_LLI_Guide.md)
- [编码规范](docs/coding_style_cn.md)

## 芯片规格

| 项目   | 规格                        |
| ------ | --------------------------- |
| CPU    | ARM Cortex-M4 @ 200MHz, FPU |
| SRAM   | 8KB + 384KB                 |
| Flash  | W25Q128 (16MB, QSPI)        |
| 调试器 | ST-Link, J-Link             |

## 许可证

MIT License

