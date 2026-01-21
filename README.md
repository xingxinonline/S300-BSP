# S300 BSP

S300 芯片 (Cortex-M4 + AI 子系统) 板级支持包。

## 依赖

- arm-none-eabi-gcc
- CMake >= 3.16
- Ninja
- Python 3
- OpenOCD (调试用)

## 编译

```bash
# 配置 (可选 -DBOARD=app_board)
cmake -B build -G Ninja -DBOARD=generic_evb

# 编译
ninja -C build

# 生成镜像 (输出带板型后缀)
ninja -C build images
```

## 调试

```bash
# 终端1: 启动 OpenOCD
openocd -f s300_openocd.cfg

# 终端2: 启动 GDB
ninja -C build dbg_uart3_echo
```

## 目录

```
Boards/           板级配置 (generic_evb, app_board)
CMSIS/            芯片定义
Drivers/SoC/      片上外设驱动
Drivers/External/ 外围器件驱动
Projects/         示例工程
docs/             文档
tools/            脚本工具
```

## 新建项目

```cmake
cmake_minimum_required(VERSION 3.16)
project(My_Demo)

s300_add_executable(
    TARGET   s300_my_demo
    SOURCES  Src/main.c
    DRIVERS  ov5640       # 依赖自动解析: ov5640 -> i2c_soft -> gpio
)
```

**驱动列表** (依赖自动解析):
- 基础: `rcc` `gpio` `uart` `dma`
- 存储: `qspi` `psram`
- 通信: `i2c_soft` `i2s` `mailbox`
- 多媒体: `mm`
- 外设: `ov5640` `w25qxx` `wm8978`

详见 `cmake/s300_common.cmake`。

## 文档

- [AI Demo](docs/AI_Model_Demo_Guide.md)
- [架构](docs/S300_BSP_Architecture.md)
- [DMA](docs/S300_DMA_LLI_Guide.md)
- [编码规范](docs/coding_style_cn.md)

## 芯片

| 模块 | 规格 |
|-----|-----|
| CPU | Cortex-M4F @ 200MHz |
| SRAM | 384KB + 8KB |
| 外存 | 8MB PSRAM + 16MB Flash |
| 外设 | 4x UART, 3x SPI, 2x I2C, DVP, RGB, I2S |

## License

MIT
