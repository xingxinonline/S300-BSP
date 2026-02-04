/**
 * @file    main.c
 * @brief   S300 智能云台目标追踪 Demo
 * 
 * @details 本 Demo 集成：
 *          1. OV5640 摄像头采集
 *          2. DSP 人脸/人形检测
 *          3. 云台伺服跟踪控制
 *          4. 双缓冲无撕裂显示
 *          5. track_id 跨帧关联
 *          6. 速度箭头渲染（卡尔曼滤波输出）
 * 
 *          硬件需求：
 *          - gimbal_master 板 (带 LCD 显示)
 *          - OV5640 摄像头模组
 *          - Hiwonder 总线舵机 (ID=6 Yaw, ID=4 Pitch)
 * 
 *          工作流程：
 *          1. M4 初始化摄像头、视频子系统、邮箱
 *          2. DSP 运行检测算法，通过邮箱发送检测结果
 *          3. M4 读取邮箱，提取目标位置
 *          4. PID 控制器计算云台运动增量
 *          5. 总线舵机驱动云台跟踪目标
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Platform */
#include "s300.h"

/* Board and subsystems */
#include "rcc.h"
#include "board.h"
#include "video.h"
#include "psram.h"
#include "mailbox.h"
#include "mailbox_proto.h"
#include "detection_proto.h"

/* Camera */
#include "camera_ov5640.h"

/* Tracking modules */
#include "gimbal_ctrl.h"
#include "target_tracker.h"

/*===========================================================================
 * 配置
 *===========================================================================*/

/** @brief 摄像头格式配置 */
#if BOARD_CAMERA_FORMAT == 0
  #define APP_CAM_FMT CAMREA_RGB565
#else
  #define APP_CAM_FMT CAMREA_YUV422
#endif

/** @brief 调试打印级别: 0=关闭, 1=关键事件, 2=每帧摘要, 3=详细 */
#ifndef TRACKING_DEBUG_LEVEL
#define TRACKING_DEBUG_LEVEL  1
#endif

#define TRK_LOG(level, fmt, ...) \
    do { if (TRACKING_DEBUG_LEVEL >= (level)) printf(fmt, ##__VA_ARGS__); } while(0)

/*===========================================================================
 * RGB565 颜色定义
 *===========================================================================*/

#define COLOR_GREEN     0x07E0u  /**< 选中目标：绿色 */
#define COLOR_BLUE      0x001Fu  /**< 非选中目标：蓝色 */
#define COLOR_CYAN      0x07FFu  /**< 速度箭头：青色 */
#define COLOR_RED       0xF800u  /**< 快速移动：红色 */

/** @brief 边界框线宽 */
#define BOX_LINE_WIDTH  2

/*===========================================================================
 * 双缓冲寄存器定义
 *===========================================================================*/

#define REG32(addr)     (*(volatile uint32_t *)(addr))
#define REG_FRAME0      (DSP_VIDEO_SS_BASE + 0x50u)
#define REG_FRAME1      (DSP_VIDEO_SS_BASE + 0x54u)

/*===========================================================================
 * 全局变量
 *===========================================================================*/

/** @brief 1ms 节拍计时 */
static volatile uint32_t g_tick_ms = 0;

/** @brief 当前前台缓冲区索引 (0 或 1) */
static uint8_t g_front_idx = 0;

/*===========================================================================
 * 跟踪目标状态（用于双缓冲绘制）
 *===========================================================================*/

/** @brief 单个目标的显示状态 */
typedef struct {
    int32_t  x1, y1, x2, y2;   /**< 边界框坐标 */
    int16_t  cx, cy;           /**< 中心点 */
    int8_t   vx, vy;           /**< 速度 */
    uint8_t  track_id;         /**< 跟踪ID */
    uint8_t  speed;            /**< 速度大小 */
    uint8_t  kf_confidence;    /**< 卡尔曼置信度 */
    bool     valid;            /**< 是否有效 */
    bool     selected;         /**< 是否选中 */
    bool     arrow_visible;    /**< 箭头是否可见 */
} DisplayBox_t;

/** @brief 上一帧状态（用于增量清除）*/
typedef struct {
    int32_t  x1, y1, x2, y2;
    int16_t  cx, cy;
    int8_t   vx, vy;
    bool     valid;
    bool     arrow_visible;
} PrevBox_t;

/** @brief 显示状态上下文 */
static struct {
    DisplayBox_t boxes[MAX_DETECTION_COUNT];  /**< 当前帧目标 */
    PrevBox_t prev_boxes_buf[2][MAX_DETECTION_COUNT]; /**< 双缓冲各自的上一帧状态 */
    uint32_t prev_count_buf[2];               /**< 每个缓冲区的上一帧数量 */
    uint32_t count;                           /**< 当前目标数量 */
    int32_t  selected_idx;                    /**< 选中索引 */
    uint32_t last_update_ts;                  /**< 最后更新时间戳 */
    uint8_t  tracked_track_id;                /**< 当前跟踪的 track_id */
    bool     has_tracked_id;                  /**< 是否有跟踪的目标 */
    /* no_detect 低频打印控制 */
    uint32_t no_detect_count;                 /**< 连续无检测帧数 */
    uint32_t no_detect_last_print_ts;         /**< 上次打印时间戳 */
} g_display = {0};

/*===========================================================================
 * 中断处理
 *===========================================================================*/

void SysTick_Handler(void)
{
    g_tick_ms++;
}

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

static inline uint32_t millis(void)
{
    return g_tick_ms;
}

/*===========================================================================
 * 视频初始化
 *===========================================================================*/

static void video_subsystem_init(void)
{
    /* 摄像头上电与探测 */
    int cam_ret = camera_ov5640_preinit();
    if (cam_ret != 0) {
        printf("[Tracking] WARN: OV5640 init failed (%d), continue without camera.\r\n", cam_ret);
    }

    /* 视频子系统（包含面板初始化）*/
    printf("[Tracking] Init video subsystem...\r\n");
    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);

    /* 初始化双缓冲显存与 Alpha 通道 */
    {
        volatile uint16_t *fb0 = (volatile uint16_t *)DISP_RFRAME0_ADDR;
        volatile uint16_t *fb1 = (volatile uint16_t *)DISP_RFRAME1_ADDR;
        volatile uint16_t *alpha0 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
        volatile uint16_t *alpha1 = (volatile uint16_t *)DISP_RALPHA1_ADDR;
        uint32_t pixels = DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT;
        uint32_t alpha_words = pixels / 2;
        
        /* 填充背景色（绿色 0x07E0）*/
        for (uint32_t i = 0; i < pixels; i++) {
            fb0[i] = 0x07E0;
            fb1[i] = 0x07E0;
        }
        
        /* Alpha 全透明 */
        for (uint32_t i = 0; i < alpha_words; i++) {
            alpha0[i] = 0x0000;
            alpha1[i] = 0x0000;
        }

        /* 触发 DSP 显示 Frame 0 */
        REG32(REG_FRAME0) = 1u;
        g_front_idx = 0;
        printf("[Tracking] Double-buffer framebuffer initialized.\r\n");
    }

    /* M4 <-> DSP 邮箱通信 */
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    set_dsp_warm_reset(true);
    write_mailbox(MAILBOX_BASE, 0x5A5A5A5A);
    printf("[Tracking] DSP mailbox initialized.\r\n");
}

/*===========================================================================
 * 检测结果处理 - 双缓冲 + 速度箭头
 *===========================================================================*/

/** @brief 检测超时 (ms) */
#define DETECT_TIMEOUT_MS   200

/*===========================================================================
 * 双缓冲辅助函数
 *===========================================================================*/

/**
 * @brief 获取后台帧缓冲地址
 */
static inline volatile uint16_t * get_back_framebuffer(void)
{
    return (g_front_idx == 0u) 
           ? (volatile uint16_t *)DISP_RFRAME1_ADDR 
           : (volatile uint16_t *)DISP_RFRAME0_ADDR;
}

/**
 * @brief 获取后台 Alpha 缓冲地址
 */
static inline volatile uint16_t * get_back_alphabuffer(void)
{
    return (g_front_idx == 0u) 
           ? (volatile uint16_t *)DISP_RALPHA1_ADDR 
           : (volatile uint16_t *)DISP_RALPHA0_ADDR;
}

/**
 * @brief 获取后台缓冲区索引
 */
static inline uint8_t get_back_buffer_idx(void)
{
    return (g_front_idx == 0u) ? 1u : 0u;
}

/**
 * @brief 交换显示缓冲区
 */
static void swap_buffers(void)
{
    uint8_t back_idx = get_back_buffer_idx();
    
    if (back_idx == 0u) {
        REG32(REG_FRAME0) = 1u;
        while ((REG32(REG_FRAME0) & 0x1u) != 0u) { /* 等待硬件受理 */ }
    } else {
        REG32(REG_FRAME1) = 1u;
        while ((REG32(REG_FRAME1) & 0x1u) != 0u) { /* 等待硬件受理 */ }
    }
    
    g_front_idx = back_idx;
    TRK_LOG(3, "[SWAP] front_idx=%u\r\n", (unsigned)g_front_idx);
}

/*===========================================================================
 * 像素绘制函数（写入后台缓冲区）
 *===========================================================================*/

/**
 * @brief 设置单个像素的颜色和 Alpha
 */
static void set_pixel(uint32_t x, uint32_t y, uint16_t color, uint8_t alpha)
{
    if (x >= DISP_IMAGE_WIDTH || y >= DISP_IMAGE_HEIGHT) return;
    
    uint32_t pixel_idx = y * DISP_IMAGE_WIDTH + x;
    
    /* 设置颜色 */
    volatile uint16_t *fb = get_back_framebuffer();
    fb[pixel_idx] = color;
    
    /* 设置 Alpha (16-bit 对齐访问) */
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
 * @brief 设置单个像素的 Alpha 值
 */
static void set_pixel_alpha(uint32_t x, uint32_t y, uint8_t alpha)
{
    if (x >= DISP_IMAGE_WIDTH || y >= DISP_IMAGE_HEIGHT) return;
    
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

/*===========================================================================
 * 绘图函数
 *===========================================================================*/

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

    /* 上下边 */
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

    /* 左右边 */
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
 * @brief 绘制矩形边框（仅 Alpha）
 */
static void draw_rect_border(int x1, int y1, int x2, int y2, uint8_t alpha, int line_width)
{
    /* 坐标裁剪 */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= DISP_IMAGE_WIDTH) x2 = DISP_IMAGE_WIDTH - 1;
    if (y2 >= DISP_IMAGE_HEIGHT) y2 = DISP_IMAGE_HEIGHT - 1;
    if (x1 > x2 || y1 > y2) return;

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
 * 速度箭头绘制
 *===========================================================================*/

/**
 * @brief 绘制直线 (Bresenham 算法)
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
 */
static void draw_velocity_arrow(int cx, int cy, int8_t vx, int8_t vy, uint16_t color)
{
    /* 箭头终点 */
    int x2 = cx + vx * ARROW_SCALE;
    int y2 = cy + vy * ARROW_SCALE;
    
    /* 绘制主线 */
    draw_line(cx, cy, x2, y2, color, 0xFF);
    
    /* 计算箭头方向 */
    float dx = (float)(x2 - cx);
    float dy = (float)(y2 - cy);
    float len = dx * dx + dy * dy;
    
    if (len < 4.0f) return;  /* 太短不画箭头 */
    
    /* 简化的箭头头部计算 */
    int hx1 = x2 - (int)(dx * 0.3f) - (int)(dy * 0.15f);
    int hy1 = y2 - (int)(dy * 0.3f) + (int)(dx * 0.15f);
    int hx2 = x2 - (int)(dx * 0.3f) + (int)(dy * 0.15f);
    int hy2 = y2 - (int)(dy * 0.3f) - (int)(dx * 0.15f);
    
    /* 绘制箭头翼 */
    draw_line(x2, y2, hx1, hy1, color, 0xFF);
    draw_line(x2, y2, hx2, hy2, color, 0xFF);
}

/**
 * @brief 清除速度箭头
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
 * 双缓冲绘制管理
 *===========================================================================*/

/**
 * @brief 保存当前帧到后台缓冲区的 prev_boxes
 */
static void save_current_to_back_prev(void)
{
    uint8_t back_idx = get_back_buffer_idx();
    for (uint32_t i = 0; i < g_display.count; i++) {
        g_display.prev_boxes_buf[back_idx][i].x1 = g_display.boxes[i].x1;
        g_display.prev_boxes_buf[back_idx][i].y1 = g_display.boxes[i].y1;
        g_display.prev_boxes_buf[back_idx][i].x2 = g_display.boxes[i].x2;
        g_display.prev_boxes_buf[back_idx][i].y2 = g_display.boxes[i].y2;
        g_display.prev_boxes_buf[back_idx][i].cx = g_display.boxes[i].cx;
        g_display.prev_boxes_buf[back_idx][i].cy = g_display.boxes[i].cy;
        g_display.prev_boxes_buf[back_idx][i].vx = g_display.boxes[i].vx;
        g_display.prev_boxes_buf[back_idx][i].vy = g_display.boxes[i].vy;
        g_display.prev_boxes_buf[back_idx][i].valid = g_display.boxes[i].valid;
        g_display.prev_boxes_buf[back_idx][i].arrow_visible = g_display.boxes[i].arrow_visible;
    }
    g_display.prev_count_buf[back_idx] = g_display.count;
}

/**
 * @brief 清除后台缓冲区的整个 Alpha 层
 * 
 * 简单粗暴但有效的方法：每帧都清除整个 overlay
 * 160x128 = 20480 像素 = 10240 words，性能可接受
 */
static void clear_back_alpha_layer(void)
{
    volatile uint16_t *ab16 = get_back_alphabuffer();
    uint32_t alpha_words = (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT) / 2;
    
    for (uint32_t i = 0; i < alpha_words; i++) {
        ab16[i] = 0x0000;
    }
}

/**
 * @brief 清除后台缓冲区上一次绘制的边界框和箭头
 * 
 * 改进：同时清除两个缓冲区的历史记录，避免重影
 */
static void clear_back_prev_boxes(void)
{
    /* 直接清除整个 Alpha 层 */
    clear_back_alpha_layer();
    
    /* 重置历史记录 */
    uint8_t back_idx = get_back_buffer_idx();
    for (uint32_t i = 0; i < MAX_DETECTION_COUNT; i++) {
        g_display.prev_boxes_buf[back_idx][i].valid = false;
    }
    g_display.prev_count_buf[back_idx] = 0;
}

/**
 * @brief 绘制所有边界框和速度箭头
 */
static void draw_all_boxes(void)
{
    for (uint32_t i = 0; i < g_display.count; i++) {
        if (!g_display.boxes[i].valid) continue;
        
        bool is_selected = ((int32_t)i == g_display.selected_idx);
        g_display.boxes[i].selected = is_selected;
        
        /* 选择边界框颜色 */
        uint16_t box_color = is_selected ? COLOR_GREEN : COLOR_BLUE;
        
        /* 绘制边界框 */
        draw_color_rect_border(
            g_display.boxes[i].x1, g_display.boxes[i].y1,
            g_display.boxes[i].x2, g_display.boxes[i].y2,
            box_color, 0xFF, BOX_LINE_WIDTH
        );
        
        /* 绘制速度箭头（仅对选中目标且有卡尔曼置信度时）*/
        if (is_selected && g_display.boxes[i].kf_confidence > 0) {
            uint8_t spd = g_display.boxes[i].speed;
            
            /* 滞后逻辑：防止箭头频繁闪烁 */
            if (g_display.boxes[i].arrow_visible) {
                if (spd < ARROW_HIDE_THRESHOLD) {
                    g_display.boxes[i].arrow_visible = false;
                }
            } else {
                if (spd > ARROW_SHOW_THRESHOLD) {
                    g_display.boxes[i].arrow_visible = true;
                }
            }
            
            if (g_display.boxes[i].arrow_visible) {
                draw_velocity_arrow(
                    g_display.boxes[i].cx, g_display.boxes[i].cy,
                    g_display.boxes[i].vx, g_display.boxes[i].vy,
                    COLOR_CYAN
                );
            }
        } else {
            g_display.boxes[i].arrow_visible = false;
        }
    }
    
    TRK_LOG(2, "[DRAW] count=%lu selected_idx=%ld\r\n",
           (unsigned long)g_display.count, (long)g_display.selected_idx);
}

/*===========================================================================
 * 检测结果处理
 *===========================================================================*/

/**
 * @brief 校验并规范化边界框坐标
 */
static bool validate_box(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2)
{
    /* 交换确保 x1 < x2, y1 < y2 */
    if (*x2 < *x1) { int32_t t = *x1; *x1 = *x2; *x2 = t; }
    if (*y2 < *y1) { int32_t t = *y1; *y1 = *y2; *y2 = t; }

    /* 范围检查 */
    if (*x1 < 0 || *y1 < 0) return false;
    if (*x2 > DISP_IMAGE_WIDTH || *y2 > DISP_IMAGE_HEIGHT) return false;
    
    /* 最小尺寸检查 */
    if ((*x2 - *x1) <= 2 || (*y2 - *y1) <= 2) return false;

    return true;
}

/**
 * @brief 处理多目标检测结果（协议 v2.x）
 * @note  只更新 g_display 状态，不绘制
 */
static void parse_multi_detection(const DetectionResult_t *result, tracker_target_t *out_target)
{
    if (!DETECTION_RESULT_IS_VALID(result)) {
        TRK_LOG(1, "[ERR] Invalid DetectionResult\r\n");
        return;
    }

    uint32_t count = result->count;
    if (count > MAX_DETECTION_COUNT) count = MAX_DETECTION_COUNT;

    TRK_LOG(2, "[FRAME] frame=%lu count=%lu selected_idx=%ld\r\n", 
           (unsigned long)result->frame_id, (unsigned long)count, (long)result->selected_idx);

    /* 先清除所有 boxes 的 valid 标志 */
    for (uint32_t i = 0; i < MAX_DETECTION_COUNT; i++) {
        g_display.boxes[i].valid = false;
    }

    /* 坐标缩放因子：AI模型输入 -> 显示尺寸 */
#if (DOWNSCALE_IMAGE_WIDTH != DISP_IMAGE_WIDTH) || (DOWNSCALE_IMAGE_HEIGHT != DISP_IMAGE_HEIGHT)
    const int32_t scale_x_num = DISP_IMAGE_WIDTH;
    const int32_t scale_x_den = DOWNSCALE_IMAGE_WIDTH;
    const int32_t scale_y_num = DISP_IMAGE_HEIGHT;
    const int32_t scale_y_den = DOWNSCALE_IMAGE_HEIGHT;
    #define SCALE_COORD 1
#else
    #define SCALE_COORD 0
#endif

    /* 解析所有目标 */
    for (uint32_t i = 0; i < count; i++) {
        const DetectionBox_t *box = &result->boxes[i];
        int32_t x1 = box->x1, y1 = box->y1, x2 = box->x2, y2 = box->y2;
        
#if SCALE_COORD
        /* 将 AI 检测坐标缩放到显示坐标 */
        x1 = x1 * scale_x_num / scale_x_den;
        x2 = x2 * scale_x_num / scale_x_den;
        y1 = y1 * scale_y_num / scale_y_den;
        y2 = y2 * scale_y_num / scale_y_den;
#endif
        
        if (!validate_box(&x1, &y1, &x2, &y2)) continue;
        
        g_display.boxes[i].x1 = x1;
        g_display.boxes[i].y1 = y1;
        g_display.boxes[i].x2 = x2;
        g_display.boxes[i].y2 = y2;
        g_display.boxes[i].cx = (int16_t)((x1 + x2) / 2);
        g_display.boxes[i].cy = (int16_t)((y1 + y2) / 2);
        g_display.boxes[i].vx = box->vx;
        g_display.boxes[i].vy = box->vy;
        g_display.boxes[i].track_id = box->track_id;
        g_display.boxes[i].speed = box->speed;
        g_display.boxes[i].kf_confidence = box->kf_confidence;
        g_display.boxes[i].valid = true;
        g_display.boxes[i].selected = false;
        
        TRK_LOG(3, "[BOX] [%lu] track_id=%u v=(%d,%d) speed=%u kf=%u\r\n",
               (unsigned long)i, box->track_id,
               (int)box->vx, (int)box->vy, (unsigned)box->speed, (unsigned)box->kf_confidence);
    }
    g_display.count = count;
    
    /* 使用 DSP 提供的 selected_idx */
    g_display.selected_idx = result->selected_idx;
    
    /* 更新跟踪的 track_id */
    if (g_display.selected_idx >= 0 && g_display.selected_idx < (int32_t)count) {
        g_display.tracked_track_id = g_display.boxes[g_display.selected_idx].track_id;
        g_display.has_tracked_id = true;
        
        /* 输出选中目标给云台控制器 */
        DisplayBox_t *sel = &g_display.boxes[g_display.selected_idx];
        out_target->x1 = sel->x1;
        out_target->y1 = sel->y1;
        out_target->x2 = sel->x2;
        out_target->y2 = sel->y2;
        out_target->track_id = sel->track_id;
        out_target->vx = sel->vx;
        out_target->vy = sel->vy;
        out_target->speed = sel->speed;
        out_target->kf_confidence = sel->kf_confidence;
        out_target->valid = true;
        out_target->selected = true;
        out_target->timestamp = millis();
        
        TRK_LOG(2, "[SELECT] idx=%ld track_id=%u\r\n",
               (long)g_display.selected_idx, sel->track_id);
    }

    g_display.last_update_ts = millis();
}

/**
 * @brief 处理单目标检测结果（旧协议兼容）
 * @note  只更新 g_display 状态，不绘制
 */
static void parse_single_detection(const DetectionBox_t *box, tracker_target_t *out_target)
{
    int32_t x1 = box->x1, y1 = box->y1, x2 = box->x2, y2 = box->y2;

    /* 先清除所有 boxes 的 valid 标志 */
    for (uint32_t i = 0; i < MAX_DETECTION_COUNT; i++) {
        g_display.boxes[i].valid = false;
    }

#if SCALE_COORD
    /* 将 AI 检测坐标缩放到显示坐标 */
    x1 = x1 * DISP_IMAGE_WIDTH / DOWNSCALE_IMAGE_WIDTH;
    x2 = x2 * DISP_IMAGE_WIDTH / DOWNSCALE_IMAGE_WIDTH;
    y1 = y1 * DISP_IMAGE_HEIGHT / DOWNSCALE_IMAGE_HEIGHT;
    y2 = y2 * DISP_IMAGE_HEIGHT / DOWNSCALE_IMAGE_HEIGHT;
#endif

    if (!validate_box(&x1, &y1, &x2, &y2)) {
        g_display.selected_idx = -1;
        g_display.count = 0;
        return;
    }

    g_display.boxes[0].x1 = x1;
    g_display.boxes[0].y1 = y1;
    g_display.boxes[0].x2 = x2;
    g_display.boxes[0].y2 = y2;
    g_display.boxes[0].cx = (int16_t)((x1 + x2) / 2);
    g_display.boxes[0].cy = (int16_t)((y1 + y2) / 2);
    g_display.boxes[0].track_id = box->track_id;
    g_display.boxes[0].vx = box->vx;
    g_display.boxes[0].vy = box->vy;
    g_display.boxes[0].speed = box->speed;
    g_display.boxes[0].kf_confidence = box->kf_confidence;
    g_display.boxes[0].valid = true;
    g_display.boxes[0].selected = true;
    g_display.count = 1;
    g_display.selected_idx = 0;

    /* 输出给云台控制器 */
    out_target->x1 = x1;
    out_target->y1 = y1;
    out_target->x2 = x2;
    out_target->y2 = y2;
    out_target->track_id = box->track_id;
    out_target->vx = box->vx;
    out_target->vy = box->vy;
    out_target->speed = box->speed;
    out_target->kf_confidence = box->kf_confidence;
    out_target->valid = true;
    out_target->selected = true;
    out_target->timestamp = millis();

    g_display.last_update_ts = millis();
}

/**
 * @brief 执行绘制和缓冲区交换
 */
static void render_and_swap(void)
{
    /* 清除后台缓冲区上一次绘制的内容 */
    clear_back_prev_boxes();
    
    /* 绘制所有边界框和箭头 */
    if (g_display.count > 0) {
        draw_all_boxes();
    }
    
    /* 保存当前绘制内容 */
    save_current_to_back_prev();
    
    /* 交换缓冲区 */
    swap_buffers();
}

/**
 * @brief 处理 DSP 邮箱检测结果
 * 
 * 策略：累积所有消息，只用最新的结果绘制一次
 */
static void process_detection_mailbox(void)
{
    tracker_target_t target;
    target.valid = false;
    
    bool has_new_data = false;
    bool is_no_detect = false;
    uint32_t msg_count = 0;
    
    /* 读取所有邮箱消息，只保留最新结果 */
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);
        
        uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
        msg_count++;

        TRK_LOG(3, "[MBOX] msg=0x%08lX type=0x%lX\r\n",
               (unsigned long)msg, (unsigned long)(msg_type >> 28));

        switch (msg_type) {
        case MAILBOX_MSG_TYPE_MULTI:
            parse_multi_detection((const DetectionResult_t *)addr, &target);
            has_new_data = true;
            is_no_detect = false;
            break;
        
        case MAILBOX_MSG_TYPE_SINGLE:
            parse_single_detection((const DetectionBox_t *)addr, &target);
            has_new_data = true;
            is_no_detect = false;
            break;
        
        case MAILBOX_MSG_TYPE_NO_RESULT:
            /* 无检测结果 */
            for (uint32_t i = 0; i < MAX_DETECTION_COUNT; i++) {
                g_display.boxes[i].valid = false;
            }
            g_display.count = 0;
            g_display.selected_idx = -1;
            has_new_data = true;
            is_no_detect = true;
            break;
            
        default:
            /* 未知消息类型，尝试按旧协议处理 */
            parse_single_detection((const DetectionBox_t *)addr, &target);
            has_new_data = true;
            is_no_detect = false;
            break;
        }
    }
    
    /* 如果有新数据，执行一次绘制和交换 */
    if (has_new_data) {
        render_and_swap();
        
        if (is_no_detect) {
            g_display.no_detect_count++;
            uint32_t now = millis();
            /* 低频打印：第1帧打印，之后每秒最多1次 */
            if (g_display.no_detect_count == 1 ||
                (now - g_display.no_detect_last_print_ts) >= 1000) {
                TRK_LOG(2, "[FRAME] no_detect x%lu\r\n", (unsigned long)g_display.no_detect_count);
                g_display.no_detect_last_print_ts = now;
            }
        } else {
            g_display.no_detect_count = 0;
        }
        
        if (msg_count > 1) {
            TRK_LOG(3, "[MBOX] Processed %lu messages, rendered once\r\n", (unsigned long)msg_count);
        }
    }
    
    /* 如果有有效目标，更新跟踪器 */
    if (target.valid) {
        tracker_update_target(&target);
    }
    
    /* 检查超时 */
    if (g_display.count > 0 && (millis() - g_display.last_update_ts > DETECT_TIMEOUT_MS))
    {
        for (uint32_t i = 0; i < MAX_DETECTION_COUNT; i++) {
            g_display.boxes[i].valid = false;
        }
        g_display.count = 0;
        g_display.selected_idx = -1;
        render_and_swap();
        TRK_LOG(1, "[TIMEOUT] Detection timeout, cleared\r\n");
    }
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

int main(void)
{
    /* 板级初始化 */
    board_init();
    printf("\r\n");
    printf("======================================\r\n");
    printf("  S300 Tracking Demo (Enhanced)\r\n");
    printf("  Features:\r\n");
    printf("    - Double-buffered display\r\n");
    printf("    - track_id association\r\n");
    printf("    - Kalman velocity arrows\r\n");
    printf("======================================\r\n");

    /* 系统时钟更新 */
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        printf("[Tracking] ERR: SysTick_Config failed!\r\n");
    }

    /* MM/DSP PLL 配置 */
    rcc_init_mm_pll(8, 400, 0, 3, 2);  /* MM 100MHz */
    rcc_init_dsp_pll(6, 800, 0, 2, 2); /* DSP 400MHz */

    /* PSRAM 初始化 */
    init_psram(4, 1);

    /* 视频子系统初始化 */
    video_subsystem_init();

    /* 初始化显示状态 */
    memset(&g_display, 0, sizeof(g_display));
    g_display.selected_idx = -1;

    /* 云台初始化 */
    printf("[Tracking] Init gimbal...\r\n");
    gimbal_init(millis);
    
    /* 等待舵机就位 */
    {
        volatile uint32_t wait = millis();
        while (millis() - wait < 1000) { /* wait 1s */ }
    }
    
    /* 云台归中 */
    printf("[Tracking] Center gimbal...\r\n");
    gimbal_center();
    
    /* 等待归中完成 */
    {
        volatile uint32_t wait = millis();
        while (millis() - wait < 600) { /* wait 600ms */ }
    }

    /* 跟踪器初始化 */
    printf("[Tracking] Init tracker (image: %dx%d)...\r\n", 
           DISP_IMAGE_WIDTH, DISP_IMAGE_HEIGHT);
    tracker_init(millis, DISP_IMAGE_WIDTH, DISP_IMAGE_HEIGHT);
    
    /* 使用默认 PID 参数，不再覆盖 */
    // tracker_set_pid(0.08f, 0.002f, 0.02f);  // 注释掉，使用 target_tracker.c 的默认值
    tracker_set_deadzone(10);

    printf("[Tracking] System ready. Waiting for detection...\r\n");
    printf("--------------------------------------\r\n");

    /* 主循环 */
    while (1)
    {
        /* 处理检测邮箱 */
        process_detection_mailbox();
        
        /* 执行跟踪控制 */
        tracker_poll();
        
        /* 简单节拍控制 (~5ms 周期) */
        {
            volatile uint32_t t0 = millis();
            while ((millis() - t0) < 5u) { /* idle spin */ }
        }
    }
}
