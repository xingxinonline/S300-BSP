# DSP 边界框过滤优化指南

**版本**: 1.0  
**日期**: 2026-01-30  
**状态**: 待实现  

---

## 1. 问题背景

### 1.1 现象描述

在单人追踪场景中，当用户静止不动时，出现以下问题：

1. **蓝框间歇性出现**：明明只有一个人，却偶尔显示蓝框（非选中目标）
2. **框闪烁**：检测框在某些帧消失或抖动

### 1.2 问题日志分析

```
[M4:FRAME] frame=337 count=2        ← DSP 报告 2 个目标
[M4:FRAME] valid_count=1            ← M4 过滤后只剩 1 个有效目标
[M4:SELECT] DSP selected_idx=1 invalid (count=1)  ← DSP 选的索引超出范围
[M4:DRAW] count=1 selected_idx=-1   ← 无选中目标，显示蓝框
```

### 1.3 根本原因

**DSP 和 M4 过滤逻辑不一致**：

```
┌─────────────────────────────────────────────────────────────────┐
│                        当前流程（有问题）                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   DSP 检测出 2 个目标                                           │
│   ├── box[0]: 有效目标 (主目标)                                 │
│   └── box[1]: 边缘/噪声目标 (坐标异常)                          │
│         ↓                                                       │
│   DSP 选择 box[1] (selected_idx = 1)  ← 可能因为更靠近中心      │
│         ↓                                                       │
│   DSP 发送 count=2, selected_idx=1                              │
│         ↓                                                       │
│   M4 接收后过滤:                                                │
│   ├── box[0]: ✓ 有效 → 保留为新 box[0]                         │
│   └── box[1]: ✗ 无效 (坐标越界/太小) → 丢弃                    │
│         ↓                                                       │
│   M4 有效目标: count=1                                          │
│   DSP selected_idx=1 > M4 count → 索引无效 → 显示蓝框           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 解决方案

### 2.1 核心原则

**DSP 在发送之前过滤无效目标**，确保：
- `count` = 有效目标数量
- `selected_idx` ∈ [0, count-1] 或 -1
- 所有 `boxes[0..count-1]` 都是有效的

### 2.2 过滤规则

DSP 应在填充 `DetectionResult_t` 之前，对每个检测框进行以下校验：

| 规则         | 条件                                                   | 说明             |
| ------------ | ------------------------------------------------------ | ---------------- |
| **坐标范围** | `x1 >= 0 && y1 >= 0`                                   | 左上角不能为负   |
| **坐标范围** | `x2 <= SCREEN_WIDTH && y2 <= SCREEN_HEIGHT`            | 右下角不能越界   |
| **坐标顺序** | `x1 < x2 && y1 < y2`                                   | 确保框有正面积   |
| **最小尺寸** | `(x2 - x1) > MIN_BOX_SIZE && (y2 - y1) > MIN_BOX_SIZE` | 过滤噪声检测     |
| **置信度**   | `score >= MIN_SCORE_THRESHOLD`                         | 过滤低置信度结果 |

**推荐参数：**

```c
#define SCREEN_WIDTH         240    /* 显示宽度 */
#define SCREEN_HEIGHT        320    /* 显示高度 */
#define MIN_BOX_SIZE         5      /* 最小边界框尺寸 (像素) */
#define MIN_SCORE_THRESHOLD  0.5f   /* 最小置信度阈值 */
```

---

## 3. DSP 参考实现

### 3.1 边界框校验函数

```c
/**
 * @brief 校验边界框是否有效
 * 
 * @param box 待校验的边界框
 * @return true 有效，false 无效（应丢弃）
 */
static bool is_box_valid(const DetectionBox_t *box)
{
    /* 1. 置信度检查 */
    if (box->score < MIN_SCORE_THRESHOLD) {
        return false;
    }
    
    /* 2. 坐标范围检查 */
    if (box->x1 < 0 || box->y1 < 0) {
        return false;
    }
    if (box->x2 > SCREEN_WIDTH || box->y2 > SCREEN_HEIGHT) {
        return false;
    }
    
    /* 3. 坐标顺序检查 */
    if (box->x1 >= box->x2 || box->y1 >= box->y2) {
        return false;
    }
    
    /* 4. 最小尺寸检查 */
    int32_t width = box->x2 - box->x1;
    int32_t height = box->y2 - box->y1;
    if (width <= MIN_BOX_SIZE || height <= MIN_BOX_SIZE) {
        return false;
    }
    
    return true;
}
```

### 3.2 过滤后的结果填充

```c
/**
 * @brief 填充检测结果（带过滤）
 * 
 * 将原始检测结果过滤后填充到 DetectionResult_t
 * 只保留有效的边界框，并重新计算 selected_idx
 * 
 * @param result      输出结果
 * @param raw_boxes   原始检测框数组
 * @param raw_count   原始检测框数量
 */
static void fill_detection_result_filtered(
    DetectionResult_t *result,
    const DetectionBox_t *raw_boxes,
    uint32_t raw_count)
{
    uint32_t valid_count = 0;
    int32_t raw_selected_idx = -1;  /* 记录原始选中索引 */
    int32_t new_selected_idx = -1;  /* 过滤后的选中索引 */
    
    /* 1. 先执行目标选择（在过滤之前，基于原始数据）*/
    raw_selected_idx = select_tracking_target(raw_boxes, raw_count);
    
    /* 2. 过滤并复制有效框 */
    for (uint32_t i = 0; i < raw_count && valid_count < MAX_DETECTION_COUNT; i++) {
        if (!is_box_valid(&raw_boxes[i])) {
            /* 无效框，跳过 */
            continue;
        }
        
        /* 复制有效框 */
        memcpy(&result->boxes[valid_count], &raw_boxes[i], sizeof(DetectionBox_t));
        
        /* 更新选中索引映射 */
        if ((int32_t)i == raw_selected_idx) {
            new_selected_idx = (int32_t)valid_count;
        }
        
        valid_count++;
    }
    
    /* 3. 填充结果头 */
    result->magic = DETECTION_RESULT_MAGIC;
    result->version = DETECTION_PROTOCOL_VERSION;
    result->count = valid_count;
    result->selected_idx = new_selected_idx;
    
    /* 4. 调试输出 */
    if (raw_count != valid_count) {
        printf("[DSP:FILTER] raw=%lu valid=%lu selected: %ld->%ld\r\n",
               (unsigned long)raw_count, (unsigned long)valid_count,
               (long)raw_selected_idx, (long)new_selected_idx);
    }
}
```

### 3.3 优化的目标选择（基于有效框）

更好的方案是**先过滤，再选择**，这样选择逻辑只处理有效目标：

```c
/**
 * @brief 填充检测结果（先过滤，再选择）
 * 
 * 推荐方案：确保选择逻辑只考虑有效目标
 */
static void fill_detection_result_v2(
    DetectionResult_t *result,
    const DetectionBox_t *raw_boxes,
    uint32_t raw_count)
{
    /* 1. 过滤并复制有效框 */
    uint32_t valid_count = 0;
    for (uint32_t i = 0; i < raw_count && valid_count < MAX_DETECTION_COUNT; i++) {
        if (is_box_valid(&raw_boxes[i])) {
            memcpy(&result->boxes[valid_count], &raw_boxes[i], sizeof(DetectionBox_t));
            valid_count++;
        }
    }
    
    /* 2. 基于有效框进行目标选择 */
    result->count = valid_count;
    result->selected_idx = select_tracking_target(result->boxes, valid_count);
    
    /* 3. 填充其他字段 */
    result->magic = DETECTION_RESULT_MAGIC;
    result->version = DETECTION_PROTOCOL_VERSION;
}
```

### 3.4 完整处理流程

```c
void detection_process_frame(const uint8_t *image)
{
    static DetectionBox_t raw_boxes[MAX_DETECTION_COUNT];
    static DetectionResult_t result;
    static uint32_t frame_counter = 0;
    static uint32_t buf_index = 0;
    
    if (!g_tracking_enabled) {
        return;
    }
    
    /* 1. 运行检测模型 */
    uint32_t raw_count = run_detection_model(image, raw_boxes, MAX_DETECTION_COUNT);
    
    /* 2. 跟踪器分配 track_id */
    tracker_assign_ids(raw_boxes, raw_count);
    
    /* 3. 卡尔曼滤波更新速度 */
    kalman_update(raw_boxes, raw_count);
    
    /* 4. 过滤 + 选择（关键步骤）★ */
    fill_detection_result_v2(&result, raw_boxes, raw_count);
    
    /* 5. 填充帧信息 */
    result.frame_id = frame_counter++;
    result.timestamp = get_timestamp_ms();
    
    /* 6. 写入共享内存 */
    uint32_t offset = (buf_index & 1) ? 0x300 : 0x000;
    memcpy((void*)(DSP_DETECTION_BASE_ADDR + offset), &result, sizeof(result));
    buf_index++;
    
    /* 7. 通知 M4 */
    uint32_t msg = MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_MULTI, offset);
    send_mailbox_ch0(msg);
}
```

---

## 4. 边界情况处理

### 4.1 所有框都被过滤

当原始检测有结果，但全部被过滤时：

```c
if (valid_count == 0) {
    result->count = 0;
    result->selected_idx = -1;
    /* 可选：发送 NO_DETECT 消息代替 MULTI 消息 */
}
```

### 4.2 选中目标被过滤

当之前选中的目标在当前帧被过滤掉时：

```c
/* 在 select_tracking_target() 中处理 */
if (g_selected_track_id != 0) {
    /* 尝试找到之前选中的目标 */
    for (uint32_t i = 0; i < count; i++) {
        if (boxes[i].track_id == g_selected_track_id) {
            return (int32_t)i;  /* 找到，继续跟踪 */
        }
    }
    /* 未找到（可能被过滤），重新选择 */
    g_selected_track_id = 0;
}

/* 重新选择最靠近中心的有效目标 */
return select_nearest_to_center(boxes, count);
```

### 4.3 坐标裁剪 vs 丢弃

对于部分越界的框，可以选择裁剪而非丢弃：

```c
/**
 * @brief 裁剪边界框到屏幕范围
 * @return true 裁剪后有效，false 完全越界应丢弃
 */
static bool clip_box_to_screen(DetectionBox_t *box)
{
    /* 裁剪到屏幕范围 */
    if (box->x1 < 0) box->x1 = 0;
    if (box->y1 < 0) box->y1 = 0;
    if (box->x2 > SCREEN_WIDTH) box->x2 = SCREEN_WIDTH;
    if (box->y2 > SCREEN_HEIGHT) box->y2 = SCREEN_HEIGHT;
    
    /* 检查裁剪后是否仍有效 */
    int32_t width = box->x2 - box->x1;
    int32_t height = box->y2 - box->y1;
    
    return (width > MIN_BOX_SIZE && height > MIN_BOX_SIZE);
}
```

---

## 5. 调试与验证

### 5.1 添加过滤日志

```c
/* 在过滤时输出详细日志 */
#define DSP_FILTER_DEBUG  1

#if DSP_FILTER_DEBUG
#define FILTER_LOG(fmt, ...) printf("[DSP:FILTER] " fmt, ##__VA_ARGS__)
#else
#define FILTER_LOG(fmt, ...) ((void)0)
#endif

static bool is_box_valid_debug(const DetectionBox_t *box, uint32_t idx)
{
    if (box->score < MIN_SCORE_THRESHOLD) {
        FILTER_LOG("box[%lu] rejected: score=%.2f < %.2f\r\n", 
                   (unsigned long)idx, (double)box->score, (double)MIN_SCORE_THRESHOLD);
        return false;
    }
    
    if (box->x1 < 0 || box->y1 < 0 || 
        box->x2 > SCREEN_WIDTH || box->y2 > SCREEN_HEIGHT) {
        FILTER_LOG("box[%lu] rejected: coords=(%ld,%ld,%ld,%ld) out of range\r\n",
                   (unsigned long)idx, 
                   (long)box->x1, (long)box->y1, (long)box->x2, (long)box->y2);
        return false;
    }
    
    int32_t w = box->x2 - box->x1;
    int32_t h = box->y2 - box->y1;
    if (w <= MIN_BOX_SIZE || h <= MIN_BOX_SIZE) {
        FILTER_LOG("box[%lu] rejected: size=%ldx%ld too small\r\n",
                   (unsigned long)idx, (long)w, (long)h);
        return false;
    }
    
    return true;
}
```

### 5.2 预期日志输出

修改后，M4 侧应该看到：

```
[M4:FRAME] frame=337 count=1        ← DSP 已过滤，只发送 1 个
[M4:FRAME] valid_count=1            ← M4 无需再过滤
[M4:SELECT] DSP selected idx=0 track_id=1  ← 索引有效
[M4:DRAW] count=1 selected_idx=0    ← 绿框正常显示
```

---

## 6. 检查清单

DSP 实现完成后，请确认以下事项：

- [ ] 添加 `is_box_valid()` 函数
- [ ] 修改结果填充逻辑，只包含有效框
- [ ] 确保 `selected_idx` 在 `[0, count-1]` 范围内或为 `-1`
- [ ] 添加过滤调试日志（可选）
- [ ] 测试边界情况：
  - [ ] 所有框被过滤 → count=0, selected_idx=-1
  - [ ] 选中目标被过滤 → 重新选择
  - [ ] 部分框被过滤 → 索引正确映射

---

## 7. 附录：M4 当前的过滤逻辑

以下是 M4 侧 `face_tracker.c` 中的过滤逻辑，DSP 应实现相同或更严格的过滤：

```c
/**
 * @brief 校验并规范化边界框坐标
 * @return true 如果坐标有效
 */
static bool validate_box(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2)
{
    /* 交换确保 x1 < x2, y1 < y2 */
    if (*x2 < *x1) { int32_t t = *x1; *x1 = *x2; *x2 = t; }
    if (*y2 < *y1) { int32_t t = *y1; *y1 = *y2; *y2 = t; }

    /* 范围检查 */
    if (*x1 < 0 || *y1 < 0) return false;
    if (*x2 > FACE_COORD_SPACE_W || *y2 > FACE_COORD_SPACE_H) return false;
    
    /* 最小尺寸检查 */
    if ((*x2 - *x1) <= 2 || (*y2 - *y1) <= 2) return false;

    return true;
}
```

**DSP 过滤参数应与 M4 保持一致：**

| 参数     | M4 值                      | DSP 推荐值                 |
| -------- | -------------------------- | -------------------------- |
| 屏幕宽度 | `FACE_COORD_SPACE_W` (240) | `SCREEN_WIDTH` (240)       |
| 屏幕高度 | `FACE_COORD_SPACE_H` (320) | `SCREEN_HEIGHT` (320)      |
| 最小尺寸 | 2 像素                     | 5 像素（更严格，减少噪声） |

---

## 8. 版本历史

| 版本 | 日期       | 变更                             |
| ---- | ---------- | -------------------------------- |
| 1.0  | 2026-01-30 | 初始版本，定义过滤规则和参考实现 |
