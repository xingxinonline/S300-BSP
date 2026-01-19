/*
 * 文件：ui_display.c
 * 说明：显示适配（单硬件buffer）版：单帧显示 + LVGL局部刷新 + DMA中断
 */
#include <string.h>
#include <stdio.h>
#include "s300.h"
#include "video.h"
#include "lvgl.h"
#include "ui_display.h"
#include "dma.h"
#include "rcc.h"

/* ---- Debug logging ---- */
#ifndef UI_DEBUG
#define UI_DEBUG 1
#endif
#ifndef UI_LOG_LEVEL
#define UI_LOG_LEVEL 1
#endif

#if UI_DEBUG
typedef enum {
    UI_LOG_LEVEL_ERROR = 0,
    UI_LOG_LEVEL_WARN  = 1,
    UI_LOG_LEVEL_INFO  = 2,
    UI_LOG_LEVEL_DEBUG = 3,
    UI_LOG_LEVEL_VERBOSE = 4,
} ui_log_level_t;

#define UI_LOG_IMPL(lv, tag, fmt, ...) do { \
    if ((lv) <= UI_LOG_LEVEL) { \
        printf("[UI][%s] " fmt "\r\n", tag, ##__VA_ARGS__); \
    } \
} while (0)

#define UI_LOGE(tag, fmt, ...) UI_LOG_IMPL(UI_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define UI_LOGW(tag, fmt, ...) UI_LOG_IMPL(UI_LOG_LEVEL_WARN,  tag, fmt, ##__VA_ARGS__)
#define UI_LOGI(tag, fmt, ...) UI_LOG_IMPL(UI_LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define UI_LOGD(tag, fmt, ...) UI_LOG_IMPL(UI_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define UI_LOGV(tag, fmt, ...) UI_LOG_IMPL(UI_LOG_LEVEL_VERBOSE, tag, fmt, ##__VA_ARGS__)
#define UI_LOG(tag, fmt, ...)  UI_LOGI(tag, fmt, ##__VA_ARGS__)
#else
#define UI_LOGE(tag, fmt, ...) ((void)0)
#define UI_LOGW(tag, fmt, ...) ((void)0)
#define UI_LOGI(tag, fmt, ...) ((void)0)
#define UI_LOGD(tag, fmt, ...) ((void)0)
#define UI_LOGV(tag, fmt, ...) ((void)0)
#define UI_LOG(tag, fmt, ...)  ((void)0)
#endif

static volatile uint16_t* s_f0;
static volatile uint16_t* s_f1;
static volatile uint8_t*  s_a0;
static volatile uint8_t*  s_a1;
static const uint32_t REG_F0 = (DSP_VIDEO_SS_BASE + 0x50u);
static const uint32_t REG_F1 = (DSP_VIDEO_SS_BASE + 0x54u);

/* LVGL 局部渲染用的双draw buffer（约屏幕 1/5 高度） */
#define DRAWBUF_LINES     15u  
#define BYTES_PER_PIXEL   2u  /* RGB565 */

LV_ATTRIBUTE_MEM_ALIGN static uint16_t s_drawbuf1[DISP_IMAGE_WIDTH * DRAWBUF_LINES] __attribute__((aligned(8)));
LV_ATTRIBUTE_MEM_ALIGN static uint16_t s_drawbuf2[DISP_IMAGE_WIDTH * DRAWBUF_LINES] __attribute__((aligned(8)));

static volatile uint8_t  s_front_idx = 0u;   /* 0->s_f0, 1->s_f1 */
static volatile uint8_t  s_dma_busy = 0u;   
static volatile uint8_t  s_dma_last = 0u;   
static lv_display_t *    s_dma_disp = NULL; 

static volatile uint32_t s_stat_frames = 0;
static volatile uint32_t s_stat_flushes = 0;
static volatile uint32_t s_stat_cpu_fallbacks = 0;
static uint32_t          s_stat_last_report_ms = 0;

#ifndef UI_STAT_OVERLAY
#define UI_STAT_OVERLAY 0
#endif

#if UI_STAT_OVERLAY
static lv_obj_t *        s_stat_label = NULL;
static char              s_stat_text[96];
#endif

#ifndef UI_KEEPALIVE_ENABLE
#define UI_KEEPALIVE_ENABLE 1
#endif
#ifndef UI_KEEPALIVE_MS
#define UI_KEEPALIVE_MS 200u
#endif
static lv_timer_t *      s_keepalive_timer = NULL;

static void ui_keepalive_timer_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
#if UI_STAT_OVERLAY
    if (s_stat_label) {
        lv_obj_invalidate(s_stat_label);
        return;
    }
#endif
    lv_obj_invalidate(lv_screen_active());
}

#ifndef UI_STAT_VERTICAL
#define UI_STAT_VERTICAL 1
#endif

#define UI_DMA_IDX          DMA_IDX0
#define UI_DMA_CH_SCATTER   2u  
#define UI_DMA_CH_LLI       3u  

LV_ATTRIBUTE_MEM_ALIGN static dma_lli_t s_dma_llis[64] __attribute__((aligned(8)));

static inline uint32_t frame_bytes(void)
{
    return (uint32_t)DISP_IMAGE_WIDTH * (uint32_t)DISP_IMAGE_HEIGHT * BYTES_PER_PIXEL;
}

static inline volatile uint16_t * get_front_fb(void)
{
    return (s_front_idx == 0u) ? s_f0 : s_f1;
}

static inline volatile uint16_t * get_back_fb(void)
{
    return (s_front_idx == 0u) ? s_f1 : s_f0;
}

static inline volatile uint16_t * get_draw_fb(void)
{
    return get_back_fb();
}

static inline void switch_present_to(uint8_t fb_idx)
{
    if (fb_idx == 0u) {
        REG32(REG_F0) = 1u;
        while ((REG32(REG_F0) & 0x1u) != 0u) { }
        s_front_idx = 0u;
    } else {
        REG32(REG_F1) = 1u;
        while ((REG32(REG_F1) & 0x1u) != 0u) { }
        s_front_idx = 1u;
    }
}

#define UI_CHROMA_KEY_COLOR  0x07E0

static inline uint16_t pack_alpha16(uint8_t a0, uint8_t a1)
{
    return (uint16_t)((a1 << 8) | a0);
}

/**
 * @brief CPU Copy with Alpha Keying (RGB565 + 8bit Alpha Plane)
 */
static void cpu_copy_with_alpha_keying(const lv_area_t * area, const uint16_t * src)
{
    volatile uint16_t *dst_rgb_base = get_draw_fb();
    volatile uint16_t *dst_alpha_word_base = (volatile uint16_t *)(uintptr_t)((s_front_idx == 0u) ? (uintptr_t)s_a1 : (uintptr_t)s_a0);
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    for (int32_t r = 0; r < h; ++r) {
        int32_t y = area->y1 + r;
        const uint16_t *src_row = src + r * w;
        volatile uint16_t *dst_rgb_row = dst_rgb_base + y * DISP_IMAGE_WIDTH + area->x1;

        for (int32_t c = 0; c < w; ++c) {
            uint16_t pixel = src_row[c];
            dst_rgb_row[c] = pixel;

            /* Calculate Alpha */
            uint8_t alpha = (pixel == UI_CHROMA_KEY_COLOR) ? 0x00 : 0xFF;

            /* Mask Top/Bottom to keep middle 240 active */
            if (y < 40 || y >= 280) {
                alpha = 0x00;
            }

            uint32_t global_idx = (uint32_t)y * DISP_IMAGE_WIDTH + (area->x1 + c);
            uint32_t word_idx = global_idx / 2;
            bool is_high_byte = (global_idx % 2) != 0;

            volatile uint16_t *alpha_word_ptr = dst_alpha_word_base + word_idx;
            uint16_t current_word = *alpha_word_ptr;
            
            if (is_high_byte) {
                current_word = (current_word & 0x00FF) | ((uint16_t)alpha << 8);
            } else {
                current_word = (current_word & 0xFF00) | (uint16_t)alpha;
            }
            *alpha_word_ptr = current_word;
        }
    }
}

static void start_dma_rect_copy(lv_display_t * disp, const lv_area_t * area, const uint16_t * src, bool is_last)
{
    /* Use CPU copy with transparency handling instead of DMA */
    cpu_copy_with_alpha_keying(area, src);
    lv_display_flush_ready(disp);
}

static void do_fullframe_baseline_copy(void)
{
    volatile uint16_t *src_rgb = get_front_fb();
    volatile uint16_t *dst_rgb = get_back_fb();
    uint32_t len_rgb = frame_bytes();

    volatile uint16_t *src_alpha = (volatile uint16_t *)(uintptr_t)((s_front_idx == 0u) ? (uintptr_t)s_a0 : (uintptr_t)s_a1);
    volatile uint16_t *dst_alpha = (volatile uint16_t *)(uintptr_t)((s_front_idx == 0u) ? (uintptr_t)s_a1 : (uintptr_t)s_a0);
    uint32_t len_alpha = (uint32_t)DISP_IMAGE_WIDTH * (uint32_t)DISP_IMAGE_HEIGHT;

    while (s_dma_busy || is_dma_busy(EM_DMA0, UI_DMA_CH_SCATTER) || is_dma_busy(EM_DMA0, UI_DMA_CH_LLI)) { }

    const bool use32_rgb = ((((uintptr_t)src_rgb | (uintptr_t)dst_rgb | (uintptr_t)len_rgb) & 0x3u) == 0u);
    emDMATRWIDTH w_rgb = use32_rgb ? EM_TR_WIDTH_32_BIT : EM_TR_WIDTH_16_BIT;
    set_dma_memcpy_lli_blocking(EM_DMA0, UI_DMA_CH_LLI, (uint32_t)(uintptr_t)src_rgb, (uint32_t)(uintptr_t)dst_rgb, len_rgb, w_rgb, s_dma_llis, (uint32_t)(sizeof(s_dma_llis)/sizeof(s_dma_llis[0])));

    const bool use32_alpha = ((((uintptr_t)src_alpha | (uintptr_t)dst_alpha | (uintptr_t)len_alpha) & 0x3u) == 0u);
    emDMATRWIDTH w_alpha = use32_alpha ? EM_TR_WIDTH_32_BIT : EM_TR_WIDTH_16_BIT;
    set_dma_memcpy_lli_blocking(EM_DMA0, UI_DMA_CH_LLI, (uint32_t)(uintptr_t)src_alpha, (uint32_t)(uintptr_t)dst_alpha, len_alpha, w_alpha, s_dma_llis, (uint32_t)(sizeof(s_dma_llis)/sizeof(s_dma_llis[0])));
}

static void lvgl_display_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_START) {
        UI_LOGD("EVT", "REFR_START: baseline copy front=%u", (unsigned)s_front_idx);
        do_fullframe_baseline_copy();
    } else if (code == LV_EVENT_REFR_READY) {
        uint8_t back_idx = (s_front_idx == 0u) ? 1u : 0u;
        UI_LOGD("EVT", "REFR_READY: present back=%u", (unsigned)back_idx);
        switch_present_to(back_idx);
        ++s_stat_frames;
        
        uint32_t now = lv_tick_get();
        if (s_stat_last_report_ms == 0) s_stat_last_report_ms = now;
        if (now - s_stat_last_report_ms >= 1000u) {
            uint32_t cpu_pct   = 0;
            lv_mem_monitor_t mem_mon;
            lv_mem_monitor(&mem_mon);
            uint32_t mem_pct = 0;
            if (mem_mon.total_size > 0) {
                uint32_t used = (uint32_t)(mem_mon.total_size - mem_mon.free_size);
                mem_pct = (used * 100u) / (uint32_t)mem_mon.total_size;
            }

            UI_LOGW("STAT", "fps=%lu flush=%lu cpu=%lu%% mem=%lu%%", (unsigned long)s_stat_frames, (unsigned long)s_stat_flushes, (unsigned long)cpu_pct, (unsigned long)mem_pct);
#if UI_STAT_OVERLAY
            if (!s_stat_label) {
                s_stat_label = lv_label_create(lv_layer_top());
                lv_obj_set_style_text_color(s_stat_label, lv_color_white(), 0);
                lv_obj_set_style_bg_opa(s_stat_label, LV_OPA_10, 0);
                lv_obj_set_style_bg_color(s_stat_label, lv_color_black(), 0);
                lv_obj_align(s_stat_label, LV_ALIGN_TOP_LEFT, 225, 5);
#if UI_STAT_VERTICAL
                #if defined(LV_USE_TRANSFORM) && LV_USE_TRANSFORM
                lv_obj_set_style_transform_pivot_x(s_stat_label, 0, 0);
                lv_obj_set_style_transform_pivot_y(s_stat_label, 0, 0);
                lv_obj_set_style_transform_angle(s_stat_label, 900, 0);
                #endif
#endif
            }
            lv_snprintf(s_stat_text, sizeof(s_stat_text), "fps=%3lu   flush=%3lu\ncpu=%3lu%%  mem=%3lu%%", (unsigned long)s_stat_frames, (unsigned long)s_stat_flushes, (unsigned long)cpu_pct, (unsigned long)mem_pct);
            lv_label_set_text(s_stat_label, s_stat_text);
#endif
            s_stat_frames = 0;
            s_stat_flushes = 0;
            s_stat_cpu_fallbacks = 0; 
            s_stat_last_report_ms = now;
        }
    }
}

static void fill_buffer(volatile uint16_t *frame, volatile uint16_t *alpha, size_t pixel_count, uint16_t color, uint16_t alpha_value)
{
    for (size_t i = 0; i < pixel_count; ++i) { frame[i] = color; }
    for (size_t i = 0; i < pixel_count / 2; ++i) { alpha[i] = alpha_value; }
}

static void lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    UI_LOGD("CALL", "flush_cb px=%p", px_map);
    start_dma_rect_copy(disp, area, (const uint16_t *)px_map, false);
    ++s_stat_flushes;
}

lv_display_t * ui_display_init(void)
{
    volatile uint16_t* f0 = (volatile uint16_t*)DISP_RFRAME0_ADDR;
    volatile uint16_t* f1 = (volatile uint16_t*)DISP_RFRAME1_ADDR;
    volatile uint8_t*  a0 = (volatile uint8_t*)DISP_RALPHA0_ADDR;
    volatile uint8_t*  a1 = (volatile uint8_t*)DISP_RALPHA1_ADDR;

    s_f0 = f0; s_f1 = f1; s_a0 = a0; s_a1 = a1;

    const size_t pixels = (size_t)DISP_IMAGE_WIDTH * (size_t)DISP_IMAGE_HEIGHT;

    REG32(REG_F0) = 0u; REG32(REG_F1) = 0u;

    lv_init();

    rcc_set_cortex_m4_sys_clock(0, 0, 1, true); 
    dma_init(UI_DMA_IDX);
    NVIC_ClearPendingIRQ(DMA0_IRQn);
    NVIC_SetPriority(DMA0_IRQn, 5);
    NVIC_EnableIRQ(DMA0_IRQn);

    lv_display_t * disp = lv_display_create(DISP_IMAGE_WIDTH, DISP_IMAGE_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, (void*)s_drawbuf1, (void*)s_drawbuf2, (uint32_t)(sizeof(s_drawbuf1)), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_add_event_cb(disp, lvgl_display_event_cb, LV_EVENT_ALL, NULL);

#if UI_STAT_OVERLAY
    if (!s_stat_label) {
        s_stat_label = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_color(s_stat_label, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(s_stat_label, LV_OPA_10, 0);
        lv_obj_set_style_bg_color(s_stat_label, lv_color_black(), 0);
        lv_obj_align(s_stat_label, LV_ALIGN_TOP_LEFT, 125, 3);
#if UI_STAT_VERTICAL
        #if defined(LV_USE_TRANSFORM) && LV_USE_TRANSFORM
        lv_obj_set_style_transform_pivot_x(s_stat_label, 0, 0);
        lv_obj_set_style_transform_pivot_y(s_stat_label, 0, 0);
        lv_obj_set_style_transform_angle(s_stat_label, 900, 0);
        #endif
#endif
        lv_label_set_text(s_stat_label, "fps=  0   flush=  0\ncpu=  0%  mem=  0%");
    }
#endif

#if UI_KEEPALIVE_ENABLE
    if (!s_keepalive_timer) {
        s_keepalive_timer = lv_timer_create(ui_keepalive_timer_cb, UI_KEEPALIVE_MS, NULL);
    }
#endif

    UI_LOGI("INIT", "drawbuf1=%p drawbuf2=%p align=%u bytes=%lu", s_drawbuf1, s_drawbuf2, (unsigned)8, (unsigned long)sizeof(s_drawbuf1));
    UI_LOGI("INIT", "fb0=%p fb1=%p alpha0=%p alpha1=%p", s_f0, s_f1, s_a0, s_a1);

    fill_buffer(f0, (uint16_t *)a0, pixels, UI_CHROMA_KEY_COLOR, 0x0000u);
    fill_buffer(f1, (uint16_t *)a1, pixels, UI_CHROMA_KEY_COLOR, 0x0000u);

    REG32(REG_F0) = 1u;
    s_front_idx = 0u;
    s_dma_busy = 0u;
    s_dma_last = 0u;

    UI_LOGI("INIT", "DMA clock+NVIC ready, front=%u", (unsigned)s_front_idx);

    return disp;
}

void ui_display_set_bg_color(uint32_t rgb24)
{
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(rgb24), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

void ui_request_refresh(void)
{
#if UI_STAT_OVERLAY
    if (s_stat_label) {
        lv_obj_invalidate(s_stat_label);
        return;
    }
#endif
    lv_obj_invalidate(lv_screen_active());
}

void DMA0_IRQHandler(void)
{
    S300_DMA_TypeDef *D = DMAC0;
    uint32_t st = D->StatusTfr;
    if (st & (1u << UI_DMA_CH_SCATTER)) {
        #if UI_DEBUG
        if (UI_LOG_LEVEL >= UI_LOG_LEVEL_DEBUG) {
            static uint32_t s_irq_log_cnt = 0;
            if ((s_irq_log_cnt++ & 0xFFu) == 0u) {
                UI_LOGD("IRQ", "StatusTfr=0x%08lX ch%u", (unsigned long)st, (unsigned)UI_DMA_CH_SCATTER);
            }
        }
        #endif
        D->ClearTfr = (1u << UI_DMA_CH_SCATTER);
        set_dma_dst_scatter(EM_DMA0, UI_DMA_CH_SCATTER, 0, 0);
        s_dma_busy = 0u;
        if (s_dma_disp) {
            lv_display_t *disp = s_dma_disp;
            s_dma_disp = NULL;
            UI_LOGD("IRQ", "flush_ready()");
            lv_display_flush_ready(disp);
        }
    } else {
        if (st) UI_LOGD("IRQ", "StatusTfr=0x%08lX (not our ch%u)", (unsigned long)st, (unsigned)UI_DMA_CH_SCATTER);
    }
}
