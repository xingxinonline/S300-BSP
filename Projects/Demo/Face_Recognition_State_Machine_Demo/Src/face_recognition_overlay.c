#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "detection_protocol.h"
#include "face_recognition_overlay.h"
#include "video.h"

#define FR_BOX_TIMEOUT_MS         500u
#define FR_SCORE_FILTER_PCT       50u
#define FR_ALPHA_SOLID            0xFFu
#define FR_ALPHA_CLEAR            0x00u
#define FR_LOG_EVERY_N_FRAMES     30u
#define FR_SUMMARY_LOG_EVERY_N_FRAMES 10u
#define FR_FONT_CHAR_W            5u
#define FR_FONT_CHAR_H            7u
#define FR_FONT_SPACING           1u
#define FR_FONT_SCALE             1u
#define FR_STATUS_PANEL_X         2u
#define FR_STATUS_PANEL_Y         2u
#define FR_STATUS_PANEL_W         124u
#define FR_STATUS_PANEL_H         28u
#define FR_SELECTED_BORDER_THICKNESS 2u
#define FR_SELECTED_CROSS_RADIUS  6u
#define FR_SELECTED_SWITCH_CONFIRM_FRAMES 3u
#define FR_SELECTED_LOST_HOLD_FRAMES 2u
#define FR_SELECTED_REACQUIRE_DIST2 576u
#define FR_STATUS_TEXT_COLOR      0xFFFFu
#define FR_STATUS_WARN_COLOR      0xFFE0u
#define FR_STATUS_MATCH_COLOR     0x07E0u
#define FR_STATUS_NOMATCH_COLOR   0xF800u
#define FR_STATUS_DIM_COLOR       0x7BEFu

#ifndef FACE_TEXT_MIRROR_COMPENSATE
#define FACE_TEXT_MIRROR_COMPENSATE 1
#endif

#define FR_REG_FRAME0             (DSP_VIDEO_SS_BASE + 0x50u)
#define FR_REG_FRAME1             (DSP_VIDEO_SS_BASE + 0x54u)

typedef struct {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
    int32_t border_thickness;
    int32_t text_x;
    int32_t text_y;
    int32_t text_len;
    int32_t cross_x;
    int32_t cross_y;
    int32_t cross_radius;
} DrawnBox_t;

static uint32_t (*s_get_millis)(void) = 0;
static DrawnBox_t s_prev_boxes[MAX_DETECTION_COUNT];
static uint32_t s_prev_count = 0u;
static uint32_t s_last_valid_ms = 0u;
static bool s_proto_warned = false;
static uint32_t s_last_logged_frame_id = 0u;
static bool s_legacy_protocol_logged = false;
static uint8_t s_last_verify_state = DETECTION_VERIFY_STATE_NONE;
static uint8_t s_last_verify_score = 0u;
static uint8_t s_last_template_count = 0u;
static uint8_t s_last_candidate_flags = 0u;
static uint32_t s_last_summary_logged_frame_id = 0u;
static uint8_t s_last_decisive_state = DETECTION_VERIFY_STATE_NONE;
static uint8_t s_last_decisive_score = 0u;
static int32_t s_last_decisive_selected_idx = -1;
static int32_t s_last_selected_idx_logged = -1;
static uint32_t s_last_selected_change_frame_id = 0u;
static int32_t s_last_display_selected_idx_logged = -1;
static uint32_t s_last_display_selected_change_frame_id = 0u;
static uint8_t s_front_buffer_idx = 0u;
static int32_t s_display_selected_idx = -1;
static int32_t s_pending_display_selected_idx = -1;
static uint8_t s_pending_display_selected_frames = 0u;
static uint8_t s_display_selected_missing_frames = 0u;
static int32_t s_display_selected_center_x = -1;
static int32_t s_display_selected_center_y = -1;

static const uint16_t s_box_colors[] = {
    0x07E0u,
    0xFFE0u,
    0x07FFu,
    0xF81Fu,
    0xFFFFu,
};

static const uint8_t s_font5x7[96][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50},
    {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x14, 0x08, 0x3E, 0x08, 0x14},
    {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02},
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
    {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00},
    {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08},
    {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43},
    {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20},
    {0x00, 0x41, 0x41, 0x7F, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00},
    {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02},
    {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20},
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44},
    {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x7F, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x10, 0x08, 0x08, 0x10, 0x08}
};

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
}

static volatile uint16_t *get_back_framebuffer(void)
{
    return (s_front_buffer_idx == 0u) ?
           (volatile uint16_t *)DISP_RFRAME1_ADDR :
           (volatile uint16_t *)DISP_RFRAME0_ADDR;
}

static volatile uint16_t *get_back_alphabuffer(void)
{
    return (s_front_buffer_idx == 0u) ?
           (volatile uint16_t *)DISP_RALPHA1_ADDR :
           (volatile uint16_t *)DISP_RALPHA0_ADDR;
}

static uint8_t get_back_buffer_idx(void)
{
    return (s_front_buffer_idx == 0u) ? 1u : 0u;
}

static void swap_overlay_buffers(void)
{
    uint8_t back_idx = get_back_buffer_idx();

    if (back_idx == 0u) {
        REG32(FR_REG_FRAME0) = 1u;
        while ((REG32(FR_REG_FRAME0) & 0x1u) != 0u) {
        }
    } else {
        REG32(FR_REG_FRAME1) = 1u;
        while ((REG32(FR_REG_FRAME1) & 0x1u) != 0u) {
        }
    }

    s_front_buffer_idx = back_idx;
}

static void clear_back_alpha_buffer(void)
{
    volatile uint16_t *alpha_buf = get_back_alphabuffer();
    uint32_t alpha_words = (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT) / 2u;

    for (uint32_t index = 0u; index < alpha_words; index++) {
        alpha_buf[index] = 0u;
    }
}

static void clear_all_alpha_buffers(void)
{
    volatile uint16_t *alpha0 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    volatile uint16_t *alpha1 = (volatile uint16_t *)DISP_RALPHA1_ADDR;
    uint32_t alpha_words = (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT) / 2u;

    for (uint32_t index = 0u; index < alpha_words; index++) {
        alpha0[index] = 0u;
        alpha1[index] = 0u;
    }
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

static bool is_supported_detection_version(uint32_t version)
{
    return ((version >> 8) == 0x02u);
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

static bool result_has_feature_tail(const DetectionResult_t *result)
{
    if (result == 0) {
        return false;
    }

    return result->version >= 0x0203u;
}

static bool result_has_verify_summary(const DetectionResult_t *result)
{
    return (result != 0) && (result->version >= DETECTION_PROTOCOL_VERSION);
}

static bool selected_face_index_is_valid(const DetectionResult_t *result)
{
    if (result == 0) {
        return false;
    }

    return (result->selected_idx >= 0) && ((uint32_t)result->selected_idx < result->count);
}

static bool result_has_valid_feature(const DetectionResult_t *result)
{
    if (!result_has_feature_tail(result)) {
        return false;
    }

    if ((result->feature_flags & DETECTION_RESULT_FLAG_HAS_FEATURE) == 0u) {
        return false;
    }

    if (!selected_face_index_is_valid(result)) {
        return false;
    }

    if (result->feature_dim != FACE_FEATURE_DIMENSION) {
        return false;
    }

    if (result_has_verify_summary(result) &&
        ((result->candidate_flags & DETECTION_CANDIDATE_FLAG_FEATURE_VALID) == 0u)) {
        return false;
    }

    return true;
}

static bool is_face_like_type(uint8_t raw_type)
{
    return (raw_type == DETECTION_TYPE_FACE) || (raw_type == DETECTION_TYPE_UNKNOWN);
}

static const char *verify_state_name(uint8_t verify_state)
{
    switch (verify_state) {
    case DETECTION_VERIFY_STATE_WAIT_ANCHOR: return "WAIT_ANCHOR";
    case DETECTION_VERIFY_STATE_MATCH: return "MATCH";
    case DETECTION_VERIFY_STATE_UNCERTAIN: return "UNCERTAIN";
    case DETECTION_VERIFY_STATE_NO_MATCH: return "NO_MATCH";
    case DETECTION_VERIFY_STATE_NONE:
    default:
        return "NONE";
    }
}

static const char *verify_state_short_name(uint8_t verify_state)
{
    switch (verify_state) {
    case DETECTION_VERIFY_STATE_WAIT_ANCHOR: return "WAIT";
    case DETECTION_VERIFY_STATE_MATCH: return "MATCH";
    case DETECTION_VERIFY_STATE_UNCERTAIN: return "UNCRT";
    case DETECTION_VERIFY_STATE_NO_MATCH: return "NOMAT";
    case DETECTION_VERIFY_STATE_NONE:
    default:
        return "NONE";
    }
}

static uint16_t verify_state_color(uint8_t verify_state)
{
    switch (verify_state) {
    case DETECTION_VERIFY_STATE_MATCH:
        return FR_STATUS_MATCH_COLOR;
    case DETECTION_VERIFY_STATE_NO_MATCH:
        return FR_STATUS_NOMATCH_COLOR;
    case DETECTION_VERIFY_STATE_WAIT_ANCHOR:
        return FR_STATUS_WARN_COLOR;
    case DETECTION_VERIFY_STATE_UNCERTAIN:
        return 0x07FFu;
    case DETECTION_VERIFY_STATE_NONE:
    default:
        return FR_STATUS_DIM_COLOR;
    }
}

static char verify_state_short_code(uint8_t verify_state)
{
    switch (verify_state) {
    case DETECTION_VERIFY_STATE_MATCH:
        return 'M';
    case DETECTION_VERIFY_STATE_NO_MATCH:
        return 'N';
    case DETECTION_VERIFY_STATE_UNCERTAIN:
        return 'U';
    case DETECTION_VERIFY_STATE_WAIT_ANCHOR:
        return 'W';
    case DETECTION_VERIFY_STATE_NONE:
    default:
        return '-';
    }
}

static void format_candidate_flags(uint8_t flags, char *buffer, size_t buffer_size)
{
    size_t used = 0u;

    if ((buffer == 0) || (buffer_size == 0u)) {
        return;
    }

    buffer[0] = '\0';

    if (flags == 0u) {
        (void)snprintf(buffer, buffer_size, "-none-");
        return;
    }

#define APPEND_FLAG(flag_bit, flag_name) \
    do { \
        if ((flags & (flag_bit)) != 0u) { \
            used += (size_t)snprintf(buffer + used, \
                                     (used < buffer_size) ? (buffer_size - used) : 0u, \
                                     "%s%s", \
                                     (used == 0u) ? "" : "|", \
                                     (flag_name)); \
        } \
    } while (0)

    APPEND_FLAG(DETECTION_CANDIDATE_FLAG_PRESENT, "PRESENT");
    APPEND_FLAG(DETECTION_CANDIDATE_FLAG_ALLOW_EXTRACT, "ALLOW_EXTRACT");
    APPEND_FLAG(DETECTION_CANDIDATE_FLAG_ALLOW_UPDATE, "ALLOW_UPDATE");
    APPEND_FLAG(DETECTION_CANDIDATE_FLAG_FEATURE_VALID, "FEATURE_VALID");
    APPEND_FLAG(DETECTION_CANDIDATE_FLAG_TEMPLATE_MATCH, "TEMPLATE_MATCH");
    APPEND_FLAG(DETECTION_CANDIDATE_FLAG_TEMPLATE_ENROLL, "TEMPLATE_ENROLL");
    APPEND_FLAG(DETECTION_CANDIDATE_FLAG_TEMPLATE_FUSE, "TEMPLATE_FUSE");

#undef APPEND_FLAG
}

static void set_pixel_alpha(uint32_t x, uint32_t y, uint8_t alpha)
{
    volatile uint16_t *alpha_buf = get_back_alphabuffer();
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
    volatile uint16_t *framebuffer = get_back_framebuffer();
    uint32_t pixel_idx = y * DISP_IMAGE_WIDTH + x;

    framebuffer[pixel_idx] = color;
}

static void clear_rect_alpha(int32_t x, int32_t y, int32_t width, int32_t height)
{
    int32_t x_end = x + width - 1;
    int32_t y_end = y + height - 1;

    if ((width <= 0) || (height <= 0)) {
        return;
    }

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x_end >= DISP_IMAGE_WIDTH) {
        x_end = DISP_IMAGE_WIDTH - 1;
    }
    if (y_end >= DISP_IMAGE_HEIGHT) {
        y_end = DISP_IMAGE_HEIGHT - 1;
    }
    if ((x > x_end) || (y > y_end)) {
        return;
    }

    for (int32_t row = y; row <= y_end; row++) {
        for (int32_t col = x; col <= x_end; col++) {
            set_pixel_alpha((uint32_t)col, (uint32_t)row, FR_ALPHA_CLEAR);
        }
    }
}

static void draw_char(int32_t x, int32_t y, char ch, uint16_t color, uint8_t alpha)
{
    if ((ch < 32) || (ch > 126)) {
        return;
    }

    for (uint32_t col = 0u; col < FR_FONT_CHAR_W; col++) {
        uint8_t glyph_col =
#if FACE_TEXT_MIRROR_COMPENSATE
            s_font5x7[(uint32_t)ch - 32u][FR_FONT_CHAR_W - 1u - col];
#else
            s_font5x7[(uint32_t)ch - 32u][col];
#endif

        for (uint32_t row = 0u; row < FR_FONT_CHAR_H; row++) {
            if (((glyph_col >> row) & 0x01u) == 0u) {
                continue;
            }

            for (uint32_t sy = 0u; sy < FR_FONT_SCALE; sy++) {
                for (uint32_t sx = 0u; sx < FR_FONT_SCALE; sx++) {
                    int32_t px = x + (int32_t)(col * FR_FONT_SCALE + sx);
                    int32_t py = y + (int32_t)(row * FR_FONT_SCALE + sy);

                    if ((px < 0) || (py < 0) || (px >= DISP_IMAGE_WIDTH) || (py >= DISP_IMAGE_HEIGHT)) {
                        continue;
                    }

                    set_pixel_color((uint32_t)px, (uint32_t)py, color);
                    set_pixel_alpha((uint32_t)px, (uint32_t)py, alpha);
                }
            }
        }
    }
}

static void draw_string(int32_t x, int32_t y, const char *text, uint16_t color, uint8_t alpha)
{
    if (text == 0) {
        return;
    }

#if FACE_TEXT_MIRROR_COMPENSATE
    {
        int32_t len = (int32_t)strlen(text);

        for (int32_t index = len - 1; index >= 0; index--) {
            draw_char(x, y, text[index], color, alpha);
            x += (int32_t)((FR_FONT_CHAR_W + FR_FONT_SPACING) * FR_FONT_SCALE);
        }
    }
#else
    while (*text != '\0') {
        draw_char(x, y, *text, color, alpha);
        x += (int32_t)((FR_FONT_CHAR_W + FR_FONT_SPACING) * FR_FONT_SCALE);
        text++;
    }
#endif
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

static void draw_rect_border_thick(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                   uint16_t color, uint8_t alpha, uint32_t thickness)
{
    if (thickness == 0u) {
        draw_rect_border(x1, y1, x2, y2, color, alpha);
        return;
    }

    for (uint32_t index = 0u; index < thickness; index++) {
        draw_rect_border(x1 - (int32_t)index,
                         y1 - (int32_t)index,
                         x2 + (int32_t)index,
                         y2 + (int32_t)index,
                         color,
                         alpha);
    }
}

static void draw_crosshair(int32_t center_x, int32_t center_y, int32_t radius,
                           uint16_t color, uint8_t alpha)
{
    if (radius <= 0) {
        return;
    }

    for (int32_t offset = -radius; offset <= radius; offset++) {
        int32_t x = center_x + offset;
        int32_t y = center_y + offset;

        if ((x >= 0) && (x < DISP_IMAGE_WIDTH) && (center_y >= 0) && (center_y < DISP_IMAGE_HEIGHT)) {
            set_pixel_color((uint32_t)x, (uint32_t)center_y, color);
            set_pixel_alpha((uint32_t)x, (uint32_t)center_y, alpha);
        }

        if ((center_x >= 0) && (center_x < DISP_IMAGE_WIDTH) && (y >= 0) && (y < DISP_IMAGE_HEIGHT)) {
            set_pixel_color((uint32_t)center_x, (uint32_t)y, color);
            set_pixel_alpha((uint32_t)center_x, (uint32_t)y, alpha);
        }
    }
}

static void clear_all_boxes(void)
{
    clear_back_alpha_buffer();
    s_prev_count = 0u;
}

static bool try_get_face_box(const DetectionResult_t *result,
                             int32_t index,
                             int32_t *x1,
                             int32_t *y1,
                             int32_t *x2,
                             int32_t *y2)
{
    const DetectionBox_t *box;
    int32_t local_x1;
    int32_t local_y1;
    int32_t local_x2;
    int32_t local_y2;
    uint8_t score_pct;

    if ((result == 0) || (index < 0) || ((uint32_t)index >= result->count)) {
        return false;
    }

    box = &result->boxes[(uint32_t)index];
    if (!is_face_like_type(box->type)) {
        return false;
    }

    local_x1 = box->x1;
    local_y1 = box->y1;
    local_x2 = box->x2;
    local_y2 = box->y2;

    if (local_x2 < local_x1) {
        int32_t temp = local_x1;
        local_x1 = local_x2;
        local_x2 = temp;
    }
    if (local_y2 < local_y1) {
        int32_t temp = local_y1;
        local_y1 = local_y2;
        local_y2 = temp;
    }

    if (((local_x2 - local_x1) <= 2) || ((local_y2 - local_y1) <= 2)) {
        return false;
    }

    score_pct = normalize_score_pct(box->score);
    if (score_pct < FR_SCORE_FILTER_PCT) {
        return false;
    }

    if (x1 != 0) {
        *x1 = local_x1;
    }
    if (y1 != 0) {
        *y1 = local_y1;
    }
    if (x2 != 0) {
        *x2 = local_x2;
    }
    if (y2 != 0) {
        *y2 = local_y2;
    }

    return true;
}

static bool get_face_center(const DetectionResult_t *result,
                            int32_t index,
                            int32_t *center_x,
                            int32_t *center_y)
{
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;

    if (!try_get_face_box(result, index, &x1, &y1, &x2, &y2)) {
        return false;
    }

    if (center_x != 0) {
        *center_x = (x1 + x2) / 2;
    }
    if (center_y != 0) {
        *center_y = (y1 + y2) / 2;
    }

    return true;
}

static uint32_t squared_distance_i32(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t dx = x0 - x1;
    int32_t dy = y0 - y1;

    return (uint32_t)((dx * dx) + (dy * dy));
}

static int32_t find_nearest_face_index(const DetectionResult_t *result,
                                       int32_t ref_x,
                                       int32_t ref_y,
                                       uint32_t max_dist2)
{
    int32_t best_index = -1;
    uint32_t best_dist2 = 0u;

    if (result == 0) {
        return -1;
    }

    for (uint32_t index = 0u; index < result->count; index++) {
        int32_t center_x;
        int32_t center_y;
        uint32_t dist2;

        if (!get_face_center(result, (int32_t)index, &center_x, &center_y)) {
            continue;
        }

        dist2 = squared_distance_i32(center_x, center_y, ref_x, ref_y);
        if (dist2 > max_dist2) {
            continue;
        }

        if ((best_index < 0) || (dist2 < best_dist2)) {
            best_index = (int32_t)index;
            best_dist2 = dist2;
        }
    }

    return best_index;
}

static int32_t resolve_display_selected_idx(const DetectionResult_t *result)
{
    int32_t raw_selected_idx;
    bool raw_valid;
    int32_t raw_center_x = -1;
    int32_t raw_center_y = -1;

    if (result == 0) {
        s_display_selected_idx = -1;
        s_pending_display_selected_idx = -1;
        s_pending_display_selected_frames = 0u;
        s_display_selected_missing_frames = 0u;
        s_display_selected_center_x = -1;
        s_display_selected_center_y = -1;
        return -1;
    }

    raw_selected_idx = result->selected_idx;
    raw_valid = get_face_center(result, raw_selected_idx, &raw_center_x, &raw_center_y);

    if (raw_valid) {
        if (s_display_selected_idx < 0) {
            s_display_selected_idx = raw_selected_idx;
            s_display_selected_center_x = raw_center_x;
            s_display_selected_center_y = raw_center_y;
            s_pending_display_selected_idx = -1;
            s_pending_display_selected_frames = 0u;
            s_display_selected_missing_frames = 0u;
            return s_display_selected_idx;
        }

        if (raw_selected_idx == s_display_selected_idx) {
            s_display_selected_idx = raw_selected_idx;
            s_display_selected_center_x = raw_center_x;
            s_display_selected_center_y = raw_center_y;
            s_pending_display_selected_idx = -1;
            s_pending_display_selected_frames = 0u;
            s_display_selected_missing_frames = 0u;
            return s_display_selected_idx;
        }

        if (s_pending_display_selected_idx != raw_selected_idx) {
            s_pending_display_selected_idx = raw_selected_idx;
            s_pending_display_selected_frames = 1u;
        } else if (s_pending_display_selected_frames < 255u) {
            s_pending_display_selected_frames++;
        }

        if (s_pending_display_selected_frames >= FR_SELECTED_SWITCH_CONFIRM_FRAMES) {
            s_display_selected_idx = raw_selected_idx;
            s_display_selected_center_x = raw_center_x;
            s_display_selected_center_y = raw_center_y;
            s_pending_display_selected_idx = -1;
            s_pending_display_selected_frames = 0u;
        }

        s_display_selected_missing_frames = 0u;
        return s_display_selected_idx;
    }

    s_pending_display_selected_idx = -1;
    s_pending_display_selected_frames = 0u;

    if ((s_display_selected_idx >= 0) &&
        (s_display_selected_center_x >= 0) &&
        (s_display_selected_center_y >= 0)) {
        int32_t nearest_idx = find_nearest_face_index(result,
                                                      s_display_selected_center_x,
                                                      s_display_selected_center_y,
                                                      FR_SELECTED_REACQUIRE_DIST2);

        if (nearest_idx >= 0) {
            int32_t center_x;
            int32_t center_y;

            if (get_face_center(result, nearest_idx, &center_x, &center_y)) {
                s_display_selected_idx = nearest_idx;
                s_display_selected_center_x = center_x;
                s_display_selected_center_y = center_y;
                s_display_selected_missing_frames = 0u;
                return s_display_selected_idx;
            }
        }
    }

    if (s_display_selected_missing_frames < 255u) {
        s_display_selected_missing_frames++;
    }

    if (s_display_selected_missing_frames > FR_SELECTED_LOST_HOLD_FRAMES) {
        s_display_selected_idx = -1;
        s_display_selected_center_x = -1;
        s_display_selected_center_y = -1;
    }

    return s_display_selected_idx;
}

static void format_flag_summary(uint8_t flags, char *buffer, size_t buffer_size)
{
    (void)snprintf(buffer,
                   buffer_size,
                   "P%c X%c U%c F%c M%c N%c B%c",
                   ((flags & DETECTION_CANDIDATE_FLAG_PRESENT) != 0u) ? '+' : '-',
                   ((flags & DETECTION_CANDIDATE_FLAG_ALLOW_EXTRACT) != 0u) ? '+' : '-',
                   ((flags & DETECTION_CANDIDATE_FLAG_ALLOW_UPDATE) != 0u) ? '+' : '-',
                   ((flags & DETECTION_CANDIDATE_FLAG_FEATURE_VALID) != 0u) ? '+' : '-',
                   ((flags & DETECTION_CANDIDATE_FLAG_TEMPLATE_MATCH) != 0u) ? '+' : '-',
                   ((flags & DETECTION_CANDIDATE_FLAG_TEMPLATE_ENROLL) != 0u) ? '+' : '-',
                   ((flags & DETECTION_CANDIDATE_FLAG_TEMPLATE_FUSE) != 0u) ? '+' : '-');
}

static void draw_selected_face_label(const DetectionResult_t *result,
                                     int32_t display_selected_idx,
                                     uint32_t draw_index,
                                     uint32_t result_index,
                                     int32_t x1,
                                     int32_t y1,
                                     int32_t y2,
                                     uint16_t color)
{
    char label[32];
    int32_t text_x;
    int32_t text_y;
    int32_t len;

    if ((result == 0) || (draw_index >= MAX_DETECTION_COUNT)) {
        return;
    }

    if ((int32_t)result_index == display_selected_idx) {
        if (result->verify_state == DETECTION_VERIFY_STATE_WAIT_ANCHOR) {
            (void)snprintf(label,
                           sizeof(label),
                           "SEL %s C%u",
                           verify_state_short_name(result->verify_state),
                           (unsigned)result->candidate_confidence);
        } else {
            (void)snprintf(label,
                           sizeof(label),
                           "SEL %s S%u",
                           verify_state_short_name(result->verify_state),
                           (unsigned)result->verify_score);
        }
    } else {
        (void)snprintf(label, sizeof(label), "FACE%lu", (unsigned long)result_index);
    }

    text_x = x1;
    text_y = y1 - (int32_t)(FR_FONT_CHAR_H * FR_FONT_SCALE) - 2;
    if (text_y < 0) {
        text_y = y2 + 2;
    }

    draw_string(text_x, text_y, label, color, FR_ALPHA_SOLID);
    len = (int32_t)strlen(label);
    s_prev_boxes[draw_index].text_x = text_x;
    s_prev_boxes[draw_index].text_y = text_y;
    s_prev_boxes[draw_index].text_len = len;
}

static void draw_status_panel(const DetectionResult_t *result, bool has_valid_feature)
{
    char line1[32];
    char line2[32];
    char line3[32];
    uint16_t state_color;

    clear_rect_alpha(FR_STATUS_PANEL_X,
                     FR_STATUS_PANEL_Y,
                     FR_STATUS_PANEL_W,
                     FR_STATUS_PANEL_H);

    if (result == 0) {
        draw_string(FR_STATUS_PANEL_X,
                    FR_STATUS_PANEL_Y,
                    "FR NO DATA",
                    FR_STATUS_DIM_COLOR,
                    FR_ALPHA_SOLID);
        return;
    }

    state_color = verify_state_color(result->verify_state);
    (void)snprintf(line1,
                   sizeof(line1),
                   "FR %s F%lu L%c%u@%ld",
                   verify_state_short_name(result->verify_state),
                   (unsigned long)result->frame_id,
                   verify_state_short_code(s_last_decisive_state),
                   (unsigned)s_last_decisive_score,
                   (long)s_last_decisive_selected_idx);
    (void)snprintf(line2,
                   sizeof(line2),
                   "R%ld D%ld S%u C%u",
                   (long)result->selected_idx,
                   (long)s_display_selected_idx,
                   (unsigned)result->verify_score,
                   (unsigned)result->candidate_confidence);
    format_flag_summary(result->candidate_flags, line3, sizeof(line3));

    if (has_valid_feature) {
        size_t used = strlen(line3);
        (void)snprintf(line3 + used,
                       (used < sizeof(line3)) ? (sizeof(line3) - used) : 0u,
                       " V+");
    }

    draw_string(FR_STATUS_PANEL_X,
                FR_STATUS_PANEL_Y,
                line1,
                state_color,
                FR_ALPHA_SOLID);
    draw_string(FR_STATUS_PANEL_X,
                FR_STATUS_PANEL_Y + (int32_t)(FR_FONT_CHAR_H * FR_FONT_SCALE + 2u),
                line2,
                FR_STATUS_TEXT_COLOR,
                FR_ALPHA_SOLID);
    draw_string(FR_STATUS_PANEL_X,
                FR_STATUS_PANEL_Y + (int32_t)(2u * (FR_FONT_CHAR_H * FR_FONT_SCALE + 2u)),
                line3,
                FR_STATUS_TEXT_COLOR,
                FR_ALPHA_SOLID);
}

static void process_multi_result(const DetectionResult_t *result)
{
    uint32_t drawn = 0u;
    int32_t display_selected_idx = -1;
    bool has_valid_feature = false;
    bool has_verify_summary = false;
    bool summary_changed = false;
    char candidate_flag_text[96];

    clear_all_boxes();

    if (!is_valid_detection_result_for_demo(result)) {
        if ((result != 0) && !s_proto_warned) {
            printf("[FR-OVL][WARN] drop result: magic=0x%08lX version=0x%04lX count=%lu\r\n",
                   (unsigned long)result->magic,
                   (unsigned long)result->version,
                   (unsigned long)result->count);
            s_proto_warned = true;
        }
        return;
    }

    has_valid_feature = result_has_valid_feature(result);
        has_verify_summary = result_has_verify_summary(result);

    if (!s_legacy_protocol_logged && !result_has_feature_tail(result)) {
        printf("[FR-OVL] compatible detection-only mode: DSP version=0x%04lX\r\n",
               (unsigned long)result->version);
        s_legacy_protocol_logged = true;
    } else if (!s_proto_warned && result_has_feature_tail(result) && !has_valid_feature &&
             (result->feature_dim != 0u || result->feature_flags != 0u)) {
         printf("[FR-OVL][WARN] feature payload ignored: flags=0x%08lX selected_idx=%ld count=%lu feature_dim=%lu candidate_flags=0x%02X\r\n",
               (unsigned long)result->feature_flags,
               (long)result->selected_idx,
               (unsigned long)result->count,
             (unsigned long)result->feature_dim,
             (unsigned)(has_verify_summary ? result->candidate_flags : 0u));
        s_proto_warned = true;
    }

    s_last_valid_ms = millis();
    display_selected_idx = resolve_display_selected_idx(result);

    for (uint32_t index = 0u; index < result->count && index < MAX_DETECTION_COUNT; index++) {
        const DetectionBox_t *box = &result->boxes[index];
        int32_t x1 = box->x1;
        int32_t y1 = box->y1;
        int32_t x2 = box->x2;
        int32_t y2 = box->y2;
        uint8_t score_pct;

        if (!is_face_like_type(box->type)) {
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
        if (score_pct < FR_SCORE_FILTER_PCT) {
            continue;
        }

        if ((int32_t)index == display_selected_idx) {
            draw_rect_border_thick(x1, y1, x2, y2,
                                   verify_state_color(result->verify_state),
                                   FR_ALPHA_SOLID,
                                   FR_SELECTED_BORDER_THICKNESS);
        } else {
            draw_rect_border(x1, y1, x2, y2,
                             s_box_colors[drawn % (sizeof(s_box_colors) / sizeof(s_box_colors[0]))],
                             FR_ALPHA_SOLID);
        }

        if (drawn < MAX_DETECTION_COUNT) {
            s_prev_boxes[drawn].x1 = x1;
            s_prev_boxes[drawn].y1 = y1;
            s_prev_boxes[drawn].x2 = x2;
            s_prev_boxes[drawn].y2 = y2;
            s_prev_boxes[drawn].border_thickness = ((int32_t)index == display_selected_idx) ?
                                                   (int32_t)FR_SELECTED_BORDER_THICKNESS : 1;
            s_prev_boxes[drawn].text_x = 0;
            s_prev_boxes[drawn].text_y = 0;
            s_prev_boxes[drawn].text_len = 0;
            s_prev_boxes[drawn].cross_x = 0;
            s_prev_boxes[drawn].cross_y = 0;
            s_prev_boxes[drawn].cross_radius = 0;

            if ((int32_t)index == display_selected_idx) {
                int32_t center_x = (x1 + x2) / 2;
                int32_t center_y = (y1 + y2) / 2;

                draw_crosshair(center_x,
                               center_y,
                               (int32_t)FR_SELECTED_CROSS_RADIUS,
                               verify_state_color(result->verify_state),
                               FR_ALPHA_SOLID);
                s_prev_boxes[drawn].cross_x = center_x;
                s_prev_boxes[drawn].cross_y = center_y;
                s_prev_boxes[drawn].cross_radius = (int32_t)FR_SELECTED_CROSS_RADIUS;
            }

            draw_selected_face_label(result,
                                     display_selected_idx,
                                     drawn,
                                     index,
                                     x1,
                                     y1,
                                     y2,
                                     ((int32_t)index == display_selected_idx) ?
                                         verify_state_color(result->verify_state) :
                                         s_box_colors[drawn % (sizeof(s_box_colors) / sizeof(s_box_colors[0]))]);
            drawn++;
        }
    }

    s_prev_count = drawn;
    draw_status_panel(result, has_valid_feature);
    swap_overlay_buffers();

    if (has_verify_summary) {
        if ((result->selected_idx != s_last_selected_idx_logged) &&
            ((result->selected_idx >= 0) || (s_last_selected_idx_logged >= 0))) {
            printf("[FR-OVL] selected face changed frame=%lu idx=%ld prev=%ld faces=%lu state=%s score=%u conf=%u\r\n",
                   (unsigned long)result->frame_id,
                   (long)result->selected_idx,
                   (long)s_last_selected_idx_logged,
                   (unsigned long)result->count,
                   verify_state_name(result->verify_state),
                   (unsigned)result->verify_score,
                   (unsigned)result->candidate_confidence);
            s_last_selected_idx_logged = result->selected_idx;
            s_last_selected_change_frame_id = result->frame_id;
        }

        if ((display_selected_idx != s_last_display_selected_idx_logged) &&
            ((display_selected_idx >= 0) || (s_last_display_selected_idx_logged >= 0))) {
            printf("[FR-OVL] display selected changed frame=%lu idx=%ld prev=%ld raw=%ld faces=%lu state=%s score=%u conf=%u\r\n",
                   (unsigned long)result->frame_id,
                   (long)display_selected_idx,
                   (long)s_last_display_selected_idx_logged,
                   (long)result->selected_idx,
                   (unsigned long)result->count,
                   verify_state_name(result->verify_state),
                   (unsigned)result->verify_score,
                   (unsigned)result->candidate_confidence);
            s_last_display_selected_idx_logged = display_selected_idx;
            s_last_display_selected_change_frame_id = result->frame_id;
        }

        if ((result->verify_state == DETECTION_VERIFY_STATE_MATCH) ||
            (result->verify_state == DETECTION_VERIFY_STATE_NO_MATCH)) {
            s_last_decisive_state = result->verify_state;
            s_last_decisive_score = result->verify_score;
            s_last_decisive_selected_idx = result->selected_idx;
        }

        format_candidate_flags(result->candidate_flags, candidate_flag_text, sizeof(candidate_flag_text));
        summary_changed = (result->verify_state != s_last_verify_state) ||
                          (result->verify_score != s_last_verify_score) ||
                          (result->template_count != s_last_template_count) ||
                          (result->candidate_flags != s_last_candidate_flags);
        if (summary_changed ||
            (s_last_summary_logged_frame_id == 0u) ||
            ((result->frame_id - s_last_summary_logged_frame_id) >= FR_SUMMARY_LOG_EVERY_N_FRAMES)) {
             printf("[FR-OVL] verify frame=%lu faces=%lu raw_idx=%ld disp_idx=%ld state=%s score=%u templates=%u cand_conf=%u cand_flags=0x%02X(%s) feature=%u\r\n",
                   (unsigned long)result->frame_id,
                   (unsigned long)result->count,
                   (long)result->selected_idx,
                 (long)display_selected_idx,
                   verify_state_name(result->verify_state),
                   (unsigned)result->verify_score,
                   (unsigned)result->template_count,
                   (unsigned)result->candidate_confidence,
                   (unsigned)result->candidate_flags,
                   candidate_flag_text,
                   has_valid_feature ? 1u : 0u);
            s_last_summary_logged_frame_id = result->frame_id;
        }

        if ((result->verify_state == DETECTION_VERIFY_STATE_WAIT_ANCHOR) &&
            ((result->candidate_flags & DETECTION_CANDIDATE_FLAG_PRESENT) != 0u) &&
            ((result->candidate_flags & DETECTION_CANDIDATE_FLAG_ALLOW_EXTRACT) == 0u) &&
            summary_changed) {
            printf("[FR-OVL] anchor pending: candidate present but DSP quality gate denied extract/update, keep face frontal and stable\r\n");
        }

        s_last_verify_state = result->verify_state;
        s_last_verify_score = result->verify_score;
        s_last_template_count = result->template_count;
        s_last_candidate_flags = result->candidate_flags;
    }

    if (((drawn > 0u) || has_verify_summary) &&
        ((s_last_logged_frame_id == 0u) ||
         ((result->frame_id - s_last_logged_frame_id) >= FR_LOG_EVERY_N_FRAMES))) {
        printf("[FR-OVL] frame=%lu faces=%lu selected_idx=%ld feature_flags=0x%08lX version=0x%04lX\r\n",
               (unsigned long)result->frame_id,
               (unsigned long)result->count,
               (long)result->selected_idx,
               (unsigned long)(result_has_feature_tail(result) ? result->feature_flags : 0u),
               (unsigned long)result->version);
        s_last_logged_frame_id = result->frame_id;
    }
}

void face_recognition_overlay_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;
    s_front_buffer_idx = 0u;
    face_recognition_overlay_reset();
}

void face_recognition_overlay_reset(void)
{
    clear_all_alpha_buffers();
    s_last_valid_ms = 0u;
    s_last_logged_frame_id = 0u;
    s_last_verify_state = DETECTION_VERIFY_STATE_NONE;
    s_last_verify_score = 0u;
    s_last_template_count = 0u;
    s_last_candidate_flags = 0u;
    s_last_summary_logged_frame_id = 0u;
    s_last_decisive_state = DETECTION_VERIFY_STATE_NONE;
    s_last_decisive_score = 0u;
    s_last_decisive_selected_idx = -1;
    s_last_selected_idx_logged = -1;
    s_last_selected_change_frame_id = 0u;
    s_last_display_selected_idx_logged = -1;
    s_last_display_selected_change_frame_id = 0u;
    s_display_selected_idx = -1;
    s_pending_display_selected_idx = -1;
    s_pending_display_selected_frames = 0u;
    s_display_selected_missing_frames = 0u;
    s_display_selected_center_x = -1;
    s_display_selected_center_y = -1;
}

void face_recognition_overlay_tick(void)
{
    if ((s_prev_count > 0u) &&
        ((uint32_t)(millis() - s_last_valid_ms) >= FR_BOX_TIMEOUT_MS)) {
        clear_back_alpha_buffer();
        swap_overlay_buffers();
        s_prev_count = 0u;
        printf("[FR-OVL] clear stale boxes\r\n");
    }
}

bool face_recognition_overlay_handle_mailbox_message(uint32_t msg)
{
    uint32_t msg_type = GET_MSG_TYPE(msg);
    uint32_t payload = GET_MSG_PAYLOAD(msg);

    switch (msg_type) {
    case MAILBOX_MSG_TYPE_MULTI: {
        uintptr_t addr = (uintptr_t)DSP_PTCM_M4_BASE_OFFSET + (uintptr_t)payload;
        process_multi_result((const DetectionResult_t *)addr);
        return true;
    }

    case MAILBOX_MSG_TYPE_NO_RESULT:
        clear_back_alpha_buffer();
        swap_overlay_buffers();
        s_prev_count = 0u;
        return true;

    default:
        return false;
    }
}