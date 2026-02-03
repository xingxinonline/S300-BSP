/**
 * @file face_tracker.c
 * @brief 单人追踪模块实现
 * 
 * 应用场景：画面可能有多人，只追踪 1 人
 * 
 * 职责分工：
 *  - CM4：启动/停止追踪命令，显示边界框
 *  - DSP：检测、跟踪、选择最接近中心的目标
 * 
 * 协议版本：v2.1
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "mailbox.h"
#include "mailbox_proto.h"
#include "detection_proto.h"
#include "video.h"
#include "face_tracker.h"

/*===========================================================================
 * 配置与常量
 *===========================================================================*/

/** 调试打印级别: 0=关闭, 1=关键事件, 2=每帧摘要, 3=详细 */
#ifndef FACE_TRACKER_DEBUG_LEVEL
#define FACE_TRACKER_DEBUG_LEVEL  2
#endif

#define FT_LOG(level, fmt, ...) \
    do { if (FACE_TRACKER_DEBUG_LEVEL >= (level)) printf(fmt, ##__VA_ARGS__); } while(0)

#ifndef FACE_COORD_SPACE_W
#define FACE_COORD_SPACE_W DISP_IMAGE_WIDTH
#endif
#ifndef FACE_COORD_SPACE_H
#define FACE_COORD_SPACE_H DISP_IMAGE_HEIGHT
#endif

/** 边界框线宽（像素）*/
#define BOX_LINE_WIDTH  2

/** RGB565 颜色定义 */
#define COLOR_GREEN     0x07E0u  /**< 选中目标：绿色 */
#define COLOR_BLUE      0x001Fu  /**< 非选中目标：蓝色 */
#define COLOR_GRAY      0x8410u  /**< 备选：灰色 */
#define COLOR_RED       0xF800u  /**< 快速移动箭头：红色 */
#define COLOR_YELLOW    0xFFE0u  /**< 慢速移动箭头：黄色 */
#define COLOR_ORANGE    0xFD20u  /**< 预测状态：橙色 */
#define COLOR_CYAN      0x07FFu  /**< 速度箭头：青色（固定）*/

/*===========================================================================
 * 内部数据结构
 *===========================================================================*/

/** 跟踪的单个目标状态 */
typedef struct {
    int32_t  x1, y1, x2, y2;  /**< 边界框坐标 */
    int16_t  cx, cy;          /**< 中心点坐标 */
    int8_t   vx, vy;          /**< 速度 (pixels/frame) */
    uint8_t  track_id;        /**< 跟踪 ID */
    uint8_t  speed;           /**< 速度大小 [0-255] */
    uint8_t  kf_confidence;   /**< 卡尔曼滤波置信度 (0-100) */
    bool     valid;           /**< 是否有效 */
    bool     selected;        /**< 是否为选中目标 */
    bool     arrow_visible;   /**< 箭头是否可见（滞后防抖）*/
} TrackedBox_t;

/** 上一帧绘制状态（用于增量更新，避免撕裂）*/
typedef struct {
    int32_t  x1, y1, x2, y2;  /**< 边界框坐标 */
    int16_t  cx, cy;          /**< 中心点坐标 */
    int8_t   vx, vy;          /**< 速度 */
    bool     valid;           /**< 是否有效（是否需要清除）*/
    bool     arrow_visible;   /**< 箭头是否可见 */
} PrevBox_t;

/** 模块状态 */
static struct {
    uint32_t (*get_millis)(void);           /**< 毫秒获取函数 */
    TrackedBox_t boxes[MAX_DETECTION_COUNT];/**< 跟踪目标数组 */
    /* 双缓冲各自的上一帧状态（Buffer 0 画帧 0,2,4...，Buffer 1 画帧 1,3,5...）*/
    PrevBox_t prev_boxes_buf[2][MAX_DETECTION_COUNT]; /**< 每个缓冲区独立的上一帧状态 */
    uint32_t prev_count_buf[2];             /**< 每个缓冲区独立的上一帧目标数量 */
    uint32_t count;                         /**< 当前目标数量 */
    uint32_t last_update_ts;                /**< 最后更新时间戳 */
    int32_t  selected_idx;                  /**< 选中目标的索引，-1表示无 */
    uint8_t  selected_track_id;             /**< 选中目标的 track_id（用于跨帧维持）*/
    bool     has_selected_track;            /**< 是否有选中的 track_id */
    bool     is_running;                    /**< 追踪是否运行中（预留）*/
    FaceTrackerStats_t stats;               /**< 统计信息 */
    /* 性能统计 */
    uint32_t perf_last_frame_ts;            /**< 上一帧时间戳（用于帧间隔计算）*/
    uint32_t perf_clear_time;               /**< 清除耗时 (ms) */
    uint32_t perf_draw_time;                /**< 绘制耗时 (ms) */
    uint32_t perf_total_time;               /**< 总处理耗时 (ms) */
    uint32_t perf_frame_interval;           /**< 帧间隔 (ms) */
    /* no_detect 低频打印控制 */
    uint32_t no_detect_count;               /**< 连续无检测帧数 */
    uint32_t no_detect_last_print_ts;       /**< 上次打印时间戳 */
    /* 双缓冲控制 */
    uint8_t  front_idx;                     /**< 当前显示的缓冲区: 0 或 1 */
} s_ctx = {0};

/*===========================================================================
 * 双缓冲寄存器定义
 *===========================================================================*/
/* REG32 已在 video.h 中定义 */
#define REG_FRAME0   (DSP_VIDEO_SS_BASE + 0x50u)  /**< Frame0 显示触发寄存器 */
#define REG_FRAME1   (DSP_VIDEO_SS_BASE + 0x54u)  /**< Frame1 显示触发寄存器 */

/*===========================================================================
 * 双缓冲辅助函数
 *===========================================================================*/

/**
 * @brief 获取当前后台帧缓冲地址（用于绘制）
 */
static inline volatile uint16_t * get_back_framebuffer(void)
{
    return (s_ctx.front_idx == 0u) 
           ? (volatile uint16_t *)DISP_RFRAME1_ADDR 
           : (volatile uint16_t *)DISP_RFRAME0_ADDR;
}

/**
 * @brief 获取当前后台 Alpha 缓冲地址（用于绘制）
 */
static inline volatile uint16_t * get_back_alphabuffer(void)
{
    return (s_ctx.front_idx == 0u) 
           ? (volatile uint16_t *)DISP_RALPHA1_ADDR 
           : (volatile uint16_t *)DISP_RALPHA0_ADDR;
}

/**
 * @brief 切换显示缓冲区（将后台变为前台）
 */
static void swap_buffers(void)
{
    uint8_t back_idx = (s_ctx.front_idx == 0u) ? 1u : 0u;
    
    if (back_idx == 0u) {
        REG32(REG_FRAME0) = 1u;
        while ((REG32(REG_FRAME0) & 0x1u) != 0u) { /* 等待硬件受理 */ }
    } else {
        REG32(REG_FRAME1) = 1u;
        while ((REG32(REG_FRAME1) & 0x1u) != 0u) { /* 等待硬件受理 */ }
    }
    
    s_ctx.front_idx = back_idx;
    FT_LOG(3, "[M4:SWAP] front_idx=%u\r\n", (unsigned)s_ctx.front_idx);
}

/*===========================================================================
 * 绘图辅助函数（使用双缓冲后台缓冲区）
 *===========================================================================*/

/**
 * @brief 设置单个像素的颜色和 Alpha（写入后台缓冲区）
 */
static void set_pixel(uint32_t x, uint32_t y, uint16_t color, uint8_t alpha)
{
    uint32_t pixel_idx = y * DISP_IMAGE_WIDTH + x;
    
    /* 设置颜色（写入后台 RGB565 framebuffer）*/
    volatile uint16_t *fb = get_back_framebuffer();
    fb[pixel_idx] = color;
    
    /* 设置 Alpha（写入后台 Alpha 缓冲，需要 16-bit 对齐访问）*/
    volatile uint16_t *ab16 = get_back_alphabuffer();
    uint32_t word_idx = pixel_idx / 2;
    uint32_t byte_pos = pixel_idx & 1;
    
    uint16_t val = ab16[word_idx];
    if (byte_pos == 0) {
        val = (val & 0xFF00) | alpha;
    } else {
        val = (val & 0x00FF) | ((uint16_t)alpha << 8);
    }
    ab16[word_idx] = val;
}

/**
 * @brief 设置单个像素的 Alpha 值（写入后台缓冲区）
 */
static void set_pixel_alpha(uint32_t x, uint32_t y, uint8_t alpha)
{
    volatile uint16_t *ab16 = get_back_alphabuffer();
    uint32_t pixel_idx = y * DISP_IMAGE_WIDTH + x;
    uint32_t word_idx = pixel_idx / 2;
    uint32_t byte_pos = pixel_idx & 1;
    
    uint16_t val = ab16[word_idx];
    if (byte_pos == 0) {
        val = (val & 0xFF00) | alpha;
    } else {
        val = (val & 0x00FF) | ((uint16_t)alpha << 8);
    }
    ab16[word_idx] = val;
}

/**
 * @brief 绘制彩色矩形边框
 */
static void draw_color_rect_border(int x1, int y1, int x2, int y2, 
                                    uint16_t color, uint8_t alpha, int line_width)
{
    /* 坐标裁剪 */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= DISP_IMAGE_WIDTH) x2 = DISP_IMAGE_WIDTH - 1;
    if (y2 >= DISP_IMAGE_HEIGHT) y2 = DISP_IMAGE_HEIGHT - 1;
    if (x1 > x2 || y1 > y2) return;

    /* 绘制多条水平线（上边和下边）*/
    for (int w = 0; w < line_width && (y1 + w) <= y2; w++) {
        for (int x = x1; x <= x2; x++) {
            set_pixel((uint32_t)x, (uint32_t)(y1 + w), color, alpha);
        }
    }
    for (int w = 0; w < line_width && (y2 - w) >= y1; w++) {
        for (int x = x1; x <= x2; x++) {
            set_pixel((uint32_t)x, (uint32_t)(y2 - w), color, alpha);
        }
    }

    /* 绘制多条垂直线（左边和右边）*/
    for (int w = 0; w < line_width && (x1 + w) <= x2; w++) {
        for (int y = y1; y <= y2; y++) {
            set_pixel((uint32_t)(x1 + w), (uint32_t)y, color, alpha);
        }
    }
    for (int w = 0; w < line_width && (x2 - w) >= x1; w++) {
        for (int y = y1; y <= y2; y++) {
            set_pixel((uint32_t)(x2 - w), (uint32_t)y, color, alpha);
        }
    }
}

/**
 * @brief 绘制矩形边框（仅 Alpha，保持原有接口）
 */
static void draw_rect_border(int x1, int y1, int x2, int y2, uint8_t alpha, int line_width)
{
    /* 坐标裁剪 */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= DISP_IMAGE_WIDTH) x2 = DISP_IMAGE_WIDTH - 1;
    if (y2 >= DISP_IMAGE_HEIGHT) y2 = DISP_IMAGE_HEIGHT - 1;
    if (x1 > x2 || y1 > y2) return;

    /* 绘制多条水平线（上边和下边）*/
    for (int w = 0; w < line_width && (y1 + w) <= y2; w++) {
        for (int x = x1; x <= x2; x++) {
            set_pixel_alpha((uint32_t)x, (uint32_t)(y1 + w), alpha);
        }
    }
    for (int w = 0; w < line_width && (y2 - w) >= y1; w++) {
        for (int x = x1; x <= x2; x++) {
            set_pixel_alpha((uint32_t)x, (uint32_t)(y2 - w), alpha);
        }
    }

    /* 绘制多条垂直线（左边和右边）*/
    for (int w = 0; w < line_width && (x1 + w) <= x2; w++) {
        for (int y = y1; y <= y2; y++) {
            set_pixel_alpha((uint32_t)(x1 + w), (uint32_t)y, alpha);
        }
    }
    for (int w = 0; w < line_width && (x2 - w) >= x1; w++) {
        for (int y = y1; y <= y2; y++) {
            set_pixel_alpha((uint32_t)(x2 - w), (uint32_t)y, alpha);
        }
    }
}

/*===========================================================================
 * 速度箭头绘制函数
 *===========================================================================*/

/**
 * @brief 绘制直线（Bresenham 算法）
 */
static void draw_line(int x0, int y0, int x1, int y1, uint16_t color, uint8_t alpha)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        if (x0 >= 0 && x0 < DISP_IMAGE_WIDTH && y0 >= 0 && y0 < DISP_IMAGE_HEIGHT) {
            set_pixel((uint32_t)x0, (uint32_t)y0, color, alpha);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/**
 * @brief 绘制速度箭头
 * 
 * @param cx    中心点 X
 * @param cy    中心点 Y
 * @param vx    速度 X (pixels/frame)
 * @param vy    速度 Y (pixels/frame)
 * @param speed 速度大小 [0-255]
 * @param color 箭头颜色
 */
static void draw_velocity_arrow(int cx, int cy, int8_t vx, int8_t vy, 
                                 uint16_t color)
{
    /* 箭头终点 */
    int x2 = cx + vx * ARROW_SCALE;
    int y2 = cy + vy * ARROW_SCALE;
    
    /* 绘制主线（从中心到终点）*/
    draw_line(cx, cy, x2, y2, color, 0xFF);
    
    /* 计算箭头方向 */
    float dx = (float)(x2 - cx);
    float dy = (float)(y2 - cy);
    float len = dx * dx + dy * dy;
    
    if (len < 4.0f) return;  /* 太短不画箭头 */
    
    /* 简化的箭头头部计算（避免使用 sqrt/sin/cos）*/
    /* 箭头翼点（简化计算）*/
    int hx1 = x2 - (int)(dx * 0.3f) - (int)(dy * 0.15f);
    int hy1 = y2 - (int)(dy * 0.3f) + (int)(dx * 0.15f);
    int hx2 = x2 - (int)(dx * 0.3f) + (int)(dy * 0.15f);
    int hy2 = y2 - (int)(dy * 0.3f) - (int)(dx * 0.15f);
    
    /* 绘制箭头翼 */
    draw_line(x2, y2, hx1, hy1, color, 0xFF);
    draw_line(x2, y2, hx2, hy2, color, 0xFF);
}

/**
 * @brief 清除速度箭头（用于下一帧前清除）
 * @note  由调用方确保只在箭头可见时调用
 */
static void clear_velocity_arrow(int cx, int cy, int8_t vx, int8_t vy)
{
    
    int x2 = cx + vx * ARROW_SCALE;
    int y2 = cy + vy * ARROW_SCALE;
    
    draw_line(cx, cy, x2, y2, 0x0000, 0x00);
    
    float dx = (float)(x2 - cx);
    float dy = (float)(y2 - cy);
    
    int hx1 = x2 - (int)(dx * 0.3f) - (int)(dy * 0.15f);
    int hy1 = y2 - (int)(dy * 0.3f) + (int)(dx * 0.15f);
    int hx2 = x2 - (int)(dx * 0.3f) + (int)(dy * 0.15f);
    int hy2 = y2 - (int)(dy * 0.3f) - (int)(dx * 0.15f);
    
    draw_line(x2, y2, hx1, hy1, 0x0000, 0x00);
    draw_line(x2, y2, hx2, hy2, 0x0000, 0x00);
}

/*===========================================================================
 * 内部处理函数
 *===========================================================================*/

static inline uint32_t millis(void)
{
    return s_ctx.get_millis ? s_ctx.get_millis() : 0u;
}

/**
 * @brief 获取后台缓冲区索引（与 front_idx 相反）
 */
static inline uint8_t get_back_buffer_idx(void)
{
    return (s_ctx.front_idx == 0u) ? 1u : 0u;
}

/**
 * @brief 保存当前帧状态到后台缓冲区的 prev_boxes
 * 
 * 双缓冲机制：Buffer 0 画帧 0,2,4...，Buffer 1 画帧 1,3,5...
 * 每个缓冲区需要记住自己上次画了什么，以便下次清除
 */
static void save_current_to_back_prev(void)
{
    uint8_t back_idx = get_back_buffer_idx();
    for (uint32_t i = 0; i < s_ctx.count; i++) {
        s_ctx.prev_boxes_buf[back_idx][i].x1 = s_ctx.boxes[i].x1;
        s_ctx.prev_boxes_buf[back_idx][i].y1 = s_ctx.boxes[i].y1;
        s_ctx.prev_boxes_buf[back_idx][i].x2 = s_ctx.boxes[i].x2;
        s_ctx.prev_boxes_buf[back_idx][i].y2 = s_ctx.boxes[i].y2;
        s_ctx.prev_boxes_buf[back_idx][i].cx = s_ctx.boxes[i].cx;
        s_ctx.prev_boxes_buf[back_idx][i].cy = s_ctx.boxes[i].cy;
        s_ctx.prev_boxes_buf[back_idx][i].vx = s_ctx.boxes[i].vx;
        s_ctx.prev_boxes_buf[back_idx][i].vy = s_ctx.boxes[i].vy;
        s_ctx.prev_boxes_buf[back_idx][i].valid = s_ctx.boxes[i].valid;
        s_ctx.prev_boxes_buf[back_idx][i].arrow_visible = s_ctx.boxes[i].arrow_visible;
    }
    s_ctx.prev_count_buf[back_idx] = s_ctx.count;
}

/**
 * @brief 清除后台缓冲区上一次绘制的边界框和箭头
 */
static void clear_back_prev_boxes(void)
{
    uint8_t back_idx = get_back_buffer_idx();
    uint32_t prev_count = s_ctx.prev_count_buf[back_idx];
    
    for (uint32_t i = 0; i < prev_count; i++) {
        if (s_ctx.prev_boxes_buf[back_idx][i].valid) {
            /* 清除边界框 */
            draw_rect_border(
                s_ctx.prev_boxes_buf[back_idx][i].x1, s_ctx.prev_boxes_buf[back_idx][i].y1,
                s_ctx.prev_boxes_buf[back_idx][i].x2, s_ctx.prev_boxes_buf[back_idx][i].y2,
                0x00, BOX_LINE_WIDTH
            );
            /* 只有当箭头可见时才清除 */
            if (s_ctx.prev_boxes_buf[back_idx][i].arrow_visible) {
                clear_velocity_arrow(
                    s_ctx.prev_boxes_buf[back_idx][i].cx, s_ctx.prev_boxes_buf[back_idx][i].cy,
                    s_ctx.prev_boxes_buf[back_idx][i].vx, s_ctx.prev_boxes_buf[back_idx][i].vy
                );
            }
            s_ctx.prev_boxes_buf[back_idx][i].valid = false;
        }
    }
    s_ctx.prev_count_buf[back_idx] = 0;
}

/**
 * @brief 清除当前帧的所有边界框和箭头（用于停止、初始化等场景）
 * 
 * 注意：这个函数只清除当前后台缓冲区的内容
 */
static void clear_all_boxes(void)
{
    for (uint32_t i = 0; i < s_ctx.count; i++) {
        if (s_ctx.boxes[i].valid) {
            /* 清除边界框 */
            draw_rect_border(
                s_ctx.boxes[i].x1, s_ctx.boxes[i].y1,
                s_ctx.boxes[i].x2, s_ctx.boxes[i].y2,
                0x00, BOX_LINE_WIDTH
            );
            /* 只有当箭头可见时才清除 */
            if (s_ctx.boxes[i].arrow_visible) {
                clear_velocity_arrow(
                    s_ctx.boxes[i].cx, s_ctx.boxes[i].cy,
                    s_ctx.boxes[i].vx, s_ctx.boxes[i].vy
                );
            }
            s_ctx.boxes[i].valid = false;
        }
    }
    s_ctx.count = 0;
    /* 清除两个缓冲区的记录 */
    s_ctx.prev_count_buf[0] = 0;
    s_ctx.prev_count_buf[1] = 0;
}

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

/**
 * @brief 绘制所有边界框和速度箭头
 * 
 * 使用 DSP 提供的 selected_idx：选中目标画绿框，其他画蓝框
 * 根据卡尔曼滤波速度绘制方向箭头（带滞后防抖）
 */
static void draw_all_boxes(void)
{
    for (uint32_t i = 0; i < s_ctx.count; i++) {
        if (!s_ctx.boxes[i].valid) continue;
        
        bool is_selected = ((int32_t)i == s_ctx.selected_idx);
        s_ctx.boxes[i].selected = is_selected;
        
        /* 选择边界框颜色：选中=绿色，其他=蓝色 */
        uint16_t box_color = is_selected ? COLOR_GREEN : COLOR_BLUE;
        
        /* 绘制边界框 */
        draw_color_rect_border(
            s_ctx.boxes[i].x1, s_ctx.boxes[i].y1,
            s_ctx.boxes[i].x2, s_ctx.boxes[i].y2,
            box_color, 0xFF, BOX_LINE_WIDTH
        );
        
        /* 绘制速度箭头（仅对选中目标且有卡尔曼置信度时）*/
        if (is_selected && s_ctx.boxes[i].kf_confidence > 0) {
            uint8_t spd = s_ctx.boxes[i].speed;
            
            /* 滞后逻辑：防止箭头在阈值附近频繁闪烁 */
            if (s_ctx.boxes[i].arrow_visible) {
                /* 已显示：速度降到低阈值以下才隐藏 */
                if (spd < ARROW_HIDE_THRESHOLD) {
                    s_ctx.boxes[i].arrow_visible = false;
                }
            } else {
                /* 未显示：速度超过高阈值才显示 */
                if (spd > ARROW_SHOW_THRESHOLD) {
                    s_ctx.boxes[i].arrow_visible = true;
                }
            }
            
            /* 根据滞后结果绘制箭头 */
            if (s_ctx.boxes[i].arrow_visible) {
                draw_velocity_arrow(
                    s_ctx.boxes[i].cx, s_ctx.boxes[i].cy,
                    s_ctx.boxes[i].vx, s_ctx.boxes[i].vy,
                    COLOR_CYAN
                );
            }
        } else {
            /* 非选中或无卡尔曼置信度时，重置箭头状态 */
            s_ctx.boxes[i].arrow_visible = false;
        }
    }
    
    /* 每帧摘要 */
    FT_LOG(2, "[M4:DRAW] count=%lu selected_idx=%ld\r\n",
           (unsigned long)s_ctx.count, (long)s_ctx.selected_idx);
}

/**
 * @brief 处理多目标检测结果（协议 v2.1）
 * 
 * DSP 负责选择目标，通过 selected_idx 字段告知 CM4
 * 
 * 更新策略（防撕裂）：
 *  1. 保存当前帧到 prev_boxes
 *  2. 解析新数据到 boxes
 *  3. 先绘制新框（新内容先出现）
 *  4. 再清除上一帧的旧框（旧内容后消失）
 *  这样避免出现"完全空白"的中间状态
 */
static void process_multi_detection(const DetectionResult_t *result)
{
    uint32_t t_start = millis();
    uint32_t t_draw, t_clear;
    
    if (!DETECTION_RESULT_IS_VALID(result)) {
        FT_LOG(1, "[M4:ERR] Invalid DetectionResult @0x%08lX (magic=0x%08lX, count=%lu)\r\n",
               (unsigned long)(uintptr_t)result,
               (unsigned long)result->magic, (unsigned long)result->count);
        return;
    }

    /* 计算帧间隔 */
    if (s_ctx.perf_last_frame_ts > 0) {
        s_ctx.perf_frame_interval = t_start - s_ctx.perf_last_frame_ts;
    }
    s_ctx.perf_last_frame_ts = t_start;

    /* 有检测结果，重置 no_detect 计数 */
    s_ctx.no_detect_count = 0;

    /* 更新统计 */
    s_ctx.stats.frame_count++;
    s_ctx.stats.last_frame_id = result->frame_id;

    /* 收集所有有效目标 */
    uint32_t count = result->count;
    if (count > MAX_DETECTION_COUNT) count = MAX_DETECTION_COUNT;

    /* 打印帧信息 */
    FT_LOG(2, "[M4:FRAME] frame=%lu count=%lu\r\n", 
           (unsigned long)result->frame_id, (unsigned long)count);

    /* 步骤2：解析新数据（直接使用 DSP 发送的数据，DSP 已过滤）*/
    for (uint32_t i = 0; i < count; i++) {
        const DetectionBox_t *box = &result->boxes[i];
        
        /* 保存跟踪状态 */
        s_ctx.boxes[i].x1 = box->x1;
        s_ctx.boxes[i].y1 = box->y1;
        s_ctx.boxes[i].x2 = box->x2;
        s_ctx.boxes[i].y2 = box->y2;
        s_ctx.boxes[i].cx = (int16_t)((box->x1 + box->x2) / 2);
        s_ctx.boxes[i].cy = (int16_t)((box->y1 + box->y2) / 2);
        s_ctx.boxes[i].vx = box->vx;
        s_ctx.boxes[i].vy = box->vy;
        s_ctx.boxes[i].track_id = box->track_id;
        s_ctx.boxes[i].speed = box->speed;
        s_ctx.boxes[i].kf_confidence = box->kf_confidence;
        s_ctx.boxes[i].valid = true;
        s_ctx.boxes[i].selected = false;
        
        FT_LOG(3, "[M4:BOX] [%lu] track_id=%u box=(%ld,%ld,%ld,%ld) v=(%d,%d) speed=%u\r\n",
               (unsigned long)i, box->track_id,
               (long)box->x1, (long)box->y1, (long)box->x2, (long)box->y2,
               (int)box->vx, (int)box->vy, (unsigned)box->speed);
    }
    s_ctx.count = count;
    
    /* 打印第一个目标的卡尔曼数据（用于调试）*/
    if (count > 0) {
        FT_LOG(2, "[M4:KALMAN] box[0] vx=%d vy=%d speed=%u kf_conf=%u\r\n",
               (int)s_ctx.boxes[0].vx, (int)s_ctx.boxes[0].vy,
               (unsigned)s_ctx.boxes[0].speed, (unsigned)s_ctx.boxes[0].kf_confidence);
    }

    /* 直接使用 DSP 提供的 selected_idx（DSP 已确保有效）*/
    s_ctx.selected_idx = result->selected_idx;
    if (s_ctx.selected_idx >= 0 && s_ctx.selected_idx < (int32_t)count) {
        FT_LOG(2, "[M4:SELECT] DSP selected idx=%ld track_id=%u\r\n",
               (long)s_ctx.selected_idx, s_ctx.boxes[s_ctx.selected_idx].track_id);
    } else if (count > 0) {
        FT_LOG(2, "[M4:SELECT] No selection (idx=%ld)\r\n", (long)s_ctx.selected_idx);
    }

    /* 步骤3：在后台缓冲区清除旧框并绘制新框 */
    clear_back_prev_boxes();  /* 清除后台缓冲区中该缓冲区上一次绘制的残留 */
    draw_all_boxes();         /* 在后台缓冲区绘制新框 */
    save_current_to_back_prev(); /* 保存当前绘制内容到后台缓冲区的 prev */
    t_draw = millis();
    
    /* 步骤4：交换缓冲区（原子切换，彻底消除撕裂）*/
    swap_buffers();
    t_clear = millis();

    /* 更新统计 */
    s_ctx.stats.current_count = s_ctx.count;
    if (s_ctx.count > s_ctx.stats.max_count) {
        s_ctx.stats.max_count = s_ctx.count;
    }

    s_ctx.last_update_ts = millis();
    
    /* 性能统计 */
    s_ctx.perf_draw_time = t_draw - t_start;
    s_ctx.perf_clear_time = t_clear - t_draw;
    s_ctx.perf_total_time = s_ctx.last_update_ts - t_start;
    
    /* 打印性能信息 */
    FT_LOG(2, "[M4:PERF] interval=%lums draw=%lums swap=%lums total=%lums fb=%u\r\n",
           (unsigned long)s_ctx.perf_frame_interval,
           (unsigned long)s_ctx.perf_draw_time,
           (unsigned long)s_ctx.perf_clear_time,
           (unsigned long)s_ctx.perf_total_time,
           (unsigned)s_ctx.front_idx);
}

/**
 * @brief 处理单目标检测结果（旧协议 v1.0 兼容）
 */
static void process_single_detection(const DetectionBox_t *fr)
{
    int32_t x1 = fr->x1, y1 = fr->y1, x2 = fr->x2, y2 = fr->y2;

    /* 在后台缓冲区清除该缓冲区上一次绘制的旧框 */
    clear_back_prev_boxes();

    s_ctx.stats.frame_count++;
    s_ctx.no_detect_count = 0;

    if (!validate_box(&x1, &y1, &x2, &y2)) {
        /* 无效坐标，表示目标丢失 */
        s_ctx.selected_idx = -1;
        s_ctx.count = 0;
        save_current_to_back_prev();
        swap_buffers();
        return;
    }

    /* 保存跟踪状态 */
    s_ctx.boxes[0].x1 = x1;
    s_ctx.boxes[0].y1 = y1;
    s_ctx.boxes[0].x2 = x2;
    s_ctx.boxes[0].y2 = y2;
    s_ctx.boxes[0].track_id = 0;
    s_ctx.boxes[0].valid = true;
    s_ctx.boxes[0].selected = true;
    s_ctx.count = 1;
    s_ctx.selected_idx = 0;

    /* 在后台缓冲区绘制，单目标始终选中，画绿框 */
    draw_color_rect_border(x1, y1, x2, y2, COLOR_GREEN, 0xFF, BOX_LINE_WIDTH);
    
    /* 保存当前绘制内容到后台缓冲区的 prev */
    save_current_to_back_prev();
    
    /* 交换缓冲区 */
    swap_buffers();

    s_ctx.stats.current_count = 1;
    if (s_ctx.stats.max_count < 1) s_ctx.stats.max_count = 1;

    s_ctx.last_update_ts = millis();
}

/*===========================================================================
 * 公开 API 实现
 *===========================================================================*/

/**
 * @brief 清除指定缓冲区的所有 Alpha 值（使整个 overlay 透明）
 */
static void clear_alpha_buffer(uint8_t buf_idx)
{
    volatile uint16_t *ab16 = (buf_idx == 0u) 
                              ? (volatile uint16_t *)DISP_RALPHA0_ADDR 
                              : (volatile uint16_t *)DISP_RALPHA1_ADDR;
    uint32_t total_words = (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT) / 2;
    for (uint32_t i = 0; i < total_words; i++) {
        ab16[i] = 0x0000;  /* 两个像素的 Alpha 都设为 0（透明）*/
    }
}

void face_tracker_init(uint32_t (*get_millis_fn)(void))
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.get_millis = get_millis_fn;
    s_ctx.selected_idx = -1;
    s_ctx.has_selected_track = false;
    s_ctx.is_running = true;  /* 默认启动 */
    s_ctx.front_idx = 0;      /* 初始显示 buffer 0 */
    
    /* 清除两个 Alpha 缓冲区（确保初始无残留）*/
    clear_alpha_buffer(0);
    clear_alpha_buffer(1);
    
    printf("[FaceTracker] Initialized (double-buffered, max_detect=%d)\r\n", MAX_DETECTION_COUNT);
}

void face_tracker_poll(void)
{
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        
        /* 如果追踪已停止，只消费消息不处理 */
        if (!s_ctx.is_running) {
            continue;
        }
        
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t offset = MAILBOX_GET_PAYLOAD(msg);

        /* 地址 = 基地址 + 偏移（DSP 应发送相对偏移）*/
        uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)offset;

        FT_LOG(3, "[M4:MBOX] msg=0x%08lX type=0x%lX offset=0x%lX addr=0x%08lX\r\n",
               (unsigned long)msg, (unsigned long)(msg_type >> 28),
               (unsigned long)offset, (unsigned long)addr);

        switch (msg_type)
        {
        case MAILBOX_MSG_TYPE_MULTI:
            /* 新协议：多目标检测结果 */
            process_multi_detection((const DetectionResult_t *)addr);
            break;

        case MAILBOX_MSG_TYPE_SINGLE:
            /* 旧协议兼容：单目标检测结果 */
            process_single_detection((const DetectionBox_t *)addr);
            break;

        case MAILBOX_MSG_TYPE_NO_DETECT:
            /* 本帧无检测结果，清除所有框（使用双缓冲）*/
            {
                uint32_t t_now = millis();
                if (s_ctx.perf_last_frame_ts > 0) {
                    s_ctx.perf_frame_interval = t_now - s_ctx.perf_last_frame_ts;
                }
                s_ctx.perf_last_frame_ts = t_now;
                
                /* 清除后台缓冲区中该缓冲区上一次绘制的内容 */
                clear_back_prev_boxes();
                s_ctx.count = 0;
                
                /* 保存当前状态（空）到后台缓冲区的 prev */
                save_current_to_back_prev();
                
                /* 交换缓冲区 */
                swap_buffers();
                
                s_ctx.last_update_ts = t_now;
                s_ctx.stats.frame_count++;
                s_ctx.no_detect_count++;
                
                /* 低频打印：第1帧打印，之后每秒最多1次 */
                if (s_ctx.no_detect_count == 1 ||
                    (t_now - s_ctx.no_detect_last_print_ts) >= 1000) {
                    FT_LOG(2, "[M4:FRAME] no_detect x%lu interval=%lums fb=%u\r\n",
                           (unsigned long)s_ctx.no_detect_count,
                           (unsigned long)s_ctx.perf_frame_interval,
                           (unsigned)s_ctx.front_idx);
                    s_ctx.no_detect_last_print_ts = t_now;
                }
            }
            break;

        default:
            /* 未知消息类型，尝试按旧协议处理（向后兼容）*/
            process_single_detection((const DetectionBox_t *)addr);
            break;
        }
    }

    /* 超时检查 */
    if (s_ctx.count > 0 && (millis() - s_ctx.last_update_ts > FACE_TRACKER_TIMEOUT_MS))
    {
        clear_all_boxes();
        s_ctx.stats.timeout_count++;
    }
}

uint32_t face_tracker_get_count(void)
{
    return s_ctx.count;
}

int32_t face_tracker_get_selected_index(void)
{
    return s_ctx.selected_idx;
}

bool face_tracker_get_selected_box(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2)
{
    if (s_ctx.selected_idx < 0 || s_ctx.selected_idx >= (int32_t)s_ctx.count) {
        return false;
    }
    
    const TrackedBox_t *box = &s_ctx.boxes[s_ctx.selected_idx];
    if (!box->valid) {
        return false;
    }
    
    if (x1) *x1 = box->x1;
    if (y1) *y1 = box->y1;
    if (x2) *x2 = box->x2;
    if (y2) *y2 = box->y2;
    return true;
}

void face_tracker_get_stats(FaceTrackerStats_t *stats)
{
    if (stats) {
        *stats = s_ctx.stats;
    }
}

void face_tracker_clear_all(void)
{
    clear_all_boxes();
    s_ctx.selected_idx = -1;
}

/*===========================================================================
 * 追踪控制 API
 *===========================================================================*/

/**
 * @brief 发送命令到 DSP（通过 Mailbox）
 */
static void send_cmd_to_dsp(uint32_t cmd)
{
    /* 使用 m4_dsp_one_data 发送命令到 DSP */
    m4_dsp_one_data(cmd);
    FT_LOG(2, "[M4:CMD] Sent cmd=0x%08lX to DSP\r\n", (unsigned long)cmd);
}

void face_tracker_start(void)
{
    s_ctx.is_running = true;
    send_cmd_to_dsp(CMD_TYPE_START_TRACKING);
    FT_LOG(1, "[M4:CTRL] Tracking started\r\n");
}

void face_tracker_stop(void)
{
    s_ctx.is_running = false;
    send_cmd_to_dsp(CMD_TYPE_STOP_TRACKING);
    clear_all_boxes();
    s_ctx.selected_idx = -1;
    FT_LOG(1, "[M4:CTRL] Tracking stopped\r\n");
}

bool face_tracker_is_running(void)
{
    return s_ctx.is_running;
}
