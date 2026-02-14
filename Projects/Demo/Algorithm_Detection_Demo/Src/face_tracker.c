/**
 * @file face_tracker.c
 * @brief 多目标检测画框模块 (v3 SORT 协议适配)
 *
 * 功能：
 *   - 从 Mailbox 读取 DSP 的 DetectionResult (v2.3 协议)
 *   - 为每个 confirmed track 绘制彩色边界框 (track_id → 颜色映射)
 *   - miss_count=0 的真实检测用全亮度, >0 的 coast 预测用半透明
 *   - 输出详细的 SORT 跟踪日志，用于验证 DSP 端实现
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "mailbox.h"
#include "mailbox_proto.h"
#include "detection_proto.h"
#include "video.h"
#include "face_tracker.h"

#ifndef FACE_COORD_SPACE_W
#define FACE_COORD_SPACE_W DISP_IMAGE_WIDTH
#endif
#ifndef FACE_COORD_SPACE_H
#define FACE_COORD_SPACE_H DISP_IMAGE_HEIGHT
#endif

/**
 * 调试打印级别:
 *   0 = 关闭
 *   1 = 关键事件 (ID 分配/释放, 错误)
 *   2 = 每帧摘要 (帧号, 目标数)
 *   3 = 每帧每目标详情 (ID, box, vx/vy, miss_count, kf_conf)
 */
#ifndef FACE_TRACKER_DEBUG_LEVEL
#define FACE_TRACKER_DEBUG_LEVEL  1
#endif

/* 最低分数阈值（百分比，0~100）：低于该值的目标将被过滤，不绘制 */
#ifndef FACE_SCORE_FILTER_PCT
#define FACE_SCORE_FILTER_PCT  60u
#endif

#define FT_LOG(level, fmt, ...) \
    do { if (FACE_TRACKER_DEBUG_LEVEL >= (level)) printf(fmt, ##__VA_ARGS__); } while(0)

/*===========================================================================
 * Track ID → 颜色映射 (RGB565)
 *===========================================================================*/
#define COLOR_GREEN   0x07E0u  /* track_id 1 */
#define COLOR_RED     0xF800u  /* track_id 2 */
#define COLOR_BLUE    0x001Fu  /* track_id 3 */
#define COLOR_YELLOW  0xFFE0u  /* track_id 4 */
#define COLOR_CYAN    0x07FFu  /* track_id 5 */
#define COLOR_MAGENTA 0xF81Fu  /* track_id ≥ 6 或 0 (fallback) */
#define COLOR_WHITE   0xFFFFu  /* 无 track_id 时的默认色 */

static const uint16_t s_track_colors[] = {
    COLOR_GREEN,    /* slot 0 */
    COLOR_RED,      /* slot 1 */
    COLOR_BLUE,     /* slot 2 */
    COLOR_YELLOW,   /* slot 3 */
    COLOR_CYAN,     /* slot 4 */
    COLOR_MAGENTA,  /* slot 5 */
};
#define NUM_COLOR_SLOTS (sizeof(s_track_colors) / sizeof(s_track_colors[0]))

/*===========================================================================
 * 动态颜色槽: 为每个活跃 track_id 分配唯一颜色, track 消失后回收
 *===========================================================================*/
static uint8_t  s_color_slot_ids[NUM_COLOR_SLOTS]; /* 绑定的 track_id, 0=空闲 */
static uint32_t s_next_slot = 0;  /* 轮询分配起点 */

/** 释放当前帧中不再出现的 track_id 的颜色槽 */
static void color_slots_update(const DetectionResult_t *result)
{
    for (uint32_t s = 0; s < NUM_COLOR_SLOTS; s++) {
        if (s_color_slot_ids[s] == 0) continue;
        bool found = false;
        for (uint32_t i = 0; i < result->count && i < MAX_DETECTION_COUNT; i++) {
            if (result->boxes[i].track_id == s_color_slot_ids[s]) {
                found = true;
                break;
            }
        }
        if (!found) {
            FT_LOG(1, "[FT] color slot %lu freed (was id=%u)\r\n",
                   (unsigned long)s, s_color_slot_ids[s]);
            s_color_slot_ids[s] = 0;
        }
    }
}

/** 为 track_id 分配颜色 (已有分配直接返回, 否则轮询占用空闲槽) */
static uint16_t get_track_color(uint8_t track_id)
{
    if (track_id == 0) return COLOR_WHITE;

    /* 已有分配? */
    for (uint32_t i = 0; i < NUM_COLOR_SLOTS; i++) {
        if (s_color_slot_ids[i] == track_id)
            return s_track_colors[i];
    }

    /* 轮询分配新槽 */
    for (uint32_t n = 0; n < NUM_COLOR_SLOTS; n++) {
        uint32_t slot = (s_next_slot + n) % NUM_COLOR_SLOTS;
        if (s_color_slot_ids[slot] == 0) {
            s_color_slot_ids[slot] = track_id;
            s_next_slot = (slot + 1) % NUM_COLOR_SLOTS;
            FT_LOG(1, "[FT] color slot %lu -> id=%u (%s)\r\n",
                   (unsigned long)slot, track_id,
                   slot == 0 ? "GREEN" : slot == 1 ? "RED" : slot == 2 ? "BLUE" :
                   slot == 3 ? "YELLOW" : slot == 4 ? "CYAN" : "MAGENTA");
            return s_track_colors[slot];
        }
    }

    /* 所有槽已满 (>6 个同时目标), 用 track_id 轮转 */
    return s_track_colors[track_id % NUM_COLOR_SLOTS];
}

/*===========================================================================
 * Alpha 亮度定义
 *===========================================================================*/
#define ALPHA_SOLID   0xFFu  /* 真实检测 (miss_count == 0) */
#define ALPHA_COAST   0x60u  /* Kalman coast 预测框 (miss_count > 0) */
#define ALPHA_CLEAR   0x00u  /* 透明 (擦除用) */

/*===========================================================================
 * 5x7 字体 (来自 Tracking_Demo)
 *===========================================================================*/
static const uint8_t font5x7[96][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x10, 0x08, 0x08, 0x10, 0x08}  // ~
};

/*===========================================================================
 * 绘图缓存: 记录上一帧所有画框，用于下一帧擦除
 *===========================================================================*/
typedef struct {
    int32_t x1, y1, x2, y2;
    int32_t text_x, text_y, text_len;  /* 文字位置和长度 */
} DrawnBox_t;

static DrawnBox_t s_prev_boxes[MAX_DETECTION_COUNT];
static uint32_t   s_prev_count = 0;
static uint32_t   s_last_valid_ts = 0;
static bool       s_had_target = false;   /* 用于 NO_RESULT 打印控制 */

#define FACE_TIMEOUT_MS 500  /* 无检测超时 (v3 容忍更长因为 coast 帧) */

/*===========================================================================
 * 平台时钟
 *===========================================================================*/
static uint32_t (*s_get_millis)(void) = 0;

void face_tracker_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;
    s_prev_count = 0;
    s_last_valid_ts = 0;
    s_had_target = false;
    s_next_slot = 0;
    for (uint32_t i = 0; i < NUM_COLOR_SLOTS; i++) s_color_slot_ids[i] = 0;
    FT_LOG(1, "[FT] face_tracker_init (proto v%u.%u, multi-target)\r\n",
           (DETECTION_PROTOCOL_VERSION >> 8) & 0xFF,
           DETECTION_PROTOCOL_VERSION & 0xFF);
}

static inline uint32_t millis(void)
{
    return s_get_millis ? s_get_millis() : 0u;
}

/*===========================================================================
 * 分数转换与显示格式化
 *===========================================================================*/

/**
 * 将 DetectionBox.score 归一化为 0~100 百分比
 * 支持 DSP 发送 0~1 或 0~100 两种格式
 */
static uint8_t normalize_score_pct(float score_raw)
{
    if (score_raw >= 0.0f && score_raw <= 1.0f) {
        uint32_t score = (uint32_t)(score_raw * 100.0f);
        if (score > 100u) score = 100u;
        return (uint8_t)score;
    }

    if (score_raw > 1.0f && score_raw <= 100.0f) {
        uint32_t score = (uint32_t)score_raw;
        if (score > 100u) score = 100u;
        return (uint8_t)score;
    }

    FT_LOG(1, "[FT] WARN invalid score=%.2f, use 0\r\n", score_raw);
    return 0u;
}

/**
 * 生成镜像显示用的分数字符串（参考 Tracking_Demo）
 * - 100% -> "%001"
 * - XY%  -> "%YX"（例如 85% => "%58"）
 */
static int format_mirrored_score(uint8_t score_pct, char *out, int out_size)
{
    if (out == 0 || out_size < 4) return 0;

    if (score_pct >= 100u) {
        if (out_size < 5) return 0;
        out[0] = '%';
        out[1] = '0';
        out[2] = '0';
        out[3] = '1';
        out[4] = '\0';
        return 4;
    }

    out[0] = '%';
    out[1] = (char)((score_pct % 10u) + '0');
    out[2] = (char)(((score_pct / 10u) % 10u) + '0');
    out[3] = '\0';
    return 3;
}

static void draw_string(int x, int y, const char *str, uint16_t color, uint8_t alpha);

/*===========================================================================
 * PSRAM-safe 像素操作 (16-bit 对齐读改写)
 *===========================================================================*/

/** 设置单个像素的 Alpha */
static void set_pixel_alpha(uint32_t x, uint32_t y, uint8_t alpha)
{
    volatile uint16_t *ab16 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
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

/** 设置单个像素的颜色 (RGB565) */
static void set_pixel_color(uint32_t x, uint32_t y, uint16_t color)
{
    volatile uint16_t *fb = (volatile uint16_t *)DISP_RFRAME0_ADDR;
    fb[y * DISP_IMAGE_WIDTH + x] = color;
}

/** 绘制矩形边框 (带颜色和透明度) */
static void draw_rect_border(int x1, int y1, int x2, int y2,
                             uint16_t color, uint8_t alpha)
{
    /* 裁剪到屏幕范围 */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= DISP_IMAGE_WIDTH) x2 = DISP_IMAGE_WIDTH - 1;
    if (y2 >= DISP_IMAGE_HEIGHT) y2 = DISP_IMAGE_HEIGHT - 1;
    if (x1 > x2 || y1 > y2) return;

    /* 上下边 */
    for (int x = x1; x <= x2; x++) {
        set_pixel_color((uint32_t)x, (uint32_t)y1, color);
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, alpha);
        set_pixel_color((uint32_t)x, (uint32_t)y2, color);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, alpha);
    }
    /* 左右边 */
    for (int y = y1 + 1; y < y2; y++) {
        set_pixel_color((uint32_t)x1, (uint32_t)y, color);
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, alpha);
        set_pixel_color((uint32_t)x2, (uint32_t)y, color);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, alpha);
    }
}

/** 仅清除矩形边框 (alpha → 0) */
static void clear_rect_border(int x1, int y1, int x2, int y2)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= DISP_IMAGE_WIDTH) x2 = DISP_IMAGE_WIDTH - 1;
    if (y2 >= DISP_IMAGE_HEIGHT) y2 = DISP_IMAGE_HEIGHT - 1;
    if (x1 > x2 || y1 > y2) return;

    for (int x = x1; x <= x2; x++) {
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, ALPHA_CLEAR);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, ALPHA_CLEAR);
    }
    for (int y = y1 + 1; y < y2; y++) {
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, ALPHA_CLEAR);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, ALPHA_CLEAR);
    }
}

/*===========================================================================
 * 文字绘制函数 (5x7 字体, 带缩放)
 *===========================================================================*/
#define FONT_CHAR_W  5
#define FONT_CHAR_H  7
#define FONT_SPACING 1

/* 字体缩放: 根据屏幕尺寸自动选择 */
#if (DISP_IMAGE_WIDTH >= 240)
  #define FONT_SCALE 2
#else
  #define FONT_SCALE 1
#endif

#define SCALED_CHAR_W  (FONT_CHAR_W * FONT_SCALE)
#define SCALED_CHAR_H  (FONT_CHAR_H * FONT_SCALE)
#define SCALED_SPACING (FONT_SPACING * FONT_SCALE)

/** 绘制单个字符 (带缩放) */
static void draw_char(int x, int y, char c, uint16_t color, uint8_t alpha)
{
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = font5x7[c - 32];
    for (int col = 0; col < FONT_CHAR_W; col++) {
        uint8_t line = glyph[FONT_CHAR_W - 1 - col];
        for (int row = 0; row < FONT_CHAR_H; row++) {
            if ((line >> row) & 0x01) {
                /* 缩放绘制 */
                for (int sy = 0; sy < FONT_SCALE; sy++) {
                    for (int sx = 0; sx < FONT_SCALE; sx++) {
                        int px = x + col * FONT_SCALE + sx;
                        int py = y + row * FONT_SCALE + sy;
                        if (px >= 0 && px < DISP_IMAGE_WIDTH && py >= 0 && py < DISP_IMAGE_HEIGHT) {
                            set_pixel_color((uint32_t)px, (uint32_t)py, color);
                            set_pixel_alpha((uint32_t)px, (uint32_t)py, alpha);
                        }
                    }
                }
            }
        }
    }
}

/** 绘制字符串 */
static void draw_string(int x, int y, const char *str, uint16_t color, uint8_t alpha)
{
    while (*str) {
        draw_char(x, y, *str++, color, alpha);
        x += SCALED_CHAR_W + SCALED_SPACING;
    }
}

/** 在框附近绘制分数（镜像格式）并返回文本长度 */
static int draw_score_label(int32_t x1, int32_t y1, int32_t y2,
                            uint8_t score_pct, uint16_t color, uint8_t alpha,
                            int *text_x_out, int *text_y_out)
{
    char label[8];
    int label_len = format_mirrored_score(score_pct, label, (int)sizeof(label));
    if (label_len <= 0) return 0;

    int text_x = x1;
    int text_y = y1 - (SCALED_CHAR_H + 2);  /* 框上方 2 像素 */
    if (text_y < 0) text_y = y2 + 2;        /* 超出上边界则放下方 */

    draw_string(text_x, text_y, label, color, alpha);
    if (text_x_out) *text_x_out = text_x;
    if (text_y_out) *text_y_out = text_y;
    return label_len;
}

/** 清除字符串区域 (仅 alpha) */
static void clear_string_area(int x, int y, int len)
{
    int w = len * (SCALED_CHAR_W + SCALED_SPACING);
    for (int row = 0; row < SCALED_CHAR_H; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col, py = y + row;
            if (px >= 0 && px < DISP_IMAGE_WIDTH && py >= 0 && py < DISP_IMAGE_HEIGHT) {
                set_pixel_alpha((uint32_t)px, (uint32_t)py, ALPHA_CLEAR);
            }
        }
    }
}

/*===========================================================================
 * 清除上一帧所有画框
 *===========================================================================*/
static void clear_all_prev_boxes(void)
{
    for (uint32_t i = 0; i < s_prev_count; i++) {
        clear_rect_border(s_prev_boxes[i].x1, s_prev_boxes[i].y1,
                          s_prev_boxes[i].x2, s_prev_boxes[i].y2);
        /* 清除文字 */
        if (s_prev_boxes[i].text_len > 0) {
            clear_string_area(s_prev_boxes[i].text_x, s_prev_boxes[i].text_y,
                              s_prev_boxes[i].text_len);
        }
    }
    s_prev_count = 0;
}

/*===========================================================================
 * 处理一帧多目标检测结果
 *===========================================================================*/
static void process_multi_result(const DetectionResult_t *result)
{
    /* 1. 先清除上一帧所有画框 */
    clear_all_prev_boxes();

    /* 2. 更新颜色槽: 释放已消失的 track_id */
    color_slots_update(result);

    if (result->count == 0) {
        FT_LOG(2, "[FT] f#%lu: 0 targets\r\n", (unsigned long)result->frame_id);
        return;
    }

    s_last_valid_ts = millis();
    uint32_t drawn = 0;
    uint32_t filtered_low_score = 0;

    FT_LOG(2, "[FT] f#%lu: %lu targets (ver=0x%04lX)\r\n",
           (unsigned long)result->frame_id,
           (unsigned long)result->count,
           (unsigned long)result->version);

    for (uint32_t i = 0; i < result->count && i < MAX_DETECTION_COUNT; i++) {
        const DetectionBox_t *box = &result->boxes[i];

        int32_t x1 = box->x1, y1 = box->y1, x2 = box->x2, y2 = box->y2;

        /* 坐标排序 */
        if (x2 < x1) { int32_t t = x1; x1 = x2; x2 = t; }
        if (y2 < y1) { int32_t t = y1; y1 = y2; y2 = t; }

        /* 检测超大越界 (Kalman coast 失控的标志) */
        int32_t margin = FACE_COORD_SPACE_W / 2;  /* 超出半屏即视为异常 */
        if (x1 < -margin || y1 < -margin ||
            x2 > FACE_COORD_SPACE_W + margin || y2 > FACE_COORD_SPACE_H + margin) {
            FT_LOG(1, "[FT] WARN ID=%u extreme coord (%ld,%ld)-(%ld,%ld), skip\r\n",
                   box->track_id, (long)x1, (long)y1, (long)x2, (long)y2);
            continue;
        }

        /* 裁剪到屏幕范围 (人脸在边缘时检测框可能超出屏幕) */
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 >= FACE_COORD_SPACE_W) x2 = FACE_COORD_SPACE_W - 1;
        if (y2 >= FACE_COORD_SPACE_H) y2 = FACE_COORD_SPACE_H - 1;
        if ((x2 - x1) <= 2 || (y2 - y1) <= 2) continue;

        uint8_t score_pct = normalize_score_pct(box->score);
        if (score_pct < FACE_SCORE_FILTER_PCT) {
            filtered_low_score++;
            FT_LOG(3, "[FT]  [%lu] ID=%u filtered score=%u%% < %u%%\r\n",
                   (unsigned long)i,
                   box->track_id,
                   (unsigned)score_pct,
                   (unsigned)FACE_SCORE_FILTER_PCT);
            continue;
        }

        /* 根据 track_id 选择颜色 */
        uint16_t color = get_track_color(box->track_id);

        /* 根据 miss_count 选择透明度:
         *   miss_count == 0 → 真实 CNN 检测, 全亮
         *   miss_count >  0 → Kalman coast 预测框, 半透明 */
        uint8_t alpha = (box->miss_count == 0) ? ALPHA_SOLID : ALPHA_COAST;

        /* 绘制带颜色的边框 */
        draw_rect_border(x1, y1, x2, y2, color, alpha);

        /* 绘制分数文字（框上方，镜像格式参考 Tracking_Demo） */
        int text_x = 0, text_y = 0;
        int label_len = draw_score_label(x1, y1, y2, score_pct, color, ALPHA_SOLID,
                         &text_x, &text_y);
        if (label_len <= 0) continue;

        /* 记录，用于下一帧擦除 */
        if (drawn < MAX_DETECTION_COUNT) {
            s_prev_boxes[drawn].x1 = x1;
            s_prev_boxes[drawn].y1 = y1;
            s_prev_boxes[drawn].x2 = x2;
            s_prev_boxes[drawn].y2 = y2;
            s_prev_boxes[drawn].text_x = text_x;
            s_prev_boxes[drawn].text_y = text_y;
            s_prev_boxes[drawn].text_len = label_len;
            drawn++;
        }

        /* 详细 SORT 跟踪日志 */
        FT_LOG(3, "[FT]  [%lu] ID=%u (%ld,%ld)-(%ld,%ld) s=%.2f v=(%d,%d) spd=%u kf=%u miss=%u %s\r\n",
               (unsigned long)i,
               box->track_id,
               (long)x1, (long)y1, (long)x2, (long)y2,
               box->score,
               (int)box->vx, (int)box->vy,
               box->speed, box->kf_confidence,
               box->miss_count,
               box->miss_count > 0 ? "[COAST]" : "");
    }

    s_prev_count = drawn;

    FT_LOG(2, "[FT]  drawn %lu boxes, filtered %lu (score<%u%%)\r\n",
           (unsigned long)drawn,
           (unsigned long)filtered_low_score,
           (unsigned)FACE_SCORE_FILTER_PCT);
}

/*===========================================================================
 * 公开接口: Mailbox 轮询
 *===========================================================================*/
void face_tracker_poll(void)
{
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

        switch (msg_type) {
        case MAILBOX_MSG_TYPE_MULTI: {
            /* v2.3 协议：多目标 SORT 跟踪结果 */
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionResult_t *result = (const DetectionResult_t *)addr;

            /* 校验 magic */
            if (!DETECTION_RESULT_IS_VALID(result)) {
                FT_LOG(1, "[FT] Invalid result @0x%08lX (magic=0x%08lX)\r\n",
                       (unsigned long)addr, (unsigned long)result->magic);
                break;
            }

            /* 版本提示（一次性） */
            {
                static bool s_version_logged = false;
                if (!s_version_logged) {
                    FT_LOG(1, "[FT] Protocol ver=0x%04lX (expected 0x%04X)\r\n",
                           (unsigned long)result->version,
                           DETECTION_PROTOCOL_VERSION);
                    s_version_logged = true;
                }
            }

            process_multi_result(result);
            if (result->count > 0) s_had_target = true;
            break;
        }

        case MAILBOX_MSG_TYPE_SINGLE: {
            /* 旧协议兼容：单目标 FaceRect (按原有逻辑) */
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionBox_t *box = (const DetectionBox_t *)addr;

            clear_all_prev_boxes();

            int32_t x1 = box->x1, y1 = box->y1, x2 = box->x2, y2 = box->y2;
            if (x2 < x1) { int32_t t = x1; x1 = x2; x2 = t; }
            if (y2 < y1) { int32_t t = y1; y1 = y2; y2 = t; }

            /* 边界裁剪 (与 MULTI 保持一致) */
            if (x1 < 0) x1 = 0;
            if (y1 < 0) y1 = 0;
            if (x2 >= FACE_COORD_SPACE_W) x2 = FACE_COORD_SPACE_W - 1;
            if (y2 >= FACE_COORD_SPACE_H) y2 = FACE_COORD_SPACE_H - 1;

            uint8_t score_pct = normalize_score_pct(box->score);

            if ((x2 - x1) > 2 && (y2 - y1) > 2 &&
                score_pct >= FACE_SCORE_FILTER_PCT) {
                draw_rect_border(x1, y1, x2, y2, COLOR_GREEN, ALPHA_SOLID);

                int text_x = 0, text_y = 0;
                int label_len = draw_score_label(x1, y1, y2, score_pct,
                                                 COLOR_GREEN, ALPHA_SOLID,
                                                 &text_x, &text_y);

                s_prev_boxes[0].x1 = x1;
                s_prev_boxes[0].y1 = y1;
                s_prev_boxes[0].x2 = x2;
                s_prev_boxes[0].y2 = y2;
                s_prev_boxes[0].text_x = text_x;
                s_prev_boxes[0].text_y = text_y;
                s_prev_boxes[0].text_len = label_len;
                s_prev_count = 1;
                s_last_valid_ts = millis();
            } else if (score_pct < FACE_SCORE_FILTER_PCT) {
                FT_LOG(2, "[FT] SINGLE filtered: score=%u%% < %u%%\r\n",
                       (unsigned)score_pct,
                       (unsigned)FACE_SCORE_FILTER_PCT);
            }

            FT_LOG(2, "[FT] SINGLE: (%ld,%ld)-(%ld,%ld) score=%.2f\r\n",
                   (long)x1, (long)y1, (long)x2, (long)y2, box->score);
            break;
        }

        case MAILBOX_MSG_TYPE_NO_RESULT: {
            /* 丢失目标打印一次，再次出现后再丢失则再打印 */
            if (s_had_target) {
                FT_LOG(2, "[FT] No detection\r\n");
                s_had_target = false;
            }
            break;
        }

        default:
            FT_LOG(1, "[FT] Unknown msg type: 0x%lX\r\n",
                   (unsigned long)msg_type >> 28);
            break;
        }
    }

    /* 超时清除所有画框 */
    if (s_prev_count > 0 && (millis() - s_last_valid_ts > FACE_TIMEOUT_MS)) {
        FT_LOG(1, "[FT] Timeout (%ums), clearing %lu boxes\r\n",
               FACE_TIMEOUT_MS, (unsigned long)s_prev_count);
        clear_all_prev_boxes();
    }
}
