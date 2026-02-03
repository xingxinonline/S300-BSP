# Gimbal Master - 智能云台主控板 BSP

## 概述

Gimbal Master 是芯天下智能云台项目的主控板，类似大疆 OM7P 智能跟随模块的设计。该项目通过四片 S300 芯片 I2C/UART 互联，实现以下 AI 功能：

- **人脸检测** (Face Detection) - 子板 Card1
- **手势检测** (Hand Gesture) - 子板 Card2
- **人形检测** (Human Detection) - 子板 Card3
- **人脸识别 + 云台跟踪控制** - 主板

## 硬件架构

```
                    ┌─────────────────────┐
                    │  Gimbal Master (U38) │
                    │   S300 主控芯片      │
                    │                     │
                    │  ├── 人脸识别       │
                    │  ├── 云台控制       │
                    │  ├── 舵机驱动       │
                    │  └── 系统协调       │
                    └──────────┬──────────┘
                               │ I2C3 / UART2
           ┌───────────────────┼───────────────────┐
           │                   │                   │
    ┌──────┴──────┐     ┌──────┴──────┐     ┌──────┴──────┐
    │   Card1     │     │   Card2     │     │   Card3     │
    │  人脸检测   │     │  手势检测   │     │  人形检测   │
    │   S300      │     │   S300      │     │   S300      │
    └─────────────┘     └─────────────┘     └─────────────┘
```

## 主要外设

### 通信接口

| 接口  | 引脚              | 功能说明               |
| ----- | ----------------- | ---------------------- |
| UART3 | PA26(RX)/PA27(TX) | 调试串口 (921600 baud) |
| UART1 | PA16(RX)/PA17(TX) | 蓝牙通信               |
| UART2 | PA23(RX)/PA24(TX) | 子板通信               |
| I2C3  | PA4(SCL)/PA5(SDA) | 多外设共享总线         |

### I2C3 总线设备

| 设备     | I2C 地址 | 功能说明        |
| -------- | -------- | --------------- |
| OV5640   | 0x3C     | 摄像头          |
| ES8311   | 0x18     | 音频 Codec      |
| ES7210   | 0x41     | 4 通道音频 ADC  |
| QMI8658A | 0x6A     | 6 轴 IMU 传感器 |

### 摄像头接口 (DVP)

| 引脚      | GPIO | 功能说明 |
| --------- | ---- | -------- |
| XCLK      | -    | 时钟输出 |
| PCLK      | -    | 像素时钟 |
| HSYNC     | -    | 行同步   |
| VSYNC     | -    | 场同步   |
| DATA[7:0] | -    | 数据总线 |
| PWDN      | PA6  | 省电控制 |
| RSTN      | PA15 | 复位     |

### 显示接口 (SPI LCD)

| 信号 | 主板 GPIO | 经 TXS0108E 转换 |
| ---- | --------- | ---------------- |
| CS_N | -         | 3.3V 电平        |
| DC   | -         | 3.3V 电平        |
| SCK  | -         | 3.3V 电平        |
| SDO  | -         | 3.3V 电平        |
| RSTN | -         | 3.3V 电平        |
| BL   | PA12      | 背光控制         |

### 音频系统

- **ES8311**: 音频 Codec，用于语音播放
- **ES7210**: 4 通道 ADC，用于麦克风阵列
- **NS4150**: 功放芯片，控制引脚 PA14

### 云台控制

| 外设       | 引脚/接口        | 功能说明          |
| ---------- | ---------------- | ----------------- |
| 舵机       | UART3 (总线舵机) | 云台俯仰/偏航控制 |
| MOTO_BUSEN | PA18             | 电机总线使能      |
| QMI8658A   | I2C3             | 姿态反馈          |
| PWM 蜂鸣器 | PA20             | 声音提示          |

### RGB LED (WS2812)

| 链路     | GPIO | LED 数量 | 用途     |
| -------- | ---- | -------- | -------- |
| RGB_DIN1 | PA30 | 8        | 状态指示 |
| RGB_DIN2 | PA21 | 5        | 功能指示 |

## 使用方法

### 1. 选择板型

```bash
cmake -B build -G Ninja -DBOARD=gimbal_master .
```

或使用 VS Code 任务：
- 运行任务 "S300: Switch to Gimbal Master (云台主控板)"

### 2. 编译

```bash
ninja -C build
```

### 3. 代码示例

```c
#include "board.h"

int main(void)
{
    /* 板级初始化 (时钟、GPIO、调试串口) */
    board_init();
    
    printf("NE005 Smart Gimbal Initialized!\r\n");
    
    /* 初始化 I2C3 总线 */
#if BOARD_I2C3_ENABLE
    i2c_soft_handle_t i2c3;
    board_i2c3_init(&i2c3);
#endif

    /* 初始化摄像头 */
#if BOARD_CAMERA_ENABLE
    board_camera_ctrl_pins_init();
    board_camera_power_on_sequence();
#endif

    /* 初始化蜂鸣器 */
#if BOARD_BUZZER_ENABLE
    board_buzzer_pin_init();
    board_buzzer_beep(2700);  /* 2.7kHz 提示音 */
#endif

    while (1) {
        /* 主循环 */
    }
}
```

## 原理图参考

- 主板原理图: [NE005_MainBoard.pdf](../docs/NE005_MainBoard.pdf)
- IO 复用表: [NE005 IOMUX for XTX.xls](../docs/NE005%20IOMUX%20for%20XTX.xls)

## 相关文档

- [S300 BSP 架构说明](../../docs/S300_BSP_Architecture.md)
- [GPIO Pinmux 指南](../../docs/S300_GPIO_Pinmux_Guide.md)
- [AI 模型 Demo 指南](../../docs/AI_Model_Demo_Guide.md)

## 注意事项

1. **I2C3 总线共享**: 摄像头、音频芯片、IMU 共用 I2C3 总线，需注意访问互斥
2. **电平转换**: LCD 接口经过 TXS0108E 进行 1.8V ↔ 3.3V 电平转换
3. **CPLD (XC2C128)**: 子板摄像头信号通过 CPLD 进行切换，主板负责控制
4. **舵机供电**: 舵机使用 5V 供电，注意电源容量

## 版本历史

| 版本  | 日期       | 说明     |
| ----- | ---------- | -------- |
| 1.0.0 | 2026-01-23 | 初始版本 |
