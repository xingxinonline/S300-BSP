# S300 跟踪系统架构设计

## 1. 系统概述

S300 跟踪系统由 DSP (HiFi4) 和 CM4 协同工作，通过 Mailbox 通信，实现目标检测与云台跟踪。

```
┌─────────────────────────────────────────────────────────────────┐
│                        摄像头 (OV5640)                           │
└──────────────────────────┬──────────────────────────────────────┘
                           │ 图像帧
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                      DSP (HiFi4) - 500MHz                       │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────┐  │
│  │ 神经网络    │──│ 目标选择     │──│ 卡尔曼滤波 + 跟踪ID    │  │
│  │ 推理        │  │ (最近优先)   │  │ (图像坐标系)           │  │
│  └─────────────┘  └──────────────┘  └────────────────────────┘  │
└──────────────────────────┬──────────────────────────────────────┘
                           │ Mailbox (DetectionResult)
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                      CM4 - 400MHz                               │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────┐  │
│  │ 目标验证    │──│ 自适应 PID   │──│ 云台控制               │  │
│  │ + 滤波      │  │ 控制器       │  │ (舵机驱动)             │  │
│  └─────────────┘  └──────────────┘  └────────────────────────┘  │
│        │                                       │                 │
│        ▼                                       ▼                 │
│  ┌─────────────┐                    ┌────────────────────────┐  │
│  │ 显示渲染    │                    │ 云台位置反馈           │  │
│  │ (双缓冲)    │                    │ (用于运动补偿)         │  │
│  └─────────────┘                    └────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 当前职责划分

### 2.1 DSP 职责

| 功能          | 当前实现 | 说明                              |
| ------------- | -------- | --------------------------------- |
| 神经网络推理  | ✅        | 人脸/人体/手势检测                |
| 目标检测 bbox | ✅        | 输出边界框坐标                    |
| 多目标跟踪 ID | ✅        | track_id 分配与维护               |
| 卡尔曼滤波    | ✅        | 输出 vx, vy, speed, kf_confidence |
| 目标选择      | ✅        | selected_idx 最近优先             |

### 2.2 CM4 职责

| 功能          | 当前实现 | 说明                 |
| ------------- | -------- | -------------------- |
| Mailbox 解析  | ✅        | 解析 DetectionResult |
| track_id 验证 | ✅        | 300ms 宽限期关联     |
| 位置滤波      | ✅        | 低通滤波平滑抖动     |
| 自适应 PID    | ✅        | 误差大→高增益        |
| 输出滤波      | ✅        | 平滑云台运动         |
| 云台控制      | ✅        | 舵机增量控制         |
| 显示渲染      | ✅        | 双缓冲 + 速度箭头    |

---

## 3. 问题分析

### 3.1 当前问题：云台运动干扰卡尔曼

**现象**：
- DSP 的卡尔曼滤波在图像坐标系工作
- 云台移动导致整个画面平移
- DSP 观测到的速度 = 目标真实速度 + 画面平移速度
- 卡尔曼预测不准确

**示例**：
```
目标静止，云台向右转 10°/s
→ 画面向左移动约 30 像素/帧
→ DSP 观测到 vx ≈ -30 (目标在画面中向左移动)
→ 实际目标速度 = 0
```

### 3.2 信息流不对称

| 信息         | DSP 知道 | CM4 知道         |
| ------------ | -------- | ---------------- |
| 检测 bbox    | ✅        | ✅ (通过 Mailbox) |
| track_id     | ✅        | ✅                |
| 卡尔曼 vx/vy | ✅        | ✅                |
| 云台当前位置 | ❌        | ✅                |
| 云台运动速度 | ❌        | ✅                |
| 控制输出量   | ❌        | ✅                |

---

## 4. 优化方案

### 方案 A：CM4 补偿（推荐）

**原理**：CM4 知道云台速度，在使用卡尔曼速度前进行补偿

```c
// CM4 端
float gimbal_vx = degrees_to_pixels(delta_yaw) / dt;  // 云台运动造成的画面速度
float true_vx = kf_vx - gimbal_vx;  // 真实目标速度
```

**优点**：
- 不需要修改 DSP
- CM4 有完整信息
- 实现简单

**缺点**：
- 需要知道 FOV 与像素的映射关系
- 有一帧延迟

### 方案 B：DSP 运动补偿

**原理**：CM4 将云台速度发送给 DSP，DSP 在卡尔曼测量前补偿

```
CM4 → DSP: 云台速度 (delta_yaw, delta_pitch)
DSP: 在卡尔曼观测中减去画面运动
```

**优点**：
- 卡尔曼状态更准确
- 预测更可靠

**缺点**：
- 需要修改 DSP 代码
- 需要增加 M4→DSP 消息
- 实现复杂

### 方案 C：禁用卡尔曼预测（当前方案）

**原理**：`VELOCITY_PREDICT_FACTOR = 0`，只用位置信息

**优点**：
- 最简单
- 无副作用

**缺点**：
- 无法预测目标运动
- 快速移动目标可能跟丢

---

## 5. 推荐架构（方案 A 增强版）

### 5.1 DSP 职责（保持不变）

```
输入: 摄像头帧
输出: DetectionResult {
    boxes[]: bbox, score, type
    track_id: 跨帧关联 ID
    vx, vy: 图像坐标系速度（含画面运动）
    kf_confidence: 卡尔曼置信度
    selected_idx: 选中目标
}
```

DSP 专注于：
1. **高速推理** - 充分利用 DSP 算力
2. **多目标跟踪** - track_id 维护
3. **原始卡尔曼** - 提供观测速度（不补偿）

### 5.2 CM4 职责（增强）

```c
// 新增：云台运动记录
typedef struct {
    float delta_yaw;      // 上次输出的 yaw 增量
    float delta_pitch;    // 上次输出的 pitch 增量
    float fov_x_deg;      // 水平 FOV (度)
    float fov_y_deg;      // 垂直 FOV (度)
} GimbalMotion_t;

// 新增：速度补偿
float compensate_velocity(float kf_v, float gimbal_delta, float fov_deg, int img_size) {
    // 云台运动在图像上的像素速度
    float gimbal_pixels = (gimbal_delta / fov_deg) * img_size;
    // 真实目标速度 = 观测速度 - 画面运动
    return kf_v - gimbal_pixels;
}
```

### 5.3 增强后的 CM4 处理流程

```
1. 接收 DetectionResult
2. track_id 验证与关联
3. 位置低通滤波
4. 【新增】速度补偿
   - true_vx = kf_vx - gimbal_motion_pixels_x
   - true_vy = kf_vy - gimbal_motion_pixels_y
5. 【可选】速度预测补偿（仅当 kf_confidence > 70）
   - predict_x = filtered_x + true_vx * factor
   - predict_y = filtered_y + true_vy * factor
6. 自适应 PID 计算
7. 输出滤波
8. 云台控制
9. 保存本次云台运动量（用于下次补偿）
```

---

## 6. 代码实现建议

### 6.1 新增配置参数

```c
/** 摄像头 FOV (度) - 用于云台运动补偿 */
#define CAMERA_FOV_X_DEG        60.0f   /* 水平 FOV */
#define CAMERA_FOV_Y_DEG        45.0f   /* 垂直 FOV */

/** 速度预测启用条件 */
#define VELOCITY_PREDICT_ENABLE     true
#define VELOCITY_PREDICT_FACTOR     0.3f
#define VELOCITY_PREDICT_MIN_CONF   70    /* 最低置信度 */
```

### 6.2 新增状态变量

```c
/* 上次云台运动量 (用于速度补偿) */
static float s_last_gimbal_delta_yaw = 0.0f;
static float s_last_gimbal_delta_pitch = 0.0f;
```

### 6.3 补偿逻辑

```c
/* 计算云台运动造成的画面像素速度 */
float gimbal_px_x = (s_last_gimbal_delta_yaw / CAMERA_FOV_X_DEG) * s_img_width;
float gimbal_px_y = (s_last_gimbal_delta_pitch / CAMERA_FOV_Y_DEG) * s_img_height;

/* 补偿后的真实目标速度 */
float true_vx = (float)s_target_vx - gimbal_px_x;
float true_vy = (float)s_target_vy - gimbal_px_y;

/* 速度预测 */
if (s_kf_confidence > VELOCITY_PREDICT_MIN_CONF) {
    predict_cx += true_vx * VELOCITY_PREDICT_FACTOR;
    predict_cy += true_vy * VELOCITY_PREDICT_FACTOR;
}
```

---

## 7. 总结

### 7.1 职责划分原则

| 原则           | 说明                      |
| -------------- | ------------------------- |
| **DSP 做检测** | 神经网络推理、bbox 输出   |
| **DSP 做跟踪** | track_id、原始卡尔曼      |
| **CM4 做控制** | PID、云台驱动             |
| **CM4 做补偿** | 利用云台信息补偿 DSP 输出 |

### 7.2 推荐实施顺序

1. ✅ **Phase 1**（已完成）：基础跟踪 + 自适应 PID
2. 🔄 **Phase 2**：云台运动补偿 + 速度预测
3. 📋 **Phase 3**：多目标切换策略优化
4. 📋 **Phase 4**：DSP 增强（可选，M4→DSP 反馈）

### 7.3 关键指标

| 指标         | 目标值           | 当前值  |
| ------------ | ---------------- | ------- |
| 控制周期     | ≤50ms            | 50ms ✅  |
| 稳态误差     | <10px            | ~10px ✅ |
| jitter       | <15%             | <15% ✅  |
| 快速移动跟随 | 目标保持在画面内 | 待优化  |
