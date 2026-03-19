#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "detection_proto.h"
#include "human_tracking_overlay.h"
#include "mailbox_proto.h"
#include "video.h"

#define HT_BOX_TIMEOUT_MS       500u
#define HT_SCORE_FILTER_PCT     50u
#define HT_ALPHA_SOLID          0xFFu
#define HT_ALPHA_CLEAR          0x00u
#define HT_LOG_EVERY_N_FRAMES   30u
#define HT_BOX_COLOR_IDLE       0x8410u
#define HT_BOX_COLOR_TRACKING   0x07E0u
#define HT_BOX_COLOR_PREDICTED  0xFFE0u
#define HT_TEXT_COLOR           0x07FFu
#define HT_TEXT_SHADOW_COLOR    0x0000u
#define HT_TEXT_SHADOW_ALPHA    0x90u
#define HT_TEXT_X               2
#define HT_TEXT_Y               2
#define HT_TEXT_W               42
#define HT_TEXT_H               9
#define HT_DASH_PERIOD          6
#define HT_DASH_FILL            3

#define HT_ARROW_SCALE_NUM      2
#define HT_ARROW_SCALE_DEN      1
#define HT_ARROW_MAX_LEN        24
#define HT_ARROW_MIN_HEAD_LEN   3
#define HT_ARROW_HEAD_HALF_W    2

#ifndef HT_TEXT_MIRROR_COMPENSATE
#define HT_TEXT_MIRROR_COMPENSATE 1
#endif

#define HT_FONT_CHAR_W          5
#define HT_FONT_CHAR_H          7
#define HT_FONT_SPACING         1

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
static DrawnBox_t s_prev_boxes[MAX_DETECTION_COUNT];
static uint32_t s_prev_count = 0u;
static uint32_t s_last_valid_ms = 0u;
static bool s_proto_warned = false;
static uint32_t s_last_logged_frame_id = 0u;
static bool s_tracking_active = false;
static uint32_t s_fps_window_start_ms = 0u;
static uint32_t s_fps_frame_counter = 0u;
static uint32_t s_display_fps = 0u;
static human_tracking_overlay_status_t s_status = {
    .selected_idx = -1,
};

static bool is_valid_detection_result_for_demo(const DetectionResult_t *result);

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

static int32_t fps_osd_origin_x(void)
{
#if HT_TEXT_MIRROR_COMPENSATE
    return (int32_t)DISP_IMAGE_WIDTH - HT_TEXT_X - HT_TEXT_W;
#else
    return HT_TEXT_X;
#endif
}

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
}

static int32_t abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
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

static uint8_t normalize_score_pct(float score_raw)
{
    if ((score_raw >= 0.0f) && (score_raw <= 1.0f)) {
        uint32_t score = (uint32_t)(score_raw * 100.0f);
        return (uint8_t)((score > 100u) ? 100u : score);
    }

    if ((score_raw > 1.0f) && (score_raw <= 100.0f)) {
        uint32_t score = (uint32_t)score_raw;
        return (uint8_t)((score > 100u) ? 100u : score);
    }

    return 0u;
}

static void reset_status(void)
{
    s_status.frame_id = 0u;
    s_status.timestamp_ms = 0u;
    s_status.count = 0u;
    s_status.selected_idx = -1;
    s_status.tracker_state = 0u;
    s_status.tracker_flags = 0u;
    s_status.primary_track_id = 0u;
    s_status.primary_score_pct = 0u;
    s_status.primary_miss_count = 0u;
    s_status.primary_x1 = 0;
    s_status.primary_y1 = 0;
    s_status.primary_x2 = 0;
    s_status.primary_y2 = 0;
    s_status.has_target = false;
}

static void clear_target_status_preserve_frame(void)
{
    s_status.count = 0u;
    s_status.selected_idx = -1;
    s_status.tracker_state = 0u;
    s_status.tracker_flags = 0u;
    s_status.primary_track_id = 0u;
    s_status.primary_score_pct = 0u;
    s_status.primary_miss_count = 0u;
    s_status.primary_x1 = 0;
    s_status.primary_y1 = 0;
    s_status.primary_x2 = 0;
    s_status.primary_y2 = 0;
    s_status.has_target = false;
}

static bool box_geometry_is_valid(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    return ((x2 - x1) > 2) && ((y2 - y1) > 2);
}

static int32_t normalize_selected_idx(const DetectionResult_t *result)
{
    if ((result == 0) || (result->count == 0u)) {
        return -1;
    }

    if ((result->selected_idx >= 0) &&
        ((uint32_t)result->selected_idx < result->count)) {
        return result->selected_idx;
    }

    if (result->count == 1u) {
        return 0;
    }

    return -1;
}

static void update_status_from_result(const DetectionResult_t *result)
{
    int32_t selected_idx;

    reset_status();
    if (!is_valid_detection_result_for_demo(result)) {
        return;
    }

    selected_idx = normalize_selected_idx(result);
    s_status.frame_id = result->frame_id;
    s_status.timestamp_ms = result->timestamp_ms;
    s_status.count = result->count;
    s_status.selected_idx = selected_idx;
    s_status.tracker_state = result->tracker_state;
    s_status.tracker_flags = result->tracker_flags;

    if ((selected_idx >= 0) && ((uint32_t)selected_idx < result->count)) {
        const DetectionBox_t *box = &result->boxes[selected_idx];
        DetectionType_t type = detection_type_from_raw(box->type);

        s_status.primary_track_id = box->track_id;
        s_status.primary_score_pct = normalize_score_pct(box->score);
        s_status.primary_miss_count = box->miss_count;
        s_status.primary_x1 = box->x1;
        s_status.primary_y1 = box->y1;
        s_status.primary_x2 = box->x2;
        s_status.primary_y2 = box->y2;
        s_status.has_target = ((type == DETECTION_TYPE_PERSON) ||
                               (type == DETECTION_TYPE_HUMAN) ||
                               (type == DETECTION_TYPE_UNKNOWN)) &&
                              box_geometry_is_valid(box->x1, box->y1, box->x2, box->y2);
    }
}

static bool is_supported_detection_version(uint32_t version)
{
    uint32_t major = version >> 8;
    return (major == 0x02u) || (major == 0x03u);
}

static bool is_valid_detection_result_for_demo(const DetectionResult_t *result)
{
    if (result == 0) {
        return false;
    }

    if (result->magic != DETECTION_RESULT_MAGIC) {
        return false;
    }

    if (!is_supported_detection_version(result->version)) {
        return false;
    }

    if (result->count > MAX_DETECTION_COUNT) {
        return false;
    }

    return true;
}

static bool is_human_like_type(uint8_t raw_type)
{
    DetectionType_t type = detection_type_from_raw(raw_type);
    return (type == DETECTION_TYPE_PERSON) ||
           (type == DETECTION_TYPE_HUMAN) ||
           (type == DETECTION_TYPE_UNKNOWN);
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

static void plot_pixel_if_visible(int32_t x, int32_t y, uint16_t color, uint8_t alpha)
{
    if ((x < 0) || (y < 0) || (x >= DISP_IMAGE_WIDTH) || (y >= DISP_IMAGE_HEIGHT)) {
        return;
    }

    set_pixel_color((uint32_t)x, (uint32_t)y, color);
    set_pixel_alpha((uint32_t)x, (uint32_t)y, alpha);
}

static void clear_pixel_if_visible(int32_t x, int32_t y)
{
    if ((x < 0) || (y < 0) || (x >= DISP_IMAGE_WIDTH) || (y >= DISP_IMAGE_HEIGHT)) {
        return;
    }

    set_pixel_alpha((uint32_t)x, (uint32_t)y, HT_ALPHA_CLEAR);
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

    for (int32_t col = 0; col < HT_FONT_CHAR_W; col++) {
        uint8_t line =
#if HT_TEXT_MIRROR_COMPENSATE
            glyph[HT_FONT_CHAR_W - 1 - col];
#else
            glyph[col];
#endif
        for (int32_t row = 0; row < HT_FONT_CHAR_H; row++) {
            if (((line >> row) & 0x01u) != 0u) {
                plot_pixel_if_visible(x + col, y + row, color, alpha);
            }
        }
    }
}

static void draw_string_5x7(int32_t x, int32_t y, const char *text, uint16_t color, uint8_t alpha)
{
    if (text == 0) {
        return;
    }

#if HT_TEXT_MIRROR_COMPENSATE
    {
        const char *cursor = text;
        while (*cursor != '\0') {
            cursor++;
        }

        while (cursor > text) {
            cursor--;
            draw_char_5x7(x, y, *cursor, color, alpha);
            x += HT_FONT_CHAR_W + HT_FONT_SPACING;
        }
    }
#else
    while (*text != '\0') {
        draw_char_5x7(x, y, *text, color, alpha);
        x += HT_FONT_CHAR_W + HT_FONT_SPACING;
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
            set_pixel_alpha((uint32_t)px, (uint32_t)py, HT_ALPHA_CLEAR);
        }
    }
}

static void draw_fps_osd(void)
{
    char text[12];
    int32_t osd_x = fps_osd_origin_x();

    snprintf(text, sizeof(text), "FPS:%2lu", (unsigned long)s_display_fps);
    clear_rect_fill(osd_x, HT_TEXT_Y, HT_TEXT_W, HT_TEXT_H);
    draw_string_5x7(osd_x + 2, HT_TEXT_Y + 2, text, HT_TEXT_SHADOW_COLOR, HT_TEXT_SHADOW_ALPHA);
    draw_string_5x7(osd_x + 1, HT_TEXT_Y + 1, text, HT_TEXT_COLOR, HT_ALPHA_SOLID);
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
        if (!dashed || ((step % HT_DASH_PERIOD) < HT_DASH_FILL)) {
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
        clear_pixel_if_visible(x0, y0);

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
    draw_line_segment(x1, y2, x2, y2, color, alpha, true);
    draw_line_segment(x1, y1, x1, y2, color, alpha, true);
    draw_line_segment(x2, y1, x2, y2, color, alpha, true);
}

static bool compute_arrow_geometry(const DetectionBox_t *box,
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

    if ((box == 0) || (box->speed < ARROW_SHOW_THRESHOLD)) {
        return false;
    }

    dir_x = (int32_t)box->vx;
    dir_y = (int32_t)box->vy;
    if ((dir_x == 0) && (dir_y == 0)) {
        return false;
    }

    *out_x0 = (box->x1 + box->x2) / 2;
    *out_y0 = (box->y1 + box->y2) / 2;

    scaled_dx = dir_x * HT_ARROW_SCALE_NUM;
    scaled_dy = dir_y * HT_ARROW_SCALE_NUM;
    if (HT_ARROW_SCALE_DEN > 1) {
        scaled_dx /= HT_ARROW_SCALE_DEN;
        scaled_dy /= HT_ARROW_SCALE_DEN;
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

    if (major > HT_ARROW_MAX_LEN) {
        scaled_dx = (scaled_dx * HT_ARROW_MAX_LEN) / major;
        scaled_dy = (scaled_dy * HT_ARROW_MAX_LEN) / major;
        major = HT_ARROW_MAX_LEN;
    }

    if ((scaled_dx == 0) && (dir_x != 0)) {
        scaled_dx = sign_i32(dir_x);
    }
    if ((scaled_dy == 0) && (dir_y != 0)) {
        scaled_dy = sign_i32(dir_y);
    }

    *out_x1 = *out_x0 + scaled_dx;
    *out_y1 = *out_y0 + scaled_dy;

    head_len = HT_ARROW_MIN_HEAD_LEN;
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

    perp_x = (-body_y * HT_ARROW_HEAD_HALF_W) / head_len;
    perp_y = (body_x * HT_ARROW_HEAD_HALF_W) / head_len;
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

static void draw_arrow(const DetectionBox_t *box, uint16_t color, uint8_t alpha, DrawnBox_t *drawn)
{
    if ((drawn == 0) || !compute_arrow_geometry(box,
                                                &drawn->arrow_x0,
                                                &drawn->arrow_y0,
                                                &drawn->arrow_x1,
                                                &drawn->arrow_y1,
                                                &drawn->arrow_head1_x,
                                                &drawn->arrow_head1_y,
                                                &drawn->arrow_head2_x,
                                                &drawn->arrow_head2_y)) {
        if (drawn != 0) {
            drawn->has_arrow = false;
        }
        return;
    }

    draw_line_segment(drawn->arrow_x0, drawn->arrow_y0, drawn->arrow_x1, drawn->arrow_y1, color, alpha, false);
    draw_line_segment(drawn->arrow_x1, drawn->arrow_y1, drawn->arrow_head1_x, drawn->arrow_head1_y, color, alpha, false);
    draw_line_segment(drawn->arrow_x1, drawn->arrow_y1, drawn->arrow_head2_x, drawn->arrow_head2_y, color, alpha, false);
    drawn->has_arrow = true;
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
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, HT_ALPHA_CLEAR);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, HT_ALPHA_CLEAR);
    }

    for (int32_t y = y1 + 1; y < y2; y++) {
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, HT_ALPHA_CLEAR);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, HT_ALPHA_CLEAR);
    }
}

static void clear_all_boxes(void)
{
    for (uint32_t index = 0u; index < s_prev_count; index++) {
        clear_rect_border(s_prev_boxes[index].x1,
                          s_prev_boxes[index].y1,
                          s_prev_boxes[index].x2,
                          s_prev_boxes[index].y2);
        if (s_prev_boxes[index].has_arrow) {
            clear_line_segment(s_prev_boxes[index].arrow_x0,
                               s_prev_boxes[index].arrow_y0,
                               s_prev_boxes[index].arrow_x1,
                               s_prev_boxes[index].arrow_y1);
            clear_line_segment(s_prev_boxes[index].arrow_x1,
                               s_prev_boxes[index].arrow_y1,
                               s_prev_boxes[index].arrow_head1_x,
                               s_prev_boxes[index].arrow_head1_y);
            clear_line_segment(s_prev_boxes[index].arrow_x1,
                               s_prev_boxes[index].arrow_y1,
                               s_prev_boxes[index].arrow_head2_x,
                               s_prev_boxes[index].arrow_head2_y);
        }
    }
    s_prev_count = 0u;
}

static void process_multi_result(const DetectionResult_t *result)
{
    uint32_t drawn = 0u;
    int32_t selected_idx;

    clear_all_boxes();
    update_status_from_result(result);

    if (!is_valid_detection_result_for_demo(result)) {
        if ((result != 0) && !s_proto_warned) {
            printf("[HT-OVL][WARN] drop result: magic=0x%08lX version=0x%04lX count=%lu\r\n",
                   (unsigned long)result->magic,
                   (unsigned long)result->version,
                   (unsigned long)result->count);
            s_proto_warned = true;
        }
        return;
    }

    if ((result->version >> 8) == 0x02u) {
        static bool s_v2_logged = false;
        if (!s_v2_logged) {
            printf("[HT-OVL] compatible protocol mode: DSP version=0x%04lX\r\n",
                   (unsigned long)result->version);
            s_v2_logged = true;
        }
    }

    s_last_valid_ms = millis();
    update_fps_stats();
    selected_idx = s_status.selected_idx;

    for (uint32_t index = 0u; index < result->count && index < MAX_DETECTION_COUNT; index++) {
        const DetectionBox_t *box = &result->boxes[index];
        DrawnBox_t *drawn_box = (drawn < MAX_DETECTION_COUNT) ? &s_prev_boxes[drawn] : 0;
        int32_t x1 = box->x1;
        int32_t y1 = box->y1;
        int32_t x2 = box->x2;
        int32_t y2 = box->y2;
        uint8_t score_pct;
        bool is_selected;
        bool is_predicted;
        uint16_t box_color;

        if (!is_human_like_type(box->type)) {
            continue;
        }

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

        if (((x2 - x1) <= 2) || ((y2 - y1) <= 2)) {
            continue;
        }

        score_pct = normalize_score_pct(box->score);
        if (score_pct < HT_SCORE_FILTER_PCT) {
            continue;
        }

        is_selected = ((int32_t)index == selected_idx);
        is_predicted = is_selected && s_tracking_active && (box->miss_count > 0u);
        box_color = HT_BOX_COLOR_IDLE;
        if (is_selected && s_tracking_active) {
            box_color = is_predicted ? HT_BOX_COLOR_PREDICTED : HT_BOX_COLOR_TRACKING;
        }

        if (is_predicted) {
            draw_rect_border_dashed(x1, y1, x2, y2, box_color, HT_ALPHA_SOLID);
        } else {
            draw_rect_border(x1, y1, x2, y2, box_color, HT_ALPHA_SOLID);
        }

        if (drawn_box != 0) {
            drawn_box->x1 = x1;
            drawn_box->y1 = y1;
            drawn_box->x2 = x2;
            drawn_box->y2 = y2;
            drawn_box->has_arrow = false;

            if (is_selected && s_tracking_active) {
                draw_arrow(box, box_color, HT_ALPHA_SOLID, drawn_box);
            }
        }

        if (drawn < MAX_DETECTION_COUNT) {
            drawn++;
        }
    }

    s_prev_count = drawn;

    if ((drawn > 0u) &&
        ((s_last_logged_frame_id == 0u) ||
         ((result->frame_id - s_last_logged_frame_id) >= HT_LOG_EVERY_N_FRAMES))) {
        printf("[HT-OVL] frame=%lu humans=%lu selected=%ld raw_tracker=%u raw_flags=0x%02X primary_id=%u\r\n",
               (unsigned long)result->frame_id,
               (unsigned long)drawn,
               (long)s_status.selected_idx,
               (unsigned)s_status.tracker_state,
               (unsigned)s_status.tracker_flags,
               (unsigned)s_status.primary_track_id);
        s_last_logged_frame_id = result->frame_id;
    }
}

void human_tracking_overlay_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;
    human_tracking_overlay_reset();
}

void human_tracking_overlay_reset(void)
{
    clear_all_boxes();
    s_last_valid_ms = 0u;
    s_last_logged_frame_id = 0u;
    s_tracking_active = false;
    s_fps_window_start_ms = 0u;
    s_fps_frame_counter = 0u;
    s_display_fps = 0u;
    reset_status();
    draw_fps_osd();
}

void human_tracking_overlay_tick(void)
{
    if ((s_prev_count > 0u) &&
        ((uint32_t)(millis() - s_last_valid_ms) >= HT_BOX_TIMEOUT_MS)) {
        clear_all_boxes();
        printf("[HT-OVL] clear stale boxes\r\n");
    }

    draw_fps_osd();
}

bool human_tracking_overlay_handle_mailbox_message(uint32_t msg)
{
    uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
    uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

    switch (msg_type) {
    case MAILBOX_MSG_TYPE_MULTI: {
        uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
        process_multi_result((const DetectionResult_t *)addr);
        return true;
    }

    case MAILBOX_MSG_TYPE_SINGLE: {
        DetectionResult_t temp = {0};
        const DetectionBox_t *box = (const DetectionBox_t *)((uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload);

        temp.magic = DETECTION_RESULT_MAGIC;
        temp.version = DETECTION_PROTOCOL_VERSION;
        temp.count = 1u;
        temp.boxes[0] = *box;
        process_multi_result(&temp);
        return true;
    }

    case MAILBOX_MSG_TYPE_NO_RESULT:
        clear_all_boxes();
        clear_target_status_preserve_frame();
        return true;

    default:
        return false;
    }
}

void human_tracking_overlay_get_status(human_tracking_overlay_status_t *out_status)
{
    if (out_status == 0) {
        return;
    }

    *out_status = s_status;
}

void human_tracking_overlay_set_tracking_active(bool tracking_active)
{
    s_tracking_active = tracking_active;
}