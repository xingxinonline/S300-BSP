# Gyroscope Demo - QMI8658A 六轴IMU自稳云台演示

## 概述

本Demo演示了如何使用 QMI8658A 六轴IMU传感器配合总线舵机实现自稳云台：

- **传感器**: QMI8658A (3轴加速度计 + 3轴陀螺仪)
- **通信接口**: 软件 I2C (I2C3)
- **姿态解算**: 互补滤波算法
- **自稳控制**: 舵机2 (Roll) + 舵机3 (Pitch)

## 功能特性

1. **传感器数据读取**
   - 加速度计数据 (m/s²)
   - 陀螺仪数据 (rad/s)
   - 温度数据 (°C)

2. **姿态解算**
   - 欧拉角输出 (Roll, Pitch, Yaw)
   - 四元数计算
   - 自动校准

3. **自稳云台控制**
   - 舵机2补偿 Roll 轴
   - 舵机3补偿 Pitch 轴
   - 保持陀螺仪/摄像头与地面垂直
   - 可配置增益和死区

4. **数据输出**
   - 通过 UART3 (115200bps) 输出
   - 实时显示欧拉角和舵机角度

## 硬件连接

| 信号       | 引脚    | 说明         |
| ---------- | ------- | ------------ |
| I2C3_SCL   | GPIOA4  | QMI8658A SCL |
| I2C3_SDA   | GPIOA5  | QMI8658A SDA |
| UART3_TX   | GPIOA26 | 调试串口 TX  |
| UART3_RX   | GPIOA27 | 调试串口 RX  |
| UART2_TX   | GPIOA23 | 总线舵机 TX  |
| UART2_RX   | GPIOA24 | 总线舵机 RX  |
| MOTO_BUSEN | GPIO22  | 舵机方向控制 |

### 舵机配置

| 参数               | 默认值 | 说明            |
| ------------------ | ------ | --------------- |
| SERVO_ROLL_ID      | 2      | Roll补偿舵机ID  |
| SERVO_PITCH_ID     | 3      | Pitch补偿舵机ID |
| SERVO_CENTER_ANGLE | 120°   | 舵机中位角度    |
| SERVO_MIN_ANGLE    | 30°    | 舵机最小角度    |
| SERVO_MAX_ANGLE    | 210°   | 舵机最大角度    |
| SERVO_MOVE_TIME    | 20ms   | 舵机移动时间    |
| DEADZONE_DEG       | 0.5°   | 死区阈值        |

## 编译

```bash
# 配置项目 (选择对应的板子)
cmake -B build -G Ninja -DBOARD=gimbal_master .

# 编译
ninja -C build s300_gyroscope_demo

# 或使用调试目标
ninja -C build dbg_gyro
```

## 运行

1. 将开发板连接到电脑
2. 连接舵机2 (Roll) 和舵机3 (Pitch)
3. 烧录固件到开发板
4. 打开串口终端 (115200, 8N1)
5. 复位开发板，观察输出

## 输出示例

```
========================================
  S300 IMU Stabilized Gimbal Demo
========================================

[Init] I2C3 at 400000 Hz...
[Init] I2C initialized.
[Init] QMI8658A (addr=0x6A)...
[Init] QMI8658A initialized.
[Info] Sensor temperature: 25.50 C
[Init] IMU attitude solver...
[Init] IMU initialized.
[Init] Bus Servo (UART2, DIR=GPIO22)...
[Init] Bus Servo initialized.
[Init] Moving servos to center position...
[Init] Servos centered.

[Info] Configuration:
       - Sample rate: 100 Hz
       - Servo Roll ID: 2
       - Servo Pitch ID: 3
       - Servo center: 120.0 deg
       - Servo range: 30.0 ~ 210.0 deg
       - Stabilization: ENABLED

[Info] Calibrating, please keep the device still...

Roll:   0.1 Pitch:   0.1 Yaw:   0.0 | Servo: R=120.0 P=120.0
Roll:  10.5 Pitch:   5.2 Yaw:   0.1 | Servo: R=109.5 P=114.8
...
```

## 代码结构

```
Gyroscope_Demo/
├── CMakeLists.txt      # CMake 配置
├── README.md           # 本文档
└── Src/
    └── main.c          # 主程序
```

## 依赖

- `uart` - UART 驱动
- `qmi8658a` - QMI8658A 传感器驱动
- `imu` - IMU 姿态解算库
- `bus_servo` - 总线舵机驱动
- `i2c_soft` - 软件 I2C 驱动 (自动依赖)
- `gpio` - GPIO 驱动 (自动依赖)
- `rcc` - 时钟控制驱动 (自动依赖)

## API 参考

### QMI8658A 驱动

```c
// 初始化传感器
int qmi8658a_init(qmi8658a_t *dev, i2c_soft_t *i2c, uint8_t addr);

// 读取传感器数据
int qmi8658a_read_data(qmi8658a_t *dev, qmi8658a_data_t *data);

// 读取温度
float qmi8658a_read_temperature(qmi8658a_t *dev);
```

### IMU 姿态解算

```c
// 初始化姿态解算
void imu_init(imu_t *imu);

// 更新姿态 (返回欧拉角)
imu_euler_t* imu_update(imu_t *imu, float acc[3], float gyr[3], float dt);

// 获取欧拉角
imu_euler_t* imu_get_euler(imu_t *imu);
```

## 注意事项

1. **校准**: 启动后需要保持设备静止约1-2秒进行校准
2. **采样率**: 建议采样率 ≥ 100Hz 以获得良好的姿态跟踪效果
3. **温漂**: 陀螺仪存在温漂，长时间使用 Yaw 角会累积误差
4. **磁力计**: 当前版本不包含磁力计，Yaw 角无绝对参考

## 扩展

- 添加磁力计融合以获得绝对 Yaw 角
- 实现卡尔曼滤波提高精度
- 添加运动检测功能
