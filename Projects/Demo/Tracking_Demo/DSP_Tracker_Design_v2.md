# S300 多目标跟踪系统架构设计 v2

## 1. 系统架构图

```
┌────────────────────────────────────────────────────────────────┐
│                         DSP (检测层)                            │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐    │
│  │ 图像输入  │ → │ CNN推理  │ → │ 过滤     │ → │ ID关联   │    │
│  │ 160x128  │   │ 48层     │   │ score≥0.6│   │ IoU匹配  │    │
│  │ RGB      │   │ ~50ms    │   │ size≥16  │   │ prev_box │    │
│  └──────────┘   └──────────┘   └──────────┘   └────┬─────┘    │
│                                                     ↓          │
│                                              ┌──────────┐      │
│                                              │ 速度估计  │      │
│                                              │ Kalman   │      │
│                                              │ vx, vy   │      │
│                                              └────┬─────┘      │
└───────────────────────────────────────────────────┼────────────┘
                                                    ↓
                                              Mailbox (共享内存)
                                              DetectionResult
                                                    ↓
┌───────────────────────────────────────────────────┼────────────┐
│                         M4 (决策控制层)             ↓            │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐    │
│  │ 接收检测  │ → │ 目标选择  │ → │ 跟踪状态  │ → │ 云台PID  │    │
│  │ 解析协议  │   │ 确认逻辑  │   │ 遮挡预测  │   │ 控制输出  │    │
│  └──────────┘   └──────────┘   └──────────┘   └──────────┘    │
└────────────────────────────────────────────────────────────────┘
```

---

## 2. DSP 职责 (检测层)

### 2.1 输入
- 160x128 RGB 图像 (来自 M4 的快照)

### 2.2 处理流程

| 步骤 | 功能     | 实现                                         |
| ---- | -------- | -------------------------------------------- |
| 1    | CNN 推理 | `objectdetect_cnn()`                         |
| 2    | 过滤     | `score ≥ 0.6`, `width/height ≥ 16`           |
| 3    | ID 关联  | IoU ≥ 15% 或 距离 < 50px 复用 ID，否则新分配 |
| 4    | 漏检容忍 | 目标消失时保留槽位，最多容忍 15 帧 (~500ms)  |
| 5    | 速度估计 | 每个 ID 维护简单滤波，输出 vx, vy            |

### 2.3 输出

```c
DetectionResult_t {
    magic, version, frame_id, timestamp,
    count,              // 有效检测数量
    selected_idx = -1,  // 固定 -1，DSP 不做选择
    boxes[count] = {
        score,          // 0.0~1.0
        x1, y1, x2, y2, // 坐标
        lm[10],         // 关键点
        track_id,       // DSP 分配的帧间 ID
        vx, vy,         // 速度 (像素/帧)
        speed,          // 速度大小
        kf_confidence   // 速度可信度 (0-100)
    }
}
```

### 2.4 DSP 不做的事情
- ❌ 目标选择 (谁是跟踪目标)
- ❌ 遮挡判断
- ❌ 预测框生成
- ❌ 状态机 (IDLE/TRACKING/OCCLUDED/COOLDOWN)

---

## 3. M4 职责 (决策控制层)

### 3.1 输入
- `DetectionResult_t` (所有有效检测框 + 速度)

### 3.2 处理流程

| 步骤 | 功能     | 说明                                                   |
| ---- | -------- | ------------------------------------------------------ |
| 1    | 目标选择 | 无锁定时，选最靠近中心的；有锁定时，按 `track_id` 匹配 |
| 2    | 确认逻辑 | 新目标需连续 N 帧出现才确认锁定                        |
| 3    | 遮挡处理 | 锁定目标消失时，用其 `vx, vy` 预测位置，继续跟踪       |
| 4    | 丢失判断 | 连续 M 帧预测仍无匹配 → 确认丢失 → 重选                |
| 5    | PID 控制 | 根据目标位置误差输出云台控制                           |

### 3.3 M4 状态机

```
IDLE ──(检测到目标)──→ CONFIRMING ──(N帧确认)──→ TRACKING
                            ↑                        │
                            │                        ↓
                            └────(重选)───── LOST ←──(M帧丢失)
                                              ↑
                              PREDICTING ─────┘
                                  ↑
                          (目标消失,用vx,vy预测)
```

### 3.4 遮挡预测逻辑 (M4 实现)

```c
// M4 伪代码
if (收到 result 中有 track_id == 锁定ID) {
    // 正常跟踪
    target_pos = result 中的位置;
    last_vx = result.vx;
    last_vy = result.vy;
    predict_count = 0;
} else {
    // 目标消失，用上一帧速度预测
    predict_count++;
    if (predict_count <= MAX_PREDICT) {
        target_pos.x += last_vx;
        target_pos.y += last_vy;
        // 继续用预测位置控制云台
    } else {
        // 确认丢失
        state = LOST;
    }
}
```

---

## 4. 协议定义 (detection_protocol.h)

```c
typedef struct __attribute__((packed)) {
    float    score;           // 0.0~1.0 置信度
    int32_t  x1, y1, x2, y2;  // 边界框坐标
    float    lm[10];          // 5个关键点 (x0,y0,...,x4,y4)
    uint8_t  type;            // 检测类型 (FACE=1)
    uint8_t  track_id;        // DSP 分配的帧间关联 ID
    int8_t   vx, vy;          // 速度 (像素/帧)
    uint8_t  speed;           // 速度大小 (0-255)
    uint8_t  kf_confidence;   // 速度可信度 (0-100)
    uint8_t  reserved[2];     // 保留
} DetectionBox_t;             // 68 bytes

typedef struct __attribute__((packed)) {
    uint32_t       magic;        // 0x44455446
    uint32_t       version;      // 0x0202
    uint32_t       frame_id;     // 递增帧号
    uint32_t       timestamp;    // 时间戳 (ms)
    uint32_t       count;        // 检测数量 [0, 10]
    int32_t        selected_idx; // DSP 固定填 -1
    DetectionBox_t boxes[10];    // 检测框数组
} DetectionResult_t;             // 704 bytes
```

**协议无需更新**，当前定义已满足需求。

---

## 5. 配置参数

### DSP 参数 (dsp_tracker_v2.c)

| 参数                            | 值  | 说明                          |
| ------------------------------- | --- | ----------------------------- |
| `MAX_TRACK_SLOTS`               | 5   | 最大同时跟踪目标数            |
| `TRACK_MAX_MISS`                | 15  | 漏检容忍帧数 (30FPS下约500ms) |
| `TRACKER_IOU_THRESHOLD_PCT`     | 15  | IoU 匹配阈值 (%)              |
| `TRACKER_CENTER_DIST_THRESHOLD` | 50  | 距离匹配阈值 (像素)           |
| `TRACKER_DEBUG_LEVEL`           | 1   | 调试级别                      |

### DSP 过滤参数 (testbench_dsp.c / face_detect.c)

| 参数               | 值  | 说明       |
| ------------------ | --- | ---------- |
| `confidence`       | 0.6 | 最小置信度 |
| `DSP_MIN_BOX_SIZE` | 16  | 最小框尺寸 |

---

## 6. ID 关联机制 (TrackSlot)

### 核心数据结构

```c
typedef struct {
    DetectionBox_t box;         // 最后检测到的位置
    uint8_t        track_id;    // 分配的 ID
    uint8_t        miss_count;  // 连续漏检帧数
    uint8_t        active;      // 是否活跃
} TrackSlot_t;

TrackSlot_t slots[5];           // 最多同时跟踪5个目标
```

### 工作流程

```
帧N: 检测到人脸A
     → 分配 slot[0], ID=1, miss=0

帧N+1: 人脸A 匹配 slot[0] (IoU≥15%)
     → 复用 ID=1, miss=0

帧N+2: 人脸A 漏检 (侧脸/遮挡)
     → slot[0].miss=1, 保留

帧N+3~N+10: 持续漏检
     → miss=2,3,...,9, 仍保留

帧N+11: 人脸A 重新出现
     → 匹配 slot[0], 复用 ID=1, miss=0 ✓

帧N+20: 人脸A 消失15帧
     → miss=15 > TRACK_MAX_MISS
     → 删除 slot[0]

帧N+21: 新检测
     → 分配新 slot, ID=2
```

---

## 7. 版本历史

| 版本 | 日期       | 改动                                            |
| ---- | ---------- | ----------------------------------------------- |
| v1.0 | 2026-01-30 | 初版，DSP 做完整跟踪                            |
| v2.0 | 2026-02-04 | 简化，DSP 只做检测+ID关联，M4 做决策            |
| v2.1 | 2026-02-04 | 添加 TrackSlot 漏检容忍，500ms内重现可复用原 ID |
