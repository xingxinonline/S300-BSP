# DSP 单人追踪系统设计文档

**版本**: 2.0  
**日期**: 2026-01-29  
**状态**: 设计输入  

---

## 1. 概述

### 1.1 应用场景

**单人追踪**：画面中可能同时出现多人，但系统只追踪其中**一个人**。

| 场景特征   | 描述                               |
| ---------- | ---------------------------------- |
| 检测目标数 | 0 ~ N（可能多人）                  |
| 追踪目标数 | 最多 1 人                          |
| 选中规则   | **DSP** 选择最靠近画面中心的目标   |
| 追踪维持   | 基于 track_id 跨帧跟踪，避免框跳变 |
| 显示效果   | 选中目标：绿框；其他目标：蓝框     |

### 1.2 职责分工（重要！）

| 职责              | 负责方  | 说明                                      |
| ----------------- | ------- | ----------------------------------------- |
| **启动/停止追踪** | CM4     | 通过 Mailbox 发送命令                     |
| **检测**          | DSP     | 运行 AI 模型                              |
| **跟踪**          | DSP     | IoU 匹配，分配 track_id                   |
| **选择目标**      | **DSP** | 选择最靠近中心的目标，填写 `selected_idx` |
| **显示**          | CM4     | 根据 `selected_idx` 绘制绿框/蓝框         |

### 1.3 通信架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        S300 SoC                                 │
├────────────────────────────┬────────────────────────────────────┤
│         DSP 核             │            CM4 核                  │
│                            │                                    │
│  ┌─────────────────────┐   │   ┌─────────────────────────────┐  │
│  │  Mailbox CH1 IRQ ◄──┼───┼───┼─ 启动/停止命令              │  │
│  └─────────────────────┘   │   │                             │  │
│           ↓                │   │  face_tracker.c             │  │
│  ┌─────────────────────┐   │   │  ┌─────────────────────┐    │  │
│  │   Camera Input      │   │   │  │ 1. 发送控制命令     │    │  │
│  │         ↓           │   │   │  │ 2. 接收检测结果     │    │  │
│  │   Detection Model   │   │   │  │ 3. 读取 selected_idx│    │  │
│  │         ↓           │   │   │  │ 4. 绘制边界框       │    │  │
│  │   Tracker (IoU)     │   │   │  │    - 绿框: 选中     │    │  │
│  │         ↓           │   │   │  │    - 蓝框: 其他     │    │  │
│  │   Select Target ★   │   │   │  └─────────────────────┘    │  │
│  │         ↓           │   │   │                             │  │
│  │   Pack Result       │   │   │                             │  │
│  │         ↓           │   │   │                             │  │
│  │   Send Mailbox ─────┼───┼───┼→ Mailbox CH0 IRQ            │  │
│  └─────────────────────┘   │   └─────────────────────────────┘  │
│                            │                                    │
│     Shared Memory          │                                    │
│  ┌─────────────────────────┴───────────────────────────────┐    │
│  │  0x44800000: DetectionResult[0]  (双缓冲 Buffer 0)      │    │
│  │  0x44800300: DetectionResult[1]  (双缓冲 Buffer 1)      │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘

★ DSP 负责选择目标，通过 selected_idx 告知 CM4
```

---

## 2. 数据结构规范

### 2.1 共享头文件

DSP 和 M4 **必须**使用相同的数据结构定义。请引用以下头文件：

```c
#include "detection_protocol.h"
```

或确保以下定义完全一致：

### 2.2 核心数据结构

#### 2.2.1 单个检测目标 (64 bytes)

```c
typedef struct __attribute__((packed)) {
    float    score;           // [0.0, 1.0] 检测置信度
    int32_t  x1;              // 左上角 X (像素坐标)
    int32_t  y1;              // 左上角 Y
    int32_t  x2;              // 右下角 X
    int32_t  y2;              // 右下角 Y
    float    lm[10];          // 5 个关键点 [x0,y0,x1,y1,...,x4,y4]
    uint8_t  type;            // 检测类型 (见 2.2.3)
    uint8_t  track_id;        // ★ 跟踪 ID (1-255, 0=未分配)
    uint8_t  reserved[2];     // 保留，确保 64 字节对齐
} DetectionBox_t;
```

**关键字段说明**：

| 字段          | 说明        | DSP 职责                           |
| ------------- | ----------- | ---------------------------------- |
| `score`       | 检测置信度  | 模型输出，建议过滤 < 0.5 的结果    |
| `x1,y1,x2,y2` | 边界框坐标  | 必须在 `[0, 图像宽高]` 范围内      |
| `lm[10]`      | 关键点      | 人脸检测时填充，其他类型可置 0     |
| `type`        | 目标类型    | 见枚举定义                         |
| `track_id`    | **跟踪 ID** | **DSP 必须实现**，用于帧间目标关联 |

#### 2.2.2 多目标检测结果 (664 bytes)

```c
#define MAX_DETECTION_COUNT  10
#define DETECTION_RESULT_MAGIC  0x44455446u  // "DETF"

typedef struct __attribute__((packed)) {
    uint32_t       magic;       // = DETECTION_RESULT_MAGIC
    uint32_t       version;     // = 0x0200 (v2.0)
    uint32_t       frame_id;      // 帧序号 (每帧递增)
    uint32_t       timestamp;     // 时间戳 (毫秒, 可选)
    uint32_t       count;         // 目标数量 [0, MAX_DETECTION_COUNT]
    int32_t        selected_idx;  // ★ DSP 选中的目标索引 [0,count-1]，-1=无选中
    DetectionBox_t boxes[MAX_DETECTION_COUNT];  // 目标数组
} DetectionResult_t;
```

> **重要变更 (v2.1)**：`reserved` 字段改为 `selected_idx`，由 DSP 填写选中目标的索引。

#### 2.2.3 检测类型枚举

```c
typedef enum {
    DETECTION_TYPE_UNKNOWN  = 0,
    DETECTION_TYPE_FACE     = 1,    // 人脸
    DETECTION_TYPE_HUMAN    = 2,    // 人体
    DETECTION_TYPE_HAND     = 3,    // 手势
    DETECTION_TYPE_OBJECT   = 4,    // 通用物体
} DetectionType_t;
```

---

## 3. CM4 → DSP 控制命令

### 3.1 命令格式

CM4 通过 Mailbox **Channel 1** 向 DSP 发送控制命令：

```
┌───────────────────────────────────────────────┐
│              32-bit 命令消息                   │
├──────────────┬────────────────────────────────┤
│  Bit [31:28] │  命令类型 (CmdType)             │
├──────────────┼────────────────────────────────┤
│  Bit [27:0]  │  命令参数 (Param)               │
└──────────────┴────────────────────────────────┘
```

### 3.2 命令类型

| CmdType | 命令值       | 说明     | DSP 行为                          |
| ------- | ------------ | -------- | --------------------------------- |
| 0x8     | `0x80000000` | 启动追踪 | 开始输出检测结果                  |
| 0x9     | `0x90000000` | 停止追踪 | 停止输出检测结果                  |
| 0xA     | `0xA0000000` | 重置选中 | 重新选择目标（丢弃当前 track_id） |

### 3.3 DSP 命令处理伪代码

```c
void dsp_mailbox_ch1_handler(void)
{
    uint32_t cmd = read_mailbox_ch1();
    uint32_t cmd_type = CMD_GET_TYPE(cmd);
    
    switch (cmd_type) {
    case CMD_TYPE_START_TRACKING:
        g_tracking_enabled = true;
        printf("[DSP:CMD] Tracking started\r\n");
        break;
        
    case CMD_TYPE_STOP_TRACKING:
        g_tracking_enabled = false;
        tracker_reset();  // 重置跟踪器状态
        printf("[DSP:CMD] Tracking stopped\r\n");
        break;
        
    case CMD_TYPE_RESET_SELECTION:
        g_selected_track_id = 0;  // 下一帧重新选择
        printf("[DSP:CMD] Selection reset\r\n");
        break;
    }
}
```

---

## 4. DSP 选择目标算法

### 4.1 选择规则

DSP 负责从检测到的多个目标中选择一个作为追踪目标：

1. **优先级 1**：如果之前有选中目标且该目标仍存在（`track_id` 匹配），继续跟踪
2. **优先级 2**：否则选择最靠近画面中心的目标

### 4.2 实现伪代码

```c
/**
 * @brief 选择追踪目标
 * @param result 当前帧检测结果
 * @return 选中目标的索引，-1 表示无
 */
static int32_t select_tracking_target(DetectionResult_t *result)
{
    if (result->count == 0) {
        g_selected_track_id = 0;
        return -1;
    }
    
    /* 策略1：继续跟踪之前选中的目标 */
    if (g_selected_track_id != 0) {
        for (uint32_t i = 0; i < result->count; i++) {
            if (result->boxes[i].track_id == g_selected_track_id) {
                return (int32_t)i;
            }
        }
        /* 之前的目标丢失，需要重新选择 */
    }
    
    /* 策略2：选择最靠近画面中心的目标 */
    int32_t screen_cx = SCREEN_WIDTH / 2;
    int32_t screen_cy = SCREEN_HEIGHT / 2;
    int32_t min_dist = 0x7FFFFFFF;
    int32_t selected = -1;
    
    for (uint32_t i = 0; i < result->count; i++) {
        DetectionBox_t *box = &result->boxes[i];
        int32_t cx = (box->x1 + box->x2) / 2;
        int32_t cy = (box->y1 + box->y2) / 2;
        int32_t dist = (cx - screen_cx) * (cx - screen_cx) 
                     + (cy - screen_cy) * (cy - screen_cy);
        if (dist < min_dist) {
            min_dist = dist;
            selected = (int32_t)i;
        }
    }
    
    if (selected >= 0) {
        g_selected_track_id = result->boxes[selected].track_id;
    }
    
    return selected;
}
```

### 4.3 每帧处理流程

```c
void detection_process_frame(const uint8_t *image)
{
    if (!g_tracking_enabled) {
        return;  // 追踪未启动，不处理
    }
    
    /* 1. 运行检测模型 */
    run_detection_model(image, &g_result);
    
    /* 2. 跟踪器分配 track_id */
    tracker_assign_ids(&g_result);
    
    /* 3. 选择追踪目标 ★ */
    g_result.selected_idx = select_tracking_target(&g_result);
    
    /* 4. 填充结果 */
    g_result.magic = DETECTION_RESULT_MAGIC;
    g_result.version = DETECTION_PROTOCOL_VERSION;
    g_result.frame_id = g_frame_counter++;
    
    /* 5. 写入共享内存 */
    uint32_t offset = (g_buf_index & 1) ? 0x300 : 0x000;
    memcpy((void*)(DSP_DETECTION_BASE_ADDR + offset), &g_result, sizeof(g_result));
    g_buf_index++;
    
    /* 6. 通知 CM4 */
    uint32_t msg = MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_MULTI, offset);
    send_mailbox_ch0(msg);
}
```

---

## 5. 跟踪算法设计

### 5.1 Track ID 的重要性

**问题**：M4 需要"锁定"一个选中目标，即使画面中有多个目标移动，绿框也应跟随同一个目标。

**解决方案**：DSP 为每个目标分配稳定的 `track_id`，同一目标在连续帧中保持相同 ID。

```
帧 1:  目标 A (track_id=1)    目标 B (track_id=2)
          ↓                        ↓
帧 2:  目标 A (track_id=1)    目标 B (track_id=2)   ← ID 不变
          ↓                        ↓
帧 3:  目标 A (track_id=1)    [B 消失]
          ↓
帧 4:  目标 A (track_id=1)    目标 C (track_id=3)   ← 新目标分配新 ID
```

### 3.2 推荐算法：IoU 匹配跟踪

#### 3.2.1 算法流程

```
┌─────────────────────────────────────────────────────────────┐
│                    每帧处理流程                              │
├─────────────────────────────────────────────────────────────┤
│  1. 运行检测模型，得到当前帧 N 个目标                        │
│                        ↓                                    │
│  2. 计算当前帧每个目标与上一帧每个目标的 IoU                 │
│                        ↓                                    │
│  3. 使用贪婪匹配或匈牙利算法找到最优匹配                     │
│     - IoU > 阈值 (如 0.3) 才认为匹配成功                    │
│                        ↓                                    │
│  4. 分配 track_id:                                          │
│     - 匹配成功: 继承上一帧的 track_id                       │
│     - 匹配失败: 分配新的 track_id                           │
│                        ↓                                    │
│  5. 保存当前帧结果，用于下一帧匹配                          │
└─────────────────────────────────────────────────────────────┘
```

#### 3.2.2 IoU 计算公式

```
        ┌─────────────┐
        │  Box A      │
        │    ┌────────┼────┐
        │    │ Inter  │    │
        └────┼────────┘    │
             │      Box B  │
             └─────────────┘

IoU = Area(Intersection) / Area(Union)
    = Area(Intersection) / (Area(A) + Area(B) - Area(Intersection))
```

### 3.3 参考实现

```c
/*===========================================================================
 * DSP 侧跟踪器实现参考
 *===========================================================================*/

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "detection_protocol.h"

/* 跟踪器配置 */
#define IOU_THRESHOLD       0.3f    /* IoU 匹配阈值 */
#define MAX_LOST_FRAMES     5       /* 目标丢失帧数阈值 (可选) */

/* 跟踪器状态 */
typedef struct {
    DetectionBox_t prev_boxes[MAX_DETECTION_COUNT];
    uint32_t       prev_count;
    uint8_t        next_track_id;
} Tracker_t;

static Tracker_t g_tracker = {
    .prev_count = 0,
    .next_track_id = 1
};

/*---------------------------------------------------------------------------
 * 辅助函数
 *---------------------------------------------------------------------------*/

static inline int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t min_i32(int32_t a, int32_t b) { return a < b ? a : b; }

/**
 * @brief 计算两个边界框的 IoU
 */
static float calc_iou(const DetectionBox_t *a, const DetectionBox_t *b)
{
    int32_t x1 = max_i32(a->x1, b->x1);
    int32_t y1 = max_i32(a->y1, b->y1);
    int32_t x2 = min_i32(a->x2, b->x2);
    int32_t y2 = min_i32(a->y2, b->y2);

    int32_t inter_w = max_i32(0, x2 - x1);
    int32_t inter_h = max_i32(0, y2 - y1);
    int32_t inter_area = inter_w * inter_h;

    int32_t area_a = (a->x2 - a->x1) * (a->y2 - a->y1);
    int32_t area_b = (b->x2 - b->x1) * (b->y2 - b->y1);
    int32_t union_area = area_a + area_b - inter_area;

    if (union_area <= 0) return 0.0f;
    return (float)inter_area / (float)union_area;
}

/**
 * @brief 分配新的 track_id
 */
static uint8_t alloc_track_id(void)
{
    uint8_t id = g_tracker.next_track_id;
    g_tracker.next_track_id++;
    if (g_tracker.next_track_id == 0) {
        g_tracker.next_track_id = 1;  /* 跳过 0，0 表示未分配 */
    }
    return id;
}

/*---------------------------------------------------------------------------
 * 核心跟踪函数
 *---------------------------------------------------------------------------*/

/**
 * @brief 为当前帧的检测结果分配 track_id
 * 
 * @param result  当前帧检测结果 (输入/输出)
 * 
 * 算法: 贪婪 IoU 匹配
 * 1. 遍历当前帧每个目标
 * 2. 在上一帧中找 IoU 最大且 > 阈值的目标
 * 3. 匹配成功则继承 track_id，否则分配新 ID
 */
void tracker_assign_ids(DetectionResult_t *result)
{
    /* 标记上一帧哪些目标已被匹配 */
    bool prev_matched[MAX_DETECTION_COUNT] = {false};

    for (uint32_t i = 0; i < result->count && i < MAX_DETECTION_COUNT; i++)
    {
        DetectionBox_t *cur = &result->boxes[i];
        float best_iou = IOU_THRESHOLD;
        int32_t best_match = -1;

        /* 在上一帧中查找最佳匹配 */
        for (uint32_t j = 0; j < g_tracker.prev_count; j++)
        {
            if (prev_matched[j]) continue;  /* 已被其他目标匹配 */

            float iou = calc_iou(cur, &g_tracker.prev_boxes[j]);
            if (iou > best_iou) {
                best_iou = iou;
                best_match = (int32_t)j;
            }
        }

        if (best_match >= 0) {
            /* 匹配成功，继承 track_id */
            cur->track_id = g_tracker.prev_boxes[best_match].track_id;
            prev_matched[best_match] = true;
        } else {
            /* 新目标，分配新 track_id */
            cur->track_id = alloc_track_id();
        }
    }

    /* 保存当前帧用于下一帧匹配 */
    memcpy(g_tracker.prev_boxes, result->boxes, 
           result->count * sizeof(DetectionBox_t));
    g_tracker.prev_count = result->count;
}

/**
 * @brief 重置跟踪器状态
 */
void tracker_reset(void)
{
    g_tracker.prev_count = 0;
    g_tracker.next_track_id = 1;
}
```

---

## 6. Mailbox 通信协议

### 4.1 消息格式

32-bit Mailbox 消息:

```
┌────────────┬────────────────────────────────┐
│ Bit 31-28  │          Bit 27-0              │
├────────────┼────────────────────────────────┤
│ Msg Type   │     Payload (Offset)           │
└────────────┴────────────────────────────────┘
```

### 4.2 消息类型

| Type      | 值      | 说明                 | Payload                    |
| --------- | ------- | -------------------- | -------------------------- |
| SINGLE    | 0x0     | 单目标（旧协议兼容） | FaceRect offset            |
| **MULTI** | **0x1** | **多目标（新协议）** | **DetectionResult offset** |
| NO_DETECT | 0xF     | 本帧无检测结果       | 忽略                       |

### 4.3 Offset 计算

**重要**: Offset 是相对于 `DSP_DETECTION_BASE_ADDR` (0x44800000) 的偏移，**不是绝对地址**。

```c
/* ✅ 正确方式 */
uint32_t offset = (uint32_t)((uintptr_t)result - DSP_DETECTION_BASE_ADDR);
uint32_t msg = MAILBOX_MSG_TYPE_MULTI | (offset & 0x0FFFFFFF);

/* ❌ 错误方式 (发送绝对地址低 28 位) */
uint32_t msg = MAILBOX_MSG_TYPE_MULTI | ((uint32_t)result & 0x0FFFFFFF);
```

### 4.4 通信示例

```c
#define MAILBOX_MSG_TYPE_MULTI     0x10000000u
#define MAILBOX_MSG_TYPE_NO_DETECT 0xF0000000u

void send_detection_result(DetectionResult_t *result)
{
    uint32_t msg;
    
    if (result->count > 0) {
        /* 计算相对偏移 */
        uint32_t offset = (uint32_t)((uintptr_t)result - DSP_DETECTION_BASE_ADDR);
        msg = MAILBOX_MSG_TYPE_MULTI | (offset & 0x0FFFFFFF);
    } else {
        /* 无检测结果 */
        msg = MAILBOX_MSG_TYPE_NO_DETECT;
    }
    
    mailbox_write(MAILBOX_BASE, msg);
}
```

---

## 7. 完整实现示例

### 5.1 DSP 主循环

```c
/*===========================================================================
 * DSP 检测与跟踪主流程
 *===========================================================================*/

#include "detection_protocol.h"
#include "tracker.h"       /* 上面定义的跟踪器 */
#include "model_api.h"     /* AI 模型接口 */
#include "mailbox.h"

/* 双缓冲 */
static DetectionResult_t g_result_buf[2] __attribute__((section(".shared_mem")));
static uint32_t g_buf_index = 0;
static uint32_t g_frame_counter = 0;

void detection_init(void)
{
    /* 初始化结果缓冲区 */
    for (int i = 0; i < 2; i++) {
        g_result_buf[i].magic = DETECTION_RESULT_MAGIC;
        g_result_buf[i].version = DETECTION_PROTOCOL_VERSION;
        g_result_buf[i].count = 0;
    }
    
    /* 初始化跟踪器 */
    tracker_reset();
    
    printf("[DETECT] Multi-target detection initialized (v2.0)\r\n");
    printf("[DETECT] buf[0] DSP=0x%08X M4=0x%08X\r\n",
           (uint32_t)((uintptr_t)&g_result_buf[0] - DSP_DETECTION_BASE_ADDR),
           (uint32_t)(uintptr_t)&g_result_buf[0]);
    printf("[DETECT] buf[1] DSP=0x%08X M4=0x%08X\r\n",
           (uint32_t)((uintptr_t)&g_result_buf[1] - DSP_DETECTION_BASE_ADDR),
           (uint32_t)(uintptr_t)&g_result_buf[1]);
}

void detection_process_frame(const uint8_t *image, uint32_t width, uint32_t height)
{
    /* 选择当前缓冲区 */
    DetectionResult_t *result = &g_result_buf[g_buf_index];
    
    /* 填充头部 */
    result->magic = DETECTION_RESULT_MAGIC;
    result->version = DETECTION_PROTOCOL_VERSION;
    result->frame_id = g_frame_counter++;
    result->timestamp = get_timestamp_ms();
    result->count = 0;
    
    /* 1. 运行检测模型 */
    ModelOutput_t model_out[32];
    int num_detections = run_detection_model(image, width, height, model_out, 32);
    
    /* 2. 收集有效检测结果 */
    for (int i = 0; i < num_detections && result->count < MAX_DETECTION_COUNT; i++)
    {
        ModelOutput_t *det = &model_out[i];
        
        /* 置信度过滤 */
        if (det->score < 0.5f) continue;
        
        /* 边界检查 */
        if (det->x1 < 0 || det->y1 < 0 || 
            det->x2 > (int32_t)width || det->y2 > (int32_t)height) continue;
        
        DetectionBox_t *box = &result->boxes[result->count];
        box->score = det->score;
        box->x1 = det->x1;
        box->y1 = det->y1;
        box->x2 = det->x2;
        box->y2 = det->y2;
        box->type = DETECTION_TYPE_FACE;
        box->track_id = 0;  /* 稍后由跟踪器分配 */
        
        /* 复制关键点 */
        for (int j = 0; j < 10; j++) {
            box->lm[j] = det->landmarks[j];
        }
        
        result->count++;
    }
    
    /* 3. ★ 跟踪器分配 track_id */
    tracker_assign_ids(result);
    
    /* 4. 调试输出 */
    if (result->count > 0) {
        printf("face = %lu\r\n", (unsigned long)result->count);
        for (uint32_t i = 0; i < result->count; i++) {
            printf("  [%lu] track_id=%u box=(%ld,%ld,%ld,%ld)\r\n",
                   (unsigned long)i,
                   result->boxes[i].track_id,
                   (long)result->boxes[i].x1, (long)result->boxes[i].y1,
                   (long)result->boxes[i].x2, (long)result->boxes[i].y2);
        }
    }
    
    /* 5. 发送 Mailbox 消息 */
    uint32_t offset = (uint32_t)((uintptr_t)result - DSP_DETECTION_BASE_ADDR);
    uint32_t msg = (result->count > 0) 
                 ? (MAILBOX_MSG_TYPE_MULTI | (offset & 0x0FFFFFFF))
                 : MAILBOX_MSG_TYPE_NO_DETECT;
    
    mailbox_write(MAILBOX_BASE, msg);
    
    printf("[DETECT] frame=%lu count=%lu msg=0x%08lX\r\n",
           (unsigned long)result->frame_id, 
           (unsigned long)result->count,
           (unsigned long)msg);
    
    /* 6. 切换缓冲区 */
    g_buf_index = 1 - g_buf_index;
}
```

---

## 8. 调试日志规范

### 6.1 DSP 侧必须输出的日志

为便于联调，DSP 侧**必须**按以下格式输出日志（前缀 `[DSP:xxx]`）：

#### 6.1.1 初始化日志

```c
printf("[DSP:INIT] Multi-target detection initialized (v2.0)\r\n");
printf("[DSP:INIT] buf[0] offset=0x%08lX addr=0x%08lX\r\n", offset0, addr0);
printf("[DSP:INIT] buf[1] offset=0x%08lX addr=0x%08lX\r\n", offset1, addr1);
printf("[DSP:INIT] screen=%dx%d\r\n", width, height);
```

#### 6.1.2 每帧检测日志

```c
// 检测开始
printf("[DSP:DET] frame=%lu start\r\n", frame_id);

// 原始检测结果（模型输出）
printf("[DSP:DET] raw_count=%d\r\n", raw_count);
for (int i = 0; i < raw_count; i++) {
    printf("[DSP:DET] raw[%d] score=%.2f box=(%d,%d,%d,%d)\r\n",
           i, score, x1, y1, x2, y2);
}
```

#### 6.1.3 跟踪日志（关键！）

```c
// 跟踪器匹配过程
printf("[DSP:TRK] frame=%lu prev_count=%lu cur_count=%lu\r\n", 
       frame_id, prev_count, cur_count);

// 每个目标的匹配结果
for (int i = 0; i < cur_count; i++) {
    printf("[DSP:TRK] [%d] track_id=%u matched_prev=%d iou=%.2f box=(%d,%d,%d,%d)\r\n",
           i, track_id, matched_prev_idx, iou, x1, y1, x2, y2);
}

// 新分配的 track_id
printf("[DSP:TRK] new_ids_allocated=%d next_id=%u\r\n", new_count, next_track_id);
```

#### 6.1.4 发送日志

```c
printf("[DSP:SEND] frame=%lu count=%lu msg=0x%08lX offset=0x%lX\r\n",
       frame_id, count, msg, offset);
```

### 6.2 M4 侧输出的日志

M4 侧日志前缀为 `[M4:xxx]`，便于区分：

```
[M4:MBOX] msg=0x10000000 type=0x1 offset=0x0 addr=0x44800000
[M4:FRAME] frame=0 count=2
[M4:BOX] [0] raw: track_id=1 score=0.95 box=(50,100,150,200)
[M4:BOX] [1] raw: track_id=2 score=0.88 box=(180,120,280,220)
[M4:FRAME] valid_count=2
[M4:SELECT] count=2 selected_idx=0 track_id=1
```

### 6.3 联调日志示例（完整流程）

以下是正常工作时 DSP 和 M4 的交错日志：

```
===== 帧 0: 检测到 2 个目标 =====
[DSP:DET] frame=0 start
[DSP:DET] raw_count=2
[DSP:DET] raw[0] score=0.95 box=(50,100,150,200)
[DSP:DET] raw[1] score=0.88 box=(180,120,280,220)
[DSP:TRK] frame=0 prev_count=0 cur_count=2
[DSP:TRK] [0] track_id=1 matched_prev=-1 iou=0.00 box=(50,100,150,200)    ← 新目标
[DSP:TRK] [1] track_id=2 matched_prev=-1 iou=0.00 box=(180,120,280,220)   ← 新目标
[DSP:TRK] new_ids_allocated=2 next_id=3
[DSP:SEND] frame=0 count=2 msg=0x10000000 offset=0x0

[M4:MBOX] msg=0x10000000 type=0x1 offset=0x0 addr=0x44800000
[M4:FRAME] frame=0 count=2
[M4:BOX] [0] raw: track_id=1 score=0.95 box=(50,100,150,200)
[M4:BOX] [1] raw: track_id=2 score=0.88 box=(180,120,280,220)
[M4:FRAME] valid_count=2
[M4:SELECT] ★ Selected: idx=0 track_id=1 reason=center_nearest box=(50,100,150,200)
[M4:SELECT] count=2 selected_idx=0 track_id=1

===== 帧 1: 目标移动，track_id 保持 =====
[DSP:DET] frame=1 start
[DSP:DET] raw_count=2
[DSP:DET] raw[0] score=0.94 box=(55,105,155,205)
[DSP:DET] raw[1] score=0.87 box=(175,115,275,215)
[DSP:TRK] frame=1 prev_count=2 cur_count=2
[DSP:TRK] [0] track_id=1 matched_prev=0 iou=0.82 box=(55,105,155,205)     ← 匹配成功
[DSP:TRK] [1] track_id=2 matched_prev=1 iou=0.85 box=(175,115,275,215)    ← 匹配成功
[DSP:TRK] new_ids_allocated=0 next_id=3
[DSP:SEND] frame=1 count=2 msg=0x10000300 offset=0x300

[M4:MBOX] msg=0x10000300 type=0x1 offset=0x300 addr=0x44800300
[M4:FRAME] frame=1 count=2
[M4:BOX] [0] raw: track_id=1 score=0.94 box=(55,105,155,205)
[M4:BOX] [1] raw: track_id=2 score=0.87 box=(175,115,275,215)
[M4:FRAME] valid_count=2
[M4:SELECT] Found prev track_id=1 at idx=0                                ← 继续跟踪
[M4:SELECT] count=2 selected_idx=0 track_id=1

===== 帧 2: 目标 2 消失 =====
[DSP:DET] frame=2 start
[DSP:DET] raw_count=1
[DSP:DET] raw[0] score=0.93 box=(60,110,160,210)
[DSP:TRK] frame=2 prev_count=2 cur_count=1
[DSP:TRK] [0] track_id=1 matched_prev=0 iou=0.78 box=(60,110,160,210)
[DSP:TRK] new_ids_allocated=0 next_id=3
[DSP:SEND] frame=2 count=1 msg=0x10000000 offset=0x0

[M4:MBOX] msg=0x10000000 type=0x1 offset=0x0 addr=0x44800000
[M4:FRAME] frame=2 count=1
[M4:BOX] [0] raw: track_id=1 score=0.93 box=(60,110,160,210)
[M4:FRAME] valid_count=1
[M4:SELECT] Found prev track_id=1 at idx=0
[M4:SELECT] count=1 selected_idx=0 track_id=1

===== 帧 3: 新目标出现 =====
[DSP:DET] frame=3 start
[DSP:DET] raw_count=2
[DSP:DET] raw[0] score=0.92 box=(65,115,165,215)
[DSP:DET] raw[1] score=0.75 box=(200,50,280,150)                          ← 新目标
[DSP:TRK] frame=3 prev_count=1 cur_count=2
[DSP:TRK] [0] track_id=1 matched_prev=0 iou=0.76 box=(65,115,165,215)
[DSP:TRK] [1] track_id=3 matched_prev=-1 iou=0.00 box=(200,50,280,150)    ← 分配新 ID=3
[DSP:TRK] new_ids_allocated=1 next_id=4
[DSP:SEND] frame=3 count=2 msg=0x10000300 offset=0x300

[M4:MBOX] msg=0x10000300 type=0x1 offset=0x300 addr=0x44800300
[M4:FRAME] frame=3 count=2
[M4:BOX] [0] raw: track_id=1 score=0.92 box=(65,115,165,215)
[M4:BOX] [1] raw: track_id=3 score=0.75 box=(200,50,280,150)
[M4:FRAME] valid_count=2
[M4:SELECT] Found prev track_id=1 at idx=0                                ← 仍跟踪 ID=1
[M4:SELECT] count=2 selected_idx=0 track_id=1
```

### 6.4 调试级别控制

DSP 侧建议也实现调试级别控制：

```c
#define DSP_DEBUG_LEVEL  2  // 0=off, 1=关键, 2=摘要, 3=详细

#define DSP_LOG(level, fmt, ...) \
    do { if (DSP_DEBUG_LEVEL >= (level)) printf(fmt, ##__VA_ARGS__); } while(0)

// 使用示例
DSP_LOG(1, "[DSP:INIT] ...\r\n");     // 级别 1: 初始化、错误
DSP_LOG(2, "[DSP:SEND] ...\r\n");     // 级别 2: 每帧摘要
DSP_LOG(3, "[DSP:TRK] [%d] ...\r\n"); // 级别 3: 每个目标详情
```

---

## 9. 验证与测试

### 7.1 自测检查清单

| #   | 检查项           | 预期结果                |
| --- | ---------------- | ----------------------- |
| 1   | `magic` 字段     | = `0x44455446`          |
| 2   | `version` 字段   | = `0x0200`              |
| 3   | `count` 范围     | `[0, 10]`               |
| 4   | 坐标范围         | `[0, 图像宽高]`         |
| 5   | `track_id` 非零  | 每个有效目标都有 ID     |
| 6   | `track_id` 稳定  | 同一目标连续帧 ID 不变  |
| 7   | Mailbox offset   | = `(addr - 0x44800000)` |
| 8   | Mailbox msg type | 高 4 位 = `0x1`         |

### 6.2 调试日志示例

**期望输出**:
```
[DETECT] Multi-target detection initialized (v2.0)
[DETECT] buf[0] DSP=0x00000000 M4=0x44800000
[DETECT] buf[1] DSP=0x00000300 M4=0x44800300

face = 2
  [0] track_id=1 box=(50,100,150,200)
  [1] track_id=2 box=(180,120,280,220)
[DETECT] frame=0 count=2 msg=0x10000000

face = 2
  [0] track_id=1 box=(52,102,152,202)    ← ID 保持 1
  [1] track_id=2 box=(178,118,278,218)   ← ID 保持 2
[DETECT] frame=1 count=2 msg=0x10000300

face = 1
  [0] track_id=1 box=(55,105,155,205)    ← track_id=2 的目标消失
[DETECT] frame=2 count=1 msg=0x10000000

face = 2
  [0] track_id=1 box=(58,108,158,208)
  [1] track_id=3 box=(200,140,300,240)   ← 新目标分配新 ID=3
[DETECT] frame=3 count=2 msg=0x10000300
```

### 6.3 M4 侧验证

M4 收到数据后会打印:
```
[FaceTracker] First valid frame: count=2, box[0]=(50,100,150,200)
```

并在屏幕上显示:
- **绿框**: 选中目标（最靠近中心，或之前锁定的 track_id）
- **蓝框**: 其他目标

---

## 10. 接口汇总

### 7.1 DSP 需要实现的函数

```c
/* 初始化 */
void detection_init(void);

/* 每帧处理 (由视频流回调调用) */
void detection_process_frame(const uint8_t *image, uint32_t width, uint32_t height);

/* 跟踪器 */
void tracker_assign_ids(DetectionResult_t *result);
void tracker_reset(void);
```

### 7.2 共享内存布局

| 地址       | 内容                         | 大小      |
| ---------- | ---------------------------- | --------- |
| 0x44800000 | `DetectionResult_t` buffer 0 | 664 bytes |
| 0x44800300 | `DetectionResult_t` buffer 1 | 664 bytes |
| 0x44800600 | 保留                         | -         |

### 7.3 Mailbox 消息

| 场景              | 消息值       |
| ----------------- | ------------ |
| 多目标 (buffer 0) | `0x10000000` |
| 多目标 (buffer 1) | `0x10000300` |
| 无检测结果        | `0xF0000000` |

---

## 附录

### A.1 常见问题

**Q1: track_id 溢出怎么办？**  
A: `track_id` 是 `uint8_t`，范围 1-255。溢出后从 1 重新开始。由于同时存在的目标通常不超过 10 个，不会产生冲突。

**Q2: 如何处理遮挡？**  
A: 简单的 IoU 匹配无法处理完全遮挡。可考虑：
- 增加卡尔曼滤波预测
- 使用 DeepSORT (需要 Re-ID 特征)

**Q3: IoU 阈值如何选择？**  
A: 建议 0.3-0.5。太高会导致快速移动时丢失跟踪，太低会导致误匹配。

### A.2 参考资料

- SORT: Simple Online and Realtime Tracking
- DeepSORT: Deep Association Metric
- ByteTrack: Multi-Object Tracking by Associating Every Detection Box

---

**文档结束**
