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

typedef struct {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
} DrawnBox_t;

static uint32_t (*s_get_millis)(void) = 0;
static DrawnBox_t s_prev_boxes[MAX_DETECTION_COUNT];
static uint32_t s_prev_count = 0u;
static uint32_t s_last_valid_ms = 0u;
static bool s_proto_warned = false;
static uint32_t s_last_logged_frame_id = 0u;
static bool s_tracking_active = false;
static human_tracking_overlay_status_t s_status = {
    .selected_idx = -1,
};

static bool is_valid_detection_result_for_demo(const DetectionResult_t *result);

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
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
    selected_idx = s_status.selected_idx;

    for (uint32_t index = 0u; index < result->count && index < MAX_DETECTION_COUNT; index++) {
        const DetectionBox_t *box = &result->boxes[index];
        int32_t x1 = box->x1;
        int32_t y1 = box->y1;
        int32_t x2 = box->x2;
        int32_t y2 = box->y2;
        uint8_t score_pct;

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

        draw_rect_border(x1, y1, x2, y2,
                 (((int32_t)index == selected_idx) && s_tracking_active) ?
                 HT_BOX_COLOR_TRACKING : HT_BOX_COLOR_IDLE,
                 HT_ALPHA_SOLID);

        if (drawn < MAX_DETECTION_COUNT) {
            s_prev_boxes[drawn].x1 = x1;
            s_prev_boxes[drawn].y1 = y1;
            s_prev_boxes[drawn].x2 = x2;
            s_prev_boxes[drawn].y2 = y2;
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
    reset_status();
}

void human_tracking_overlay_tick(void)
{
    if ((s_prev_count > 0u) &&
        ((uint32_t)(millis() - s_last_valid_ms) >= HT_BOX_TIMEOUT_MS)) {
        clear_all_boxes();
        printf("[HT-OVL] clear stale boxes\r\n");
    }
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