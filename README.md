# S300 BSP - PiMCHIP S300 Board Support Package

## 📋 项目概述

S300 BSP是为PiMCHIP S300 (ARM Cortex-M4)芯片设计的完整板级支持包，基于CMSIS标准实现，提供了从底层驱动到高级应用的完整解决方案。

### 🎯 2025-08-30 重大更新
- **� 文档架构重构**: 核心文档从19个精简到11个，减少42%冗余
- **🔄 软件复位方案**: 完整的无硬件复位电路解决方案
- **�🚀 现代工具链**: 支持uv/scoop等现代包管理工具
- **🏗️ 统一架构**: BSP和启动架构文档完全整合

## 🚀 主要特性

- **🏗️ CMSIS兼容**: 完全符合ARM CMSIS标准规范
- **🔄 四级启动**: ROMBOOT → RBL → SBL → Application启动架构
- **📱 ESP32兼容**: 兼容ESP32的OTA和分区管理机制
- **🔧 硬件解决方案**: 软件复位替代硬件复位电路
- **🎯 多种项目**: 从简单Hello World到复杂OTA应用
- **🛠️ 完整工具链**: 支持GCC、OpenOCD、多种调试器
- **🐍 现代Python工具**: uv/scoop包管理，rich CLI界面

## 📁 项目结构 (重构后)

```text
S300_BSP/
├── docs/                           # 📚 架构设计文档 (11个核心文档)
│   ├── README.md                  # 📍 文档索引和导航
│   ├── S300_BSP_Architecture.md   # 🏗️ BSP架构设计总览
│   ├── S300_Boot_Architecture.md  # 🚀 启动架构设计
│   ├── S300_Hardware_Solutions.md # 🔧 硬件约束解决方案
│   ├── S300_Software_Reset_Guide.md # 🔄 软件复位完整指南
│   └── ...                       # 其他核心技术文档
├── CMSIS/                          # 🔧 CMSIS标准实现
│   ├── Core/Include/              # ARM核心头文件
│   └── Device/PiMCHIP/S300/       # S300设备特定代码
├── Drivers/                        # 🚗 硬件抽象层驱动
│   ├── SoC/                       # 片上外设驱动 (UART/GPIO/I2S/QSPI等)
│   └── External/                  # 外部设备驱动 (OV5640/W25Qxx/WM8978)
├── Projects/                       # 🎯 示例项目和应用
│   ├── HelloWorld/                # Hello World示例 (集成软件复位)
│   ├── RBL/                       # ROM Bootloader (芯片检测+下载)
│   ├── SBL/                       # Secondary Bootloader (OTA管理)
│   ├── App_YmodemOTA/             # OTA演示应用
│   └── Demo/                      # 各种演示项目 (I2S/QSPI/DMA等)
├── tools/                          # 🐍 Python 工具（复位/OTA/串口监视）
│   ├── s300_reset_tool.py         # 软件复位工具
│   ├── s300_ota_tool.py           # OTA升级工具
│   ├── serial_monitor.py          # 串口监视工具
│   └── pyproject.toml             # uv/pip 包管理配置
├── ld/                            # 🔗 链接脚本
└── Boards/                        # 🔌 开发板配置
```

## 🛠️ 快速开始

### 环境要求

- **工具链**: ARM GNU Toolchain (arm-none-eabi-gcc)
- **构建工具**: CMake + Ninja (推荐) 或 GNU Make
- **调试器**: OpenOCD + ST-Link/J-Link
- **操作系统**: Linux/Windows/macOS

### 构建示例

#### CMake + Ninja (推荐)

```bash
# 配置构建系统
mkdir build && cd build
cmake -G Ninja ..

# 构建所有项目
ninja

# 构建特定项目
ninja s300_rbl_minimal
ninja s300_sbl_minimal
```

#### GNU Make (传统方式)

```bash
# 构建Hello World项目
cd Projects/HelloWorld/GCC
make

# 构建RBL引导程序
cmake --build ../../build --target s300_image

# 构建所有项目
make -C Projects/HelloWorld/GCC
make -C Projects/SBL/GCC
```

### 烧写和调试

```bash
# 使用OpenOCD烧写
openocd -f s300_openocd.cfg -c "program build/app.elf verify reset exit"

# 使用CMake目标快速生成并烧录RBL镜像
cmake --build build --target s300_image
cmake --build build --target flash_image
./Projects/SBL/build.sh flash
```

## 🎯 项目导航

### 👨‍💻 开发者入门

1. **阅读架构文档** → `docs/S300_BSP_Architecture.md`
2. **查看Hello World** → `Projects/HelloWorld/`
3. **了解启动流程** → `docs/S300_Boot_Architecture_Final.md`
4. **尝试OTA应用** → `Projects/App_YmodemOTA/`

### 🔧 系统集成

1. **RBL引导程序** → `Projects/RBL/` - 第一级引导
2. **SBL引导程序** → `Projects/SBL/` - 第二级引导，OTA管理
3. **应用程序开发** → `Projects/App_YmodemOTA/` - 应用层示例

### 📖 文档资源

- **[完整文档索引](docs/README.md)** - 所有文档的导航
- **[BSP架构设计](docs/S300_BSP_Architecture.md)** - 技术架构详解
- **[启动架构设计](docs/S300_Boot_Architecture_Final.md)** - 四级启动流程
- **[硬件解决方案](docs/Hardware_Workaround_Solutions.md)** - 硬件约束解决

## 🔍 核心特性详解

### CMSIS标准支持

- **Device Headers**: 完整的S300设备头文件
- **System Files**: 系统初始化和时钟配置  
- **Startup Code**: 标准的启动代码和中断向量表
- **Core Support**: ARM Cortex-M4核心支持

### 驱动程序架构

- **HAL层**: 硬件抽象层，提供统一API
- **外设驱动**: UART、GPIO、DMA、I2S、QSPI等
- **外部设备**: OV5640摄像头、W25Q128 Flash、WM8978音频等

### 启动和OTA系统

- **四级启动**: ROMBOOT → RBL → SBL → Application
- **A/B分区**: 无缝OTA更新，自动故障回滚
- **ESP32兼容**: 分区表格式和OTA机制兼容ESP32
- **安全启动**: 固件完整性验证和安全跳转

## 📊 技术规格

| 项目          | 规格                         |
| ------------- | ---------------------------- |
| **目标芯片**  | PiMCHIP S300 (ARM Cortex-M4) |
| **内部SRAM**  | 8KB + 384KB                  |
| **外部Flash** | W25Q128 (16MB, QSPI)         |
| **时钟频率**  | 最高200MHz                   |
| **工具链**    | ARM GCC 10.3+                |
| **调试器**    | ST-Link, J-Link, DAPLink     |

## 🔬 演示项目

### 基础演示
- **HelloWorld** - 基本的UART输出和LED闪烁
- **UART Echo** - 串口回显测试
- **GPIO控制** - GPIO输入输出控制

### 外设演示  
- **DMA传输** - 内存到内存、UART DMA等
- **I2S音频** - I2S接口音频录放
- **QSPI Flash** - QSPI Flash读写和XIP模式

### 系统演示
- **QSPI XIP** - Flash就地执行演示
- **OV5640摄像头** - 摄像头图像采集
- **WM8978音频** - 音频编解码器控制

## 🤝 贡献指南

### 代码规范
- 遵循`docs/coding_style_cn.md`中的编码规范
- 使用统一的头文件保护和注释格式
- 确保代码与CMSIS标准兼容

### 提交要求
- 清晰的提交信息
- 完整的测试验证
- 更新相关文档

## 📄 许可证

本项目采用MIT许可证，详见LICENSE文件。

## 📞 技术支持

- **文档**: 查看`docs/`目录下的详细技术文档
- **示例**: 参考`Projects/`目录下的各种示例项目
- **问题**: 通过Issue或邮件联系技术支持

---

*S300 BSP - 专业级嵌入式开发平台*  
*最后更新: 2025-08-30*

