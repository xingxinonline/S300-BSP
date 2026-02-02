# DSP 侧多目标检测协议升级指南

## 概述

本文档描述如何将 DSP 侧的检测算法从单目标模式升级到多目标模式，以配合 M4 侧的 `face_tracker` 模块。

> **详细设计文档**: 请参阅 [DSP_Multi_Target_Tracking_Design.md](docs/DSP_Multi_Target_Tracking_Design.md)，包含完整的架构设计、跟踪算法和实现示例。

## 协议版本

| 版本 | 说明       | 兼容性        |
| ---- | ---------- | ------------- |
| v1.0 | 单目标检测 | 旧版本        |
| v2.0 | 多目标检测 | 向后兼容 v1.0 |

## 数据结构定义

请在 DSP 侧代码中包含 `detection_protocol.h`，确保与 M4 侧使用完全相同的数据结构。

### 核心结构

```c
#include "detection_protocol.h"

// 或者直接定义（确保与 M4 侧一致）：

#define MAX_DETECTION_COUNT  10
#define DETECTION_RESULT_MAGIC  0x44455446u  // "DETF"
#define DETECTION_PROTOCOL_VERSION  0x0200u

typedef struct __attribute__((packed)) {
    float    score;           // 置信度 [0.0, 1.0]
    int32_t  x1, y1;          // 左上角
    int32_t  x2, y2;          // 右下角
    float    lm[10];          // 5个关键点
    uint8_t  type;            // 检测类型
    uint8_t  track_id;        // ★ 跟踪ID (重要! 见跟踪算法章节)
    uint8_t  reserved[2];     // 保留
} DetectionBox_t;             // 64 bytes

typedef struct __attribute__((packed)) {
    uint32_t       magic;     // = DETECTION_RESULT_MAGIC
    uint32_t       version;   // = DETECTION_PROTOCOL_VERSION
    uint32_t       frame_id;  // 帧序号（递增）
    uint32_t       timestamp; // 时间戳（可选）
    uint32_t       count;     // 目标数量 [0, MAX_DETECTION_COUNT]
    uint32_t       reserved;  // 保留
    DetectionBox_t boxes[MAX_DETECTION_COUNT];
} DetectionResult_t;          // 664 bytes
```

## ★ 跟踪算法要求

### 为什么需要 track_id？

M4 侧需要"锁定"选中的目标。即使画面中有多个目标移动，**绿框应该跟随同一个目标**，而不是跳到另一个目标上。

### DSP 侧的职责

DSP 需要实现**目标跟踪**，为每个目标分配**稳定的 `track_id`**：

- 同一目标在连续帧中保持相同的 `track_id`
- 新目标分配新的 `track_id`
- 消失的目标不再使用其 `track_id`

### 推荐算法：IoU 匹配

详见 [DSP_Multi_Target_Tracking_Design.md](docs/DSP_Multi_Target_Tracking_Design.md) 第 3 节。

## 共享内存布局

```
地址: 0x44800000 (DSP_DETECTION_BASE_ADDR)
      ├── Offset 0x000: DetectionResult_t frame_0  (664 bytes)
      ├── Offset 0x300: DetectionResult_t frame_1  (664 bytes)  // 双缓冲（可选）
      └── ...
```

## Mailbox 消息格式

32-bit 消息格式：

```
Bit [31:28]: 消息类型
Bit [27:0]:  Payload（DetectionResult 的 offset）

消息类型：
  0x0: 单目标（旧协议兼容，Payload = FaceRect offset）
  0x1: 多目标（新协议，Payload = DetectionResult offset）
  0xF: 本帧无检测结果
```

### 构造消息

```c
#define MAILBOX_MSG_TYPE_SINGLE    0x00000000u
#define MAILBOX_MSG_TYPE_MULTI     0x10000000u
#define MAILBOX_MSG_TYPE_NO_DETECT 0xF0000000u

// 构造多目标消息
uint32_t msg = MAILBOX_MSG_TYPE_MULTI | (offset & 0x0FFFFFFF);
mailbox_write(msg);
```

## DSP 侧代码示例

### 1. 初始化共享内存

```c
#include "detection_protocol.h"

// 双缓冲
static DetectionResult_t *g_result_buf[2];
static uint32_t g_buf_index = 0;
static uint32_t g_frame_counter = 0;

void detection_init(void)
{
    // 设置双缓冲地址
    g_result_buf[0] = (DetectionResult_t *)DSP_DETECTION_BASE_ADDR;
    g_result_buf[1] = (DetectionResult_t *)(DSP_DETECTION_BASE_ADDR + 0x300);
    
    // 初始化结构
    for (int i = 0; i < 2; i++) {
        g_result_buf[i]->magic = DETECTION_RESULT_MAGIC;
        g_result_buf[i]->version = DETECTION_PROTOCOL_VERSION;
        g_result_buf[i]->count = 0;
    }
}
```

### 2. 填充检测结果

```c
void detection_process_frame(const ImageFrame_t *frame)
{
    // 选择当前缓冲区
    DetectionResult_t *result = g_result_buf[g_buf_index];
    
    // 清空结果
    result->count = 0;
    result->frame_id = g_frame_counter++;
    result->timestamp = get_timestamp_ms();
    
    // 执行检测算法
    DetectionOutput_t raw_detections[32];
    int num_detections = run_detection_model(frame, raw_detections, 32);
    
    // 填充结果（限制最大数量）
    uint32_t count = 0;
    for (int i = 0; i < num_detections && count < MAX_DETECTION_COUNT; i++) {
        DetectionOutput_t *det = &raw_detections[i];
        
        // 置信度过滤
        if (det->score < 0.5f) continue;
        
        DetectionBox_t *box = &result->boxes[count];
        box->score = det->score;
        box->x1 = det->x1;
        box->y1 = det->y1;
        box->x2 = det->x2;
        box->y2 = det->y2;
        
        // 复制关键点（如果有）
        for (int j = 0; j < 10; j++) {
            box->lm[j] = det->landmarks[j];
        }
        
        box->type = DETECTION_TYPE_FACE;
        box->track_id = det->track_id;  // 如果有跟踪器
        
        count++;
    }
    
    result->count = count;
}
```

### 3. 通过 Mailbox 发送结果

```c
void detection_send_result(void)
{
    DetectionResult_t *result = g_result_buf[g_buf_index];
    
    // 计算 offset
    uint32_t offset = (uint32_t)((uintptr_t)result - DSP_DETECTION_BASE_ADDR);
    
    // 构造并发送消息
    uint32_t msg;
    if (result->count > 0) {
        msg = MAILBOX_MSG_TYPE_MULTI | (offset & 0x0FFFFFFF);
    } else {
        msg = MAILBOX_MSG_TYPE_NO_DETECT;
    }
    
    mailbox_write(msg);
    
    // 切换缓冲区
    g_buf_index = 1 - g_buf_index;
}
```

### 4. 主循环

```c
void detection_main_loop(void)
{
    detection_init();
    
    while (1) {
        // 等待新帧
        ImageFrame_t *frame = wait_for_frame();
        
        // 处理帧
        detection_process_frame(frame);
        
        // 发送结果到 M4
        detection_send_result();
    }
}
```

## 兼容性考虑

### 向后兼容（DSP 发送旧格式）

如果 DSP 暂时不升级，仍然发送旧格式消息：
- Mailbox 消息的高 4 位为 0（`MAILBOX_MSG_TYPE_SINGLE`）
- M4 侧会自动按单目标模式处理

### 向前兼容（M4 接收新格式）

M4 侧代码已经支持：
- `MAILBOX_MSG_TYPE_MULTI`: 解析 `DetectionResult_t`
- `MAILBOX_MSG_TYPE_SINGLE`: 解析单个 `DetectionBox_t`
- `MAILBOX_MSG_TYPE_NO_DETECT`: 清除显示

## 调试建议

1. **校验魔数**：M4 侧会校验 `magic == 0x44455446`，确保 DSP 正确填充
2. **打印日志**：在 DSP 侧打印每帧的 `count` 值
3. **边界检查**：确保坐标在 `[0, DISP_IMAGE_WIDTH/HEIGHT]` 范围内
4. **Mailbox 状态**：检查 Mailbox FIFO 是否满（避免阻塞）

## 性能考虑

1. **共享内存访问**：使用 32-bit 对齐访问
2. **双缓冲**：避免 M4 读取时 DSP 正在写入
3. **DCache**：如果启用 Cache，需要在写入后执行 `dcache_flush()`

## 文件清单

| 文件                       | 说明                        |
| -------------------------- | --------------------------- |
| `Inc/detection_protocol.h` | 共享协议定义（M4/DSP 共用） |
| `Src/face_tracker.c`       | M4 侧多目标处理实现         |
| `Inc/face_tracker.h`       | M4 侧接口定义               |

## 测试步骤

1. 确保 DSP 侧包含 `detection_protocol.h`
2. DSP 按上述示例填充 `DetectionResult_t`
3. DSP 通过 Mailbox 发送 `MAILBOX_MSG_TYPE_MULTI | offset`
4. M4 侧运行 `face_tracker_poll()`
5. 观察屏幕上是否正确显示多个边界框

---

如有问题，请联系固件团队。
