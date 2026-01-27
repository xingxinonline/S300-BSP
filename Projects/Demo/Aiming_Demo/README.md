# Aiming Demo - 视觉指向控制

本 Demo 演示摄像头固定、云台指向目标的模式。

## 与 Tracking Demo 的区别

| 特性       | Tracking Demo (追踪) | Aiming Demo (指向) |
| ---------- | -------------------- | ------------------ |
| 摄像头位置 | 安装在云台上         | 固定安装           |
| 控制目标   | 让目标保持在画面中心 | 让云台指向目标方向 |
| 坐标映射   | 目标右 → 云台左转    | 目标右 → 云台右转  |
| 应用场景   | 人脸跟拍、监控追踪   | 视觉引导、目标瞄准 |

## 工作原理

```
┌─────────────┐                    ┌─────────────┐
│  固定摄像头  │ ───观察场景───→   │  检测目标   │
│  (OV5640)   │                    │  (DSP AI)   │
└─────────────┘                    └──────┬──────┘
                                          │
                                          ▼
┌─────────────┐                    ┌─────────────┐
│  云台舵机   │ ←───指向目标───    │  指向控制器 │
│  (独立安装) │                    │  (M4 Core)  │
└─────────────┘                    └─────────────┘
```

## 硬件配置

- **摄像头**: OV5640 模组 (固定安装)
- **云台**: Hiwonder 总线舵机 (Yaw=ID6, Pitch=ID4)
- **主控**: gimbal_master 板

## 编译运行

```bash
# 配置
cmake -B build -G Ninja -DBOARD=gimbal_master .

# 编译
ninja -C build s300_aiming_demo

# 运行 (人脸检测模式)
ninja -C build dbg_aiming_face-detection

# 运行 (人形检测模式)
ninja -C build dbg_aiming_human-detection
```

## 调试目标

| 目标                         | 说明         |
| ---------------------------- | ------------ |
| `dbg_aiming_demo`            | 仅 M4，无 AI |
| `dbg_aiming_face-detection`  | 人脸检测指向 |
| `dbg_aiming_human-detection` | 人形检测指向 |

## 参数配置

在 main.c 中可调整：

```c
/* PID 参数 (指向模式需要更快响应) */
aiming_set_pid(0.15f, 0.005f, 0.03f);
aiming_set_deadzone(8);

/* 视场角 (根据实际摄像头调整) */
aiming_set_fov(60.0f, 45.0f);
```

### 视场角 (FOV) 说明

视场角用于将像素偏移转换为角度偏移，确保云台准确指向目标：

- `fov_x`: 水平视场角 (默认 60°)
- `fov_y`: 垂直视场角 (默认 45°)

如果云台指向偏移过大或过小，请调整视场角参数。

## 模块结构

```
Aiming_Demo/
├── CMakeLists.txt
├── README.md
├── Inc/
│   └── aiming_ctrl.h      # 指向控制器 API
└── Src/
    ├── main.c             # 主程序
    └── aiming_ctrl.c      # 指向控制器实现
```

复用的模块：
- `gimbal_ctrl` - 来自 Tracking_Demo
- `camera_ov5640` - 来自 Algorithm_Detection_Demo
