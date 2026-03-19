#include "master_detection_overlay.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "app_gimbal_tracking_input.h"
#include "detection_proto.h"
#include "s300.h"
#include "video.h"

#define MASTER_BOX_TIMEOUT_MS 500u
#define MASTER_BOX_COLOR_IDLE      0x8410u
#define MASTER_BOX_COLOR_TRACKING  0x07E0u
#define MASTER_BOX_COLOR_PREDICTED 0xFFE0u
#define MASTER_BOX_ALPHA      0xFFu
#define MASTER_BOX_DASH_PERIOD 6
#define MASTER_BOX_DASH_FILL   3
#define MASTER_TEXT_COLOR           0x07FFu
#define MASTER_TEXT_SHADOW_COLOR    0x0000u
#define MASTER_TEXT_SHADOW_ALPHA    0x90u
#define MASTER_TEXT_X               2
#define MASTER_TEXT_Y               2
#define MASTER_TEXT_W               42
#define MASTER_TEXT_H               9
#define MASTER_TEXT_MIRROR_COMPENSATE 1
#define MASTER_FONT_CHAR_W          5
#define MASTER_FONT_CHAR_H          7
#define MASTER_FONT_SPACING         1
#define MASTER_ARROW_SCALE_NUM    2
#define MASTER_ARROW_SCALE_DEN    1
#define MASTER_ARROW_MAX_LEN      24
#define MASTER_ARROW_MIN_HEAD_LEN 3
#define MASTER_ARROW_HEAD_HALF_W  2

typedef struct {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
    bool has_arrow;
    int32_t arrow_x0;
    int32_t arrow_y0;
    int32_t arrow_x1;
    int32_t arrow_y1;
    int32_t arrow_head1_x;
    int32_t arrow_head1_y;
    int32_t arrow_head2_x;
    int32_t arrow_head2_y;
} DrawnBox_t;

static uint32_t (*s_get_millis)(void) = 0;
static DrawnBox_t s_drawn_box;
static bool s_box_drawn = false;
static uint32_t s_last_box_ms = 0u;
static uint32_t s_fps_window_start_ms = 0u;
static uint32_t s_fps_frame_counter = 0u;
static uint32_t s_display_fps = 0u;

static const uint8_t s_font5x7_digits[][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E},
};

static const uint8_t s_font5x7_f[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
static const uint8_t s_font5x7_p[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
static const uint8_t s_font5x7_s[5] = {0x26, 0x49, 0x49, 0x49, 0x32};
static const uint8_t s_font5x7_colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
static const uint8_t s_font5x7_space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
}

static int32_t fps_osd_origin_x(void)
{
#if MASTER_TEXT_MIRROR_COMPENSATE
    return (int32_t)DISP_IMAGE_WIDTH - MASTER_TEXT_X - MASTER_TEXT_W;
#else
    return MASTER_TEXT_X;
#endif
}

static void trigger_overlay_refresh(void)
{
    *(volatile uint32_t *)(DSP_VIDEO_SS_BASE + 0x50u) = 1u;
}

static void set_pixel_alpha(uint32_t x, uint32_t y, uint8_t alpha)
{
    volatile uint16_t *alpha_buf = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    uint32_t pixel_idx = y * DISP_IMAGE_WIDTH + x;
    uint32_t word_idx = pixel_idx / 2u;
    uint32_t byte_pos = pixel_idx & 1u;
    uint16_t value = alpha_buf[word_idx];

    if (byte_pos == 0u) {
        value = (uint16_t)((value & 0xFF00u) | alpha);
    } else {
        value = (uint16_t)((value & 0x00FFu) | ((uint16_t)alpha << 8));
    }

    alpha_buf[word_idx] = value;
}

static void set_pixel_color(uint32_t x, uint32_t y, uint16_t color)
{
    volatile uint16_t *framebuffer = (volatile uint16_t *)DISP_RFRAME0_ADDR;
    framebuffer[y * DISP_IMAGE_WIDTH + x] = color;
}

static int32_t abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static void plot_pixel_if_visible(int32_t x, int32_t y, uint16_t color, uint8_t alpha)
{
    if ((x < 0) || (y < 0) || (x >= DISP_IMAGE_WIDTH) || (y >= DISP_IMAGE_HEIGHT)) {
        return;
    }

    set_pixel_color((uint32_t)x, (uint32_t)y, color);
    set_pixel_alpha((uint32_t)x, (uint32_t)y, alpha);
}

static const uint8_t *fps_glyph_for_char(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return s_font5x7_digits[(uint32_t)(c - '0')];
    }

    switch (c) {
    case 'F': return s_font5x7_f;
    case 'P': return s_font5x7_p;
    case 'S': return s_font5x7_s;
    case ':': return s_font5x7_colon;
    case ' ': return s_font5x7_space;
    default: return s_font5x7_space;
    }
}

static void draw_char_5x7(int32_t x, int32_t y, char c, uint16_t color, uint8_t alpha)
{
    const uint8_t *glyph = fps_glyph_for_char(c);

    for (int32_t col = 0; col < MASTER_FONT_CHAR_W; col++) {
        uint8_t line =
#if MASTER_TEXT_MIRROR_COMPENSATE
            glyph[MASTER_FONT_CHAR_W - 1 - col];
#else
            glyph[col];
#endif
        for (int32_t row = 0; row < MASTER_FONT_CHAR_H; row++) {
            if (((line >> row) & 0x01u) != 0u) {
                plot_pixel_if_visible(x + col, y + row, color, alpha);
            }
        }
    }
}

static void draw_string_5x7(int32_t x, int32_t y, const char *text, uint16_t color, uint8_t alpha)
{
    if (text == NULL) {
        return;
    }

#if MASTER_TEXT_MIRROR_COMPENSATE
    {
        const char *cursor = text;
        while (*cursor != '\0') {
            cursor++;
        }

        while (cursor > text) {
            cursor--;
            draw_char_5x7(x, y, *cursor, color, alpha);
            x += MASTER_FONT_CHAR_W + MASTER_FONT_SPACING;
        }
    }
#else
    while (*text != '\0') {
        draw_char_5x7(x, y, *text, color, alpha);
        x += MASTER_FONT_CHAR_W + MASTER_FONT_SPACING;
        text++;
    }
#endif
}

static void clear_rect_fill(int32_t x, int32_t y, int32_t width, int32_t height)
{
    for (int32_t py = y; py < (y + height); py++) {
        for (int32_t px = x; px < (x + width); px++) {
            if ((px < 0) || (py < 0) || (px >= DISP_IMAGE_WIDTH) || (py >= DISP_IMAGE_HEIGHT)) {
                continue;
            }
            set_pixel_alpha((uint32_t)px, (uint32_t)py, 0u);
        }
    }
}

static void draw_fps_osd(void)
{
    char text[12];
    int32_t osd_x = fps_osd_origin_x();

    snprintf(text, sizeof(text), "FPS:%2lu", (unsigned long)s_display_fps);
    clear_rect_fill(osd_x, MASTER_TEXT_Y, MASTER_TEXT_W, MASTER_TEXT_H);
    draw_string_5x7(osd_x + 2, MASTER_TEXT_Y + 2, text, MASTER_TEXT_SHADOW_COLOR, MASTER_TEXT_SHADOW_ALPHA);
    draw_string_5x7(osd_x + 1, MASTER_TEXT_Y + 1, text, MASTER_TEXT_COLOR, MASTER_BOX_ALPHA);
}

static void update_fps_stats(void)
{
    uint32_t now_ms = millis();

    if (s_fps_window_start_ms == 0u) {
        s_fps_window_start_ms = now_ms;
    }

    s_fps_frame_counter++;
    if ((uint32_t)(now_ms - s_fps_window_start_ms) >= 1000u) {
        uint32_t elapsed_ms = (uint32_t)(now_ms - s_fps_window_start_ms);
        if (elapsed_ms == 0u) {
            elapsed_ms = 1u;
        }
        s_display_fps = (s_fps_frame_counter * 1000u) / elapsed_ms;
        s_fps_frame_counter = 0u;
        s_fps_window_start_ms = now_ms;
    }

    draw_fps_osd();
}

static void draw_line_segment(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                              uint16_t color, uint8_t alpha, bool dashed)
{
    int32_t dx = abs_i32(x1 - x0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t dy = -abs_i32(y1 - y0);
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;
    int32_t step = 0;

    while (1) {
        if (!dashed || ((step % MASTER_BOX_DASH_PERIOD) < MASTER_BOX_DASH_FILL)) {
            plot_pixel_if_visible(x0, y0, color, alpha);
        }

        if ((x0 == x1) && (y0 == y1)) {
            break;
        }

        {
            int32_t twice_err = err << 1;
            if (twice_err >= dy) {
                err += dy;
                x0 += sx;
            }
            if (twice_err <= dx) {
                err += dx;
                y0 += sy;
            }
        }
        step++;
    }
}

static void clear_line_segment(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t dx = abs_i32(x1 - x0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t dy = -abs_i32(y1 - y0);
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;

    while (1) {
        if ((x0 >= 0) && (y0 >= 0) && (x0 < DISP_IMAGE_WIDTH) && (y0 < DISP_IMAGE_HEIGHT)) {
            set_pixel_alpha((uint32_t)x0, (uint32_t)y0, 0u);
        }

        if ((x0 == x1) && (y0 == y1)) {
            break;
        }

        {
            int32_t twice_err = err << 1;
            if (twice_err >= dy) {
                err += dy;
                x0 += sx;
            }
            if (twice_err <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

static int32_t sign_i32(int32_t value)
{
    if (value > 0) {
        return 1;
    }
    if (value < 0) {
        return -1;
    }
    return 0;
}

static void draw_rect_border(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                             uint16_t color, uint8_t alpha)
{
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 >= DISP_IMAGE_WIDTH) {
        x2 = DISP_IMAGE_WIDTH - 1;
    }
    if (y2 >= DISP_IMAGE_HEIGHT) {
        y2 = DISP_IMAGE_HEIGHT - 1;
    }
    if ((x1 > x2) || (y1 > y2)) {
        return;
    }

    for (int32_t x = x1; x <= x2; x++) {
        set_pixel_color((uint32_t)x, (uint32_t)y1, color);
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, alpha);
        set_pixel_color((uint32_t)x, (uint32_t)y2, color);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, alpha);
    }

    for (int32_t y = y1 + 1; y < y2; y++) {
        set_pixel_color((uint32_t)x1, (uint32_t)y, color);
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, alpha);
        set_pixel_color((uint32_t)x2, (uint32_t)y, color);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, alpha);
    }
}

static void draw_rect_border_dashed(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                    uint16_t color, uint8_t alpha)
{
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 >= DISP_IMAGE_WIDTH) {
        x2 = DISP_IMAGE_WIDTH - 1;
    }
    if (y2 >= DISP_IMAGE_HEIGHT) {
        y2 = DISP_IMAGE_HEIGHT - 1;
    }
    if ((x1 > x2) || (y1 > y2)) {
        return;
    }

    draw_line_segment(x1, y1, x2, y1, color, alpha, true);
    draw_line_segment(x2, y1, x2, y2, color, alpha, true);
    draw_line_segment(x2, y2, x1, y2, color, alpha, true);
    draw_line_segment(x1, y2, x1, y1, color, alpha, true);
}

static uint16_t box_color_for_tracking(bool *out_dashed)
{
    app_gimbal_tracking_input_output_t output;

    if (out_dashed != NULL) {
        *out_dashed = false;
    }

    app_gimbal_tracking_input_get_output(&output);
    if (output.predicted) {
        if (out_dashed != NULL) {
            *out_dashed = true;
        }
        return MASTER_BOX_COLOR_PREDICTED;
    }

    if (output.tracking_active) {
        return MASTER_BOX_COLOR_TRACKING;
    }

    return MASTER_BOX_COLOR_IDLE;
}

static bool compute_arrow_geometry(const app_gimbal_tracking_input_output_t *output,
                                   int32_t *out_x0,
                                   int32_t *out_y0,
                                   int32_t *out_x1,
                                   int32_t *out_y1,
                                   int32_t *out_head1_x,
                                   int32_t *out_head1_y,
                                   int32_t *out_head2_x,
                                   int32_t *out_head2_y)
{
    int32_t dir_x;
    int32_t dir_y;
    int32_t major;
    int32_t head_len;
    int32_t body_x;
    int32_t body_y;
    int32_t scaled_dx;
    int32_t scaled_dy;
    int32_t shaft_dx;
    int32_t shaft_dy;
    int32_t perp_x;
    int32_t perp_y;

    if ((output == NULL) || !output->valid) {
        return false;
    }

    dir_x = (int32_t)output->vx;
    dir_y = (int32_t)output->vy;
    major = abs_i32(dir_x);
    if (abs_i32(dir_y) > major) {
        major = abs_i32(dir_y);
    }
    if (major < ARROW_SHOW_THRESHOLD) {
        return false;
    }

    *out_x0 = output->target_cx;
    *out_y0 = output->target_cy;

    scaled_dx = dir_x * MASTER_ARROW_SCALE_NUM;
    scaled_dy = dir_y * MASTER_ARROW_SCALE_NUM;
    if (MASTER_ARROW_SCALE_DEN > 1) {
        scaled_dx /= MASTER_ARROW_SCALE_DEN;
        scaled_dy /= MASTER_ARROW_SCALE_DEN;
    }

    major = abs_i32(scaled_dx);
    if (abs_i32(scaled_dy) > major) {
        major = abs_i32(scaled_dy);
    }
    if (major == 0) {
        scaled_dx = sign_i32(dir_x);
        scaled_dy = sign_i32(dir_y);
        major = abs_i32(scaled_dx);
        if (abs_i32(scaled_dy) > major) {
            major = abs_i32(scaled_dy);
        }
    }

    if (major > MASTER_ARROW_MAX_LEN) {
        scaled_dx = (scaled_dx * MASTER_ARROW_MAX_LEN) / major;
        scaled_dy = (scaled_dy * MASTER_ARROW_MAX_LEN) / major;
        major = MASTER_ARROW_MAX_LEN;
    }

    if ((scaled_dx == 0) && (dir_x != 0)) {
        scaled_dx = sign_i32(dir_x);
    }
    if ((scaled_dy == 0) && (dir_y != 0)) {
        scaled_dy = sign_i32(dir_y);
    }

    *out_x1 = *out_x0 + scaled_dx;
    *out_y1 = *out_y0 + scaled_dy;

    head_len = MASTER_ARROW_MIN_HEAD_LEN;
    if (major <= (head_len + 1)) {
        head_len = (major > 1) ? (major - 1) : 1;
    }

    body_x = (scaled_dx * head_len) / major;
    body_y = (scaled_dy * head_len) / major;
    if ((body_x == 0) && (scaled_dx != 0)) {
        body_x = sign_i32(scaled_dx);
    }
    if ((body_y == 0) && (scaled_dy != 0)) {
        body_y = sign_i32(scaled_dy);
    }

    shaft_dx = scaled_dx - body_x;
    shaft_dy = scaled_dy - body_y;
    if ((shaft_dx == 0) && (scaled_dx != 0)) {
        shaft_dx = sign_i32(scaled_dx);
    }
    if ((shaft_dy == 0) && (scaled_dy != 0)) {
        shaft_dy = sign_i32(scaled_dy);
    }

    *out_x1 = *out_x0 + shaft_dx + body_x;
    *out_y1 = *out_y0 + shaft_dy + body_y;

    perp_x = (-body_y * MASTER_ARROW_HEAD_HALF_W) / head_len;
    perp_y = (body_x * MASTER_ARROW_HEAD_HALF_W) / head_len;
    if ((perp_x == 0) && (perp_y == 0)) {
        perp_x = -sign_i32(body_y);
        perp_y = sign_i32(body_x);
    }

    *out_head1_x = *out_x1 - body_x + perp_x;
    *out_head1_y = *out_y1 - body_y + perp_y;
    *out_head2_x = *out_x1 - body_x - perp_x;
    *out_head2_y = *out_y1 - body_y - perp_y;
    return true;
}

static void draw_arrow(const app_gimbal_tracking_input_output_t *output,
                       uint16_t color,
                       uint8_t alpha,
                       DrawnBox_t *drawn)
{
    if ((drawn == NULL) || !compute_arrow_geometry(output,
                                                   &drawn->arrow_x0,
                                                   &drawn->arrow_y0,
                                                   &drawn->arrow_x1,
                                                   &drawn->arrow_y1,
                                                   &drawn->arrow_head1_x,
                                                   &drawn->arrow_head1_y,
                                                   &drawn->arrow_head2_x,
                                                   &drawn->arrow_head2_y)) {
        if (drawn != NULL) {
            drawn->has_arrow = false;
        }
        return;
    }

    draw_line_segment(drawn->arrow_x0, drawn->arrow_y0, drawn->arrow_x1, drawn->arrow_y1, color, alpha, false);
    draw_line_segment(drawn->arrow_x1, drawn->arrow_y1, drawn->arrow_head1_x, drawn->arrow_head1_y, color, alpha, false);
    draw_line_segment(drawn->arrow_x1, drawn->arrow_y1, drawn->arrow_head2_x, drawn->arrow_head2_y, color, alpha, false);
    drawn->has_arrow = true;
}

static void clear_arrow(const DrawnBox_t *drawn)
{
    if ((drawn == NULL) || !drawn->has_arrow) {
        return;
    }

    clear_line_segment(drawn->arrow_x0, drawn->arrow_y0, drawn->arrow_x1, drawn->arrow_y1);
    clear_line_segment(drawn->arrow_x1, drawn->arrow_y1, drawn->arrow_head1_x, drawn->arrow_head1_y);
    clear_line_segment(drawn->arrow_x1, drawn->arrow_y1, drawn->arrow_head2_x, drawn->arrow_head2_y);
}

static void clear_rect_border(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 >= DISP_IMAGE_WIDTH) {
        x2 = DISP_IMAGE_WIDTH - 1;
    }
    if (y2 >= DISP_IMAGE_HEIGHT) {
        y2 = DISP_IMAGE_HEIGHT - 1;
    }
    if ((x1 > x2) || (y1 > y2)) {
        return;
    }

    for (int32_t x = x1; x <= x2; x++) {
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, 0u);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, 0u);
    }

    for (int32_t y = y1 + 1; y < y2; y++) {
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, 0u);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, 0u);
    }
}

void master_detection_overlay_init(uint32_t (*get_millis_fn)(void))
{
    volatile uint16_t *alpha_buf = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    uint32_t alpha_words = (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT) / 2u;

    s_get_millis = get_millis_fn;
    for (uint32_t index = 0u; index < alpha_words; index++) {
        alpha_buf[index] = 0u;
    }

    s_box_drawn = false;
    s_last_box_ms = 0u;
    s_fps_window_start_ms = 0u;
    s_fps_frame_counter = 0u;
    s_display_fps = 0u;
    draw_fps_osd();
    trigger_overlay_refresh();
}

void master_detection_overlay_clear(void)
{
    if (!s_box_drawn) {
        return;
    }

    clear_rect_border(s_drawn_box.x1,
                      s_drawn_box.y1,
                      s_drawn_box.x2,
                      s_drawn_box.y2);
    clear_arrow(&s_drawn_box);
    s_box_drawn = false;
    trigger_overlay_refresh();
}

static void sync_overlay_from_tracking_output(void)
{
    app_gimbal_tracking_input_output_t output;
    bool dashed = false;
    uint16_t box_color;
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;

    app_gimbal_tracking_input_get_output(&output);
    if (!output.valid || (output.target_x2 <= output.target_x1) || (output.target_y2 <= output.target_y1)) {
        if (s_box_drawn && ((uint32_t)(millis() - s_last_box_ms) >= MASTER_BOX_TIMEOUT_MS)) {
            master_detection_overlay_clear();
        }
        return;
    }

    x1 = output.target_x1;
    y1 = output.target_y1;
    x2 = output.target_x2;
    y2 = output.target_y2;

    if (x2 < x1) {
        int32_t temp = x1;
        x1 = x2;
        x2 = temp;
    }
    if (y2 < y1) {
        int32_t temp = y1;
        y1 = y2;
        y2 = temp;
    }

    master_detection_overlay_clear();
    box_color = box_color_for_tracking(&dashed);
    if (dashed) {
        draw_rect_border_dashed(x1, y1, x2, y2, box_color, MASTER_BOX_ALPHA);
    } else {
        draw_rect_border(x1, y1, x2, y2, box_color, MASTER_BOX_ALPHA);
    }

    s_drawn_box.x1 = x1;
    s_drawn_box.y1 = y1;
    s_drawn_box.x2 = x2;
    s_drawn_box.y2 = y2;
    s_drawn_box.has_arrow = false;
    if (output.tracking_active && output.valid) {
        draw_arrow(&output, box_color, MASTER_BOX_ALPHA, &s_drawn_box);
    }
    s_box_drawn = true;
    s_last_box_ms = millis();
    trigger_overlay_refresh();
}

void master_detection_overlay_draw(const subboard_detection_result_t *result)
{
    (void)result;
    sync_overlay_from_tracking_output();
}

void master_detection_overlay_tick(void)
{
    sync_overlay_from_tracking_output();
    update_fps_stats();
    trigger_overlay_refresh();
}

bool master_detection_overlay_is_active(void)
{
    return s_box_drawn;
}