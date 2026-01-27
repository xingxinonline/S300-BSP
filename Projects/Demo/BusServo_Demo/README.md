# 总线舵机控制演示 (BusServo Demo)

## 概述

本演示程序展示如何使用 S300 BSP 控制 Hiwonder (幻尔科技) 总线舵机。

## 硬件要求

- **开发板**: gimbal_master (智能云台主控板)
- **舵机**: Hiwonder 总线舵机 (如 LX-16A, LX-224 等)
- **供电**: 5V~8.4V (根据舵机型号)
- **连接**: 舵机连接到 MOTO1 接口

## 硬件连接

```
S300 主板                     总线舵机
+----------+                 +--------+
| GPIO26 --|-- SN74LVC1G3157 |        |
| (U3TXD)  |                 |        |
|          |  MOTO_SIG ------|-- SIG  |
| GPIO27 --|                 |        |
| (U3RXD)  |                 |        |
|          |                 |        |
| GPIO22 --|-- SELECT        |        |
|(BUSEN)   |                 |        |
+----------+                 +--------+
                             | VCC ---|-- 5V~8.4V
                             | GND ---|-- GND
```

## 功能演示

1. **舵机检测**: 通过广播 ID 检测总线上的舵机
2. **运动控制**: 控制舵机转到指定角度
3. **预设运动**: 预设目标位置，同步启动
4. **电机模式**: 切换到连续旋转模式
5. **LED 控制**: 控制舵机 LED 指示灯
6. **状态查询**: 读取舵机位置、电压、温度等信息

## 协议说明

- **通信方式**: 半双工 UART
- **波特率**: 115200 bps
- **ID 范围**: 0~253 (254 为广播 ID)
- **角度范围**: 0°~240° (协议值 0~1000)
- **时间范围**: 0~30000 ms

## 编译运行

```bash
# 选择 gimbal_master 板
cmake -B build -G Ninja -DBOARD=gimbal_master .

# 编译
ninja -C build s300_bus_servo_demo

# 下载运行
python tools/s300_download.py build/Projects/Demo/BusServo_Demo/s300_bus_servo_demo.bin
```

## API 使用示例

```c
#include "bus_servo.h"

bus_servo_t servo;

// 初始化
board_servo_init(&servo);

// 控制舵机转到 120°，时间 500ms
bus_servo_move(&servo, 1, 120.0f, 500);

// 读取当前角度
float angle;
bus_servo_read_angle(&servo, 1, &angle);

// 读取完整状态
bus_servo_status_t status;
bus_servo_read_status(&servo, 1, &status);

// 电机模式 (连续旋转)
bus_servo_set_mode(&servo, 1, BUS_SERVO_MODE_MOTOR, BUS_SERVO_MOTOR_DUTY, 500);
```

## 注意事项

1. **舵机供电**: 确保舵机供电充足，否则可能出现通信失败
2. **ID 冲突**: 同一总线上的舵机 ID 必须唯一
3. **通信间隔**: 发送指令后需等待执行完成再发送下一条
4. **方向切换**: 半双工通信需要正确切换 TX/RX 方向
