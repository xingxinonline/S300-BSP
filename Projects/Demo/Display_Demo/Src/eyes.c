#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "eyes.h"

/* ---------------- Internal animation helpers ---------------- */
static void anim_set_y(void * obj, int32_t v) { lv_obj_set_y((lv_obj_t*)obj, v); }
static void anim_set_x(void * obj, int32_t v) { lv_obj_set_x((lv_obj_t*)obj, v); }

static void start_ud_anim_one_shot(lv_obj_t * obj, int32_t from_y, int32_t to_y, uint32_t t_ms, uint32_t delay_ms)
{
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from_y, to_y);
    lv_anim_set_time(&a, t_ms);
    lv_anim_set_playback_time(&a, 0);
    lv_anim_set_repeat_count(&a, 0);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_exec_cb(&a, anim_set_y);
#if EYE_ANIM_LINEAR
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
#else
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
#endif
    lv_anim_start(&a);
}

static void retarget_ud_anim_one_shot(lv_obj_t * obj, int32_t to_y, uint32_t t_ms, uint32_t delay_ms)
{
    lv_anim_delete(obj, anim_set_y);
    int32_t from_y = lv_obj_get_y(obj);
    start_ud_anim_one_shot(obj, from_y, to_y, t_ms, delay_ms);
}

static void retarget_lr_anim_one_shot(lv_obj_t * obj, int32_t to_x, uint32_t t_ms, uint32_t delay_ms)
{
    lv_anim_delete(obj, anim_set_x);
    int32_t from_x = lv_obj_get_x(obj);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from_x, to_x);
    lv_anim_set_time(&a, t_ms);
    lv_anim_set_playback_time(&a, 0);
    lv_anim_set_repeat_count(&a, 0);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_exec_cb(&a, anim_set_x);
#if EYE_ANIM_LINEAR
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
#else
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
#endif
    lv_anim_start(&a);
}

/* ---------------- Eyes globals and helpers ---------------- */
static lv_obj_t * g_eye_top = NULL;
static lv_obj_t * g_eye_bot = NULL;
static lv_obj_t * g_bg_top  = NULL;
static lv_obj_t * g_bg_bot  = NULL;
static lv_obj_t * g_blink_top = NULL;
static lv_obj_t * g_blink_bot = NULL;
static int        g_blink_active = 0;
static int        g_spawn_hidden_once = 0;
static int32_t    g_eye_h = 16;
static int32_t    g_eye_spacing = 0;  /* 初始化时根据屏幕尺寸计算 */
static int32_t    g_eye_vis_h = 16;
static int32_t    g_socket_vis_h = 16;
static int32_t    g_eye_vis_w = 16;
static int32_t    g_socket_vis_w = 16;
static int32_t    g_mid_x_last = 0;
static int32_t    g_mid_y_last = 0;
static int        g_mid_last_inited = 0;

/* Geometry constants (no runtime rotation) */
static const int32_t C_EYE_W = 38;
static const int32_t C_EYE_H = 30;
static const int32_t C_BG_W  = 95;
static const int32_t C_BG_H  = 74;
static const int32_t C_SOCKET_W_EFF = 83;
static const int32_t C_SOCKET_CENTER_IN_IMG = 52;

static void eyes_debug_dump(const char *tag)
{
#if EYES_DEBUG
    printf("[S300][EYES][%s] blink_active=%d top=%p bot=%p bg_top=%p bg_bot=%p\r\n",
           (tag?tag:"?"), g_blink_active, (void*)g_eye_top, (void*)g_eye_bot, (void*)g_bg_top, (void*)g_bg_bot);
    if (g_eye_top) {
        printf("  top: pos=(%ld,%ld) hidden=%d\r\n",
               (long)lv_obj_get_x(g_eye_top), (long)lv_obj_get_y(g_eye_top),
               lv_obj_has_flag(g_eye_top, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    }
    if (g_eye_bot) {
        printf("  bot: pos=(%ld,%ld) hidden=%d\r\n",
               (long)lv_obj_get_x(g_eye_bot), (long)lv_obj_get_y(g_eye_bot),
               lv_obj_has_flag(g_eye_bot, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    }
    if (g_blink_top) {
        printf("  gif_top: pos=(%ld,%ld)\r\n",
               (long)lv_obj_get_x(g_blink_top), (long)lv_obj_get_y(g_blink_top));
    }
    if (g_blink_bot) {
        printf("  gif_bot: pos=(%ld,%ld)\r\n",
               (long)lv_obj_get_x(g_blink_bot), (long)lv_obj_get_y(g_blink_bot));
    }
    printf("  last_mid=(%ld,%ld) inited=%d spacing=%ld vis(w,h)=(%ld,%ld)\r\n",
           (long)g_mid_x_last, (long)g_mid_y_last, g_mid_last_inited,
           (long)g_eye_spacing, (long)g_eye_vis_w, (long)g_eye_vis_h);
#else
    (void)tag;
#endif
}

/* Forward declare limits */
static inline void eyes_mid_limits_y_in(int32_t *min_out, int32_t *max_out);
static inline void eyes_mid_limits_x_in(int32_t *min_out, int32_t *max_out);

/* One-shot timer: reveal eyes after positions are applied */
static void eyes_restore_apply_cb(lv_timer_t *tm)
{
    (void)tm;
    if (!(g_eye_top && g_eye_bot)) { lv_timer_del(tm); return; }
    int32_t min_x, max_x; eyes_mid_limits_x_in(&min_x, &max_x);
    int32_t min_y, max_y; eyes_mid_limits_y_in(&min_y, &max_y);
    int32_t mid_x = g_mid_last_inited ? g_mid_x_last : (min_x + max_x) / 2;
    int32_t mid_y = g_mid_last_inited ? g_mid_y_last : (min_y + max_y) / 2;
    if (mid_x < min_x) mid_x = min_x;
    if (mid_x > max_x) mid_x = max_x;
    if (mid_y < min_y) mid_y = min_y;
    if (mid_y > max_y) mid_y = max_y;

    const int32_t half_h = g_eye_h / 2;
    const int32_t half_w = g_eye_vis_w / 2;
    int32_t to_y_top = (mid_y - g_eye_spacing) - half_h;
    int32_t to_y_bot = (mid_y + g_eye_spacing) - half_h;
    int32_t to_x = mid_x - half_w;

    lv_anim_delete(g_eye_top, anim_set_y);
    lv_anim_delete(g_eye_top, anim_set_x);
    lv_anim_delete(g_eye_bot, anim_set_y);
    lv_anim_delete(g_eye_bot, anim_set_x);
    lv_obj_add_flag(g_eye_top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_eye_bot, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_x(g_eye_top, to_x); lv_obj_set_y(g_eye_top, to_y_top);
    lv_obj_set_x(g_eye_bot, to_x); lv_obj_set_y(g_eye_bot, to_y_bot);

    eyes_debug_dump("restore.apply.hidden");

    if (g_eye_top) lv_obj_clear_flag(g_eye_top, LV_OBJ_FLAG_HIDDEN);
    if (g_eye_bot) lv_obj_clear_flag(g_eye_bot, LV_OBJ_FLAG_HIDDEN);
    eyes_debug_dump("restore.reveal");

    lv_timer_del(tm);
}

/* Create backgrounds and eyeballs; installs globals and visible geometry */
static void eyes_ui_create(void)
{
    if (g_eye_top || g_eye_bot || g_bg_top || g_bg_bot) return;
    eyes_debug_dump("eyes_ui_create.begin");

    const int32_t screen_w = DISP_IMAGE_WIDTH;
    const int32_t screen_h = DISP_IMAGE_HEIGHT;
    const int32_t center_x = screen_w / 2;
    const int32_t center_y = screen_h / 2;

    /* 自适应计算眼睛间距：优先使用固定值，否则按屏幕高度比例计算 */
    if (g_eye_spacing == 0) {
#if EYE_SPACING_FIXED > 0
        g_eye_spacing = EYE_SPACING_FIXED;
#else
        g_eye_spacing = (int32_t)(screen_h * EYE_SPACING_RATIO);
#endif
    }

    const int32_t base_x    = center_x - C_EYE_W / 2;
    const int32_t base_x_bg = center_x - C_SOCKET_CENTER_IN_IMG;
    int32_t base_y_bg_top = (center_y - g_eye_spacing) - C_BG_H / 2;
    int32_t base_y_bg_bot = (center_y + g_eye_spacing) - C_BG_H / 2;
    int32_t base_y_top    = (center_y - g_eye_spacing) - C_EYE_H / 2;
    int32_t base_y_bot    = (center_y + g_eye_spacing) - C_EYE_H / 2;

    g_bg_top = lv_image_create(lv_screen_active());
    lv_image_set_src(g_bg_top, &img_yanbai_rotated_cw);
    lv_image_set_pivot(g_bg_top, C_BG_W / 2, C_BG_H / 2);
    lv_obj_set_pos(g_bg_top, base_x_bg, base_y_bg_top);

    g_bg_bot = lv_image_create(lv_screen_active());
    lv_image_set_src(g_bg_bot, &img_yanbai1_rotated_cw);
    lv_image_set_pivot(g_bg_bot, C_BG_W / 2, C_BG_H / 2);
    lv_obj_set_pos(g_bg_bot, base_x_bg, base_y_bg_bot);

    g_eye_top = lv_image_create(lv_screen_active());
    lv_image_set_src(g_eye_top, &img_yanzhu_small_rotated_cw);
    lv_image_set_pivot(g_eye_top, C_EYE_W / 2, C_EYE_H / 2);
    if (g_spawn_hidden_once) lv_obj_add_flag(g_eye_top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(g_eye_top, base_x, base_y_top);

    g_eye_bot = lv_image_create(lv_screen_active());
    lv_image_set_src(g_eye_bot, &img_yanzhu_small_rotated_cw);
    lv_image_set_pivot(g_eye_bot, C_EYE_W / 2, C_EYE_H / 2);
    if (g_spawn_hidden_once) lv_obj_add_flag(g_eye_bot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(g_eye_bot, base_x, base_y_bot);

    g_eye_h = C_EYE_H;
    g_eye_vis_h = C_EYE_H;
    g_socket_vis_h = C_BG_H;
    g_eye_vis_w = C_EYE_W;
    g_socket_vis_w = C_SOCKET_W_EFF;

    if (g_spawn_hidden_once) g_spawn_hidden_once = 0;

    if (g_eye_bot) {
        int32_t bx = lv_obj_get_x(g_eye_bot);
        int32_t by = lv_obj_get_y(g_eye_bot);
        if (bx == 0 && by == 0) {
            lv_obj_set_x(g_eye_bot, base_x);
            lv_obj_set_y(g_eye_bot, base_y_bot);
        }
    }

    eyes_debug_dump("eyes_ui_create.after");
}

static void eyes_ui_destroy(void)
{
    eyes_debug_dump("eyes_ui_destroy.begin");
    if (g_eye_top) {
        lv_anim_delete(g_eye_top, anim_set_y);
        lv_anim_delete(g_eye_top, anim_set_x);
        lv_obj_del(g_eye_top); g_eye_top = NULL;
    }
    if (g_eye_bot) {
        lv_anim_delete(g_eye_bot, anim_set_y);
        lv_anim_delete(g_eye_bot, anim_set_x);
        lv_obj_del(g_eye_bot); g_eye_bot = NULL;
    }
    if (g_bg_top)  { lv_obj_del(g_bg_top);  g_bg_top  = NULL; }
    if (g_bg_bot)  { lv_obj_del(g_bg_bot);  g_bg_bot  = NULL; }
    eyes_debug_dump("eyes_ui_destroy.after");
}

/* Public wrappers */
void eyes_create(void) { eyes_ui_create(); }
void eyes_destroy(void) { eyes_ui_destroy(); }

void eyes_set_spacing(int32_t spacing)
{
    if (spacing < 0) spacing = 0;
    g_eye_spacing = spacing;

    const int32_t screen_w = DISP_IMAGE_WIDTH;
    const int32_t screen_h = DISP_IMAGE_HEIGHT;
    const int32_t center_x = screen_w / 2;
    const int32_t center_y = screen_h / 2;
    const int32_t base_x_bg = center_x - C_SOCKET_CENTER_IN_IMG;

    if (g_bg_top && g_bg_bot) {
        int32_t base_y_bg_top = (center_y - g_eye_spacing) - C_BG_H / 2;
        int32_t base_y_bg_bot = (center_y + g_eye_spacing) - C_BG_H / 2;
        lv_obj_set_pos(g_bg_top, base_x_bg, base_y_bg_top);
        lv_obj_set_pos(g_bg_bot, base_x_bg, base_y_bg_bot);
    }

    if (g_eye_top && g_eye_bot) {
        int32_t min_x, max_x; eyes_mid_limits_x_in(&min_x, &max_x);
        int32_t min_y, max_y; eyes_mid_limits_y_in(&min_y, &max_y);
        int32_t mid_x = g_mid_last_inited ? g_mid_x_last : (min_x + max_x) / 2;
        int32_t mid_y = g_mid_last_inited ? g_mid_y_last : (min_y + max_y) / 2;
        if (mid_x < min_x) mid_x = min_x;
        if (mid_x > max_x) mid_x = max_x;
        if (mid_y < min_y) mid_y = min_y;
        if (mid_y > max_y) mid_y = max_y;
        const int32_t half_h = g_eye_h / 2;
        const int32_t half_w = g_eye_vis_w / 2;
        int32_t to_y_top = (mid_y - g_eye_spacing) - half_h;
        int32_t to_y_bot = (mid_y + g_eye_spacing) - half_h;
        int32_t to_x = mid_x - half_w;
    lv_anim_delete(g_eye_top, anim_set_y);
    lv_anim_delete(g_eye_top, anim_set_x);
    lv_anim_delete(g_eye_bot, anim_set_y);
    lv_anim_delete(g_eye_bot, anim_set_x);
        lv_obj_set_pos(g_eye_top, to_x, to_y_top);
        lv_obj_set_pos(g_eye_bot, to_x, to_y_bot);
        g_mid_x_last = mid_x; g_mid_y_last = mid_y; g_mid_last_inited = 1;
    }

    eyes_debug_dump("spacing.update");
}

void eyes_blink_show(void)
{
    if (g_blink_active) return;
    eyes_debug_dump("blink_show.begin");
    eyes_ui_destroy();

    const int32_t screen_w = DISP_IMAGE_WIDTH;
    const int32_t screen_h = DISP_IMAGE_HEIGHT;
    const int32_t center_x = screen_w / 2;
    const int32_t center_y = screen_h / 2;
    const int32_t base_x_bg = center_x - C_SOCKET_CENTER_IN_IMG;
    int32_t base_y_bg_top = (center_y - g_eye_spacing) - C_BG_H / 2;
    int32_t base_y_bg_bot = (center_y + g_eye_spacing) - C_BG_H / 2;

    g_blink_top = lv_gif_create(lv_screen_active());
    lv_gif_set_color_format(g_blink_top, LV_COLOR_FORMAT_RGB565);
    lv_gif_set_src(g_blink_top, &biyan_final_rotated_cw_first9);
    lv_gif_set_loop_count(g_blink_top, 1);
    lv_obj_set_pos(g_blink_top, base_x_bg, base_y_bg_top);

    g_blink_bot = lv_gif_create(lv_screen_active());
    lv_gif_set_color_format(g_blink_bot, LV_COLOR_FORMAT_RGB565);
    lv_gif_set_src(g_blink_bot, &biyan1_final_rotated_cw_first9);
    lv_gif_set_loop_count(g_blink_bot, 1);
    lv_obj_set_pos(g_blink_bot, base_x_bg, base_y_bg_bot);

    g_blink_active = 1;
    eyes_debug_dump("blink_show.after");
}

void eyes_blink_hide_and_restore(void)
{
    if (!g_blink_active) return;
    eyes_debug_dump("restore.begin");
    if (g_blink_top) { lv_obj_del(g_blink_top); g_blink_top = NULL; }
    if (g_blink_bot) { lv_obj_del(g_blink_bot); g_blink_bot = NULL; }
    g_blink_active = 0;
    g_spawn_hidden_once = 1;
    eyes_ui_create();
    eyes_debug_dump("restore.afterCreate");
    (void)lv_timer_create(eyes_restore_apply_cb, 1, NULL);
}

static int32_t eyes_goto_mid_y(int32_t mid_y)
{
    if (!g_eye_top || !g_eye_bot) return mid_y;
    const int32_t half = g_eye_h / 2;
    int32_t mid_min, mid_max;
    eyes_mid_limits_y_in(&mid_min, &mid_max);
    if (mid_y < mid_min) mid_y = mid_min;
    if (mid_y > mid_max) mid_y = mid_max;

    int32_t top_center_y = mid_y - g_eye_spacing;
    int32_t bot_center_y = mid_y + g_eye_spacing;
    int32_t to_y_top = top_center_y - half;
    int32_t to_y_bot = bot_center_y - half;

    const uint32_t per_px_ms = EYE_PER_PX_MS;
    int32_t cur_top_y = lv_obj_get_y(g_eye_top);
    int32_t cur_bot_y = lv_obj_get_y(g_eye_bot);
    uint32_t dy_top = (cur_top_y > to_y_top) ? (uint32_t)(cur_top_y - to_y_top) : (uint32_t)(to_y_top - cur_top_y);
    uint32_t dy_bot = (cur_bot_y > to_y_bot) ? (uint32_t)(cur_bot_y - to_y_bot) : (uint32_t)(to_y_bot - cur_bot_y);
    uint32_t dy_max = (dy_top > dy_bot) ? dy_top : dy_bot;
    uint32_t t_ms = dy_max * per_px_ms;

    if (t_ms == 0u) { lv_obj_set_y(g_eye_top, to_y_top); } else { retarget_ud_anim_one_shot(g_eye_top, to_y_top, t_ms, 0); }
    if (t_ms == 0u) { lv_obj_set_y(g_eye_bot, to_y_bot); } else { retarget_ud_anim_one_shot(g_eye_bot, to_y_bot, t_ms, 0); }

    if (!g_mid_last_inited)
    {
        int32_t min_x, max_x; eyes_mid_limits_x_in(&min_x, &max_x);
        g_mid_x_last = (min_x + max_x) / 2;
        g_mid_last_inited = 1;
    }
    g_mid_y_last = mid_y;

    return mid_y;
}

static void eyes_goto_mid_xy(int32_t mid_x, int32_t mid_y)
{
    if (!g_eye_top || !g_eye_bot) return;

    int32_t mid_min_y, mid_max_y; eyes_mid_limits_y_in(&mid_min_y, &mid_max_y);
    if (mid_y < mid_min_y) { mid_y = mid_min_y; }
    if (mid_y > mid_max_y) { mid_y = mid_max_y; }
    int32_t mid_min_x, mid_max_x; eyes_mid_limits_x_in(&mid_min_x, &mid_max_x);
    if (mid_x < mid_min_x) { mid_x = mid_min_x; }
    if (mid_x > mid_max_x) { mid_x = mid_max_x; }

    const int32_t half_h = g_eye_h / 2;
    const int32_t half_w = g_eye_vis_w / 2;

    int32_t top_center_y = mid_y - g_eye_spacing;
    int32_t bot_center_y = mid_y + g_eye_spacing;
    int32_t to_y_top = top_center_y - half_h;
    int32_t to_y_bot = bot_center_y - half_h;
    int32_t to_x = mid_x - half_w;

    const uint32_t per_px_ms = EYE_PER_PX_MS;
    int32_t cur_top_y = lv_obj_get_y(g_eye_top);
    int32_t cur_bot_y = lv_obj_get_y(g_eye_bot);
    int32_t cur_top_x = lv_obj_get_x(g_eye_top);
    int32_t cur_bot_x = lv_obj_get_x(g_eye_bot);

    uint32_t dy_top = (cur_top_y > to_y_top) ? (uint32_t)(cur_top_y - to_y_top) : (uint32_t)(to_y_top - cur_top_y);
    uint32_t dy_bot = (cur_bot_y > to_y_bot) ? (uint32_t)(cur_bot_y - to_y_bot) : (uint32_t)(to_y_bot - cur_bot_y);
    uint32_t dx_top = (cur_top_x > to_x) ? (uint32_t)(cur_top_x - to_x) : (uint32_t)(to_x - cur_top_x);
    uint32_t dx_bot = (cur_bot_x > to_x) ? (uint32_t)(cur_bot_x - to_x) : (uint32_t)(to_x - cur_bot_x);

    uint32_t dy_max = (dy_top > dy_bot) ? dy_top : dy_bot;
    uint32_t dx_max = (dx_top > dx_bot) ? dx_top : dx_bot;
    uint32_t t_y = dy_max * per_px_ms;
    uint32_t t_x = dx_max * per_px_ms;

    if (t_y == 0u) lv_obj_set_y(g_eye_top, to_y_top); else retarget_ud_anim_one_shot(g_eye_top, to_y_top, t_y, 0);
    if (t_y == 0u) lv_obj_set_y(g_eye_bot, to_y_bot); else retarget_ud_anim_one_shot(g_eye_bot, to_y_bot, t_y, 0);
    if (t_x == 0u) lv_obj_set_x(g_eye_top, to_x); else retarget_lr_anim_one_shot(g_eye_top, to_x, t_x, 0);
    if (t_x == 0u) lv_obj_set_x(g_eye_bot, to_x); else retarget_lr_anim_one_shot(g_eye_bot, to_x, t_x, 0);

    g_mid_x_last = mid_x; g_mid_y_last = mid_y; g_mid_last_inited = 1;
}

/* Exports */
void eyes_move_to_mid_y(int32_t mid_y) { (void)eyes_goto_mid_y(mid_y); }
void eyes_move_to_xy(int32_t mid_x, int32_t mid_y) { eyes_goto_mid_xy(mid_x, mid_y); }

static inline void eyes_mid_limits_y_in(int32_t *min_out, int32_t *max_out)
{
    const int32_t screen_h = DISP_IMAGE_HEIGHT;
    const int32_t center_y = screen_h / 2;
    int32_t S = g_socket_vis_h; int32_t E = g_eye_vis_h; if (S < E) S = E;
    int32_t margin = (S - E) / 2;
    int32_t mid_min = center_y - margin;
    int32_t mid_max = center_y + margin;
    int32_t half_eye = g_eye_h / 2;
    int32_t scr_min = g_eye_spacing + half_eye;
    int32_t scr_max = screen_h - (g_eye_spacing + half_eye);
    if (mid_min < scr_min) mid_min = scr_min;
    if (mid_max > scr_max) mid_max = scr_max;
    if (mid_min > mid_max) { mid_min = mid_max = center_y; }
    if (min_out) *min_out = mid_min;
    if (max_out) *max_out = mid_max;
}

static inline void eyes_mid_limits_x_in(int32_t *min_out, int32_t *max_out)
{
    const int32_t screen_w = DISP_IMAGE_WIDTH;
    const int32_t center_x = screen_w / 2;
    int32_t S = g_socket_vis_w; int32_t E = g_eye_vis_w; if (S < E) S = E;
    int32_t margin = (S - E) / 2;
    int32_t mid_min = center_x - margin;
    int32_t mid_max = center_x + margin;
    int32_t half_eye_w = g_eye_vis_w / 2;
    int32_t scr_min = half_eye_w;
    int32_t scr_max = screen_w - half_eye_w;
    if (mid_min < scr_min) mid_min = scr_min;
    if (mid_max > scr_max) mid_max = scr_max;
    if (mid_min > mid_max) { mid_min = mid_max = center_x; }
    if (min_out) *min_out = mid_min;
    if (max_out) *max_out = mid_max;
}

void eyes_mid_limits_y(int32_t *min_out, int32_t *max_out) { eyes_mid_limits_y_in(min_out, max_out); }
void eyes_mid_limits_x(int32_t *min_out, int32_t *max_out) { eyes_mid_limits_x_in(min_out, max_out); }

int eyes_get_last_mid(int32_t *mid_x, int32_t *mid_y)
{
    if (!g_mid_last_inited) return 0;
    if (mid_x) *mid_x = g_mid_x_last;
    if (mid_y) *mid_y = g_mid_y_last;
    return 1;
}
