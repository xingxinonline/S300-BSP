/**
 * @file main.c
 * @brief LVGL Progress Bar Demo
 * 
 * Functional Requirements:
 * 1. Progress Bar duration 3 seconds.
 * 2. Detection Logic:
 *    - Continuous detection for 3s -> Toggle State (Closed <-> Open).
 *    - Reset timer if detection lost for > 200ms.
 *    - Show "Open" / "Closed" text.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* Platform */
#include "s300.h"

/* Board and subsystems */
#include "rcc.h"
#include "board.h"
#include "video.h"
#include "psram.h"
#include "mailbox.h"
#include "gpio.h"
#include "camera_ov5640.h"

/* App modules */
#include "app_mailbox.h"
#include "ui_display.h"
#include "lvgl.h"

/* 1ms tick */
static volatile uint32_t g_tick_ms = 0;

void SysTick_Handler(void)
{
    g_tick_ms++;
    lv_tick_inc(1);
}

static uint32_t millis(void)
{
    return g_tick_ms;
}

/**
 * @brief Backlight Init (Unused in this demo)
 */
// static void background_light_init(void)
// {
//     set_gpio_function(GPIOA, 24, FUNCTION_2);
//     set_gpio_mode(GPIOA, 24, GPIO_UP);
//     set_gpio_direction(GPIOA, 24, 1);
//     set_gpio_data(GPIOA, 24, 0);
// }

/* UI Objects */
static lv_obj_t *ui_bar = NULL;
static lv_obj_t *ui_label = NULL;

/* Logic State */
typedef enum {
    STATE_CLOSED,
    STATE_OPEN
} app_state_t;

static app_state_t g_state = STATE_CLOSED;
static uint32_t g_timer_ms = 0;
#define TARGET_TIME_MS 3000

/* Create UI components */
static void create_ui(void)
{
    /* Set global screen bg to PURE GREEN (0x07E0) for chroma keying */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(0, 255, 0), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    int32_t scr_w = lv_obj_get_width(lv_scr_act());
    int32_t scr_h = lv_obj_get_height(lv_scr_act());
    
    /* Determine font size based on screen size (small vs large screen) */
    const lv_font_t *font;
    if (scr_w <= 128) {
        /* Small screen (128x160): use smaller built-in font */
        font = &lv_font_unscii_8;
    } else {
        /* Larger screen (240x320+): use Montserrat 28 */
        font = &lv_font_montserrat_28;
    }

    /* 
     * Layout Strategy:
     * Screen is physically Portrait (e.g., 128w x 160h), viewed rotated 90° CW.
     * In Landscape view: 160w x 128h.
     * 
     * For rotated objects, LVGL still positions them in Portrait coordinates.
     * Rotation happens around the object's center AFTER positioning.
     * 
     * To avoid clipping issues with rotation, we'll use a SIMPLE approach:
     * - Place both Label and Bar at screen CENTER (no offset)
     * - Offset them along Portrait Y-axis to separate in Landscape X direction
     *   (Portrait Y becomes Landscape X when rotated)
     */

    /* Label */
    ui_label = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label, "CLOSED");
    lv_obj_set_style_text_font(ui_label, font, 0);
    lv_obj_set_style_text_color(ui_label, lv_color_make(255, 0, 0), 0);  /* Red for CLOSED */
    /* Rotate 90° CW for landscape viewing */
    lv_obj_set_style_transform_rotation(ui_label, 900, 0);
    /* Set pivot to center (50%) for center-based alignment after rotation */
    lv_obj_set_style_transform_pivot_x(ui_label, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(ui_label, LV_PCT(50), 0);
    
    /* Progress Bar - create as HORIZONTAL bar, then rotate -90° (CCW)
     * Horizontal bar fills left-to-right by default.
     * After -90° CCW rotation: left becomes top, right becomes bottom in Portrait.
     * When device rotates 90° CW: top becomes right, bottom becomes left.
     * So visually it fills RIGHT-to-LEFT... still wrong.
     * 
     * Let's try: Horizontal bar + rotate +90° CW (same as label).
     * left becomes bottom, right becomes top in Portrait.
     * When device rotates 90° CW: bottom becomes left, top becomes right.
     * So visually it fills LEFT-to-RIGHT. Correct!
     */
    ui_bar = lv_bar_create(lv_scr_act());
    
    int32_t bar_len = (scr_h * 55) / 100;  /* 55% of screen height */
    int32_t bar_thick = (scr_w <= 128) ? 10 : 16;
    
    /* Horizontal bar: width=len, height=thick */
    lv_obj_set_size(ui_bar, bar_len, bar_thick);
    /* Rotate 90° CW - same direction as text */
    lv_obj_set_style_transform_rotation(ui_bar, 900, 0);
    /* Set pivot to center (50%) for center-based alignment after rotation */
    lv_obj_set_style_transform_pivot_x(ui_bar, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(ui_bar, LV_PCT(50), 0);
    
    /* 
     * Centered symmetric layout:
     * Both elements centered, separated along Portrait X axis (Landscape vertical).
     * - Label: negative X offset (Landscape top half)
     * - Bar: negative X offset (Landscape bottom half)
     * Use smaller offset to keep elements on screen.
     * Note: Positive X = Landscape top, Negative X = Landscape bottom
     */
    int32_t offset = scr_w / 8;  /* 1/8 of width - smaller to stay on screen */
    
    lv_obj_align(ui_label, LV_ALIGN_CENTER, offset, 0);   /* Label on top (Landscape) */
    lv_obj_align(ui_bar, LV_ALIGN_CENTER, -offset, 0);    /* Bar on bottom (Landscape) */

    lv_bar_set_range(ui_bar, 0, TARGET_TIME_MS);
    lv_bar_set_value(ui_bar, 0, LV_ANIM_OFF);
    
    /* Track (main) color: Gray (so it's visible, not transparent green) */
    lv_obj_set_style_bg_color(ui_bar, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_bar, LV_OPA_COVER, LV_PART_MAIN);
    /* Indicator: Red progress */
    lv_obj_set_style_bg_color(ui_bar, lv_color_make(255, 0, 0), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ui_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    /* Rounded corners - smaller for small bar */
    int32_t radius = (scr_w <= 128) ? 5 : 10;
    lv_obj_set_style_radius(ui_bar, radius, LV_PART_MAIN);
    lv_obj_set_style_radius(ui_bar, radius, LV_PART_INDICATOR);
}

/* Main logic update (called periodically) */
static void update_logic(void)
{
    static uint32_t last_calc_ms = 0;
    uint32_t now = millis();
    uint32_t dt = now - last_calc_ms;
    
    /* Avoid huge jumps or multiple calls in same tick */
    if (dt == 0) return;
    last_calc_ms = now;
    
    bool face_detected = app_mailbox_is_face_present();

    if (face_detected) {
        g_timer_ms += dt;
        if (g_timer_ms >= TARGET_TIME_MS) {
            g_timer_ms = 0;
            /* Toggle state */
            if (g_state == STATE_CLOSED) {
                g_state = STATE_OPEN;
                lv_label_set_text(ui_label, " OPEN "); 
                lv_obj_set_style_text_color(ui_label, lv_color_make(0, 255, 0), 0);  /* Green */
            } else {
                g_state = STATE_CLOSED;
                lv_label_set_text(ui_label, "CLOSED");
                lv_obj_set_style_text_color(ui_label, lv_color_make(255, 0, 0), 0);  /* Red */
            }
            /* Re-align text to ensure center is maintained after length change */
            int32_t scr_w = lv_obj_get_width(lv_scr_act());
            lv_obj_align(ui_label, LV_ALIGN_CENTER, scr_w / 8, 0);
        }
    } else {
        /* Reset timer if face lost */
        g_timer_ms = 0;
    }

    /* Update Bar */
    lv_bar_set_value(ui_bar, g_timer_ms, LV_ANIM_OFF);
}

int main(void)
{
    /* Board Init */
    board_init();
    printf("\r\n========================================\r\n");
    printf("[LVGL_Gesture_Progress_Bar] Starting...\r\n");
    printf("========================================\r\n");

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        printf("[ERR] SysTick_Config failed!\r\n");
    }

    printf("[Init] PSRAM...\r\n");
    init_psram(4, 1);

    printf("[Init] PLL...\r\n");
    rcc_init_mm_pll(8, 400, 0, 3, 2);   /* 100MHz */
    rcc_init_dsp_pll(8, 400, 0, 2, 1);  /* 300MHz (Matched to Display_Demo) */

    // background_light_init(); /* Removed: Display_Demo doesn't use it, and setting to 0 might turn it off */
    
    printf("[Init] Camera...\r\n");
    if (camera_ov5640_preinit() != 0) {
        printf("[WARN] Camera init failed, only display will work.\r\n");
    }

    printf("[Init] Video...\r\n");
    init_video(EM_DVP, CAMREA_YUV422, C1080X720P);

    printf("[Init] LVGL & Display...\r\n");
    // lv_init() is called inside ui_display_init()
    ui_display_init();

    printf("[Init] Mailbox...\r\n");
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    set_dsp_warm_reset(true);
    write_mailbox(MAILBOX_BASE, 0x5A5A5A5A);
    
    app_mailbox_init();
    app_mailbox_set_time_callback(millis);

    /* Create UI */
    create_ui();

    printf("[System] Loop start.\r\n");
    
    /* Main Loop */
    while (1)
    {
        /* 1. Poll Mailbox for detection results */
        app_mailbox_poll();

        /* 2. Update Application Logic */
        update_logic();

        /* 3. LVGL Task Handler */
        lv_timer_handler();

        /* 4. Simple Delay / Yield */
        uint32_t t = millis();
        while((millis() - t) < 5); 
    }
}
