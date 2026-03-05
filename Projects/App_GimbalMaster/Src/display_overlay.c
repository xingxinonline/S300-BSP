#include "display_overlay.h"

#include "app_log.h"
#include "track_state.h"

#include "board.h"
#include "gpio.h"
#include "i2c_soft.h"
#include "ov5640.h"
#include "psram.h"
#include "rcc.h"
#include "s300.h"
#include "video.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#if BOARD_CAMERA_FORMAT == 0
#define APP_OV5640_FMT OV5640_FMT_RGB565_R5G3_G3B5
#define APP_CAM_FMT CAMREA_RGB565
#else
#define APP_OV5640_FMT OV5640_FMT_YUV422_YUYV
#define APP_CAM_FMT CAMREA_YUV422
#endif

#define HUD_BG_COLOR         0x0000u
#define HUD_FG_COLOR         0x07E0u
#define HUD_ALPHA_CLEAR      0x00u
#define HUD_ALPHA_TEXT       0xFFu
#define HUD_X                4
#define HUD_Y                4
#define HUD_W                148
#define HUD_H                48
#define HUD_MIRROR_X         1
#define HUD_FORCE_SUBMIT_MS  0u
#define HUD_STAT_REPORT_MS   5000u
#define HUD_STAT_IDLE_KEEPALIVE_ROUNDS 6u

#define REG_FRAME0           (DSP_VIDEO_SS_BASE + 0x50u)
#define REG_FRAME1           (DSP_VIDEO_SS_BASE + 0x54u)

#define HUD_REG_FRAME0_ADDR  (DSP_VIDEO_SS_BASE + 0x40u)
#define HUD_REG_FRAME1_ADDR  (DSP_VIDEO_SS_BASE + 0x44u)
#define HUD_REG_ALPHA0_ADDR  (DSP_VIDEO_SS_BASE + 0x48u)
#define HUD_REG_ALPHA1_ADDR  (DSP_VIDEO_SS_BASE + 0x4Cu)
#define HUD_REG_CFG1         (DSP_VIDEO_SS_BASE + 0x194u)
#define HUD_REG_CFG2         (DSP_VIDEO_SS_BASE + 0x198u)
#define HUD_REG_CFG3         (DSP_VIDEO_SS_BASE + 0x19Cu)
#define HUD_REG_DISP_SIZE    (DSP_VIDEO_SS_BASE + 0x1C4u)

static uint8_t g_overlay_ready;
static uint8_t g_mm_ok;
static uint8_t g_psram_ok;
static uint8_t g_cam_ok;
static uint8_t g_cam_saddr;
static uint8_t g_cam_idh;
static uint8_t g_cam_idl;
static uint8_t g_write_idx;
static uint8_t g_last_submit_idx;
static char g_prev_line0[32];
static char g_prev_line1[32];
static char g_prev_line2[32];
static char g_prev_line3[32];
static uint8_t g_prev_valid;

static uint32_t g_stat_poll_cnt;
static uint32_t g_stat_call_cnt;
static uint32_t g_stat_render_cnt;
static uint32_t g_stat_submit_cnt;
static uint32_t g_stat_req0_hit;
static uint32_t g_stat_req1_hit;
static uint32_t g_stat_fallback_hit;
static uint32_t g_stat_submit0;
static uint32_t g_stat_submit1;
static uint32_t g_stat_skip_nochange;
static uint32_t g_stat_busy_both;
static uint32_t g_stat_shadow_mismatch;
static uint32_t g_stat_shadow_repair;
static uint32_t g_stat_reg_repair;
static uint32_t g_stat_last_report_ms;
static uint32_t g_stat_idle_rounds;
static uint8_t g_hud_shadow_valid;
static uint8_t g_hud_shadow[HUD_W * HUD_H];
#if HUD_FORCE_SUBMIT_MS > 0u
static uint32_t g_last_force_submit_ms;
#endif

static void hud_capture_alpha_rect(uint8_t src_idx);
static void hud_shadow_guard_check_and_repair(void);

static uint32_t hud_expected_cfg1(void)
{
    uint32_t data_row = (uint32_t)(DISP_IMAGE_WIDTH + DISP_START_X - 1);
    uint32_t start_x = (uint32_t)DISP_START_X;
    return ((((data_row >> 8) & 0xFFu) << 24) |
            ((start_x & 0xFFu) << 16) |
            (((start_x >> 8) & 0xFFu) << 8) |
            0x2Au);
}

static uint32_t hud_expected_cfg2(void)
{
    uint32_t data_row = (uint32_t)(DISP_IMAGE_WIDTH + DISP_START_X - 1);
    uint32_t start_y = (uint32_t)DISP_START_Y;
    return (((start_y & 0xFFu) << 24) |
            (((start_y >> 8) & 0xFFu) << 16) |
            (0x2Bu << 8) |
            ((data_row >> 0) & 0xFFu));
}

static uint32_t hud_expected_cfg3(void)
{
    uint32_t data_col = (uint32_t)(DISP_IMAGE_HEIGHT + DISP_START_Y - 1);
    return ((0x00u << 24) |
            (0x2Cu << 16) |
            (((data_col >> 0) & 0xFFu) << 8) |
            ((data_col >> 8) & 0xFFu));
}

static void hud_display_reg_guard_check_and_repair(void)
{
    uint8_t changed = 0u;

    if (REG32(HUD_REG_FRAME0_ADDR) != (uint32_t)DISP_RFRAME0_ADDR) {
        REG32(HUD_REG_FRAME0_ADDR) = (uint32_t)DISP_RFRAME0_ADDR;
        changed = 1u;
    }
    if (REG32(HUD_REG_FRAME1_ADDR) != (uint32_t)DISP_RFRAME1_ADDR) {
        REG32(HUD_REG_FRAME1_ADDR) = (uint32_t)DISP_RFRAME1_ADDR;
        changed = 1u;
    }
    if (REG32(HUD_REG_ALPHA0_ADDR) != (uint32_t)DISP_RALPHA0_ADDR) {
        REG32(HUD_REG_ALPHA0_ADDR) = (uint32_t)DISP_RALPHA0_ADDR;
        changed = 1u;
    }
    if (REG32(HUD_REG_ALPHA1_ADDR) != (uint32_t)DISP_RALPHA1_ADDR) {
        REG32(HUD_REG_ALPHA1_ADDR) = (uint32_t)DISP_RALPHA1_ADDR;
        changed = 1u;
    }

    if (REG32(HUD_REG_CFG1) != hud_expected_cfg1()) {
        REG32(HUD_REG_CFG1) = hud_expected_cfg1();
        changed = 1u;
    }
    if (REG32(HUD_REG_CFG2) != hud_expected_cfg2()) {
        REG32(HUD_REG_CFG2) = hud_expected_cfg2();
        changed = 1u;
    }
    if (REG32(HUD_REG_CFG3) != hud_expected_cfg3()) {
        REG32(HUD_REG_CFG3) = hud_expected_cfg3();
        changed = 1u;
    }
    if (REG32(HUD_REG_DISP_SIZE) != ((uint32_t)DISP_IMAGE_WIDTH | ((uint32_t)DISP_IMAGE_HEIGHT << 16))) {
        REG32(HUD_REG_DISP_SIZE) = ((uint32_t)DISP_IMAGE_WIDTH | ((uint32_t)DISP_IMAGE_HEIGHT << 16));
        changed = 1u;
    }

    if (changed != 0u) {
        /* Trigger M4 side sync after repairing MM display registers. */
        REG32(DSP_VIDEO_SS_BASE + 0x70u) = 1u;
        REG32(DSP_VIDEO_SS_BASE + 0x1E0u) = 1u;
        g_stat_reg_repair++;
        app_log_puts("[HUD][GUARD] display regs restored\r\n");
    }
}

static inline volatile uint16_t *hud_write_alphabuffer(void)
{
    return (g_write_idx == 0u) ? (volatile uint16_t *)DISP_RALPHA0_ADDR
                               : (volatile uint16_t *)DISP_RALPHA1_ADDR;
}

static uint8_t hud_acquire_write_buffer(void)
{
    uint32_t req0 = REG32(REG_FRAME0) & 0x1u;
    uint32_t req1 = REG32(REG_FRAME1) & 0x1u;

    g_stat_poll_cnt++;

    /* req bit: 0=idle/free, 1=pending/busy */
    if ((req0 == 0u) && (req1 != 0u)) {
        g_stat_req0_hit++;
        return 0u;
    }
    if ((req1 == 0u) && (req0 != 0u)) {
        g_stat_req1_hit++;
        return 1u;
    }

    /* Fallback: alternate by last submitted buffer to keep true ping-pong. */
    g_stat_fallback_hit++;
    return (uint8_t)((g_last_submit_idx == 0u) ? 1u : 0u);
}

static void hud_fill_color_buffers(uint16_t color)
{
    volatile uint16_t *f0 = (volatile uint16_t *)DISP_RFRAME0_ADDR;
    volatile uint16_t *f1 = (volatile uint16_t *)DISP_RFRAME1_ADDR;
    uint32_t pixels = (uint32_t)DISP_IMAGE_WIDTH * (uint32_t)DISP_IMAGE_HEIGHT;

    for (uint32_t i = 0; i < pixels; i++) {
        f0[i] = color;
        f1[i] = color;
    }
}

static void hud_clear_alpha_buffer(uint8_t buf_idx)
{
    volatile uint16_t *ab16 = (buf_idx == 0u)
                              ? (volatile uint16_t *)DISP_RALPHA0_ADDR
                              : (volatile uint16_t *)DISP_RALPHA1_ADDR;
    uint32_t words = ((uint32_t)DISP_IMAGE_WIDTH * (uint32_t)DISP_IMAGE_HEIGHT) / 2u;

    for (uint32_t i = 0; i < words; i++) {
        ab16[i] = 0x0000u;
    }
}

static uint8_t hud_alpha_get_pixel(uint8_t buf_idx, int x, int y)
{
    volatile uint16_t *ab16 = (buf_idx == 0u)
                              ? (volatile uint16_t *)DISP_RALPHA0_ADDR
                              : (volatile uint16_t *)DISP_RALPHA1_ADDR;
    uint32_t pixel_idx = (uint32_t)y * (uint32_t)DISP_IMAGE_WIDTH + (uint32_t)x;
    uint32_t word_idx = pixel_idx / 2u;
    uint32_t byte_pos = pixel_idx & 1u;
    uint16_t val = ab16[word_idx];

    return (byte_pos == 0u) ? (uint8_t)(val & 0xFFu) : (uint8_t)((val >> 8) & 0xFFu);
}

static void hud_alpha_set_pixel(uint8_t buf_idx, int x, int y, uint8_t alpha)
{
    volatile uint16_t *ab16 = (buf_idx == 0u)
                              ? (volatile uint16_t *)DISP_RALPHA0_ADDR
                              : (volatile uint16_t *)DISP_RALPHA1_ADDR;
    uint32_t pixel_idx = (uint32_t)y * (uint32_t)DISP_IMAGE_WIDTH + (uint32_t)x;
    uint32_t word_idx = pixel_idx / 2u;
    uint32_t byte_pos = pixel_idx & 1u;
    uint16_t val = ab16[word_idx];

    if (byte_pos == 0u) {
        val = (uint16_t)((val & 0xFF00u) | alpha);
    } else {
        val = (uint16_t)((val & 0x00FFu) | ((uint16_t)alpha << 8));
    }

    ab16[word_idx] = val;
}

static void hud_mirror_alpha_rect(uint8_t src_idx)
{
    uint8_t dst_idx = (src_idx == 0u) ? 1u : 0u;

    for (int y = HUD_Y; y < (HUD_Y + HUD_H); y++) {
        for (int x = HUD_X; x < (HUD_X + HUD_W); x++) {
            uint8_t a = hud_alpha_get_pixel(src_idx, x, y);
            hud_alpha_set_pixel(dst_idx, x, y, a);
        }
    }
}

static void hud_submit_write_buffer(uint8_t buf_idx)
{
    if (buf_idx == 0u) {
        g_stat_submit0++;
        REG32(REG_FRAME0) = 1u;
        while ((REG32(REG_FRAME0) & 0x1u) != 0u) {
        }
    } else {
        g_stat_submit1++;
        REG32(REG_FRAME1) = 1u;
        while ((REG32(REG_FRAME1) & 0x1u) != 0u) {
        }
    }

    g_stat_submit_cnt++;
    g_last_submit_idx = buf_idx;
}

static void hud_report_stats_if_due(void)
{
    uint32_t now_ms;
    uint8_t is_idle;

    now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now_ms - g_stat_last_report_ms) < HUD_STAT_REPORT_MS) {
        return;
    }

    hud_display_reg_guard_check_and_repair();
    hud_shadow_guard_check_and_repair();

    is_idle = (uint8_t)((g_stat_render_cnt == 0u) &&
                        (g_stat_submit_cnt == 0u) &&
                        (g_stat_poll_cnt == 0u) &&
                        (g_stat_req0_hit == 0u) &&
                        (g_stat_req1_hit == 0u) &&
                        (g_stat_fallback_hit == 0u) &&
                        (g_stat_call_cnt > 0u) &&
                        (g_stat_skip_nochange == g_stat_call_cnt));

    if (is_idle != 0u) {
        g_stat_idle_rounds++;
        if (g_stat_idle_rounds < HUD_STAT_IDLE_KEEPALIVE_ROUNDS) {
            g_stat_call_cnt = 0u;
            g_stat_poll_cnt = 0u;
            g_stat_skip_nochange = 0u;
            g_stat_render_cnt = 0u;
            g_stat_submit_cnt = 0u;
            g_stat_req0_hit = 0u;
            g_stat_req1_hit = 0u;
            g_stat_fallback_hit = 0u;
            g_stat_submit0 = 0u;
            g_stat_submit1 = 0u;
            g_stat_last_report_ms = now_ms;
            return;
        }
    }

    g_stat_idle_rounds = 0u;

    app_log_printf("[HUD][STAT] call=%lu poll=%lu skip=%lu render=%lu submit=%lu fps=%lu req0=%lu req1=%lu fb=%lu busy=%lu drift=%lu fix=%lu regfix=%lu s0=%lu s1=%lu\r\n",
                   (unsigned long)g_stat_call_cnt,
                   (unsigned long)g_stat_poll_cnt,
                   (unsigned long)g_stat_skip_nochange,
                   (unsigned long)g_stat_render_cnt,
                   (unsigned long)g_stat_submit_cnt,
                   (unsigned long)g_stat_submit_cnt,
                   (unsigned long)g_stat_req0_hit,
                   (unsigned long)g_stat_req1_hit,
                   (unsigned long)g_stat_fallback_hit,
                   (unsigned long)g_stat_busy_both,
                   (unsigned long)g_stat_shadow_mismatch,
                   (unsigned long)g_stat_shadow_repair,
                   (unsigned long)g_stat_reg_repair,
                   (unsigned long)g_stat_submit0,
                   (unsigned long)g_stat_submit1);

    g_stat_call_cnt = 0u;
    g_stat_poll_cnt = 0u;
    g_stat_skip_nochange = 0u;
    g_stat_render_cnt = 0u;
    g_stat_submit_cnt = 0u;
    g_stat_req0_hit = 0u;
    g_stat_req1_hit = 0u;
    g_stat_fallback_hit = 0u;
    g_stat_busy_both = 0u;
    g_stat_shadow_mismatch = 0u;
    g_stat_shadow_repair = 0u;
    g_stat_reg_repair = 0u;
    g_stat_submit0 = 0u;
    g_stat_submit1 = 0u;
    g_stat_last_report_ms = now_ms;
}

static const uint8_t g_font5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08}
};

static void hud_put_pixel(int vx, int vy, uint16_t color, uint8_t alpha)
{
    int px = vx;
    int py = vy;
    uint32_t pixel_idx;
    uint32_t word_idx;
    uint32_t byte_pos;
    uint16_t aval;
    volatile uint16_t *ab16;

#if HUD_MIRROR_X
    px = (DISP_IMAGE_WIDTH - 1) - px;
#endif

    if (px < 0 || px >= DISP_IMAGE_WIDTH || py < 0 || py >= DISP_IMAGE_HEIGHT) {
        return;
    }

    pixel_idx = (uint32_t)py * (uint32_t)DISP_IMAGE_WIDTH + (uint32_t)px;
    word_idx = pixel_idx / 2u;
    byte_pos = pixel_idx & 1u;

    (void)color;
    ab16 = hud_write_alphabuffer();

    aval = ab16[word_idx];
    if (byte_pos == 0u) {
        aval = (uint16_t)((aval & 0xFF00u) | alpha);
    } else {
        aval = (uint16_t)((aval & 0x00FFu) | ((uint16_t)alpha << 8));
    }
    ab16[word_idx] = aval;
}

static void hud_clear_rect(int x, int y, int w, int h)
{
    for (int iy = 0; iy < h; iy++) {
        for (int ix = 0; ix < w; ix++) {
            hud_put_pixel(x + ix, y + iy, HUD_BG_COLOR, HUD_ALPHA_CLEAR);
        }
    }
}

static void hud_draw_char(int x, int y, char c)
{
    const uint8_t *glyph;

    if (c < 32 || c > 126) {
        return;
    }

    glyph = g_font5x7[(int)c - 32];

    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (((bits >> row) & 0x01u) != 0u) {
                hud_put_pixel(x + col, y + row, HUD_FG_COLOR, HUD_ALPHA_TEXT);
            }
        }
    }
}

static void hud_draw_text(int x, int y, const char *s)
{
    int cx = x;
    while (*s != '\0') {
        hud_draw_char(cx, y, *s);
        cx += 6;
        s++;
    }
}

static int camera_ov5640_preinit(void)
{
    i2c_soft_t i2c;
    i2c_soft_cfg_t cfg;
    uint8_t saddr = OV5640_I2C_ADDR;
    uint8_t idh = 0u;
    uint8_t idl = 0u;
    int ret;
    int p3c;
    int p3d;

#if BOARD_CAMERA_ENABLE
    board_camera_i2c_pins_init();
    board_camera_ctrl_pins_init();
#endif

    cfg.port = BOARD_CAMERA_I2C_PORT;
    cfg.pin_scl = BOARD_CAMERA_I2C_SCL_PIN;
    cfg.pin_sda = BOARD_CAMERA_I2C_SDA_PIN;
    cfg.func_scl = FUNCTION_2;
    cfg.func_sda = FUNCTION_2;
    cfg.pull_mode = GPIO_UP;
    cfg.bus_hz = BOARD_CAMERA_I2C_FREQ;

    ret = i2c_soft_init(&i2c, &cfg, SystemCoreClock);
    if (ret != 0) {
        app_log_printf("[DISPLAY] camera i2c init failed=%d\r\n", ret);
        g_cam_ok = 0u;
        return -1;
    }

    ov5640_hard_init();
    i2c_soft_bus_recover(&i2c);

    p3c = i2c_soft_probe(&i2c, 0x3C);
    p3d = i2c_soft_probe(&i2c, 0x3D);

    if ((p3c != 0) && (p3d != 0)) {
        app_log_puts("[DISPLAY] OV5640 not found on 0x3C/0x3D\r\n");
        g_cam_ok = 0u;
        return -1;
    }

    if (p3c != 0) {
        saddr = 0x3D;
    }

    g_cam_saddr = saddr;

    (void)i2c_soft_mem_read(&i2c, saddr, 0x300Au, true, &idh, 1);
    (void)i2c_soft_mem_read(&i2c, saddr, 0x300Bu, true, &idl, 1);
    app_log_printf("[DISPLAY] OV5640 id=0x%02X%02X saddr=0x%02X\r\n",
                   (unsigned int)idh,
                   (unsigned int)idl,
                   (unsigned int)saddr);

    g_cam_idh = idh;
    g_cam_idl = idl;

    ret = ov5640_init(&i2c, saddr, APP_OV5640_FMT);
    if (ret != 0) {
        app_log_printf("[DISPLAY] ov5640_init failed=%d\r\n", ret);
        g_cam_ok = 0u;
        return -1;
    }

    g_cam_ok = 1u;

    return 0;
}

static void set_alpha_buffer(uint8_t alpha)
{
    volatile uint16_t *a0 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    volatile uint16_t *a1 = (volatile uint16_t *)DISP_RALPHA1_ADDR;
    uint16_t val = (uint16_t)alpha | ((uint16_t)alpha << 8);
    uint32_t n_words = (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT) / 2u;

    for (uint32_t i = 0; i < n_words; i++) {
        a0[i] = val;
        a1[i] = val;
    }
}

int display_overlay_init(void)
{
    int cam_ret;

    if (rcc_init_mm_pll(8, 400, 0, 3, 2) < 0) {
        app_log_puts("[DISPLAY] rcc_init_mm_pll failed\r\n");
        g_mm_ok = 0u;
        return -1;
    }
    g_mm_ok = 1u;

    init_psram(4, 1);
    g_psram_ok = 1u;

    cam_ret = camera_ov5640_preinit();
    if (cam_ret != 0) {
        app_log_printf("[DISPLAY] camera preinit failed=%d, continue init_video\r\n", cam_ret);
    }

    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);

    hud_fill_color_buffers(HUD_FG_COLOR);

    /* Default to camera passthrough: fully transparent overlay. */
    set_alpha_buffer(0x00u);

    REG32(DSP_VIDEO_SS_BASE + 0x50) = 1u;
    while ((REG32(DSP_VIDEO_SS_BASE + 0x50) & 0x1u) != 0u) {
    }

    /* Force MM display register synchronization on M4 side. */
    REG32(DSP_VIDEO_SS_BASE + 0x70) = 1u;
    REG32(DSP_VIDEO_SS_BASE + 0x1E0) = 1u;

    app_log_puts("[DISPLAY] video init OK\r\n");
    g_write_idx = 0u;
    g_last_submit_idx = 0u;
    g_overlay_ready = 1u;
    return cam_ret;
}

int display_overlay_is_ready(void)
{
    return (g_overlay_ready != 0u) ? 1 : 0;
}

void display_overlay_render_debug(void)
{
    char line0[32];
    char line1[32];
    char line2[32];
    char line3[32];
    const char *state_name = track_state_to_string(track_state_get());
    uint32_t reg70;
    uint32_t reg1e0;
    uint8_t target_buf;
    uint8_t force_submit = 0u;
    uint32_t req0;
    uint32_t req1;

    if (g_overlay_ready == 0u) {
        return;
    }

    g_stat_call_cnt++;

#if HUD_FORCE_SUBMIT_MS > 0u
    {
        uint32_t now_ms;
        now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now_ms - g_last_force_submit_ms) >= HUD_FORCE_SUBMIT_MS) {
        force_submit = 1u;
        g_last_force_submit_ms = now_ms;
    }
    }
#endif

    hud_report_stats_if_due();

    reg70 = REG32(DSP_VIDEO_SS_BASE + 0x70);
    reg1e0 = REG32(DSP_VIDEO_SS_BASE + 0x1E0);

    (void)snprintf(line0, sizeof(line0), "MM:%u PS:%u CAM:%u", g_mm_ok, g_psram_ok, g_cam_ok);
    (void)snprintf(line1, sizeof(line1), "ID:%02X%02X A:%02X", g_cam_idh, g_cam_idl, g_cam_saddr);
    (void)snprintf(line2, sizeof(line2), "R70:%08lX E0:%08lX", (unsigned long)reg70, (unsigned long)reg1e0);
    (void)snprintf(line3, sizeof(line3), "STATE:%s", state_name);

    if (g_prev_valid != 0u &&
        (strcmp(line0, g_prev_line0) == 0) &&
        (strcmp(line1, g_prev_line1) == 0) &&
        (strcmp(line2, g_prev_line2) == 0) &&
        (strcmp(line3, g_prev_line3) == 0) &&
        (force_submit == 0u)) {
        g_stat_skip_nochange++;
        return;
    }

    req0 = REG32(REG_FRAME0) & 0x1u;
    req1 = REG32(REG_FRAME1) & 0x1u;
    if ((req0 != 0u) && (req1 != 0u)) {
        /* Both buffers are pending in HW, retry next cycle to avoid writing into busy frame. */
        g_stat_busy_both++;
        return;
    }

    (void)strncpy(g_prev_line0, line0, sizeof(g_prev_line0) - 1u);
    g_prev_line0[sizeof(g_prev_line0) - 1u] = '\0';
    (void)strncpy(g_prev_line1, line1, sizeof(g_prev_line1) - 1u);
    g_prev_line1[sizeof(g_prev_line1) - 1u] = '\0';
    (void)strncpy(g_prev_line2, line2, sizeof(g_prev_line2) - 1u);
    g_prev_line2[sizeof(g_prev_line2) - 1u] = '\0';
    (void)strncpy(g_prev_line3, line3, sizeof(g_prev_line3) - 1u);
    g_prev_line3[sizeof(g_prev_line3) - 1u] = '\0';
    g_prev_valid = 1u;

    g_stat_render_cnt++;

    target_buf = hud_acquire_write_buffer();
    g_write_idx = target_buf;

    /* Always clear full back alpha to avoid stale glyph residues after buffer swap. */
    hud_clear_alpha_buffer(g_write_idx);
    hud_clear_rect(HUD_X, HUD_Y, HUD_W, HUD_H);
    hud_draw_text(HUD_X + 2, HUD_Y + 2, line0);
    hud_draw_text(HUD_X + 2, HUD_Y + 12, line1);
    hud_draw_text(HUD_X + 2, HUD_Y + 22, line2);
    hud_draw_text(HUD_X + 2, HUD_Y + 32, line3);

    /* Keep both ping-pong buffers visually identical for static HUD stability. */
    hud_mirror_alpha_rect(g_write_idx);
    hud_capture_alpha_rect(g_write_idx);

    hud_submit_write_buffer(g_write_idx);
}

static void hud_capture_alpha_rect(uint8_t src_idx)
{
    uint32_t k = 0u;

    for (int y = HUD_Y; y < (HUD_Y + HUD_H); y++) {
        for (int x = HUD_X; x < (HUD_X + HUD_W); x++) {
            g_hud_shadow[k++] = hud_alpha_get_pixel(src_idx, x, y);
        }
    }

    g_hud_shadow_valid = 1u;
}

static uint32_t hud_alpha_rect_checksum_buf(uint8_t buf_idx)
{
    uint32_t sum = 2166136261u;

    for (int y = HUD_Y; y < (HUD_Y + HUD_H); y++) {
        for (int x = HUD_X; x < (HUD_X + HUD_W); x++) {
            sum ^= (uint32_t)hud_alpha_get_pixel(buf_idx, x, y);
            sum *= 16777619u;
        }
    }

    return sum;
}

static uint32_t hud_alpha_rect_checksum_shadow(void)
{
    uint32_t sum = 2166136261u;
    uint32_t n = (uint32_t)(HUD_W * HUD_H);

    for (uint32_t i = 0u; i < n; i++) {
        sum ^= (uint32_t)g_hud_shadow[i];
        sum *= 16777619u;
    }

    return sum;
}

static void hud_restore_alpha_rect_from_shadow(uint8_t dst_idx)
{
    uint32_t k = 0u;

    for (int y = HUD_Y; y < (HUD_Y + HUD_H); y++) {
        for (int x = HUD_X; x < (HUD_X + HUD_W); x++) {
            hud_alpha_set_pixel(dst_idx, x, y, g_hud_shadow[k++]);
        }
    }
}

static void hud_shadow_guard_check_and_repair(void)
{
    uint32_t sum_ref;
    uint32_t sum0;
    uint32_t sum1;

    if (g_hud_shadow_valid == 0u) {
        return;
    }

    sum_ref = hud_alpha_rect_checksum_shadow();
    sum0 = hud_alpha_rect_checksum_buf(0u);
    sum1 = hud_alpha_rect_checksum_buf(1u);

    if ((sum0 == sum_ref) && (sum1 == sum_ref)) {
        return;
    }

    g_stat_shadow_mismatch++;
    hud_restore_alpha_rect_from_shadow(0u);
    hud_restore_alpha_rect_from_shadow(1u);
    g_stat_shadow_repair++;

    app_log_printf("[HUD][GUARD] alpha drift fixed ref=%08lX b0=%08lX b1=%08lX\r\n",
                   (unsigned long)sum_ref,
                   (unsigned long)sum0,
                   (unsigned long)sum1);
}