# Tracking Demo - 智能云台目标追踪

本 Demo 演示 S300 平台的智能目标追踪功能，将人脸/人形检测与云台舵机控制相结合。

## 功能特性

- **摄像头采集**: OV5640 摄像头模组
- **AI 检测**: DSP 运行人脸/人形检测算法
- **云台控制**: Hiwonder 总线舵机 (二轴: Yaw + Pitch)
- **PID 跟踪**: 平滑的闭环控制，自动跟随目标
- **双缓冲显示**: 无撕裂的边界框渲染
- **track_id 关联**: 跨帧目标关联，防止目标切换抖动
- **速度箭头**: 卡尔曼滤波输出的运动方向可视化
- **卡尔曼预测补偿**: 使用速度预测目标未来位置
- **Coast 感知 (v3)**: 识别 DSP Kalman 预测框，降低增益避免跟随幽灵框
- **帧超时保护 (v3)**: 100ms 无新帧自动暂停控制
- **自动回中 (v3)**: 丢失目标 10 秒后云台自动回到中心位置

## v3 架构增强

本 Demo 实现了 `Gimbal_Tracking_System_Design_v3.md` 中的关键特性：

| 特性                | 描述                                                   |
| ------------------- | ------------------------------------------------------ |
| **miss_count 感知** | DSP 返回 `miss_count` 字段区分真实检测 vs coast 预测框 |
| **Coast 降增益**    | `miss_count > 0` 时增益降至 30%，避免跟随惯性预测框    |
| **CENTER 状态**     | LOST 超时 10 秒后进入 CENTER 状态，云台自动回中        |
| **帧超时保护**      | 100ms 无新帧暂停 PID 输出，帧恢复后自动继续            |
| **五状态机**        | IDLE → TRACKING → PREDICTING → LOST → CENTER           |

## v3.2 新增特性

| 特性               | 描述                                                      |
| ------------------ | --------------------------------------------------------- |
| **CM4→DSP 角速度** | 每帧发送云台控制角速度给 DSP，用于 Kalman 滤波器运动补偿  |
| **IMU 手抖补偿**   | 读取 QMI8658A 陀螺仪数据，实时补偿手持抖动带来的画面位移  |
| **100Hz IMU 采样** | 陀螺仪以 100Hz 采样，控制周期 50ms 内多次读取确保数据新鲜 |

### IMU 手抖补偿原理

```
云台实际角速度 = 控制输出 + 手持抖动
                    ↓
              IMU 测量值 (gyro_z=Yaw, gyro_x=Pitch)
                    ↓
            低通滤波 + 死区处理
                    ↓
         补偿量 = -角速度 × dt × 增益
                    ↓
       最终输出 = 跟踪控制 + 手抖补偿
```

相关参数 (可在 `target_tracker.c` 中调整):
- `HANDSHAKE_COMP_GAIN`: 补偿增益 (默认 0.8)
- `GYRO_DEADZONE_DPS`: 陀螺仪死区 (默认 1.5 °/s)
- `GYRO_LPF_ALPHA`: 低通滤波系数 (默认 0.3)

## 硬件需求

| 组件       | 规格                               |
| ---------- | ---------------------------------- |
| 主控板     | gimbal_master (NE005 智能云台主板) |
| 摄像头     | OV5640 模组                        |
| 显示屏     | ST7789 160x128 LCD                 |
| Yaw 舵机   | Hiwonder 总线舵机, ID=6            |
| Pitch 舵机 | Hiwonder 总线舵机, ID=4            |
| IMU (v3.2) | QMI8658A 六轴 IMU (I2C3, 0x6A)     |

### 舵机连接

```
总线舵机信号线 → UART3 (TX=GPIO27, RX=GPIO26)
总线使能引脚   → GPIO22 (MOTO_BUSEN)
```

## 编译

1. 切换到 gimbal_master 板:
   ```bash
   # 在 VS Code 中运行任务: "S300: Switch to Gimbal Master"
   # 或手动执行:
   cmake -B build -G Ninja -DBOARD=gimbal_master .
   ```

2. 编译:
   ```bash
   ninja -C build s300_tracking_demo
   ```

## 调试运行

### 基础调试 (无算法模型)

```bash
ninja -C build dbg_tracking_demo
```

### 加载人脸检测模型 (推荐)

```bash
ninja -C build dbg_tracking_face-detection
```

### 加载人形检测模型

```bash
ninja -C build dbg_tracking_human-detection
```

### 可用的调试目标

| 目标                           | 模型            | 说明                   |
| ------------------------------ | --------------- | ---------------------- |
| `dbg_tracking_demo`            | 无              | 仅 M4 程序，无 AI 模型 |
| `dbg_tracking_face-detection`  | Face_Detection  | 人脸检测跟踪           |
| `dbg_tracking_human-detection` | Human_Detection | 人形检测跟踪           |

## 工作原理

```
┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│   Camera    │  ──▶ │  DSP 检测   │  ──▶ │   Mailbox   │
│  (OV5640)   │      │  (AI Model) │      │   (Result)  │
└─────────────┘      └─────────────┘      └──────┬──────┘
                                                  │
                                                  ▼
┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│   Gimbal    │  ◀── │ PID Tracker │  ◀── │  M4 Core    │
│  (Servos)   │      │  (Control)  │      │  (Parse)    │
└─────────────┘      └─────────────┘      └─────────────┘
```

1. **采集**: OV5640 摄像头采集图像，通过 DVP 接口传输
2. **检测**: DSP 运行 AI 检测算法，识别人脸/人形
3. **通信**: 检测结果通过 Mailbox 发送给 M4 核心
4. **解析**: M4 从共享内存读取检测框坐标
5. **跟踪**: PID 控制器计算目标相对图像中心的偏移
6. **控制**: 总线舵机驱动器控制云台运动

## PID 参数调整

在 `main.c` 中可以调整跟踪参数:

```c
/* PID 参数 */
tracker_set_pid(0.08f,   /* Kp - 比例系数 */
                0.002f,  /* Ki - 积分系数 */
                0.02f);  /* Kd - 微分系数 */

/* 死区设置 (像素) */
tracker_set_deadzone(10);
```

### 参数说明

| 参数     | 作用       | 调整建议                       |
| -------- | ---------- | ------------------------------ |
| Kp       | 响应速度   | 增大使跟踪更快，太大会抖动     |
| Ki       | 消除静差   | 增大可减小稳态误差，太大会超调 |
| Kd       | 抑制振荡   | 增大可减少抖动，太大会变慢     |
| Deadzone | 忽略小偏移 | 增大使中心区域更稳定           |

## 模块结构

```
Tracking_Demo/
├── CMakeLists.txt           # 构建配置
├── README.md                # 本文档
├── Inc/
│   ├── gimbal_ctrl.h        # 云台控制 API
│   └── target_tracker.h     # 目标跟踪器 API
└── Src/
    ├── main.c               # 主程序
    ├── gimbal_ctrl.c        # 云台控制实现
    └── target_tracker.c     # PID 跟踪器实现
```

## 依赖

- `bus_servo` - 总线舵机驱动 (Drivers/External/BusServo)
- `camera_ov5640.c` - 摄像头驱动 (复用 Algorithm_Detection_Demo)
- `mailbox` - M4-DSP 通信
- `mm` - 多媒体子系统
- `psram` - 外部存储

## 故障排除

### 舵机无响应

1. 检查舵机供电 (6-8.4V)
2. 确认舵机 ID 设置正确 (Yaw=6, Pitch=4)
3. 检查 UART3 接线

### 检测无结果

1. 确认已加载正确的算法模型
2. 检查摄像头是否正常工作
3. 确保环境光线充足

### 跟踪抖动

1. 增大死区值 `tracker_set_deadzone(15)`
2. 减小 Kp 值
3. 增大 Kd 值

## 许可证

本项目遵循 S300 BSP 整体许可协议。
