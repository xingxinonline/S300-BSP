# M4 集成指南 - DSP 跟踪结果处理

本文档描述 M4 MCU 如何处理 DSP 发送的跟踪结果，包括基于速度的箭头渲染以实现视觉反馈。

## 1. 通信协议

### 1.1 数据流

```
┌─────────┐  检测数据  ┌─────────┐  邮箱通信  ┌─────────┐  显示
│  摄像头  │ ──────────► │   DSP   │ ─────────► │   M4    │ ────────► LCD
└─────────┘             └─────────┘            └─────────┘
                         卡尔曼滤波
                         + 目标跟踪
```

### 1.2 邮箱消息格式

DSP 通过邮箱 FIFO 发送 `DetectionResult_t` 结构体：

```c
typedef struct {
    uint32_t magic;              // = 0x44455446 ("DETF")
    uint32_t version;            // = 0x0202 (v2.2)
    uint32_t frame_id;           // 帧计数器
    uint32_t timestamp;          // DSP 时间戳（毫秒）
    uint32_t count;              // 检测到的目标数量 (0-10)
    int32_t  selected_idx;       // 主跟踪目标索引 (-1 表示无目标)
    DetectionBox_t boxes[MAX_DETECTIONS];  // 检测框（最多10个）
} DetectionResult_t;  // 总大小: 704 字节
```

### 1.3 检测框结构体

```c
typedef struct __attribute__((packed)) {
    float    score;              // 检测置信度 [0.0, 1.0]
    int32_t  x1, y1;             // 左上角坐标
    int32_t  x2, y2;             // 右下角坐标
    float    lm[10];             // 5个关键点 (x0,y0,x1,y1,...,x4,y4)
    uint8_t  type;               // 检测类型 (1=人脸, 2=行人, 3=手势)
    uint8_t  track_id;           // 唯一跟踪ID (1-255, 0=未跟踪)
    int8_t   vx;                 // X方向速度 (像素/帧, 卡尔曼输出) [新增]
    int8_t   vy;                 // Y方向速度 (像素/帧, 卡尔曼输出) [新增]
    uint8_t  speed;              // 速度大小，用于箭头尺寸 [新增]
    uint8_t  kf_confidence;      // 卡尔曼滤波置信度 (0-100) [新增]
    uint8_t  reserved[2];        // 预留字段
} DetectionBox_t;  // 总大小: 68 字节
```

## 2. 速度字段说明

从 DSP Tracker v2.2 开始，每个 `DetectionBox_t` 包含卡尔曼滤波后的速度信息：

| 字段            | 类型    | 范围        | 说明                                  |
| --------------- | ------- | ----------- | ------------------------------------- |
| `vx`            | int8_t  | -127 ~ +127 | 水平速度（像素/帧），正值表示向右移动 |
| `vy`            | int8_t  | -127 ~ +127 | 垂直速度（像素/帧），正值表示向下移动 |
| `speed`         | uint8_t | 0 ~ 255     | 速度大小，用于控制箭头尺寸            |
| `kf_confidence` | uint8_t | 0 ~ 100     | 卡尔曼滤波置信度（预测时会降低）      |

### 2.1 速度计算公式

`speed` 字段计算方式：

$$
\text{speed} = \min\left(255, \sqrt{v_x^2 + v_y^2} \times 12.75\right)
$$

该公式将 0-20 像素/帧的速度映射到 0-255 的范围。

### 2.2 标志位定义

`flags` 字段用于指示跟踪状态（未来扩展）：

```c
#define FLAG_TRACKED        0x01  // 目标正在被跟踪
#define FLAG_SELECTED       0x02  // 这是主目标
#define FLAG_PREDICTED      0x04  // 位置为卡尔曼预测值（本帧无检测）
#define FLAG_NEW_TRACK      0x08  // 新获取的跟踪目标
#define FLAG_LOST           0x10  // 目标正在丢失（超过3帧无检测）
```

## 3. 箭头渲染算法

### 3.1 何时绘制箭头

满足以下条件时绘制速度箭头：
- `speed >= ARROW_MIN_SPEED`（建议值：10）
- 目标在屏幕上可见
- `kf_confidence > 0`

### 3.2 箭头参数配置

```c
// 配置参数
#define ARROW_MIN_SPEED       10    // 绘制箭头的最小速度
#define ARROW_SCALE           3     // 速度到长度的乘数
#define ARROW_HEAD_RATIO      0.3f  // 箭头头部尺寸比例
#define ARROW_WIDTH_BASE      2     // 基础线宽
#define ARROW_WIDTH_SCALE     0.1f  // 线宽随速度的缩放系数
```

### 3.3 M4 端 C 语言实现

```c
#include <math.h>

typedef struct {
    int16_t x1, y1;  // 线段起点（箭头尾部）
    int16_t x2, y2;  // 线段终点（箭头顶端）
    int16_t hx1, hy1, hx2, hy2;  // 箭头两翼点
    uint8_t width;   // 线宽
    uint8_t valid;   // 1 = 应该绘制箭头
} ArrowParams_t;

/**
 * @brief 根据速度计算箭头渲染参数
 * 
 * @param cx      边界框中心 X 坐标
 * @param cy      边界框中心 Y 坐标
 * @param vx      X 方向速度（像素/帧）
 * @param vy      Y 方向速度（像素/帧）
 * @param speed   速度大小 (0-255)
 * @param out     输出的箭头参数
 */
void calculate_arrow(int16_t cx, int16_t cy, 
                     int16_t vx, int16_t vy, 
                     uint8_t speed,
                     ArrowParams_t *out)
{
    // 速度过低时跳过
    if (speed < ARROW_MIN_SPEED) {
        out->valid = 0;
        return;
    }
    
    // 箭头尾部位于检测框中心
    out->x1 = cx;
    out->y1 = cy;
    
    // 箭头顶端：沿速度方向延伸
    out->x2 = cx + (int16_t)(vx * ARROW_SCALE);
    out->y2 = cy + (int16_t)(vy * ARROW_SCALE);
    
    // 计算箭头头部
    float dx = (float)(out->x2 - out->x1);
    float dy = (float)(out->y2 - out->y1);
    float len = sqrtf(dx * dx + dy * dy);
    
    if (len < 5.0f) {
        out->valid = 0;
        return;
    }
    
    // 单位向量
    float ux = dx / len;
    float uy = dy / len;
    
    // 箭头头部长度
    float head_len = len * ARROW_HEAD_RATIO;
    
    // 计算箭头两翼
    float bx = out->x2 - ux * head_len;
    float by = out->y2 - uy * head_len;
    float wing_len = head_len * 0.5f;
    
    out->hx1 = (int16_t)(bx - uy * wing_len);
    out->hy1 = (int16_t)(by + ux * wing_len);
    out->hx2 = (int16_t)(bx + uy * wing_len);
    out->hy2 = (int16_t)(by - ux * wing_len);
    
    // 线宽随速度增加
    out->width = ARROW_WIDTH_BASE + (uint8_t)(speed * ARROW_WIDTH_SCALE);
    out->valid = 1;
}
```

### 3.4 箭头绘制示例

```c
void draw_velocity_arrow(LCD_TypeDef *lcd, const DetectionBox_t *box)
{
    // 仅为选中目标绘制箭头
    if (box->speed < ARROW_MIN_SPEED) {
        return;
    }
    
    ArrowParams_t arrow;
    int16_t cx = (box->x1 + box->x2) / 2;
    int16_t cy = (box->y1 + box->y2) / 2;
    
    calculate_arrow(cx, cy, box->vx, box->vy, box->speed, &arrow);
    
    if (arrow.valid) {
        // 根据速度选择颜色
        uint16_t color;
        if (box->speed > 150) {
            color = COLOR_RED;        // 高速：红色
        } else if (box->speed > 80) {
            color = COLOR_YELLOW;     // 中速：黄色
        } else {
            color = COLOR_GREEN;      // 低速：绿色
        }
        
        // 绘制箭头主体
        lcd_draw_line(lcd, arrow.x1, arrow.y1, arrow.x2, arrow.y2, 
                      color, arrow.width);
        
        // 绘制箭头头部
        lcd_draw_line(lcd, arrow.x2, arrow.y2, arrow.hx1, arrow.hy1, 
                      color, arrow.width);
        lcd_draw_line(lcd, arrow.x2, arrow.y2, arrow.hx2, arrow.hy2, 
                      color, arrow.width);
    }
}
```

## 4. 跟踪状态处理

### 4.1 正常跟踪流程

```c
void process_detection_result(const DetectionResult_t *result)
{
    // 1. 验证数据有效性
    if (result->magic != DETECTION_MAGIC) {
        return;  // 无效数据
    }
    
    if (result->version != 0x0202) {
        // 版本不匹配，可能需要更新协议
        log_warning("协议版本不匹配: 0x%04X", result->version);
    }
    
    // 2. 处理选中目标
    if (result->selected_idx >= 0 && result->selected_idx < result->count) {
        const DetectionBox_t *target = &result->boxes[result->selected_idx];
        
        // 绘制边界框
        draw_bounding_box(target, COLOR_GREEN);
        
        // 绘制速度箭头
        draw_velocity_arrow(lcd, target);
        
        // 显示跟踪ID
        display_track_id(target->x1, target->y1 - 20, target->track_id);
    }
    
    // 3. 绘制其他目标（可选）
    for (uint32_t i = 0; i < result->count; i++) {
        if (i != result->selected_idx) {
            draw_bounding_box(&result->boxes[i], COLOR_GRAY);
        }
    }
}
```

### 4.2 目标丢失处理

当 `selected_idx == -1` 时，表示本帧没有检测到目标。M4 应该：

1. **短暂丢失**（`kf_confidence > 0`）：显示预测位置，使用虚线边框
2. **持续丢失**（多帧 `selected_idx == -1`）：清除显示，返回扫描状态

```c
static uint8_t s_lost_count = 0;
#define MAX_LOST_DISPLAY  10  // 丢失后最多继续显示10帧

void handle_target_lost(const DetectionResult_t *result)
{
    if (result->selected_idx >= 0) {
        s_lost_count = 0;
        return;  // 目标存在
    }
    
    s_lost_count++;
    
    if (s_lost_count <= MAX_LOST_DISPLAY) {
        // 使用上一帧的目标显示预测框
        draw_predicted_box(last_target, COLOR_YELLOW);  // 虚线黄色框
        display_status("目标丢失中...");
    } else {
        // 彻底丢失
        clear_tracking_display();
        display_status("搜索目标...");
    }
}
```

## 5. 数据结构大小说明

### 5.1 协议版本 v2.2 结构体大小

| 结构体              | 大小     | 说明                         |
| ------------------- | -------- | ---------------------------- |
| `DetectionBox_t`    | 68 字节  | 包含速度字段的检测框         |
| `DetectionResult_t` | 704 字节 | 完整检测结果（含10个检测框） |

### 5.2 编译时断言

M4 端应添加编译时断言确保结构体大小匹配：

```c
// 确保与 DSP 端结构体大小一致
_Static_assert(sizeof(DetectionBox_t) == 68, "DetectionBox_t 大小不匹配");
_Static_assert(sizeof(DetectionResult_t) == 704, "DetectionResult_t 大小不匹配");
```

## 6. 调试建议

### 6.1 UART 日志格式

DSP 跟踪器日志前缀：
- `[DSP:TRK]` - 跟踪器状态
- `[DSP:KF]` - 卡尔曼滤波器状态

### 6.2 常见问题排查

| 问题             | 可能原因                       | 解决方案                         |
| ---------------- | ------------------------------ | -------------------------------- |
| 箭头方向错误     | 坐标系不匹配                   | 检查 Y 轴方向定义                |
| 箭头抖动严重     | 卡尔曼参数不佳                 | 调整 DSP 端 Q/R 参数             |
| 目标ID频繁变化   | IoU 阈值过高                   | 降低 `TRACKER_IOU_THRESHOLD_PCT` |
| 丢失后无法恢复   | `TRACKER_MAX_LOST_FRAMES` 过小 | 增加丢失容忍帧数                 |
| 结构体大小不匹配 | 编译器对齐不一致               | 使用 `__attribute__((packed))`   |

### 6.3 性能指标

- 正常跟踪时延：<5ms（包含卡尔曼滤波）
- 目标切换时延：1 帧
- 丢失容忍时间：5 帧（约 250ms @ 20FPS）

## 7. 版本历史

| 版本 | 日期       | 更改内容                                    |
| ---- | ---------- | ------------------------------------------- |
| v2.2 | 2026-01-30 | 新增速度字段 (vx, vy, speed, kf_confidence) |
| v2.1 | 2026-01-29 | 新增丢失容忍机制，卡尔曼滤波器              |
| v2.0 | 2026-01-20 | 新增 track_id 跟踪                          |
| v1.0 | 2026-01-01 | 初始版本                                    |
