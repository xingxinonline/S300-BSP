# Touch_Test Demo - 电容触摸屏测试

## 简介

本 Demo 用于验证 FT6X36 电容触摸屏驱动是否正常工作。

## 硬件要求

- **仅支持 app_board（应用板）** - 开发板没有触摸屏接口
- 带 FT6X36 触摸 IC 的电容触摸屏

## 硬件连接

| 信号 | GPIO | 说明 |
|------|------|------|
| SCL  | GPIO4 | I2C 时钟线 |
| SDA  | GPIO5 | I2C 数据线 |
| RST  | GPIO23 | 复位引脚（低电平有效） |
| VDD  | D3V3 | 电源 |
| GND  | GND | 地 |

I2C 地址: **0x38**

## 功能特性

- 触摸 IC 设备检测和信息读取
- 实时触摸坐标输出（最多 2 点）
- 手势识别（上/下/左/右滑动，放大/缩小）
- 详细的调试日志输出

## 编译

```bash
# 切换到应用板
cmake -B build -G Ninja -DBOARD=app_board .

# 编译
ninja -C build s300_touch_test
```

## 运行

1. 连接调试串口（921600 baud）
2. 下载并运行程序
3. 触摸屏幕，观察串口输出

## 预期输出

```
[TOUCH] [INF] ========================================
[TOUCH] [INF]   FT6X36 Touch Test Demo
[TOUCH] [INF]   Board: app_board
[TOUCH] [INF] ========================================
[TOUCH] [INF] Touch Pin Configuration:
[TOUCH] [INF]   I2C SCL:  GPIO4
[TOUCH] [INF]   I2C SDA:  GPIO5
[TOUCH] [INF]   RESET:    GPIO23
[TOUCH] [INF]   I2C ADDR: 0x38
[TOUCH] [INF]   I2C FREQ: 100000 Hz
[TOUCH] [INF] Initializing touch pins...
[TOUCH] [INF] Performing hardware reset...
[TOUCH] [INF] Initializing I2C...
[TOUCH] [INF] I2C init OK
[TOUCH] [INF] Probing I2C address 0x38...
[TOUCH] [INF] Device found at 0x38
[TOUCH] [INF] Initializing FT6X36 driver...
[TOUCH] [INF] FT6X36 init OK
[TOUCH] [INF] Screen size: 240x320
[TOUCH] [INF] === FT6X36 Device Info ===
[TOUCH] [INF]   Chip ID:      0x36
[TOUCH] [INF]   Firmware ID:  0x01
[TOUCH] [INF]   Vendor ID:    0x11
[TOUCH] [INF]   Lib Version:  0x0001
[TOUCH] [INF] ========================================
[TOUCH] [INF] Touch the screen to see coordinates...
[TOUCH] [INF] ========================================
[TOUCH] [INF] Touch detected: 1 point(s)
[TOUCH] [INF]   P1: (120, 160) [PRESS] w=50
[TOUCH] [INF]   P1: (121, 161) [CONT ] w=52
[TOUCH] [INF] Touch released
```

## 调试级别

可以通过定义 `DBG_LEVEL` 调整输出详细程度：

- `DBG_LEVEL_NONE (0)` - 无输出
- `DBG_LEVEL_ERROR (1)` - 仅错误
- `DBG_LEVEL_INFO (2)` - 信息 + 错误
- `DBG_LEVEL_VERBOSE (3)` - 详细输出（默认）

## 故障排除

### 设备未找到

```
[TOUCH] [ERR] Device not found at 0x38!
```

检查:
1. 触摸屏 FPC 连接是否牢固
2. I2C 引脚（SCL/SDA）接线是否正确
3. 电源是否正常供电
4. 确认使用的是 app_board

### 读取失败

```
[TOUCH] [ERR] Read touch failed
```

可能原因:
1. I2C 总线故障 - 程序会自动尝试恢复
2. 触摸 IC 需要复位 - 检查 RST 引脚连接
