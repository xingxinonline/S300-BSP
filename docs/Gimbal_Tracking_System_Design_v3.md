# S300 多目标跟踪与云台控制系统架构设计 v3

> **版本**: v3.0  
> **日期**: 2026-02-06  
> **状态**: 方案评审稿  
> **前序文档**: `DSP_Tracker_Design_v2.md`, `Tracking_System_Architecture.md`

---

## 0. 修订历史

| 版本 | 日期       | 改动说明                                                         |
| ---- | ---------- | ---------------------------------------------------------------- |
| v1.0 | 2026-01-30 | 初版，DSP 做完整跟踪 + 选择                                      |
| v2.0 | 2026-02-04 | 简化 DSP，只做检测+ID关联；M4 做决策控制                         |
| v2.1 | 2026-02-04 | TrackSlot 漏检容忍，500ms 内保留 ID                              |
| v3.0 | 2026-02-06 | 完整方案：含 Mailbox 零拷贝通信、SORT 跟踪、CM4 状态机、安全保护 |
| v3.1 | 2026-02-13 | 新增 SOT 单目标跟踪模式 + 128 维外观特征 (PTZ 自拍杆场景)        |
| v3.2 | 2026-02-13 | **本文**。协议 v3.0 双向通信、IMU 陀螺仪集成、云台角速度补偿     |

---

## 1. 系统总览

### 1.1 硬件平台

| 组件      | 型号/参数                                              | 备注                   |
| --------- | ------------------------------------------------------ | ---------------------- |
| 主控 SoC  | PiMCHIP S300 (CM4 200MHz + CEVA SensPro250 DSP 400MHz) | 双核异构               |
| 摄像头    | OV5640                                                 | 检测分辨率 160×128 RGB |
| 显示      | LCD (双缓冲 Overlay)                                   | 160×128 RGB565         |
| Pan 电机  | Hiwonder 总线舵机 ID=6                                 | 0-1000 脉冲 / 240°     |
| Tilt 电机 | Hiwonder 总线舵机 ID=4                                 | 同上                   |
| **IMU**   | **QMI8658A** (I2C)                                     | **六轴 IMU**           |
| 通信      | Mailbox + 共享内存 (SRAM)                              | 零拷贝                 |

### 1.2 端到端数据流

```
Camera (OV5640)                                IMU (QMI8658A)
    │  160×128 RGB @20FPS                          │ gyro_x, gyro_z
    ▼                                              │ 100Hz I2C (0x6A)
┌───────────────────────────────────────────────────────────────────┐
│               DSP (CEVA SensPro250 400MHz) — 检测层               │
│                                                                   │
│   ┌──────────┐    ┌──────────┐    ┌──────────────────────────┐   │
│   │ CNN 推理 │ →  │  过滤    │ →  │ SOT / SORT 跟踪器        │   │
│   │ ~50ms    │    │ score≥0.6│    │ 8D Kalman predict        │   │
│   │ 48层     │    │ size≥16  │    │ → 云台角速度补偿 ←───────│───│── CM4 Mailbox
│   └──────────┘    └──────────┘    │ → IoU 匹配 / 外观匹配    │   │
│                                   │ → Kalman update          │   │
│                                   │ → vx,vy,vs / track_id    │   │
│                                   └────────────┬─────────────┘   │
│                                                        │         │
│   ┌────────────────────────────────────────────────────┘         │
│   │  写入共享内存: DetectionResult_t (704B)                      │
│   │  Mailbox 发送: MSG_TYPE_MULTI | offset                      │
│   └──────────────────────────────────┬──────────────────────────┘│
└──────────────────────────────────────┼───────────────────────────┘
                    Mailbox 中断       │
                    (零拷贝指针)        ▼
┌──────────────────────────────────────┼───────────────────────────┐
│                   CM4 (200MHz) — 决策控制层               │      │
│                                      │                    ▼      │
│   ┌──────────┐    ┌──────────┐    ┌──┴───────┐    ┌──────────┐  │
│   │ Mailbox  │ →  │ 目标确认 │ →  │ ID 选择  │ →  │  外环    │  │
│   │ 解析     │    │ N帧连续  │    │ 粘滞策略 │    │  P 控制  │  │
│   └──────────┘    └──────────┘    └──────────┘    └────┬─────┘  │
│                                                        │        │
│   ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌────┴─────┐  │
│   │ 显示渲染 │ ←  │ 安全保护 │ ←  │ 状态机   │ ←  │ 平滑滤波 │  │
│   │ 双缓冲   │    │ 堵转检测 │    │ FSM      │    │ + 限步   │  │
│   └──────────┘    └──────────┘    └──────────┘    └──────────┘  │
│         │                                              │        │
│         │    ┌──────────┐                              │        │
│         │    │   IMU    │──→ 手抖补偿 ──────┬──────────┤        │
│         │    │QMI8658A │                   │          │        │
│         │    └──────────┘                   │          │        │
│         │         │                         │          │        │
│         │         │ SET_GIMBAL_VEL          │          │        │
│         │         └────────────────────────►│ Mailbox  │        │
│         │                    (CM4→DSP)      │ 发送     │        │
│         │                                   │          ▼        │
│         │                                   │  Bus Servo (UART) │
│         │                                   │  ID6=PAN, ID4=TILT│
│         └───────────────────────────────────┴───────────────────│
└─────────────────────────────────────────────────────────────────┘

双向通信:
  DSP→CM4: 检测结果 (DetectionResult_t)
  CM4→DSP: 云台角速度 (SET_GIMBAL_VEL)、跟踪控制命令
```

---

## 2. DSP 侧设计（检测层）

### 2.1 职责边界

DSP **只负责**图像级处理（含完整 SORT 跟踪），**不做**任何上层决策。

| DSP 做                                | DSP 不做       |
| ------------------------------------- | -------------- |
| CNN 推理                              | ❌ 目标选择     |
| 置信度 & 尺寸过滤                     | ❌ 遮挡判断     |
| **完整 SORT**: 8D Kalman + 匈牙利匹配 | ❌ 跟踪决策     |
| Kalman predict → 预测框生成           | ❌ 状态机       |
| Kalman update → 状态校正 + 速度估计   | ❌ 云台控制指令 |
| TrackSlot 漏检容忍 (coast)            |                |

### 2.2 SORT 算法概述

采用完整版 SORT（Simple Online and Realtime Tracking, Bewley 2016）算法，核心要素：

1. **8 维 Kalman 滤波器** — 每个 Track 维护完整运动状态
2. **Kalman Predict** — 每帧先预测所有已有 Track 的下一帧位置
3. **IoU 代价矩阵** — 用预测框（而非上一帧检测框）与当前检测框计算 IoU
4. **匈牙利算法** — 最优匹配（线性分配），最小化代价
5. **Track 生命周期管理** — tentative → confirmed → coasted → deleted

### 2.3 Kalman 滤波器设计（8 维）

**状态向量** $\mathbf{x} \in \mathbb{R}^{8}$：

$$
\mathbf{x} = [c_x, c_y, s, r, \dot{c}_x, \dot{c}_y, \dot{s}, \dot{r}]^T
$$

| 分量                   | 含义                | 单位     |
| ---------------------- | ------------------- | -------- |
| $c_x, c_y$             | 边界框中心坐标      | 像素     |
| $s$                    | 尺度 (面积 = w × h) | 像素²    |
| $r$                    | 宽高比 (w / h)      | 无量纲   |
| $\dot{c}_x, \dot{c}_y$ | 中心速度            | 像素/帧  |
| $\dot{s}$              | 尺度变化率          | 像素²/帧 |
| $\dot{r}$              | 宽高比变化率        | 1/帧     |

**观测向量** $\mathbf{z} \in \mathbb{R}^{4}$：

$$
\mathbf{z} = [c_x, c_y, s, r]^T
$$

**运动模型**（匀速假设）：

$$
\mathbf{F} = \begin{bmatrix}
\mathbf{I}_4 & \Delta t \cdot \mathbf{I}_4 \\
\mathbf{0}_4 & \mathbf{I}_4
\end{bmatrix}_{8 \times 8}
, \quad
\mathbf{H} = \begin{bmatrix}
\mathbf{I}_4 & \mathbf{0}_4
\end{bmatrix}_{4 \times 8}
$$

其中 $\Delta t = 1$（帧间隔归一化）。

**Predict 步骤**（每帧对所有活跃 Track 执行）：

$$
\hat{\mathbf{x}}_{k|k-1} = \mathbf{F} \hat{\mathbf{x}}_{k-1|k-1}
, \quad
\mathbf{P}_{k|k-1} = \mathbf{F} \mathbf{P}_{k-1|k-1} \mathbf{F}^T + \mathbf{Q}
$$

从预测状态 $\hat{\mathbf{x}}_{k|k-1}$ 反算预测边界框 $[\hat{c}_x, \hat{c}_y, \hat{s}, \hat{r}]$，用于后续 IoU 匹配。

**Update 步骤**（对匹配成功的 Track 执行）：

$$
\mathbf{K}_k = \mathbf{P}_{k|k-1} \mathbf{H}^T (\mathbf{H} \mathbf{P}_{k|k-1} \mathbf{H}^T + \mathbf{R})^{-1}
$$

$$
\hat{\mathbf{x}}_{k|k} = \hat{\mathbf{x}}_{k|k-1} + \mathbf{K}_k (\mathbf{z}_k - \mathbf{H} \hat{\mathbf{x}}_{k|k-1})
$$

$$
\mathbf{P}_{k|k} = (\mathbf{I} - \mathbf{K}_k \mathbf{H}) \mathbf{P}_{k|k-1}
$$

**噪声参数**（初始值，需根据实际调优）：

| 参数           | 维度     | 说明       | 初始值                                                                             |
| -------------- | -------- | ---------- | ---------------------------------------------------------------------------------- |
| $\mathbf{Q}$   | 8×8 对角 | 过程噪声   | 位置: $\sigma^2_{pos}=1.0$, 尺度: $\sigma^2_{s}=0.01$, 速度: $\sigma^2_{vel}=0.01$ |
| $\mathbf{R}$   | 4×4 对角 | 观测噪声   | 位置: $\sigma^2_{z,pos}=1.0$, 尺度: $\sigma^2_{z,s}=10.0$                          |
| $\mathbf{P}_0$ | 8×8 对角 | 初始协方差 | 位置: 10, 尺度: 10, 速度: $10^4$（高不确定性）                                     |

> **实现说明**：CEVA SensPro250 支持标量浮点运算。5 个 Track × 8D Kalman 的计算量极小（< 0.5ms），远在性能预算内。可使用 float32 直接实现，无需定点化。

### 2.4 处理流程

```
输入: 160×128 RGB 图像
  │
  ▼
[1. CNN 推理] objectdetect_cnn() → 原始检测列表 (最多 10 个)
  │
  ▼
[2. 过滤] score ≥ 0.6 AND (width ≥ 16 AND height ≥ 16)
  │         → 有效检测列表 detections[M]
  │
  ▼
[3. Kalman Predict] 对每个活跃 TrackSlot 执行 predict
  │         → 生成预测框 predicted_boxes[N]
  │
  ▼
[4. IoU 代价矩阵] cost[N][M] = 1 - IoU(predicted_boxes[i], detections[j])
  │         N = 活跃 Track 数, M = 当前检测数
  │
  ▼
[5. 匈牙利算法] linear_assignment(cost) → 最优匹配
  │  ├─ matched:    (track_i, det_j) 且 IoU ≥ IOU_MIN_THRESHOLD
  │  ├─ unmatched_tracks: 未匹配的已有 Track
  │  └─ unmatched_dets:   未匹配的新检测
  │
  ▼
[6. Kalman Update] 对 matched pairs 执行 update
  │         → 校正位置/速度, 提取 vx, vy, speed, kf_confidence
  │
  ▼
[7. Track 管理]
  │  ├─ matched tracks:     miss_count = 0, hits++
  │  ├─ unmatched tracks:   miss_count++, 仅 predict (coast)
  │  │   └─ miss > TRACK_MAX_MISS → 删除
  │  └─ unmatched dets:     创建新 Track (tentative, hits=1)
  │         → Track hits ≥ MIN_HITS → confirmed (开始输出)
  │
  ▼
[8. 写入共享内存] 只输出 confirmed 的 Track → DetectionResult_t
  │
  ▼
[9. Mailbox 通知] MAILBOX_MSG_TYPE_MULTI | offset
```

### 2.5 匈牙利算法

对于最多 5 Track × 10 Detection 的小规模矩阵，使用经典 **Munkres/Hungarian** 算法：

- 时间复杂度：$O(n^3)$，$n = \max(N, M) \leq 10$
- 实际开销：< 0.1ms（对 5×10 矩阵）
- IoU 门限：IoU < `IOU_MIN_THRESHOLD` (25%) 的匹配对视为无效，退回 unmatched

> 也可使用贪心匹配作为候选：按 IoU 降序逐对匹配，复杂度 $O(NM \log NM)$。适合资源极度受限场景，但最优性不如匈牙利。当前设计采用匈牙利。

### 2.6 TrackSlot 数据结构

```c
#define MAX_TRACK_SLOTS         5     /* 最多同时跟踪 5 个目标 */
#define MAX_DETECTIONS_PER_FRAME 10   /* 每帧最多检测数 */
#define TRACK_MAX_MISS          15    /* 漏检容忍帧数 (~750ms @20FPS) */
#define IOU_MIN_THRESHOLD       0.25f /* IoU 匹配最低阈值 */
#define MIN_HITS                3     /* 新 Track 确认所需连续命中帧数 */

/* 8 维 Kalman 滤波器状态 */
typedef struct {
    float x[8];              /* 状态向量 [cx, cy, s, r, vx, vy, vs, vr] */
    float P[8][8];           /* 协方差矩阵 */
} KalmanState_t;

typedef struct {
    DetectionBox_t box;      /* 最新校正后的边界框 (输出用) */
    KalmanState_t  kf;       /* 8D Kalman 滤波器完整状态 */
    uint8_t  track_id;       /* 分配的 ID (1-255, 0=无效) */
    uint8_t  miss_count;     /* 连续漏检帧数 (coast 计数) */
    uint8_t  hits;           /* 连续命中帧数 */
    uint8_t  state;          /* 0=空闲, 1=tentative, 2=confirmed */
    float    predicted_box[4]; /* Kalman predict 输出 [cx, cy, s, r] */
} TrackSlot_t;

static TrackSlot_t g_slots[MAX_TRACK_SLOTS];

/* 匈牙利算法工作空间 (避免动态分配) */
static float g_cost_matrix[MAX_TRACK_SLOTS][MAX_DETECTIONS_PER_FRAME];
static int   g_assignment[MAX_TRACK_SLOTS];  /* track→det 映射, -1=未匹配 */
```

### 2.7 Track 生命周期

```
                     检测匹配
  ┌──────────────┐  hits≥MIN_HITS  ┌───────────────┐
  │  Tentative   │ ──────────────→ │  Confirmed    │
  │  (不输出)    │                 │  (输出到CM4)  │
  └──────┬───────┘                 └──────┬────────┘
         │ miss_count++                   │ miss_count++
         │ miss > MAX_MISS                │ (持续 predict, 不输出)
         ▼                                ▼
  ┌──────────────┐                 ┌───────────────┐
  │   Deleted    │                 │   Coasting    │
  │  (释放槽位)  │                 │  (仅predict)  │
  └──────────────┘                 └──────┬────────┘
                                          │ miss > MAX_MISS
                                          ▼
                                   ┌───────────────┐
                                   │   Deleted     │
                                   │  (释放槽位)   │
                                   └───────────────┘
```

### 2.8 ID 关联时序示例

```
帧N  : 检测到人脸A → 创建 slot[0], ID=1, state=tentative, hits=1
帧N+1: Kalman predict slot[0] → 预测框; IoU匹配成功 → update, hits=2
帧N+2: 再次匹配 → hits=3 ≥ MIN_HITS → state=confirmed, 开始输出
帧N+3: 人脸A 漏检 (侧脸/遮挡) → Kalman predict only (coast), miss=1
帧N+4~N+10: 持续漏检 → miss=2..8, 每帧 predict 维持预测框位置
帧N+11: 人脸A 重新出现 → IoU(predicted, det) ≥ 25% → 匹配! update, miss=0 ✓
帧N+26: 消失 15 帧 → miss=15 > TRACK_MAX_MISS → 删除 slot[0], 释放 ID
帧N+27: 新检测 → 分配 slot[0], ID=2 (新 ID), state=tentative
```

---

## 3. 通信设计（DSP ↔ CM4）

### 3.1 Mailbox 硬件

```
S300 SoC 内部:
  MAILBOX0 (CM4 端): 基地址 0x40020000
  DSP_MAILBOX0 (DSP 端): 基地址由 DSP 映射

  寄存器:
    WRDATA (0x00): 写数据 (发送方写)
    RDDATA (0x08): 读数据 (接收方读)
    STATUS (0x10): FIFO 状态
    SIT/RIT:       中断触发/原始状态
    IE:            中断使能
```

### 3.2 消息格式 (32 位)

```
┌────────────────┬──────────────────────────────┐
│ [31:28] 4-bit  │ [27:0] 28-bit                │
│ 消息类型       │ Payload (共享内存偏移)         │
├────────────────┼──────────────────────────────┤
│ 0x0 SINGLE     │ 旧协议：单目标               │
│ 0x1 MULTI      │ 共享内存中 DetectionResult 偏移│
│ 0x2 KWS        │ 语音识别结果偏移             │
│ 0xF NO_RESULT  │ 本帧无检测结果               │
├────────────────┼──────────────────────────────┤
│ 0x8 CMD_START  │ CM4→DSP: 启动追踪            │
│ 0x9 CMD_STOP   │ CM4→DSP: 停止追踪            │
│ 0xA CMD_RESET  │ CM4→DSP: 重置选中            │
└────────────────┴──────────────────────────────┘
```

### 3.3 零拷贝数据流

```
DSP 侧:
    static DetectionResult_t g_result;  // DSP 可访问的共享内存
    fill_detection_result(&g_result);
    uint32_t offset = (uint32_t)&g_result - DSP_DETECTION_BASE_ADDR;
    mailbox_send(MAILBOX_MSG_TYPE_MULTI | offset);

CM4 侧:
    uint32_t msg = mailbox_read();
    uint32_t type   = msg >> 28;           // MAILBOX_GET_MSG_TYPE
    uint32_t offset = msg & 0x0FFFFFFFu;   // MAILBOX_GET_PAYLOAD
    if (type == 0x1) {
        const DetectionResult_t *result =
            (const DetectionResult_t *)(DSP_DETECTION_BASE_ADDR + offset);
        // 直接访问 DSP 内存，零拷贝
        if (DETECTION_RESULT_IS_VALID(result)) {
            process_detection(result);
        }
    }
```

### 3.4 共享数据结构 (detection_proto.h，双方共用)

```c
/* ===== DetectionBox_t (68 bytes, packed) ===== */
typedef struct __attribute__((packed)) {
    float    score;           // [0.0, 1.0] 置信度
    int32_t  x1, y1, x2, y2; // 边界框 (像素坐标, 160×128 空间)
    float    lm[10];          // 5 个关键点 (x0,y0,...,x4,y4)
    uint8_t  type;            // DetectionType_t (FACE=1, HUMAN=2, ...)
    uint8_t  track_id;        // DSP 分配的帧间关联 ID (0=未分配)
    int8_t   vx, vy;          // 速度 (像素/帧, Kalman 输出)
    uint8_t  speed;           // 速度大小 [0-255]
    uint8_t  kf_confidence;   // Kalman 置信度 [0-100]
    uint8_t  miss_count;      // DSP 侧连续漏检帧数 (0=本帧有真实检测)
    uint8_t  reserved;        // 对齐
} DetectionBox_t;
_Static_assert(sizeof(DetectionBox_t) == 68, "size mismatch");

/* ===== DetectionResult_t (704 bytes, packed) ===== */
typedef struct __attribute__((packed)) {
    uint32_t       magic;         // 0x44455446 ("DETF")
    uint32_t       version;       // 0x0203 (v2.3)
    uint32_t       frame_id;      // 递增
    uint32_t       timestamp;     // ms
    uint32_t       count;         // [0, 10]
    int32_t        selected_idx;  // DSP 选中 (v3: 固定 -1)
    DetectionBox_t boxes[10];     // 检测目标数组
} DetectionResult_t;
_Static_assert(sizeof(DetectionResult_t) == 704, "size mismatch");
```

> **协议变更 (v3)**：将 `reserved[2]` 拆分为 `miss_count` + `reserved`。`miss_count` 由 DSP 填写该 Track 的连续漏检帧数（0 表示本帧有真实 CNN 检测，>0 表示 Kalman coast 预测框）。CM4 据此区分"真检测"和"幽灵预测框"，实现差异化控制策略。结构体大小不变（68B），版本号升至 0x0203。

---

## 4. CM4 侧设计（决策控制层）

### 4.1 总体架构

CM4 是整个系统的"控制大脑"，由以下模块组成：

```
┌─────────────────────────────────────────────────────────────────┐
│                        CM4 软件架构                              │
│                                                                 │
│  ┌───────────────┐                                              │
│  │  main.c       │ ← SysTick (1ms) + 主循环                    │
│  │  主控流程     │                                              │
│  └───────┬───────┘                                              │
│          │ mailbox_poll() @每帧                                  │
│          ▼                                                      │
│  ┌───────────────┐  解析后传给                                   │
│  │ detect_parse  │ ─────────────┐                               │
│  │ 检测解析模块  │              │                               │
│  └───────────────┘              ▼                               │
│                        ┌───────────────┐                        │
│                        │ target_sel    │                        │
│                        │ 目标选择器    │ ← Active ID 粘滞策略   │
│                        └───────┬───────┘                        │
│                                │ 选中目标                       │
│                                ▼                                │
│                        ┌───────────────┐                        │
│                        │ tracker_fsm   │                        │
│                        │ 跟踪状态机    │ ← FOLLOW/LOST/CENTER   │
│                        └───────┬───────┘                        │
│                                │ 误差 (ex, ey)                  │
│                                ▼                                │
│                        ┌───────────────┐                        │
│                        │ ctrl_loop     │                        │
│                        │ 外环控制器    │ ← P + 死区 + 限步 + LPF│
│                        └───────┬───────┘                        │
│                                │ 角度 (pan°, tilt°)             │
│                                ▼                                │
│                        ┌───────────────┐                        │
│                        │ gimbal_ctrl   │ ← 角度 → 脉冲映射     │
│                        │ 云台驱动      │    安全保护             │
│                        └───────┬───────┘                        │
│                                │ UART (Bus Servo)               │
│                                ▼                                │
│                           电机 ID6, ID4                         │
└─────────────────────────────────────────────────────────────────┘
```

#### 4.1.1 Mailbox 中断与主循环交互

```
Mailbox 采用 "中断置 flag + 主循环 poll" 模式:

  ISR (Mailbox RX 中断):
    g_new_det_flag = true;                     // volatile
    g_det_offset = mailbox_read() & 0x0FFFFFFFu;
    // 仅保存 flag + offset, 不做任何处理

  主循环 (SysTick 50ms 周期):
    if (g_new_det_flag) {
        g_new_det_flag = false;                 // 单读者, 无需锁
        const DetectionResult_t *r =
            (const DetectionResult_t *)(DSP_DETECTION_BASE_ADDR + g_det_offset);
        if (DETECTION_RESULT_IS_VALID(r))
            tracker_update_target(r);           // 含完整性校验
    }
    tracker_poll();                              // 状态机 + 控制输出

零拷贝安全性:
  · DSP 使用 ping-pong 双缓冲写入 DetectionResult
  · CM4 读取的始终是"上一帧完成的"缓冲区, 不与 DSP 当前写入冲突
  · 若 DSP 帧率 > CM4 处理速率, CM4 skip 中间帧 (始终取最新)
```

### 4.2 模块 1：目标选择器 (Target Selector)

#### 4.2.1 设计目标

在多人场景下，保证云台**稳定跟踪一个目标**，不跳变。

#### 4.2.2 Active ID 粘滞策略

```c
/*
 * 策略核心：一旦锁定 track_id，只有"确认丢失"后才允许切换。
 *
 * 优先级：
 *   1. 当前 active_id 仍在列表中 → 继续跟踪（最高优先级）
 *   2. active_id == -1（无锁定）→ 按策略选新目标
 *      - 策略 A: 选置信度最高的
 *      - 策略 B: 选面积最大的（距离最近）
 *      - 策略 C: 选最靠近画面中心的（推荐，便于锁定）
 *   3. 绝不在 FOLLOW 状态下切换到新 ID
 */

int select_target(const DetectionResult_t *result, int active_id)
{
    /* 1. 已锁定：在列表中按 track_id 精确匹配 */
    if (active_id > 0) {
        for (int i = 0; i < result->count; i++) {
            if (result->boxes[i].track_id == active_id)
                return i;  /* 找到了，继续跟 */
        }
        return -1;  /* 没找到，返回 MISS */
    }

    /* 2. 未锁定：选最靠近画面中心的 */
    int best = -1;
    int min_dist_sq = INT_MAX;
    int cx = 80, cy = 64;  /* 160×128 的中心 */

    for (int i = 0; i < result->count; i++) {
        const DetectionBox_t *b = &result->boxes[i];
        int bx = (b->x1 + b->x2) / 2;
        int by = (b->y1 + b->y2) / 2;
        int d = (bx - cx) * (bx - cx) + (by - cy) * (by - cy);
        if (d < min_dist_sq) {
            min_dist_sq = d;
            best = i;
        }
    }
    return best;
}
```

#### 4.2.3 确认逻辑（防误检）

新目标需连续 **2 帧** 在 **300ms** 窗口内出现，且位置偏移 < 40px 才确认。

```
帧 N  : 新 track_id=3 出现 → confirm_count=1, 不跟踪
帧 N+1: track_id=3 再次出现, 位置接近 → confirm_count=2 ≥ 阈值 → CONFIRMED!
        → active_id = 3, 开始 FOLLOW
```

### 4.3 模块 2：跟踪状态机 (Tracker FSM)

#### 4.3.1 状态转移图

```
                    ┌────────────────────────────┐
                    │        IDLE (空闲)          │
                    │  等待视觉结果              │
                    └─────────┬──────────────────┘
                              │ 检测到已确认目标
                              │ active_id 锁定
                              ▼
                    ┌────────────────────────────┐
        ┌──────────│      FOLLOW (跟踪)         │◄───────────────────┐
        │          │  P 控制, 更新电机          │                    │
        │          │                            │                    │
        │          │  miss_count==0 → 正常控制   │                    │
        │          │  miss_count>0  → coast 降增益│                    │
        │          └─────────┬──────────────────┘                    │
        │                    │ active_id 在当前帧彻底消失              │
        │                    │ (DSP 删除该 Track, 不再输出)            │
        │                    ▼                                       │
        │          ┌────────────────────────────┐                    │
        │          │    PREDICTING (等待恢复)     │                    │
        │          │  ≤ 5 控制周期 (250ms)       │                    │
        │          │  保持最后位置，不控制云台    │                    │
        │          │                            │                    │
        │          │  恢复条件:                  │                    │
        │          │  - 原ID回来                 │                    │
        │          │  - 位置偏差 ≤ 50px          │                    │
        │          └──┬─────────────┬───────────┘                    │
        │  超时(5帧)  │             │ 原ID恢复 + 位置验证通过         │
        │             ▼             └────────────────────────────────┘
        │  ┌────────────────────────────┐
        │  │       LOST (丢失)           │
        │  │  清除 active_id             │
        │  │  记录: 丢失位置/边缘方向    │
        │  │                            │
        │  │  四级超时策略:              │
        │  │  ① >50ms:  接受原ID(位置检查)│
        │  │  ② >500ms: 接受新ID(位置+   │
        │  │            边缘方向检查)    │
        │  │  ③ >1s:    接受任意目标     │
        │  │  ④ >10s:   强制回中         │
        │  └──┬─────────────┬───────────┘
        │     │             │ 10s 超时
        │     │ 目标恢复    ▼
        │     │          ┌────────────────────────────┐
        │     │          │     CENTER (回中)            │
        │     │          │  缓慢回到 (0°, -45°)        │
        │     │          │  可接受任意新目标            │
        │     │          └──────────┬───────────────────┘
        │     │                     │ 新目标出现 OR 到达中心
        └─────┴─────────────────────┘
              目标恢复 → FOLLOW, 到达中心 → IDLE

异常路径:
  任何状态 →  堵转/越界检测 → ERROR → 停止电机

ERROR 恢复:
  ERROR → tracker_reset() (外部干预) → IDLE (重新开始)
  ERROR → 堵转消除 + 5s 超时 → CENTER → IDLE (自动恢复)

帧超时保护:
  任何跟踪状态 → 100ms 无新帧 → 暂停控制输出, 保持最后位置
  帧恢复后 → 自动继续原状态 (无需状态切换)
```

#### 4.3.2 DSP Coast 感知机制 (关键优化)

DSP 升级为完整 SORT 后，coast 期间（`miss_count > 0`）仍会输出预测框，`track_id` 不变。CM4 **必须区分**"真检测"和"Kalman coast 预测"：

```
┌──────────────────────────────────────────────────────────────────────┐
│                     FOLLOW 状态内部两模式                             │
│                                                                      │
│  收到匹配 active_id 的框:                                            │
│                                                                      │
│  ┌─── miss_count == 0 ───────────────────────────────────────────┐   │
│  │  [真检测模式] CNN 有真实检测                                  │   │
│  │  · 正常 P 控制 (Kp × 100%)                                   │   │
│  │  · 更新参考位置 (last_cx, last_cy)                            │   │
│  │  · 更新速度/置信度                                            │   │
│  │  · coast_frame_count = 0                                      │   │
│  └───────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─── miss_count > 0 ───────────────────────────────────────────┐   │
│  │  [Coast 模式] DSP Kalman 惯性预测框                           │   │
│  │  · 降低增益: Kp × COAST_GAIN_FACTOR (0.3)                    │   │
│  │  · 不更新参考位置 (保持最后真实位置)                          │   │
│  │  · coast_frame_count++                                        │   │
│  │  · coast_frame_count > MAX_COAST_FOLLOW → 保持最后位置       │   │
│  └───────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  未收到匹配 active_id 的框 (DSP 已删除该 Track):                     │
│  → 进入 PREDICTING 状态                                             │
└──────────────────────────────────────────────────────────────────────┘
```

```c
/* Coast 相关参数 */
#define COAST_GAIN_FACTOR       0.3f    /* coast 模式增益衰减 (30%) */
#define MAX_COAST_FOLLOW        5       /* coast 最多跟随 5 帧后停止控制 */
#define COAST_CONFIDENCE_THRESHOLD 0    /* miss_count > 0 即为 coast */

/* FOLLOW 状态处理伪代码 */
void follow_process(const DetectionBox_t *target)
{
    if (target->miss_count == 0) {
        /* 真检测: 正常控制 */
        kp_scale = 1.0f;
        update_reference_position(target);
        coast_frame_count = 0;
    } else {
        /* Coast 预测: 降增益或停止 */
        coast_frame_count++;
        if (coast_frame_count <= MAX_COAST_FOLLOW) {
            kp_scale = COAST_GAIN_FACTOR;  /* 弱跟随 */
        } else {
            return;  /* 保持最后位置, 不控制云台 */
        }
    }
    
    /* 跳变检测 (此步骤对真检测和 coast 都执行) */
    if (detect_position_jump(target)) return;
    
    /* P 控制 */
    ex = normalize_error_x(target);
    ey = normalize_error_y(target);
    delta_pan  = -Kp_pan * kp_scale * soft_deadzone(ex);
    delta_tilt = -Kp_tilt * kp_scale * soft_deadzone(ey);
    /* ... 限步 + 边缘保护 + LPF ... */
}
```

> **设计理由**：DSP coast 是纯惯性预测（无 CNN 观测校正），位置精度随 miss_count 快速衰减。CM4 如果全力跟随幽灵框，会把云台带偏，导致目标重现时偏出画面。降增益策略是"半信半疑"：小幅跟随（万一预测对了），但不大幅移动（防止跑偏）。

#### 4.3.3 位置跳变检测 (最后防线)

即使 DSP 匈牙利匹配更准确，CNN 检测抖动或 track_id 回绕仍可能产生错误关联。CM4 作为**最后防线**，需检测并拒绝不合理的位置跳变。

```c
/* 跳变检测参数 */
#define JUMP_ABSOLUTE_MAX    100    /* 曼哈顿距离 > 100px: 无条件拒绝 */
#define JUMP_SPEED_CHECK     50     /* 曼哈顿距离 > 50px: 需速度验证 */
#define JUMP_DIRECTION_CHECK 30     /* 曼哈顿距离 > 30px: 需方向验证 */
#define JUMP_MIN_SPEED       20     /* 速度验证的最小速度 (曼哈顿) */

/**
 * @brief 多准则位置跳变检测
 *
 * 对比当前帧目标位置与上一帧位置：
 * 1. 距离 > 100px → 直接拒绝 (物理上不可能的跳变)
 * 2. 距离 > 50px 且速度 < 20 → 拒绝 (距离大但速度不支持)
 * 3. 距离 > 30px 且位移方向与速度方向矛盾 → 拒绝 (误关联)
 *
 * @return true = 跳变, 应忽略此帧; false = 正常
 */
bool detect_position_jump(const DetectionBox_t *target)
{
    int dx = target_cx - last_cx;
    int dy = target_cy - last_cy;
    int dist = abs(dx) + abs(dy);         /* 曼哈顿距离 */
    int vel  = abs(target->vx) + abs(target->vy);
    
    /* 检查 1: 超大跳变, 物理不可能 */
    if (dist > JUMP_ABSOLUTE_MAX)
        return true;
    
    /* 检查 2: 距离大但速度小, 说明是错误关联 */
    if (dist > JUMP_SPEED_CHECK && vel < JUMP_MIN_SPEED)
        return true;
    
    /* 检查 3: 位移方向与速度方向矛盾 (中等跳变) */
    if (dist > JUMP_DIRECTION_CHECK) {
        if ((dx < -20 && target->vx > 5) || (dx > 20 && target->vx < -5) ||
            (dy < -20 && target->vy > 5) || (dy > 20 && target->vy < -5))
            return true;
    }
    
    return false;  /* 通过所有检查, 正常 */
}
```

> **经验总结**：此逻辑直接从现有实测代码提取。实际运行中，最常触发的是"检查 2"——DSP 因为人脸相似度高，偶尔把两个相距较远的人的 track_id 关联在一起。

#### 4.3.4 状态转移条件表

| 当前状态   | 事件                                        | 下一状态   | 动作                                         |
| ---------- | ------------------------------------------- | ---------- | -------------------------------------------- |
| IDLE       | 检测到已确认目标                            | FOLLOW     | 锁定 active_id, 初始化控制器, 首帧限幅       |
| FOLLOW     | active_id 匹配 + miss_count==0              | FOLLOW     | 正常 P 控制 (100% Kp)                        |
| FOLLOW     | active_id 匹配 + miss_count>0 (coast)       | FOLLOW     | 降增益 P 控制 (30% Kp) 或保持位置            |
| FOLLOW     | active_id 匹配 + 位置跳变检测触发           | FOLLOW     | 忽略此帧数据, 保持上一帧控制输出             |
| FOLLOW     | active_id 在列表中消失 (DSP 删除 Track)     | PREDICTING | 保存最后位置/速度/边缘标记, coast_count=0    |
| PREDICTING | 原 active_id 重新出现 + 位置偏差 ≤ 50px     | FOLLOW     | 位置验证通过后恢复, 清除运动补偿状态         |
| PREDICTING | 原 active_id 出现但位置偏差 > 50px          | PREDICTING | 拒绝恢复 (可能是 ID 误分配), 继续等待        |
| PREDICTING | 等待 > 5 控制周期 (250ms)                   | LOST       | 清除 active_id, 记录 lost_cx/cy + edge_flags |
| LOST       | 原 ID 回来 (≥50ms) + 位置 ≤ 40px            | FOLLOW     | 位置一致性检查通过后恢复, 首帧限幅           |
| LOST       | 原 ID 回来但位置 > 40px                     | LOST       | 拒绝 (可能 DSP 误分配), 降级为新 ID 待遇     |
| LOST       | 新 ID (≥500ms) + 位置 ≤ 60px + 边缘方向匹配 | FOLLOW     | 接受新目标, 首帧限幅                         |
| LOST       | 任意目标 (≥1s, 重新寻找模式)                | FOLLOW     | 放宽限制, 选最近中心的目标                   |
| LOST       | 超时 ≥ 10s                                  | CENTER     | 开始回中运动                                 |
| CENTER     | 检测到新已确认目标                          | FOLLOW     | 锁定新 active_id, 首帧限幅                   |
| CENTER     | 已回到中心位置                              | IDLE       | 等待                                         |
| 任意       | 堵转/越界异常                               | ERROR      | 立即停止电机                                 |
| ERROR      | tracker_reset() 调用                        | IDLE       | 清除全部跟踪状态, 重新开始                   |
| ERROR      | 堵转消除 + 超时 5s                          | CENTER     | 自动恢复, 开始回中运动                       |
| 任意跟踪态 | 100ms 无新帧 (帧超时)                       | (不变)     | 暂停控制输出, 保持最后位置, 帧恢复后继续     |

#### 4.3.5 LOST 状态四级超时策略

```
进入 LOST 状态:
  记录: lost_track_id, lost_cx, lost_cy, lost_edge_flags
  清除: active_id = 0

  时间线:
  ─────────────────────────────────────────────────────────────→ t
  0ms        50ms       500ms       1000ms      10000ms
  │          │          │           │           │
  │ 冷却     │ 接受原ID │ 接受新ID  │ 接受任意  │ 强制回中
  │ (全拒绝) │ (位置≤40)│ (位置≤60  │ (无限制)  │ → CENTER
  │          │          │ +边缘匹配)│           │

① 0~50ms [冷却期]:
   全部拒绝。防止 PREDICTING→LOST 边界上的干扰帧。

② 50ms~500ms [原ID优先期]:
   · 仅接受 lost_track_id (短冷却, 快速恢复)
   · 位置一致性: |target - lost_pos| ≤ 40px (曼哈顿距离)
   · 位置差 > 40px: 拒绝 (可能是 DSP 误分配相同 ID)

③ 500ms~1000ms [新ID接受期]:
   · 接受新 track_id
   · 位置约束: |target - lost_pos| ≤ 60px (允许稍大偏差)
   · 边缘方向匹配: 如果从左边出画面(left_exit), 拒绝从右边进入(right_enter)的目标
     (左出→左入 OK, 左出→右入 拒绝)

④ 1000ms~10000ms [重新寻找期]:
   · 接受任意已确认目标, 无位置限制
   · 选择策略: 最靠近画面中心的目标 (与 IDLE 状态相同)

⑤ > 10000ms [超时回中]:
   · 强制进入 CENTER 状态
   · 缓慢回到初始姿态 (0°, -45°)
```

```c
/* LOST 状态处理伪代码 */
void lost_process(const DetectionBox_t *target, int target_cx, int target_cy)
{
    uint32_t elapsed = now - lost_enter_time;
    bool is_same_id = (target->track_id == lost_track_id && lost_track_id != 0);
    int dist = abs(target_cx - lost_cx) + abs(target_cy - lost_cy);
    bool accept = false;
    
    /* ① 冷却期 */
    if (elapsed < 50) return;
    
    /* ② 原 ID 优先 */
    if (is_same_id && elapsed >= 50) {
        if (dist <= 40) {
            accept = true;
        } else {
            /* 位置差太大, 降级为新 ID */
            is_same_id = false;
        }
    }
    
    /* ③ 新 ID 接受 */
    if (!accept && !is_same_id && elapsed >= 500) {
        if (elapsed < 1000) {
            /* 500ms~1s: 位置+边缘约束 */
            bool pos_ok = (dist <= 60);
            bool edge_ok = check_edge_direction(lost_edge_flags, target->edge_flags);
            accept = pos_ok && edge_ok;
        } else {
            /* ④ >1s: 接受任意 */
            accept = true;
        }
    }
    
    if (accept) {
        lock_target(target);
        change_state(FOLLOW);
    }
}

/* 边缘方向匹配: 左出→右入 = false (不合理) */
bool check_edge_direction(uint8_t lost_flags, uint8_t new_flags)
{
    bool left_exit  = lost_flags & 0x01;
    bool right_exit = lost_flags & 0x02;
    bool left_enter  = new_flags & 0x01;
    bool right_enter = new_flags & 0x02;
    
    /* 从左出去,不应该从右边回来 */
    if ((left_exit && right_enter) || (right_exit && left_enter))
        return false;
    return true;
}
```

> **edge_flags 说明**：`edge_flags` 是 CM4 根据目标最后位置在本地计算的变量（不在通信协议中）。计算方式：`left_exit = (last_cx < 24)`, `right_exit = (last_cx > 136)`, `top_exit = (last_cy < 19)`, `bottom_exit = (last_cy > 109)`（阈值 = 图像尺寸 × 15%，与边缘保护区域一致）。

### 4.4 模块 3：外环控制器 (Outer Loop Controller)

#### 4.4.1 控制流水线

```
输入: 目标框 (x1,y1,x2,y2) from DetectionResult
       kp_scale (1.0=真检测, 0.3=coast 模式, 来自 4.3.2)
       box_area_ratio = (框宽 × 框高) / (160 × 128)

Step 1: 归一化误差
    cx = (x1 + x2) / 2.0 / 160.0    // → [0, 1]
    cy = (y1 + y2) / 2.0 / 128.0    // → [0, 1]
    ex = cx - 0.5                     // 水平误差 (正=目标偏右)
    ey = cy - 0.5                     // 垂直误差 (正=目标偏下)

Step 2: 渐进软死区 (Soft Deadzone)
    DEAD_ZONE = 0.015  (归一化, ~2.4px)
    HALF_DZ = DEAD_ZONE / 2

    对 ex, ey 分别执行:
      |e| < HALF_DZ              →  e = 0              (完全静止)
      HALF_DZ ≤ |e| < DEAD_ZONE →  e = sign(e) × t × |e|
                                    其中 t = (|e| - HALF_DZ) / HALF_DZ
                                    (C0 连续过渡: 边界处 t=1 → 输出=|e|=DZ)
      |e| ≥ DEAD_ZONE            →  e 不变              (正常)

    (对比旧版公式 ratio×HALF_DZ: 在边界处 输出=0.0075…0.015
     存在 2× 不连续跳变; 新公式 t×|e| = 1.0×DZ 消除了此问题)

Step 3: 速度前馈 (Velocity Feedforward)
    利用 DSP 输出的 vx, vy (px/帧) 预测目标下一帧位置:

    vx_norm = (float)target->vx / 160.0    // 归一化速度
    vy_norm = (float)target->vy / 128.0

    // 云台运动补偿 (消除因云台转动产生的虚假速度)
    gimbal_vx = last_delta_pan / FOV_X     // 归一化云台角速度
    gimbal_vy = last_delta_tilt / FOV_Y
    true_vx = vx_norm + gimbal_vx          // 真实目标速度
    true_vy = vy_norm + gimbal_vy

    // 前馈条件: 置信度高 + 真检测 + 速度足够大
    if (kf_confidence > VEL_FF_MIN_CONF
        && miss_count == 0
        && (|true_vx| + |true_vy|) > VEL_FF_MIN_SPEED) {
        ex += true_vx × VEL_PREDICT_FACTOR    // 提前半帧预瞄
        ey += true_vy × VEL_PREDICT_FACTOR
    }

    (设计理由: 单纯 P 控制对快速移动目标有生放滞后;
     DSP Kalman 已经提供了高质量的速度估计, 不用白不用;
     云台运动补偿可消除 DSP vx/vy 中的云台分量, 更精确)

Step 4: 自适应 Kp + P 控制
    // 误差维度: 小误差 → 精调 (低 Kp), 大误差 → 快赶 (高 Kp)
    err_mag = √(ex² + ey²)
    err_scale = lerp(KP_ERR_SCALE_MIN, KP_ERR_SCALE_MAX,
                     clamp((err_mag - 0.02) / 0.18, 0, 1))

    // 框大小维度: 大框(近) → 低 Kp, 小框(远) → 高 Kp
    box_scale = lerp(KP_BOX_SCALE_MAX, KP_BOX_SCALE_MIN,
                     clamp((box_area_ratio - 0.01) / 0.29, 0, 1))

    Kp_adaptive = Kp_base × err_scale × box_scale × kp_scale
    (kp_scale 来自 coast 感知: 1.0=真检测, 0.3=coast)

    Δpan  = -Kp_adaptive × ex
    Δtilt = -Kp_adaptive × ey
    (方向符号随实际安装调整)

    // 有效 Kp 范围: 3.0 × 0.6 × 0.7 = 1.26 ~ 3.0 × 1.5 × 1.3 = 5.85
    // coast 时进一步衰减至 ×0.3

Step 5: 首帧限幅 (目标切换保护)
    if (is_first_frame_after_switch) {
        Δpan  = clamp(Δpan,  -0.75°, +0.75°)  // 首帧硬限制 (比步进限幅更严)
        Δtilt = clamp(Δtilt, -0.75°, +0.75°)  // 防止目标切换导致突跳
        is_first_frame_after_switch = false
    }

Step 6: 步进限幅
    Δpan  = clamp(Δpan,  -MAX_STEP_DEG, +MAX_STEP_DEG)
    Δtilt = clamp(Δtilt, -MAX_STEP_DEG, +MAX_STEP_DEG)
    (20Hz × 1.5° = 最大 30°/s, 避免突变)

Step 7: 边缘保护 (防止目标追出画面)
    edge_gain_x = get_edge_gain(target_cx, img_width)
    edge_gain_y = get_edge_gain(target_cy, img_height)

    Δpan  *= edge_gain_x
    Δtilt *= edge_gain_y

Step 8: 累加成目标角度 (增量式控制)
    θ_pan_target  += Δpan
    θ_tilt_target += Δtilt

    ⚠ 注意: 这是增量式 P 控制 —— 每个周期将误差转化为角度增量
    累加到位置, 从而实现连续追随。累加本身已隐含积分特性,
    因此不需要额外的 I 项。

Step 9: 物理限幅
    θ_pan_target  = clamp(θ_pan_target,  -90°, +90°)
    θ_tilt_target = clamp(θ_tilt_target, -60°, -30°)

Step 10: 一阶平滑滤波 (Low-Pass Filter)
    θ_pan_filt  = α × θ_pan_filt_prev  + (1-α) × θ_pan_target
    θ_tilt_filt = α × θ_tilt_filt_prev + (1-α) × θ_tilt_target
    其中 α = 0.7 (可调, 越大越平滑但越滞后)

Step 11: 输出电机位置
    gimbal_set_pan(θ_pan_filt)
    gimbal_set_tilt(θ_tilt_filt)
```

#### 4.4.2 软死区实现

```c
/**
 * @brief 渐进软死区 (Graduated Soft Deadzone)
 *
 * 与硬截断 (|e| < dz → 0) 不同, 软死区在半死区到死区之间
 * 提供 C0 连续过渡, 消除边界抖动。
 *
 *   响应
 *    │        /
 *    │       / ← 正常 (1:1)
 *    │      /
 *    │    ╱  ← 连续过渡 (0 → dz, C0 连续)
 *    │   ╱
 *    │──╱─── ← 完全为 0
 *    └──┬──┬──┬── 输入误差
 *       0  H  D
 *       H = HALF_DZ, D = DEAD_ZONE
 *
 * 边界连续性证明:
 *   |e| = HALF_DZ: t=0 → output = 0   (与静止区接续 ✔)
 *   |e| = DZ:      t=1 → output = DZ  (与正常区接续 ✔)
 */
float soft_deadzone(float e, float dz)
{
    float half_dz = dz * 0.5f;
    float abs_e = fabsf(e);
    
    if (abs_e < half_dz) {
        return 0.0f;                      /* 完全静止 */
    } else if (abs_e < dz) {
        float t = (abs_e - half_dz) / half_dz;  /* [0, 1] */
        return copysignf(t * abs_e, e);   /* C0 连续: 边界处输出 = dz */
    } else {
        return e;                          /* 正常 */
    }
}
```

#### 4.4.3 边缘保护实现

```c
/**
 * @brief 边缘保护: 目标靠近画面边缘时衰减追踪力度
 *
 * 防止追过头导致目标出画面。实测证明这是"防丢"的基础安全机制,
 * 而非可选优化。
 */
#define EDGE_RATIO    0.15f   /* 边缘区域占图像比例 (15%) */
#define EDGE_GAIN_MIN 0.3f    /* 边缘时的最低增益 */

float get_edge_gain(float pos, float size)
{
    float edge_zone = size * EDGE_RATIO;
    
    if (pos < edge_zone) {
        /* 靠近左/上边缘 */
        return EDGE_GAIN_MIN + (1.0f - EDGE_GAIN_MIN) * (pos / edge_zone);
    } else if (pos > size - edge_zone) {
        /* 靠近右/下边缘 */
        return EDGE_GAIN_MIN + (1.0f - EDGE_GAIN_MIN) * ((size - pos) / edge_zone);
    }
    return 1.0f;  /* 中部区域, 不衰减 */
}
```

#### 4.4.4 自适应增益实现

```c
/**
 * @brief 双维度自适应 Kp
 *
 * 维度 1 — 误差大小:
 *   小误差 (接近中心): Kp 低 → 精调, 避免振荡
 *   大误差 (偏移大):   Kp 高 → 快速赶上
 *
 * 维度 2 — 框大小:
 *   大框 (目标近): Kp 低 → 同样像素偏差对应的角度更小
 *   小框 (目标远): Kp 高 → 需更大角度运动
 *
 * 有效 Kp 范围: Kp_base × [0.6, 1.5] × [0.7, 1.3]
 *                  = 3.0 × [0.42, 1.95] = [1.26, 5.85]
 */
#define KP_ERR_SCALE_MIN  0.6f   /* |e| < 0.02 时 */
#define KP_ERR_SCALE_MAX  1.5f   /* |e| > 0.20 时 */
#define KP_BOX_SCALE_MIN  0.7f   /* 框面积占比 > 0.30 时 (近) */
#define KP_BOX_SCALE_MAX  1.3f   /* 框面积占比 < 0.01 时 (远) */

float adaptive_kp(float ex, float ey, float box_area_ratio,
                  float kp_base, float kp_scale)
{
    /* 误差维度 */
    float err_mag = sqrtf(ex * ex + ey * ey);
    float err_t = (err_mag - 0.02f) / 0.18f;
    err_t = fminf(fmaxf(err_t, 0.0f), 1.0f);       /* clamp [0,1] */
    float err_scale = KP_ERR_SCALE_MIN
                    + (KP_ERR_SCALE_MAX - KP_ERR_SCALE_MIN) * err_t;

    /* 框大小维度 (注意: 大框 → 低增益, 所以反向插值) */
    float box_t = (box_area_ratio - 0.01f) / 0.29f;
    box_t = fminf(fmaxf(box_t, 0.0f), 1.0f);
    float box_scale = KP_BOX_SCALE_MAX
                    + (KP_BOX_SCALE_MIN - KP_BOX_SCALE_MAX) * box_t;

    return kp_base * err_scale * box_scale * kp_scale;
}
```

| 场景       | 误差 | 框大小 | err_scale | box_scale | 有效 Kp |
| ---------- | ---- | ------ | --------- | --------- | ------- |
| 远处小偏差 | 0.01 | 0.005  | 0.6       | 1.3       | 2.34    |
| 近处小偏差 | 0.01 | 0.20   | 0.6       | 0.82      | 1.48    |
| 远处大偏差 | 0.30 | 0.005  | 1.5       | 1.3       | 5.85    |
| 近处大偏差 | 0.30 | 0.20   | 1.5       | 0.82      | 3.69    |

#### 4.4.5 速度前馈实现

```c
/**
 * @brief 速度前馈: 利用 DSP Kalman 输出的速度预测目标下一帧位置
 *
 * 两个关键点:
 * 1. 云台运动补偿: DSP 的 vx/vy 包含云台运动分量,
 *    需要减去才能得到目标真实速度
 * 2. 置信度门控: 仅在 kf_confidence 足够高时启用,
 *    避免早期 Track (速度估计不准) 的错误前馈
 */
#define FOV_X              70.0f  /* OV5640 水平视场角 (°) */
#define FOV_Y              56.0f  /* OV5640 垂直视场角 (°) */
#define VEL_PREDICT_FACTOR 0.5f   /* 前馈系数 (预测半帧, 保守) */
#define VEL_FF_MIN_CONF    70     /* 最低置信度 (启用前馈的门槛) */
#define VEL_FF_MIN_SPEED   0.01f  /* 最小归一化速度 (过滤噪声) */

void velocity_feedforward(float *ex, float *ey,
                          const DetectionBox_t *target,
                          float last_delta_pan, float last_delta_tilt)
{
    /* 仅对真检测 (miss_count==0) 且高置信度时启用 */
    if (target->miss_count > 0 || target->kf_confidence < VEL_FF_MIN_CONF)
        return;

    /* 归一化 DSP 速度 */
    float vx_norm = (float)target->vx / 160.0f;
    float vy_norm = (float)target->vy / 128.0f;

    /* 云台运动补偿: 消除 DSP 视觉速度中的云台分量 */
    float gimbal_vx = last_delta_pan  / FOV_X;
    float gimbal_vy = last_delta_tilt / FOV_Y;
    float true_vx = vx_norm + gimbal_vx;
    float true_vy = vy_norm + gimbal_vy;

    /* 速度过小时不前馈 (避免噪声诱导的微动) */
    if (fabsf(true_vx) + fabsf(true_vy) < VEL_FF_MIN_SPEED)
        return;

    /* 前馈: 预测目标下一帧位置 (VEL_PREDICT_FACTOR=0.5 → 半帧预瞄) */
    *ex += true_vx * VEL_PREDICT_FACTOR;
    *ey += true_vy * VEL_PREDICT_FACTOR;
}
```

> **FOV 标定说明**：`FOV_X = 70°` 和 `FOV_Y = 56°` 是 OV5640 在 160×128 裁切下的近似值。如果实测发现云台运动补偿过大/过小，应根据实际镜头数据调整。速度前馈的 `VEL_PREDICT_FACTOR = 0.5` 取保守值，实测可在 0.3~0.8 范围内调优。

#### 4.4.6 与现有实现的对比

| 特性       | 现有实现 (target_tracker.c)   | v3 方案                     | 评估                     |
| ---------- | ----------------------------- | --------------------------- | ------------------------ |
| 控制器类型 | 自适应 PID (Kp,Ki,Kd)         | 自适应 P 控制               | **去掉 I/D, 保留自适应** |
| 误差空间   | 像素坐标 (0-320)              | 归一化 (0-1)                | 更通用                   |
| 死区       | 渐进软死区                    | ✅ 渐进软死区 (C0 连续)      | Bug 已修复               |
| 滤波       | 位置 LPF + 输出 LPF (两级)    | 位置累加 + 单级 LPF         | 等效                     |
| 跳变检测   | 多准则 (3 级)                 | ✅ 多准则 (3 级, 保留)       | 一致                     |
| 边缘保护   | 有                            | ✅ 纳入 v3.0                 | 一致                     |
| 首帧限幅   | 有 (1.5°)                     | ✅ 纳入 v3.0 (0.75°, 更严格) | 改进                     |
| coast 感知 | ❌ (无法区分真检测/预测)       | ✅ miss_count 降增益         | **v3 新增**              |
| 自适应增益 | 按误差大小 + 框大小双维度调整 | ✅ 双维度自适应 Kp           | 一致                     |
| 速度补偿   | 云台运动补偿 (FOV 映射)       | ✅ DSP vx/vy + 云台补偿      | **v3 纳入**              |

> **设计决策**: v3.0 将现有实测代码中验证过的**所有实用机制**全部纳入，包括自适应增益和速度前馈。相比现有实现，v3 去掉了 I/D 项（以累加式 P + LPF 替代），新增了 DSP coast 感知，且软死区修复了 C0 连续性 bug。

### 4.5 模块 4：云台驱动 (Gimbal Control)

#### 4.5.1 电机映射

```
总线舵机参数:
  脉冲范围: 0 - 1000
  角度范围: 0° - 240° (0=0°, 500=120°, 1000=240°)
  分辨率: POS_PER_DEG = 1000/240 ≈ 4.1667 pulse/°

安装约定 (以中点 500 = 0° 为参考):
  Pan  (ID6):  -90° ~ +90°  → 脉冲 125 ~ 875
  Tilt (ID4):  -60° ~ -30°  → 脉冲 250 ~ 375

角度→脉冲:
  pos = 500 + angle × 4.1667
  (限制 pos ∈ [0, 1000])

初始姿态:
  Pan  = 0°   → 脉冲 500 (正前方)
  Tilt = -45° → 脉冲 312 (略微俯视)
```

#### 4.5.2 运动时间匹配

```
控制周期 = 50ms (20Hz)
电机运动时间 = 50ms (与控制周期匹配)

关键要求：运动时间 ≤ 控制周期
  - 若运动时间 > 控制周期：上一次运动未完成就被中断，导致抖动
  - 若运动时间 < 控制周期：电机到位后空等，浪费跟踪时间
  - 最佳：运动时间 = 控制周期 = 50ms
```

#### 4.5.3 安全保护

```c
/* ===== 堵转检测 ===== */
#define STALL_CHECK_WINDOW   10  /* 连续 10 个控制周期 (500ms) */
#define STALL_POS_THRESHOLD  1.0f  /* 反馈角度变化 < 1° */

static int   s_stall_count = 0;
static float s_stall_ref_pos = 0.0f;
static float s_stall_cmd_dir = 0.0f;  /* 指令方向 (正/负) */

void safety_check(float cmd_angle, float feedback_angle)
{
    /* 检查：指令持续单方向移动，但反馈几乎不变 */
    if (s_stall_count == 0) {
        s_stall_ref_pos = feedback_angle;
        s_stall_cmd_dir = cmd_angle - feedback_angle;
        s_stall_count = 1;
        return;
    }
    
    float pos_change = fabsf(feedback_angle - s_stall_ref_pos);
    bool cmd_same_dir = ((cmd_angle - feedback_angle) * s_stall_cmd_dir > 0);
    
    if (cmd_same_dir && pos_change < STALL_POS_THRESHOLD) {
        s_stall_count++;
        if (s_stall_count >= STALL_CHECK_WINDOW) {
            enter_error_state();  /* 堵转! 停止电机 */
        }
    } else {
        s_stall_count = 0;  /* 重置 */
    }
}

/* ===== 越界保护 ===== */
/* 在 gimbal_set_pan/tilt 中已有硬限幅, 这里是冗余保护 */
if (feedback_pan < -95.0f || feedback_pan > 95.0f ||
    feedback_tilt < -65.0f || feedback_tilt > -25.0f) {
    enter_error_state();  /* 物理越界! */
}
```

---

## 5. 关键设计决策与权衡

### 5.1 为什么用自适应 P 控制而非 PID？

| 分析     | 结论                                                                                              |
| -------- | ------------------------------------------------------------------------------------------------- |
| **I 项** | 云台跟踪是追随任务（目标始终在动），不是定位任务。I 项会积累误差导致过冲。且累加式 P 已隐含积分。 |
| **D 项** | 20Hz 采样率下，检测框本身有噪声，D 项会放大噪声导致抖动。                                         |
| **P 项** | 配合 LPF + 自适应增益即可实现平滑跟踪。双维度自适应替代了固定 Kp 的局限性。                       |
| **前馈** | 利用 DSP 的 Kalman 速度估计做前馈补偿，比 D 项更精确，且无噪声放大问题。                          |

v3.0 的控制策略：自适应 P + 速度前馈 + LPF，兼顾了响应速度和平滑度。

### 5.2 DSP Coast vs. CM4 PREDICTING 的分工

升级完整 SORT 后，DSP coast（miss_count > 0）与 CM4 PREDICTING 职责需要仔细区分：

| 阶段                        | 谁在工作        | 云台行为                 | 持续时间    |
| --------------------------- | --------------- | ------------------------ | ----------- |
| 正常跟踪 (miss_count==0)    | DSP CNN + CM4 P | 正常 P 控制 (100% Kp)    | 持续        |
| DSP coast (0 < miss ≤ 15)   | DSP Kalman预测  | CM4 降增益跟随 (30% Kp)  | 0~750ms     |
| DSP coast + 超过 5 帧无控制 | DSP Kalman预测  | CM4 保持最后位置, 不控制 | 250ms~750ms |
| DSP 删除 Track (miss > 15)  | 无              | CM4 PREDICTING → LOST    | 250ms       |

**为什么不全力跟 coast 框？** 实测发现：

| 问题             | 原因                                         |
| ---------------- | -------------------------------------------- |
| 预测位置误差大   | 纯惯性预测，无 CNN 观测校正，精度随帧数衰减  |
| 预测导致云台跑偏 | 错误预测 → 云台大幅偏移 → 目标重现时在画面外 |
| 恢复困难         | 目标重现位置与预测位置差异大，可能被拒绝恢复 |

**v3 方案**：coast 前 5 帧弱跟随（30% Kp，万一预测对了能小幅修正），之后保持最后位置不动，等待目标重现或 DSP 删除 Track。

### 5.3 为什么 v3.0 必须纳入边缘保护和首帧限幅？

这两个机制不是"锦上添花"的优化，而是**防止基础故障**的安全逻辑：

| 机制     | 防止的问题                                                     |
| -------- | -------------------------------------------------------------- |
| 边缘保护 | 目标在画面边缘时控制器过冲 → 追出画面 → 丢失 → 回中 (体验极差) |
| 首帧限幅 | 目标切换瞬间误差突变 → 云台猛甩 → 机械冲击 + 跟踪中断          |
| 软死区   | 目标在死区边缘时 on/off 抖动 → 云台微颤 (用户可见)             |

### 5.4 Active ID 粘滞 vs. 抢占

| 场景               | 粘滞策略行为               | 抢占策略行为     |
| ------------------ | -------------------------- | ---------------- |
| 跟踪中，新人入画   | 忽略新人，继续跟原目标     | 可能切到新人     |
| 跟踪中，原目标侧脸 | 等待几帧后恢复             | 可能切到旁边的人 |
| 原目标走出画面     | LOST → CENTER → 重选新目标 | 立即切到新人     |

**v3 选择粘滞策略**：以稳定性为优先。用户体验是"锁定一个人不放"，而非"总在人群中跳来跳去"。

---

## 6. 参数总表

### 6.1 DSP 参数 — SORT 跟踪

| 参数                       | 值   | 说明                          |
| -------------------------- | ---- | ----------------------------- |
| `MAX_TRACK_SLOTS`          | 5    | 最大同时跟踪目标数            |
| `MAX_DETECTIONS_PER_FRAME` | 10   | 每帧最大检测数                |
| `TRACK_MAX_MISS`           | 15   | 漏检容忍帧数 (~750ms @20FPS)  |
| `IOU_MIN_THRESHOLD`        | 0.25 | 匈牙利匹配后 IoU 有效门限     |
| `MIN_HITS`                 | 3    | 新 Track 确认所需连续命中帧数 |
| `confidence`               | 0.6  | 最小检测置信度                |
| `DSP_MIN_BOX_SIZE`         | 16   | 最小边界框尺寸 (像素)         |

### 6.1.1 DSP 参数 — 8D Kalman 噪声

| 参数               | 维度        | 值      | 说明                               |
| ------------------ | ----------- | ------- | ---------------------------------- |
| $\sigma^2_{Q,pos}$ | cx, cy      | 1.0     | 过程噪声 — 位置                    |
| $\sigma^2_{Q,s}$   | s           | 0.01    | 过程噪声 — 尺度                    |
| $\sigma^2_{Q,r}$   | r           | 0.0001  | 过程噪声 — 宽高比                  |
| $\sigma^2_{Q,vel}$ | vx,vy,vs,vr | 0.01    | 过程噪声 — 速度分量                |
| $\sigma^2_{R,pos}$ | cx, cy      | 1.0     | 观测噪声 — 位置                    |
| $\sigma^2_{R,s}$   | s           | 10.0    | 观测噪声 — 尺度 (检测框面积噪声大) |
| $\sigma^2_{R,r}$   | r           | 0.01    | 观测噪声 — 宽高比                  |
| $P_{0,pos}$        | cx, cy      | 10.0    | 初始协方差 — 位置                  |
| $P_{0,s}$          | s           | 10.0    | 初始协方差 — 尺度                  |
| $P_{0,vel}$        | vx,vy,vs,vr | 10000.0 | 初始协方差 — 速度 (高不确定性)     |

### 6.2 CM4 控制参数

| 参数                | 值     | 单位    | 说明                             |
| ------------------- | ------ | ------- | -------------------------------- |
| `CONTROL_FREQ`      | 20     | Hz      | 控制周期 (= 1/50ms)              |
| `Kp_base`           | 3.0    | °/norm  | 基础 P 增益 (自适应缩放前)       |
| `KP_ERR_SCALE_MIN`  | 0.6    | -       | 误差<0.02 时的 Kp 缩放           |
| `KP_ERR_SCALE_MAX`  | 1.5    | -       | 误差>0.20 时的 Kp 缩放           |
| `KP_BOX_SCALE_MIN`  | 0.7    | -       | 大框(近目标)的 Kp 缩放           |
| `KP_BOX_SCALE_MAX`  | 1.3    | -       | 小框(远目标)的 Kp 缩放           |
| `DEAD_ZONE`         | 0.015  | norm    | 归一化死区 (~2.4px)              |
| `HALF_DZ`           | 0.0075 | norm    | = DEAD_ZONE/2 (派生参数)         |
| `MAX_STEP_DEG`      | 1.5    | °/cycle | 单周期最大步进 (→30°/s)          |
| `FIRST_MOVE_LIMIT`  | 0.75   | °       | 首帧最大输出 (< MAX_STEP)        |
| `ALPHA_SMOOTH`      | 0.7    | -       | LPF 系数 (越大越平滑)            |
| `MOVE_TIME_MS`      | 50     | ms      | 电机运动时间 = 控制周期          |
| `EDGE_RATIO`        | 0.15   | -       | 边缘保护区域占画面比例 (15%)     |
| `EDGE_GAIN_MIN`     | 0.3    | -       | 边缘区域最低增益                 |
| `COAST_GAIN_FACTOR` | 0.3    | -       | DSP coast 模式增益缩放 (30%)     |
| `MAX_COAST_FOLLOW`  | 5      | 帧      | coast 最多跟随帧数, 之后保持位置 |

### 6.3 CM4 速度前馈参数

| 参数                   | 值   | 单位 | 说明                                 |
| ---------------------- | ---- | ---- | ------------------------------------ |
| `FOV_X`                | 70.0 | °    | OV5640 水平视场角 (需实测校准)       |
| `FOV_Y`                | 56.0 | °    | OV5640 垂直视场角                    |
| `VEL_PREDICT_FACTOR`   | 0.5  | -    | 前馈系数 (半帧预瞄, 可调0.3~0.8)     |
| `VEL_FF_MIN_CONF`      | 70   | -    | Kalman 置信度门槛 (启用前馈的最低值) |
| `VEL_FF_MIN_SPEED`     | 0.01 | norm | 最小归一化速度 (过滤噪声)            |
| `FRAME_TIMEOUT_MS`     | 100  | ms   | 帧超时检测阈值                       |
| `ERROR_AUTO_RECOVER_S` | 5    | s    | ERROR 状态自动恢复超时               |

### 6.4 CM4 跳变检测参数

| 参数                   | 值  | 单位  | 说明                          |
| ---------------------- | --- | ----- | ----------------------------- |
| `JUMP_ABSOLUTE_MAX`    | 100 | px    | 曼哈顿距离 > 此值: 无条件拒绝 |
| `JUMP_SPEED_CHECK`     | 50  | px    | 曼哈顿距离 > 此值: 需速度验证 |
| `JUMP_DIRECTION_CHECK` | 30  | px    | 曼哈顿距离 > 此值: 需方向验证 |
| `JUMP_MIN_SPEED`       | 20  | px/帧 | 速度验证的最小速度            |

### 6.5 CM4 状态机参数

| 参数                       | 值    | 单位    | 说明                            |
| -------------------------- | ----- | ------- | ------------------------------- |
| `BOX_CONFIRM_FRAMES`       | 2     | 帧      | 目标确认需要的连续帧数          |
| `BOX_CONFIRM_WINDOW_MS`    | 300   | ms      | 确认窗口时间                    |
| `MAX_PREDICT_FRAMES`       | 5     | 帧      | 预测等待帧数 (250ms)            |
| `LOST_COOLDOWN_SAME_ID_MS` | 50    | ms      | 原 ID 恢复冷却时间              |
| `LOST_COOLDOWN_NEW_ID_MS`  | 500   | ms      | 新 ID 替换冷却时间              |
| `LOST_REACQUIRE_MS`        | 1000  | ms      | LOST 接受任意目标超时           |
| `LOST_RETURN_HOME_MS`      | 10000 | ms      | LOST→CENTER 强制回中超时        |
| `SAME_ID_MAX_POS_DIFF`     | 40    | px      | 原 ID 恢复最大位置偏差 (曼哈顿) |
| `NEW_ID_MAX_POS_DIFF`      | 60    | px      | 新 ID 接受最大位置偏差          |
| `PREDICT_RECOVER_MAX_JUMP` | 50    | px      | PREDICTING 恢复最大位置偏差     |
| `CENTER_SPEED`             | 0.5   | °/cycle | 回中速度                        |

### 6.6 安全参数

| 参数                  | 值      | 说明                 |
| --------------------- | ------- | -------------------- |
| `STALL_CHECK_WINDOW`  | 10      | 堵转检测窗口 (500ms) |
| `STALL_POS_THRESHOLD` | 1.0     | 堵转位置变化阈值 (°) |
| `PAN_LIMIT`           | ±90     | Pan 物理限位 (°)     |
| `TILT_LIMIT`          | -60~-30 | Tilt 物理限位 (°)    |

---

## 7. SOT 单目标跟踪模式 (PTZ 专用)

> **版本**: v3.1 新增  
> **适用场景**: PTZ 云台自拍杆/单人跟踪  
> **实现文件**: `sot_tracker.h/c`, `appearance_feat.h/c`, `color_hist.h/c`

### 7.1 设计背景

SORT/ByteTrack 等 MOT 算法在 **PTZ 云台场景**存在固有问题：

| 问题     | 原因                     |
| -------- | ------------------------ |
| IOU 失效 | 云台运动导致帧间位移巨大 |
| 目标跳变 | 多人场景下 ID 漂移       |
| 资源浪费 | 同时跟踪多目标占用计算   |

业界实践 (DJI、海康、商汤) 在类似场景采用 **SOT + ReID** 架构：
- 单目标跟踪 (SOT) 减少 30-40% 计算量
- 轻量级外观特征解决重捕获问题
- 专为 PTZ 场景设计的状态机

### 7.2 SOT 整体架构

```
┌──────────────────────────────────────────────────────────────┐
│                    SOT Tracker Pipeline                       │
│                                                              │
│  ┌──────────┐    ┌──────────────┐    ┌──────────────────┐   │
│  │ 检测输入  │ →  │ 目标选择器   │ →  │ 外观特征提取     │   │
│  │ (CNN)    │    │ Center/Large │    │ 128-dim          │   │
│  └──────────┘    │ /Manual      │    │ HSV+Contrast+LBP │   │
│                  └──────────────┘    └────────┬─────────┘   │
│                                               │             │
│  ┌──────────┐    ┌──────────────┐    ┌────────▼─────────┐   │
│  │  输出    │ ←  │  状态机      │ ←  │  匹配打分        │   │
│  │ selected │    │ 4态 + ReID  │    │ IOU+Appear+Dist  │   │
│  │ _idx     │    │ + Coast     │    │ 融合评分         │   │
│  └──────────┘    └──────────────┘    └──────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

### 7.3 128 维外观特征设计

特征向量结构 (128 bytes, 每维 uint8_t 归一化):

| 分量       | 维度    | 描述                          | 计算复杂度  |
| ---------- | ------- | ----------------------------- | ----------- |
| HSV 直方图 | 84      | H:32 + S:26 + V:26 联合直方图 | O(n)        |
| 对比度特征 | 12      | 3×4 块局部对比度              | O(n/12)     |
| 形状特征   | 8       | 宽高比 + 面积 + 纵横向梯度    | O(1)        |
| uLBP 纹理  | 24      | Uniform LBP 主模式统计        | O(n)        |
| **总计**   | **128** |                               | ~0.5ms/目标 |

**设计决策**：

1. **HSV vs RGB**: HSV 分离亮度与色相，对光照变化更鲁棒
2. **uLBP vs LBP**: Uniform 模式只保留 58+1 个主模式，计算量降至 1/4
3. **定点化**: 全 uint8_t，避免 DSP 浮点运算瓶颈

```c
/* 特征结构体定义 */
typedef struct {
    uint8_t hsv_hist[84];      /* HSV 联合直方图 */
    uint8_t contrast[12];      /* 3×4 块对比度 */
    uint8_t shape[8];          /* 形状描述子 */
    uint8_t ulbp[24];          /* Uniform LBP */
} AppearanceFeat_t;            /* 128 bytes */

/* 相似度计算 (0-255) */
uint8_t appearance_similarity(const AppearanceFeat_t *a,
                              const AppearanceFeat_t *b);

/* EMA 在线更新 */
void appearance_ema_update(AppearanceFeat_t *tgt,
                           const AppearanceFeat_t *obs,
                           int alpha_shift);  /* alpha = 1/2^shift */
```

### 7.4 SOT 状态机

```
                    首次选中
        IDLE ─────────────────→ TENTATIVE
          ↑                         │
          │                    连续3帧确认
     超时回中                       ↓
          │       ┌──────────← TRACKING
          │       │    正常匹配/外观匹配
          │       │              │
        LOST ←────┤         漏检>5帧
          │       │              │
          │ ReID成功             ↓
          └───────┴─────── (继续coast)
```

| 状态      | 含义   | 进入条件    | 退出条件           |
| --------- | ------ | ----------- | ------------------ |
| IDLE      | 无目标 | 初始化/超时 | 检测到目标         |
| TENTATIVE | 待确认 | 首次选中    | 连续3帧 → TRACKING |
| TRACKING  | 跟踪中 | 目标锁定    | 漏检>5帧 → LOST    |
| LOST      | 重捕获 | 目标丢失    | ReID成功/超时回中  |

### 7.5 匹配打分机制

三维融合评分:

$$
S_{match} = w_1 \cdot S_{iou} + w_2 \cdot S_{appear} + w_3 \cdot (1 - S_{dist})
$$

默认权重: $w_1=0.3$, $w_2=0.5$, $w_3=0.2$

| 分量         | 计算方式                           | 范围    |
| ------------ | ---------------------------------- | ------- |
| $S_{iou}$    | `iou_compute(pred_box, det_box)`   | 0.0-1.0 |
| $S_{appear}$ | `appearance_similarity() / 255.0`  | 0.0-1.0 |
| $S_{dist}$   | `min(manhattan_dist / 100.0, 1.0)` | 0.0-1.0 |

匹配门限:
- `SOT_MATCH_THRESH = 0.4` — 正常匹配最低分
- `SOT_REID_THRESH = 0.6` — ReID 重捕获最低分 (更严格)

### 7.6 目标选择策略

```c
typedef enum {
    SOT_SELECT_CENTER,   /* 默认: 选最靠近画面中心 */
    SOT_SELECT_LARGEST,  /* 选最大目标 */
    SOT_SELECT_MANUAL    /* 手动指定 det_idx */
} SotSelectMode_e;
```

### 7.7 Coast 处理

漏检时的 Coast (惯性滑行) 策略:

| 帧数 | 动作                      | 速度衰减   |
| ---- | ------------------------- | ---------- |
| 1-5  | Kalman predict + 速度衰减 | 0.85^frame |
| 6-15 | 保持最后位置              | 停止预测   |
| >15  | 状态 → LOST               | 启动 ReID  |

### 7.8 与 SORT 模式对比

| 特性       | SORT (MOT)           | SOT            |
| ---------- | -------------------- | -------------- |
| 跟踪目标数 | 最多 5 个            | 仅 1 个        |
| 计算量     | 5×Kalman + 匈牙利    | 1×简化Kalman   |
| 性能节省   | 基准                 | **-30%**       |
| 外观特征   | 可选 (16B ColorHist) | 必选 (128B)    |
| ReID 能力  | 弱 (仅IoU)           | 强 (外观主导)  |
| 适用场景   | 监控/多人            | **PTZ/自拍杆** |

### 7.9 API 接口

```c
/* 初始化 */
void sot_tracker_init(SotSelectMode_e mode);

/* 每帧处理 (会修改 result->selected_idx) */
void sot_tracker_process(DetectionResult_t *result,
                         const uint8_t *bgr_image);

/* 手动指定目标 */
bool sot_tracker_select_target(int det_idx,
                               const DetectionResult_t *result,
                               const uint8_t *bgr_image);

/* 查询状态 */
SotState_e sot_tracker_get_state(void);
uint8_t sot_tracker_get_target_id(void);
```

### 7.10 SOT 参数

| 参数                  | 值   | 说明           |
| --------------------- | ---- | -------------- |
| `SOT_TENTATIVE_HITS`  | 3    | 确认所需连续帧 |
| `SOT_MAX_COAST`       | 15   | 最大coast帧数  |
| `SOT_MATCH_THRESH`    | 0.4  | 匹配门限       |
| `SOT_REID_THRESH`     | 0.6  | ReID门限       |
| `SOT_LOST_TIMEOUT_MS` | 3000 | LOST超时       |
| `APPEAR_EMA_SHIFT`    | 3    | EMA α=1/8      |
| `SOT_VEL_DECAY`       | 0.85 | coast速度衰减  |

### 7.11 双向通信协议 (v3.0 升级)

协议版本升级至 `0x0300`，支持 CM4 控制 DSP 跟踪器启停。

#### 7.11.1 CM4 → DSP 控制命令

| 命令              | 消息值       | 说明                       |
| ----------------- | ------------ | -------------------------- |
| `START_TRACK`     | `0x80000001` | 启动跟踪（启用 SOT）       |
| `STOP_TRACK`      | `0x80000002` | 停止跟踪（仅检测模式）     |
| `RESET_TRACK`     | `0x80000003` | 重置跟踪器（清除当前目标） |
| `SELECT_TARGET`   | `0x8000001X` | 手动选择目标（X=det_idx）  |
| `SET_SELECT_MODE` | `0x8000002X` | 设置选择模式（X=0/1/2）    |
| `PING`            | `0x800000FF` | 心跳/状态查询              |

**CM4 发送示例**:
```c
/* 启动跟踪 */
mailbox_write(MAILBOX_CMD_START_TRACK);

/* 停止跟踪 */
mailbox_write(MAILBOX_CMD_STOP_TRACK);

/* 手动选择第 2 个检测框 */
mailbox_write(MAILBOX_CMD_SELECT_TARGET | 2);

/* 设置为"选择最大目标"模式 */
mailbox_write(MAILBOX_CMD_SET_SELECT_MODE | 1);
```

#### 7.11.2 DSP → CM4 状态上报

`DetectionResult_t` 结构体升级（保持 704 字节），原 `timestamp` 拆分：

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* 0x44455446 */
    uint32_t version;         /* 0x0300 (v3.0) */
    uint32_t frame_id;
    uint16_t timestamp_ms;    /* 时间戳低16位 */
    uint8_t  tracker_state;   /* TrackerState_e */
    uint8_t  tracker_flags;   /* 标志位 */
    uint32_t count;
    int32_t  selected_idx;
    DetectionBox_t boxes[10];
} DetectionResult_t;
```

**跟踪器状态枚举**:
```c
typedef enum {
    TRACKER_STATE_DISABLED  = 0,  /* 跟踪器禁用（仅检测） */
    TRACKER_STATE_IDLE      = 1,  /* 等待目标 */
    TRACKER_STATE_TENTATIVE = 2,  /* 试探中 */
    TRACKER_STATE_TRACKING  = 3,  /* 稳定跟踪 */
    TRACKER_STATE_LOST      = 4,  /* 目标丢失 */
} TrackerState_e;
```

**标志位定义**:
| 位  | 宏名                        | 含义             |
| --- | --------------------------- | ---------------- |
| 0   | `TRACKER_FLAG_ENABLED`      | 跟踪器已启用     |
| 1   | `TRACKER_FLAG_REID_ACTIVE`  | 正在 ReID 搜索   |
| 2   | `TRACKER_FLAG_COASTING`     | 目标正在惯性预测 |
| 3   | `TRACKER_FLAG_APPEAR_VALID` | 外观特征有效     |

#### 7.11.3 数据链路时序

```
CM4                              DSP
 │                                │
 │  ──── START_TRACK ─────────►   │ 启用 SOT 跟踪器
 │                                │ state = IDLE
 │                                │
 │  ◄─── DetectionResult ─────    │ 每帧发送
 │       tracker_state=1 (IDLE)   │
 │                                │
 │  [目标出现]                    │
 │  ◄─── DetectionResult ─────    │ 检测到目标
 │       tracker_state=2          │
 │       selected_idx=0           │
 │                                │
 │  [连续3帧确认]                 │
 │  ◄─── DetectionResult ─────    │ 稳定跟踪
 │       tracker_state=3          │
 │       selected_idx=0           │
 │                                │
 │  ──── STOP_TRACK ──────────►   │ 停止跟踪
 │                                │ state = DISABLED
 │                                │
 │  ◄─── DetectionResult ─────    │ 仅检测模式
 │       tracker_state=0          │
 │       selected_idx=-1          │
```

#### 7.11.4 实现文件

| 文件              | 说明                           |
| ----------------- | ------------------------------ |
| `protocol.h`      | 协议定义（命令、状态、标志位） |
| `tracker_cmd.h/c` | 命令处理模块                   |
| `pipeline.c`      | 集成命令轮询和状态填充         |

### 7.12 云台运动补偿 (v3.2 新增)

在 PTZ 跟踪场景中，云台转动会导致图像整体位移，使得纯视觉速度估计失效。
v3.2 新增 CM4→DSP 云台角速度传输机制，实现精确的运动补偿。

#### 7.12.1 问题描述

```
场景：云台向右转 0.5°/帧，目标静止

无补偿时:
  DSP 检测: 目标向左移动了 ~1.1 像素 (0.5° / 70° × 160)
  Kalman: vx = -1.1 px/帧 (错误！目标实际静止)
  预测: 下一帧目标在更左边 → 预测错误 → IOU 降低

有补偿时:
  DSP 收到: gimbal_vel_pan = 0.5°/帧
  预测框补偿: px1,px2 -= 1.1 像素 (向左移)
  检测匹配: 补偿后的预测框与检测框重合 → IOU 正常
```

#### 7.12.2 补偿公式

$$
\Delta_{px,x} = \frac{\omega_{pan}}{FOV_x} \times W_{img}
$$

$$
\Delta_{px,y} = \frac{\omega_{tilt}}{FOV_y} \times H_{img}
$$

| 参数            | 值       | 单位 |
| --------------- | -------- | ---- |
| $\omega_{pan}$  | CM4 传入 | °/帧 |
| $\omega_{tilt}$ | CM4 传入 | °/帧 |
| $FOV_x$         | 70.0     | °    |
| $FOV_y$         | 56.0     | °    |
| $W_{img}$       | 160      | px   |
| $H_{img}$       | 120      | px   |

#### 7.12.3 角速度来源

CM4 有三种方式获取云台角速度：

| 方案              | 实现                                  | 精度   | 适用场景       |
| ----------------- | ------------------------------------- | ------ | -------------- |
| A. 控制指令       | `pan_vel = last_delta_pan`            | 低     | 三脚架固定     |
| B. 电机反馈差分   | `pan_vel = (pos_now - pos_prev) / dt` | 中     | 室内稳定       |
| **C. IMU 陀螺仪** | `pan_vel = gyro_z`                    | **高** | **手持云台** ✓ |

**手持云台场景 → 必须使用方案 C (IMU)**

原因：
- 手抖频率 1~10Hz，幅度可达 ±5°/s
- 控制指令无法感知外部扰动
- 电机反馈有延迟，无法补偿高频抖动

##### IMU 硬件

**使用 QMI8658A 六轴 IMU**（已在 Gyroscope_Demo 验证）

| 参数       | 值              |
| ---------- | --------------- |
| 型号       | QMI8658A        |
| I2C 地址   | 0x6A            |
| 陀螺量程   | ±2000°/s        |
| 加速度量程 | ±8g             |
| 采样率     | 100Hz           |
| 接口       | 软件 I2C (I2C3) |

**硬件连接 (gimbal_master 板)**:

| 信号     | GPIO   | 说明       |
| -------- | ------ | ---------- |
| I2C3 SCL | GPIOA4 | IMU 时钟线 |
| I2C3 SDA | GPIOA5 | IMU 数据线 |

##### IMU 安装位置

```
┌─────────────────────────────────────┐
│          手持云台结构               │
│                                     │
│    ┌───────────┐                    │
│    │  Camera   │ ← 跟随云台旋转     │
│    └─────┬─────┘                    │
│          │                          │
│    ┌─────┴─────┐                    │
│    │ Pan Motor │                    │
│    └─────┬─────┘                    │
│          │                          │
│    ┌─────┴─────┐                    │
│    │Tilt Motor │                    │
│    └─────┬─────┘                    │
│          │                          │
│    ╔═════╧═════╗                    │
│    ║   IMU     ║ ← 安装在手柄/底座  │
│    ║(QMI8658A) ║   感知手抖         │
│    ╚═══════════╝                    │
│          │                          │
│    ┌─────┴─────┐                    │
│    │  Handle   │ ← 用户手持         │
│    └───────────┘                    │
└─────────────────────────────────────┘

IMU 坐标系 (安装后，参考 Gyroscope_Demo):
  gyro_z → Pan 轴角速度 (°/s)
  gyro_x → Tilt 轴角速度 (°/s)
```

##### CM4 IMU 采集代码

复用已有驱动：`Drivers/External/QMI8658A/`

```c
#include "qmi8658a.h"
#include "imu.h"
#include "i2c_soft.h"

/* 硬件实例 */
static i2c_soft_t  s_i2c;
static qmi8658a_t  s_imu_dev;
static imu_t       s_imu;

/* 初始化 */
void imu_driver_init(void)
{
    /* 软件 I2C 初始化 (GPIOA4=SCL, GPIOA5=SDA) */
    i2c_soft_init(&s_i2c, GPIOA4, GPIOA5, 400000);
    
    /* QMI8658A 初始化 */
    qmi8658a_init(&s_imu_dev, &s_i2c, 0x6A);
    
    /* IMU 姿态解算初始化 */
    imu_init(&s_imu);
    
    /* 静止校准 (启动时保持静止 1 秒) */
    printf("[Info] Calibrating, please keep the device still...\n");
    imu_calibrate(&s_imu, &s_imu_dev, 100);  /* 100 次采样 */
}

/* 每帧更新 (100Hz 调用) */
void imu_driver_update(void)
{
    qmi8658a_data_t raw;
    qmi8658a_read_data(&s_imu_dev, &raw);
    imu_update(&s_imu, &raw);
}

/* 获取云台角速度 (°/帧, 20Hz 控制周期) */
void imu_get_gimbal_vel(float *pan_vel, float *tilt_vel)
{
    /* 从 IMU 获取角速度 (rad/s) → 转换为 °/帧 */
    float gyro_z_rad = s_imu.gyro_z;  /* Pan 轴 */
    float gyro_x_rad = s_imu.gyro_x;  /* Tilt 轴 */
    
    /* rad/s → °/s → °/帧 (20Hz) */
    *pan_vel  = (gyro_z_rad * 57.2958f) / 20.0f;
    *tilt_vel = (gyro_x_rad * 57.2958f) / 20.0f;
}
```

**依赖**: 参考 `s300-bsp/Projects/Demo/Gyroscope_Demo/`

##### 完整角速度获取流程

```c
/* CM4 ctrl_loop.c - 融合云台运动 + 手抖 */

void ctrl_loop_update(void)
{
    /* 1. 读取 IMU (手抖分量) */
    float hand_shake_pan, hand_shake_tilt;
    imu_get_gimbal_vel(&hand_shake_pan, &hand_shake_tilt);
    
    /* 2. P 控制计算 (云台主动运动分量) */
    float delta_pan  = -Kp * soft_deadzone(ex);
    float delta_tilt = -Kp * soft_deadzone(ey);
    
    /* 3. 总角速度 = 云台主动运动 + 手抖补偿 */
    float total_pan_vel  = delta_pan  + hand_shake_pan;
    float total_tilt_vel = delta_tilt + hand_shake_tilt;
    
    /* 4. 发送给 DSP */
    mailbox_write(MAKE_GIMBAL_VEL_CMD(total_pan_vel, total_tilt_vel));
    
    /* 5. 云台控制（抵消手抖） */
    theta_pan  += delta_pan  - hand_shake_pan;   /* 反向补偿 */
    theta_tilt += delta_tilt - hand_shake_tilt;
    gimbal_set_pan(theta_pan);
    gimbal_set_tilt(theta_tilt);
}
```

**关键理解**：
- DSP 需要的是**相机相对世界的角速度**（用于补偿 IOU 匹配）
- CM4 需要抵消手抖，使相机保持稳定
- 两者都需要 IMU 数据，但用途不同

#### 7.12.4 命令格式

```c
/* CM4 发送云台角速度 */
#define MAILBOX_CMD_SET_GIMBAL_VEL  0x80000030u

/* Payload 编码 (16-bit Q8 定点):
 *   [15:8] = pan_vel  (范围 ±1.0°/帧, 精度 ~0.008°)
 *   [7:0]  = tilt_vel (范围 ±1.0°/帧, 精度 ~0.008°)
 */

/* CM4 使用示例 */
float pan_vel = 0.5f;   // 每帧向右转 0.5°
float tilt_vel = -0.2f; // 每帧向上转 0.2°
mailbox_write(MAKE_GIMBAL_VEL_CMD(pan_vel, tilt_vel));
```

#### 7.12.5 时序同步

```
帧 N:
  CM4: 发送控制指令 Δpan=0.5°
       同时发送: SET_GIMBAL_VEL(0.5°, 0°)
       电机开始运动
       
帧 N+1:
  DSP: 采集图像（云台已转了约 0.5°）
       收到 gimbal_vel = 0.5°
       在 Kalman 预测时补偿: pred_box.x -= 1.1px
       匹配得分正常

帧 N+2:
  CM4: 重新发送当前帧的角速度
       (每帧都需要更新，否则 DSP 会假设云台静止)
```

**重要**：CM4 每帧都应发送 `SET_GIMBAL_VEL`，即使角速度为 0。

#### 7.12.6 DSP 侧使用

```c
/* sot_tracker.c - compute_match_score() 中 */

/* 获取补偿量 */
float gimbal_px_x, gimbal_px_y;
tracker_cmd_get_gimbal_vel_px(&gimbal_px_x, &gimbal_px_y);

/* 补偿预测框（取反：云台向右→图像向左→预测框向左补偿） */
px1 += (int32_t)(-gimbal_px_x);
px2 += (int32_t)(-gimbal_px_x);
py1 += (int32_t)(-gimbal_px_y);
py2 += (int32_t)(-gimbal_px_y);
```

---

## 8. 时序分析

### 8.1 单帧端到端延迟

```
事件                        时间           累计
──────────────────────────────────────────────────
摄像头曝光                  ~5ms           5ms
DSP CNN 推理                ~50ms          55ms
DSP SORT:                                  
  Kalman predict (5 track)  ~0.1ms         55.1ms
  IoU 代价矩阵 (5×10)       ~0.05ms        55.15ms
  匈牙利算法 (10×10)         ~0.1ms         55.25ms
  Kalman update (5 track)   ~0.1ms         55.35ms
  [SORT 合计]               ~0.4ms         55.4ms
Mailbox 传输                <1μs           55.4ms
CM4 解析 + 选择             ~0.1ms         55.5ms
CM4 P 控制 + 滤波           ~0.1ms         55.6ms
电机 UART 发送              ~2ms           57.6ms
电机运动到位                50ms           107.6ms
──────────────────────────────────────────────────
总端到端延迟:         ~108ms (约 2 帧)
```

### 8.2 执行时序图

```
时间 →
       0ms         50ms        100ms       150ms       200ms
DSP:   |==推理==|  |==推理==|  |==推理==|  |==推理==|
       |---[帧0]---|---[帧1]---|---[帧2]---|---[帧3]---|
            ↓ MB         ↓ MB        ↓ MB        ↓ MB
CM4:        |P+M|        |P+M|       |P+M|       |P+M|
            └──────电机──┘    └──电机──┘
       
MB = Mailbox 通知
P+M = Parse + P_ctrl + Motor command
电机 = 舵机执行运动 (50ms)
```

### 8.3 控制周期预算

```
50ms 控制周期 (20Hz):
  ├─ Mailbox 解析:    ~0.1ms
  ├─ 目标选择:        ~0.05ms
  ├─ P 控制计算:      ~0.05ms
  ├─ 安全检查:        ~0.02ms
  ├─ UART 发送 (×2):  ~4ms (两个舵机)
  ├─ 显示渲染:        ~5ms (最坏情况)
  └─ 余量:            ~40.78ms (充裕)
```

---

## 9. 文件结构与接口定义

### 9.1 文件映射

```
Projects/Demo/Tracking_Demo/
├── CMakeLists.txt
├── Inc/
│   ├── gimbal_ctrl.h          # 云台驱动接口
│   └── target_tracker.h       # 跟踪器接口 (含状态机)
├── Src/
│   ├── main.c                 # 主控流程, 显示渲染
│   ├── gimbal_ctrl.c          # 角度→脉冲, 安全保护
│   └── target_tracker.c       # 状态机, ID选择, P控制
└── DSP_Tracker_Design_v2.md   # DSP 侧设计参考

Algorithm_Models/protocol/
├── detection_proto.h           # 共享数据结构 (M4+DSP 双方)
└── mailbox_proto.h             # Mailbox 消息格式

Drivers/SoC/MAILBOX/
├── Include/
│   ├── mailbox.h              # Mailbox 驱动接口
│   ├── mailbox_proto.h        # 消息类型定义
│   └── mailbox_s300.h         # S300 寄存器定义
└── Src/
    └── mailbox.c              # 驱动实现
```

### 9.2 核心接口定义

```c
/* ===== target_tracker.h ===== */

typedef enum {
    TRACKER_STATE_IDLE,
    TRACKER_STATE_FOLLOW,      /* 更名: TRACKING → FOLLOW (语义更清晰) */
    TRACKER_STATE_PREDICTING,
    TRACKER_STATE_LOST,
    TRACKER_STATE_CENTER,      /* 新增: 回中状态 */
    TRACKER_STATE_ERROR        /* 新增: 异常保护 */
} TrackerState_t;

/* 初始化 */
void tracker_init(uint32_t (*get_millis)(void), int img_w, int img_h);

/* CM4 内部目标表示 (从 DetectionBox_t 选出的跟踪目标) */
typedef struct {
    int      track_id;           /* DSP 分配的 track ID */
    int      cx, cy;             /* 中心坐标 (像素) */
    int      x1, y1, x2, y2;    /* 边界框 (像素) */
    float    box_area_ratio;     /* 框面积占图像面积比 */
    float    score;              /* 置信度 */
    int8_t   vx, vy;             /* 速度 (像素/帧, DSP Kalman 输出) */
    uint8_t  speed;              /* 速度大小 */
    uint8_t  kf_confidence;      /* Kalman 置信度 [0-100] */
    uint8_t  miss_count;         /* DSP coast 计数 (0=真检测) */
} tracker_target_t;

/* 输入检测结果 (每帧调用) */
void tracker_update_target(const tracker_target_t *target);

/* 控制循环 (20Hz 周期调用) */
void tracker_poll(void);

/* 状态查询 */
TrackerState_t tracker_get_state(void);
uint8_t tracker_get_current_track_id(void);

/* 控制 */
void tracker_set_enable(bool enable);
void tracker_reset(void);


/* ===== gimbal_ctrl.h ===== */

int  gimbal_init(uint32_t (*get_millis)(void));
void gimbal_center(void);                            /* 回中 */
void gimbal_move(float yaw, float pitch, uint16_t t);/* 绝对角度 */
void gimbal_move_delta(float dy, float dp, uint16_t t);/* 增量 */
int  gimbal_get_position(float *yaw, float *pitch);  /* 读反馈 */
void gimbal_stop(void);                              /* 紧急停止 */
```

---

## 10. 迭代路线图

| 阶段     | 内容                                                                                               | 优先级 | 状态   |
| -------- | -------------------------------------------------------------------------------------------------- | ------ | ------ |
| **v3.0** | 自适应 P + 速度前馈 + 6 态状态机 + 软死区 + 边缘保护 + 首帧限幅 + 跳变检测 + coast 感知 + 安全保护 | P0     | ← 当前 |
| **v3.1** | 多目标优先级策略 (面积/方向/历史偏好)                                                              | P1     | 计划   |
| **v3.2** | M4→DSP 反馈通道 (云台角速度告知 DSP, 改善速度估计精度)                                             | P2     | 计划   |
| **v3.3** | 人脸识别 ID 跨场景持久化                                                                           | P3     | 远期   |

### v3.0 设计范围说明

**v3.0 已包含原 v3.1/v3.2 计划的自适应 Kp 和速度前馈**，因为：
1. 现有代码已实测验证这两个机制的有效性
2. DSP 已提供高质量的 vx/vy 和 kf_confidence，不用白不用
3. 固定 Kp 在远/近目标切换时体验差，自适应是必要的
4. 速度前馈有保守的置信度门控，风险可控

原 v3.1~v3.5 的多目标策略/反馈通道/人脸ID持久化依次顺延为 v3.1~v3.3。

### v3.0 控制器简要示例

```c
/* v3.0: 自适应 P + 速度前馈 */
float kp = adaptive_kp(ex, ey, box_area_ratio, Kp_base, kp_scale);
velocity_feedforward(&ex, &ey, target, last_delta_pan, last_delta_tilt);
float Δpan = -kp * soft_deadzone(ex, DEAD_ZONE);
```

### v3.0 → v3.1 升级路径

```c
/* v3.1: 多目标优先级策略 */
int select_target_v31(const DetectionResult_t *result, int active_id)
{
    // 综合分: 面积×0.3 + 居中×0.4 + 历史编好×0.3
    float score = area_score * 0.3f + center_score * 0.4f + history * 0.3f;
}
```

---

## 11. 方案总结

### 11.1 亮点

| 特性                      | 价值                                                 |
| ------------------------- | ---------------------------------------------------- |
| **完整 SORT + 8D Kalman** | DSP 侧精确的帧间关联 + 速度估计，为 CM4 提供可靠输入 |
| **Mailbox 零拷贝**        | 延迟 <1μs，无内存拷贝开销                            |
| **DSP/CM4 职责分离**      | DSP 专注视觉、CM4 专注决策，各自独立迭代             |
| **Active ID 粘滞**        | 多人场景稳定锁定，不跳变                             |
| **DSP Coast 感知**        | CM4 区分真检测 vs. 预测框，差异化控制策略            |
| **自适应 P + 速度前馈**   | 双维度自适应 Kp + Kalman 速度前馈 + 云台运动补偿     |
| **多层安全防线**          | 跳变检测 + 软死区 + 边缘保护 + 首帧限幅 + 堵转/越界  |
| **六态状态机**            | IDLE/FOLLOW/PREDICTING/LOST(四级)/CENTER/ERROR       |
| **LOST 四级超时**         | 50ms/500ms/1s/10s 渐进放宽，平衡快速恢复和稳定性     |
| **增量式 P + LPF**        | 累加式 P 模型隐含积分特性，动作丝滑如"人眼"          |
| **全功能 v3.0**           | 包含所有安全 + 自适应 + 前馈，可直接上板             |

### 11.2 已知限制

| 限制                 | 影响                       | 缓解方案 (后续版本)           |
| -------------------- | -------------------------- | ----------------------------- |
| 前馈依赖 FOV 标定    | FOV 不准导致云台补偿偏差   | 实测校准 / M4→DSP 反馈 (v3.2) |
| coast 弱跟随 5 帧    | 长遮挡后位置可能有小偏差   | 恢复时位置验证兜底            |
| DSP track_id 8-bit   | 最多 256 个 ID 后回绕      | DSP 侧处理回绕                |
| 单目标跟踪           | 不支持同时跟踪多人         | v3.1 多目标策略               |
| 边缘保护降速         | 靠近画面边缘时跟踪速度变慢 | 可接受的安全代价              |
| 自适应 Kp 范围需调优 | 初始参数可能不最优         | 按实测场景调整 err/box 系数   |

### 11.3 与 LeArm 对照

附件中 LeArm (STM32F103) 的舵机驱动采用相同的 Hiwonder 串行协议。关键对照：

| 对比项   | LeArm                          | S300 Tracking            |
| -------- | ------------------------------ | ------------------------ |
| 舵机协议 | `serial_servo.h` (0x55 帧头)   | `bus_servo.h` (兼容协议) |
| 角度换算 | `SERIAL_ANGLE_FACTOR = 4.1667` | `POS_PER_DEG = 4.1667`   |
| 控制方式 | 动作组 / 逆运动学              | P 控制 + LPF 实时闭环    |
| 运动时间 | 按动作帧定义                   | 固定 50ms (匹配控制周期) |

---

*文档结束。准备就绪可进入代码实施阶段。*
