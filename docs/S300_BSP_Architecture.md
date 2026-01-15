# S300 BSP 架构设计

## 1. 概述

S300 BSP 遵循 CMSIS 标准，为 PiMCHIP S300 芯片提供硬件抽象层和开发支持。

**设计目标：**
- **CMSIS 兼容**：提供标准化的设备支持与外设访问接口。
- **异构协同**：支持 MCU (Cortex-M4) 与 AI 算力子系统的协同工作。
- **分层清晰**：Core/Device → Drivers → Boards → Projects。
- **易于扩展**：支持多板型配置与外设驱动扩展。

### 1.1 芯片与系统资源

S300 采用异构多核架构，集成高性能 MCU 与 专用 AI 算力系统：

- **主控 CPU**: ARM Cortex-M4F @ 200MHz
  - 浮点运算单元 (FPU)
  - 负责系统控制、外设管理与应用逻辑
- **AI 算力子系统**:
  - 运行专用算法固件（如人脸检测、识别）
  - 与 M4 通过 IPC (Mailbox) 通信
- **存储资源**:
  - **SRAM1** (384KB @ 0x20000000): M4 主要代码与数据运行区
  - **SRAM0** (8KB @ 0x10000000): 专用缓冲（如 DMA 描述符、小块数据）
  - **PSRAM** (8MB @ 0x90000000): OSPI 接口，大容量数据存储（显示缓冲、模型数据、共享内存）
  - **ROM**: 10KB ROMBOOT (固化引导程序)
  - **Flash**: 16MB W25Q128 (QSPI XIP)
- **外设资源**:
  - 通信: UART×4, SPI×3, I2C×2
  - 多媒体: VIP (Video Input), DPU (Display), I2S
  - 系统: DMA (8通道), TIMER×8, GPIO, Mailbox

## 2. 目录结构

```
S300_BSP/
├── Algorithm_Models/         # AI/算法固件 (AI Firmware)
├── Boards/                   # 板级配置 (Generic EVB, App Board)
├── CMSIS/                    # CMSIS 标准支持
│   ├── Core/Include/         # ARM 核心头文件
│   └── Device/PiMCHIP/S300/  # S300 设备定义与启动代码
├── Drivers/                  # 硬件驱动库
│   ├── SoC/                  # 片上外设 (DMA, UART, GPIO, Video...)
│   └── External/             # 板载器件 (OV5640, W25Qxx, LCD...)
├── Projects/                 # 示例工程与应用
│   ├── App_HelloWorld/       # 基础模板
│   └── Demo/                 # 功能演示 (Display, Audio, AI...)
├── docs/                     # 开发文档
├── ld/                       # 链接脚本
└── tools/                    # 辅助工具 (Python脚本)
```

## 3. 软件分层架构

| 层级                    | 模块         | 说明                                                                                               |
| :---------------------- | :----------- | :------------------------------------------------------------------------------------------------- |
| **Layer 5 Application** | Projects/    | 用户应用程序、演示 Demo、业务逻辑                                                                  |
| **Layer 4 Board**       | Boards/      | 板级初始化、引脚复用配置 (PinMux)、板载设备实例                                                    |
| **Layer 3 Driver**      | Drivers/     | **SoC Drivers**: 片上外设驱动 (UART, DMA, Video)<br>**Ext Drivers**: 外部器件驱动 (Camera, Screen) |
| **Layer 2 Device**      | CMSIS/Device | 芯片寄存器定义、`SystemInit`、中断向量表                                                           |
| **Layer 1 Core**        | CMSIS/Core   | ARM Cortex-M4 标准内核接口 (NVIC, SysTick)                                                         |

## 4. 异构协同与通信

S300 的典型应用模式是 **M4 控制 + AI 算力**。

### 4.1 通信机制 (Mailbox)
- **物理层**：硬件 Mailbox 外设，提供核间中断与消息寄存器。
- **协议层**：基于共享内存的消息传递。
  - M4 发送命令/配置给 AI 子系统。
  - AI 子系统发送事件/结果（如人脸坐标）给 M4。

### 4.2 共享内存 (Shared Memory)
- **PSRAM (0x90000000)**：通常作为主要的大容量共享区。
  -用途：图像帧缓冲区、AI 模型权重、推理结果结构体。
- **一致性**：需注意 Cache 一致性（通常使用非 Cache 区域或手动无效化 Cache）。

## 5. 开发与构建

### 5.1 构建系统
基于 **CMake + Ninja**，支持 Windows/Linux 跨平台开发。

```bash
# 配置 (在根目录)
cmake -B build -G Ninja

# 编译
ninja -C build
```

### 5.2 调试架构
当前阶段主要采用 **SRAM 加载调试** 模式：

1.  **GDB 连接**：通过 OpenOCD 连接目标板。
2.  **加载**：将 ELF/BIN 下载到 SRAM1 (0x20000000)。
3.  **多核加载** (可选)：通过 GDB 脚本将 AI 固件加载到特定内存区域 (SRAM0/PSRAM/DTCM)。
4.  **运行**：设置 PC 指针与 SP 堆栈，开始执行。

### 5.3 辅助工具 (`tools/`)
- `s300_image.py`: 固件打包工具，用于生成包含 M4 与 AI 固件的合并镜像。
- `serial_monitor.py`: 串口监视器。

## 6. 驱动编程规范

驱动开发采用 **面向对象 (Struct-based)** 风格：

1.  **Handle (句柄)**：持有外设实例状态。
2.  **Config (配置)**：纯数据结构，描述初始化参数。
3.  **API (接口)**：以 Handle 指针为第一个参数。

```c
// 示例：DMA 初始化
s300_dma_ch_config_t config = {
    .direction = S300_DMA_MEM_TO_PERIPH,
    .src_addr_inc = S300_DMA_ADDR_INC,
    .dst_addr_inc = S300_DMA_ADDR_NO_CHANGE,
    .data_width = S300_DMA_DATA_WIDTH_8BIT,
};
s300_dma_init_channel(DMA1_Stream3, &config);
```

### 错误码定义 (`s300_def.h`)
- `S300_OK (0)`: 成功
- `S300_ERROR (-1)`: 通用错误
- `S300_BUSY`: 设备忙
- `S300_TIMEOUT`: 操作超时

## 7. 内存布局参考

| 区域      | 地址范围                | 大小  | 用途                                       |
| :-------- | :---------------------- | :---- | :----------------------------------------- |
| **SRAM0** | 0x10000000 - 0x10001FFF | 8KB   | DMA 描述符, 临时缓冲                       |
| **SRAM1** | 0x20000000 - 0x2005FFFF | 384KB | **M4 Main** (Text, Data, BSS, Stack, Heap) |
| **PSRAM** | 0x90000000 - 0x907FFFFF | 8MB   | 显示 Buffer, AI 模型, 共享数据             |
| **Flash** | 0x0xxxxxxx (XIP)        | 16MB  | 代码存储 (XIP 模式)                        |

## 相关文档

- [S300 DMA LLI 使用指南](S300_DMA_LLI_Guide.md)
- [编码规范 (中文)](coding_style_cn.md)
