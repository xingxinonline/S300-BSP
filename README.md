# PiMCHIP S300 BSP

**S300 (ARM Cortex-M4 + AI 子系统)** 的官方板级支持包 (BSP)，基于 CMSIS 标准构建。

本项目提供 S300 芯片的嵌入式开发框架，支持外设驱动、异构核间通信及示例应用。

## 🌟 核心特性

- **CMSIS 兼容**：遵循 ARM CMSIS 标准，提供标准化的 Register 定义与 SystemInit。
- **异构协同**：提供 **Cortex-M4** 与 **AI 算力子系统** 的通信机制 (Mailbox + Shared Memory)，支持调用 AI 算法。
- **构建系统**：基于 **CMake + Ninja**，支持 Windows/Linux 跨平台开发，提供一键编译与 GDB 调试目标。
- **驱动支持**：
  - **DMA LLI**：使用链表传输，降低大数据搬运时的 CPU 负载。
  - **PSRAM/OSPI**：板外存储驱动优化。
  - **多媒体**：集成 Camera (OV5640)、Display (LVGL v9)、Audio (I2S) 驱动。

## 📂 目录结构

```
S300_BSP/
├── Algorithm_Models/         # AI/算法固件 (预编译的 AI 模型固件)
├── Boards/                   # 板级配置 (支持 Generic EVB, App Board 等多板型)
├── CMSIS/                    # CMSIS 标准核心与设备定义
├── Drivers/                  # 硬件驱动库
│   ├── SoC/                  # 片上外设 (DMA, UART, GPIO, Video, Mailbox...)
│   └── External/             # 板载器件 (OV5640, W25Qxx, LCD...)
├── Projects/                 # 示例工程与应用
│   ├── App_HelloWorld/       # 基础模板工程
│   └── Demo/                 # 进阶功能演示 (Display, AI Face Track, Audio...)
├── docs/                     # 开发文档 (架构设计, 编程规范, DMA指南)
├── ld/                       # 链接脚本
└── tools/                    # 辅助开发工具 (Python 脚本)
```

## 🚀 快速开始

### 1. 环境准备

请确保已安装以下工具并加入 PATH 环境变量：
- **ARM GCC Toolchain** (`arm-none-eabi-gcc`)
- **CMake** (>= 3.16)
- **Ninja**
- **OpenOCD** (用于调试连接)
- **Python 3** (用于辅助脚本)

### 2. 编译工程

在仓库根目录下执行：

```bash
# 1. 配置工程 (使用 Ninja 生成器)
cmake -B build -G Ninja

# 2. 编译所有目标
ninja -C build
```

或者编译指定 Demo：
```bash
ninja -C build s300_display_demo
```

### 3. 调试运行

BSP 提供了便捷的 `dbg_` 目标，自动处理 GDB 连接、固件加载与复位：

1.  **启动 OpenOCD** (在一个单独的终端窗口)：
    ```bash
    openocd -f s300_openocd.cfg
    ```

2.  **启动调试** (在另一个终端窗口)：
    ```bash
    # 调试基础 UART 示例
    ninja -C build dbg_uart3_echo

    # 调试带 AI 人脸检测的显示 Demo
    ninja -C build dbg_display_face-detection
    ```

## 📖 文档导航

- **[架构设计](docs/S300_BSP_Architecture.md)**: 了解 S300 的存储布局、启动流程与异构架构。
- **[DMA 使用指南](docs/S300_DMA_LLI_Guide.md)**: 掌握如何使用 DMA 链表传输进行高效数据搬运。
- **[编码规范](docs/coding_style_cn.md)**: 参与贡献前的必读文档。

## 🛠️ 芯片规格摘要

| 模块              | 规格描述                                               |
| :---------------- | :----------------------------------------------------- |
| **CPU**           | ARM Cortex-M4F @ 200MHz                                |
| **Co-Processors** | AI 算力子系统 (运行 CV/Audio 算法)                     |
| **SRAM**          | 384KB (SRAM1) + 8KB (SRAM0)                            |
| **Memory**        | 8MB PSRAM (OSPI) + 16MB Flash (QSPI XIP)               |
| **Connectivity**  | 4x UART, 3x SPI, 2x I2C                                |
| **Multimedia**    | DVP Camera Interface, RGB Display Interface, I2S Audio |

## 📄 许可证

本项目采用 **MIT License** 开源授权。

