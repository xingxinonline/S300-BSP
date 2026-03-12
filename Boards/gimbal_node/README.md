# Gimbal Node - 智能云台 AI 节点 BSP

## 概述

Gimbal Node 是芯天下智能云台项目的 AI 处理节点，采用 mPCIE 接口设计，可插入主控板的 mPCIE 插槽中。

## 硬件架构

```
┌──────────────────────────────────────────────────────────────────┐
│                   Gimbal Node (mPCIE AI Node)                     │
│                                                                   │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────────┐  │
│  │   OV5640    │────│  S300 SoC   │────│  mPCIE Connector    │  │
│  │   Camera    │DVP │ Cortex-M4   │    │  (52-pin U3)        │  │
│  │             │    │  @192MHz    │    │                     │  │
│  └─────────────┘    └──────┬──────┘    │  - DVP_DATA[0:7]    │  │
│                            │           │  - DVP_XCLK/PCLK    │  │
│  ┌─────────────┐    ┌──────┴──────┐    │  - DVP_H/VSYNC      │  │
│  │  RTL8211F   │    │   W956D8    │    │  - I2C3_SCK/SDA     │  │
│  │  Ethernet   │    │   8MB       │    │  - U2TXD/U2RXD      │  │
│  │  PHY        │    │   PSRAM     │    │  - PHY MDI[0:3]±    │  │
│  └─────────────┘    └─────────────┘    │  - Power Rails      │  │
│                                        │  - PORRESETn        │  │
│  ┌─────────────┐    ┌─────────────┐    └─────────────────────┘  │
│  │  W25Q128    │    │   LED1/2    │                              │
│  │  16MB Flash │    │   (Status)  │    ┌─────────────────────┐  │
│  └─────────────┘    └─────────────┘    │  JTAG (J1 7-pin)    │  │
│                                        └─────────────────────┘  │
│  ┌─────────────┐    ┌─────────────┐                              │
│  │   SW1       │    │  X1/X2/OSC1 │                              │
│  │  (Reset)    │    │  Crystals   │                              │
│  └─────────────┘    └─────────────┘                              │
└──────────────────────────────────────────────────────────────────┘
```

## 功能分配

| 子板  | AI 功能  | 描述                                   |
| ----- | -------- | -------------------------------------- |
| Card1 | 人脸检测 | Face Detection - 检测画面中的人脸位置  |
| Card2 | 手势检测 | Gesture Detection - 识别手势控制命令   |
| Card3 | 人形检测 | Human Detection - 检测人体轮廓进行跟踪 |

## 主要芯片

| 芯片 | 型号         | 功能                          |
| ---- | ------------ | ----------------------------- |
| U1   | S300         | 主控 SoC (Cortex-M4 @ 192MHz) |
| U2   | RTL8211F     | RGMII 以太网 PHY (可选)       |
| U8   | W956D8MBYA   | 8MB PSRAM                     |
| U11  | W25Q128JVSIQ | 16MB SPI Flash                |
| OSC1 | 24MHz        | 主时钟源                      |
| X1   | 25MHz        | PHY 时钟源                    |
| X2   | 32.768kHz    | RTC 时钟源                    |

## mPCIE 接口定义

### 电源引脚
| 引脚       | 信号  | 描述      |
| ---------- | ----- | --------- |
| I/O1       | VCC_A | D3V3 电源 |
| I/O3       | VCC_B | D1V8 电源 |
| I/O5       | VCC_C | D0V9 电源 |
| I/O2,4,6,8 | GND   | 接地      |

### 摄像头数据 (DVP)
| 引脚     | 信号          | S300 GPIO             |
| -------- | ------------- | --------------------- |
| I/O29-36 | DVP_DATA[0:7] | GPIO[8,6,9,5,4,7,3,2] |
| I/O37    | DVP_XCLK      | GPIO0 (N6)            |
| I/O38    | DVP_PCLK      | GPIO1 (P10)           |
| I/O39    | DVP_HSYNC     | GPIO12 (M8)           |
| I/O40    | DVP_VSYNC     | GPIO13 (P5)           |

### 通信接口
| 引脚  | 信号     | 描述                |
| ----- | -------- | ------------------- |
| I/O41 | I2C3_SCK | 板载/本地外设 I2C (GPIO10) |
| I/O42 | I2C3_SDA | 板载/本地外设 I2C (GPIO11) |
| I/O43 | U2TXD    | UART2 发送 (GPIO24) |
| I/O44 | U2RXD    | UART2 接收 (GPIO23) |

### 以太网 (RGMII)
| 引脚     | 信号      | 描述           |
| -------- | --------- | -------------- |
| I/O9     | PHY_LED0  | PHY 状态 LED0  |
| I/O10    | PHY_LED1  | PHY 状态 LED1  |
| I/O15-22 | MDI[0:3]± | 以太网差分信号 |

### 控制信号
| 引脚  | 信号      | 描述     |
| ----- | --------- | -------- |
| I/O45 | PORRESETn | 系统复位 |

## JTAG 调试接口 (J1)

7-pin 排针接口 (PZ254V-11-07P)：

| Pin | 信号  | 描述                |
| --- | ----- | ------------------- |
| 1   | U3RXD | UART3 RX (调试串口) |
| 2   | U3TXD | UART3 TX (调试串口) |
| 3   | GND   | 接地                |
| 4   | TCK   | JTAG 时钟           |
| 5   | TDI   | JTAG 数据输入       |
| 6   | TDO   | JTAG 数据输出       |
| 7   | TMS   | JTAG 模式选择       |

## 状态 LED

| LED  | 颜色 | 控制引脚         | 用途          |
| ---- | ---- | ---------------- | ------------- |
| LED1 | 绿色 | GPIO16 (通过 Q1) | 系统状态/心跳 |
| LED2 | 绿色 | GPIO17 (通过 Q2) | AI 处理状态   |

## 编译使用

### 选择板卡
```bash
cmake -B build -G Ninja -DBOARD=gimbal_node .
```

### 编译
```bash
ninja -C build
```

## 与主板通信协议

当前主板/子板 bring-up 与控制链路按以下划分：

1. 主板通过 I2C1 与子板 I2C1 通信。
2. 主板通过 I2C3 与主板本地外设通信。
3. 子板当前不使用 I2C3 作为板间通信总线。
4. UART2 仍可作为子板与主板的高速串口通道。

因此，本 README 中 mPCIE 暴露的 I2C3 信号应理解为子板本地/预留外设接口，而不是当前 bring-up Demo 的板间控制通道。

### UART2 协议帧格式
```
┌────────┬────────┬────────┬─────────────┬────────┐
│ Header │ Length │  Cmd   │   Payload   │  CRC   │
│  0xAA  │ 1 Byte │ 1 Byte │  N Bytes    │ 2 Byte │
└────────┴────────┴────────┴─────────────┴────────┘
```

### 命令定义
| Cmd  | 描述          |
| ---- | ------------- |
| 0x01 | 心跳/状态查询 |
| 0x10 | 检测结果上报  |
| 0x20 | 配置参数下发  |
| 0x30 | 固件升级      |

## AI 模型配置

在 `video_config.h` 中配置不同子板使用的 AI 模型：

```c
/* Card1: 人脸检测 */
#define AI_FACE_DET_INPUT_WIDTH     160
#define AI_FACE_DET_INPUT_HEIGHT    160

/* Card2: 手势检测 */
#define AI_GESTURE_INPUT_WIDTH      128
#define AI_GESTURE_INPUT_HEIGHT     128

/* Card3: 人形检测 */
#define AI_HUMAN_DET_INPUT_WIDTH    160
#define AI_HUMAN_DET_INPUT_HEIGHT   160
```

## 注意事项

1. **电源时序**: 确保 D3V3 → D1V8 → D0V9 依次上电
2. **复位**: PORRESETn 由主板控制，需在所有电源稳定后释放
3. **PHY 配置**: RGMII 默认禁用，如需使用需启用 `BOARD_RGMII_ENABLE`
4. **时钟**: 子板使用独立的 24MHz 晶振，与主板时钟无关联

## 版本历史

| 版本 | 日期       | 描述     |
| ---- | ---------- | -------- |
| 1.0  | 2025-12-23 | 初始版本 |
