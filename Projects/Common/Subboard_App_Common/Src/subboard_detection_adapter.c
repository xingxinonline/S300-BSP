#include "subboard_detection_adapter.h"

#include <stdint.h>
#include <string.h>

#include "subboard_app_identity.h"
#include "subboard_log.h"

static subboard_detection_result_t s_latest_result;
static subboard_tracking_summary_t s_latest_tracking;
static bool s_proto_warned = false;
static bool s_compat_logged = false;

static uint8_t gesture_type_from_detection_type(DetectionType_t type)
{
    switch (type) {
    case DETECTION_TYPE_PALM:
        return 1u;
    case DETECTION_TYPE_PEACE:
        return 2u;
    default:
        return 0u;
    }
}

static int16_t to_i16_clamped(int32_t value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (int16_t)value;
}

static uint8_t score_to_u8(float score)
{
    int32_t value = (int32_t)(score * 100.0f + 0.5f);

    if (value < 0) {
        value = 0;
    }
    if (value > 100) {
        value = 100;
    }
    return (uint8_t)value;
}

static bool detection_protocol_supports_tracker_meta(uint32_t version)
{
    return DETECTION_PROTOCOL_IS_SUPPORTED(version);
}

static void decode_tracker_meta_from_box(uint8_t *out_state,
                                         uint8_t *out_flags,
                                         bool *out_valid,
                                         const DetectionResult_t *result,
                                         const DetectionBox_t *box,
                                         bool has_target)
{
    uint8_t meta;

    if (out_state != NULL) {
        *out_state = DETECTION_TRACKER_RAW_DISABLED;
    }
    if (out_flags != NULL) {
        *out_flags = 0u;
    }
    if (out_valid != NULL) {
        *out_valid = false;
    }

    if ((result == NULL) || (box == NULL) || !has_target) {
        return;
    }

    if (!detection_protocol_supports_tracker_meta(result->version)) {
        return;
    }

    meta = box->reserved;
    if (out_state != NULL) {
        *out_state = DETECTION_TRACKER_META_STATE(meta);
    }
    if (out_flags != NULL) {
        *out_flags = DETECTION_TRACKER_META_FLAGS(meta);
    }
    if (out_valid != NULL) {
        *out_valid = true;
    }
}

static bool raw_tracking_state_is_meaningful(const DetectionResult_t *result, bool has_target)
{
    if (result == NULL) {
        return false;
    }

    switch (result->tracker_state) {
    case TRACKER_STATE_IDLE:
    case TRACKER_STATE_TENTATIVE:
    case TRACKER_STATE_TRACKING:
    case TRACKER_STATE_LOST:
        return true;

    case TRACKER_STATE_DISABLED:
        return !has_target && (result->tracker_flags == 0u);

    default:
        return false;
    }
}

static uint8_t normalize_tracking_state(uint8_t raw_state, bool has_target)
{
    switch (raw_state) {
    case TRACKER_STATE_DISABLED:
        return SUBBOARD_TRACKING_STATE_DISABLED;
    case TRACKER_STATE_IDLE:
    case TRACKER_STATE_TENTATIVE:
        return SUBBOARD_TRACKING_STATE_IDLE;
    case TRACKER_STATE_TRACKING:
        return has_target ? SUBBOARD_TRACKING_STATE_FOLLOWING : SUBBOARD_TRACKING_STATE_FOLLOWING_LOST;
    case TRACKER_STATE_LOST:
        return SUBBOARD_TRACKING_STATE_FOLLOWING_LOST;
    default:
        return has_target ? SUBBOARD_TRACKING_STATE_FOLLOWING : SUBBOARD_TRACKING_STATE_IDLE;
    }
}

static bool is_supported_detection_version(uint32_t version)
{
    uint32_t major = version >> 8;
    return (major == 0x02u) || (major == 0x03u);
}

static bool is_valid_detection_result_for_subboard(const DetectionResult_t *result)
{
    if (result == NULL) {
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

static bool is_publishable_detection_type(uint8_t raw_type)
{
    DetectionType_t type = detection_type_from_raw(raw_type);

#if SUBBOARD_PUBLISH_GESTURE_ONLY
    return detection_type_is_gesture(type);
#else
    (void)type;
    return true;
#endif
}

static int find_best_publishable_box(const DetectionResult_t *result)
{
    int best_idx = -1;
    float best_score = 0.0f;

    if ((result->selected_idx >= 0) &&
        (result->selected_idx < (int32_t)result->count)) {
        const DetectionBox_t *selected_box = &result->boxes[result->selected_idx];

        if (is_publishable_detection_type(selected_box->type)) {
            return result->selected_idx;
        }
    }

    for (uint32_t index = 0u; index < result->count; index++) {
        const DetectionBox_t *box = &result->boxes[index];

        if (is_publishable_detection_type(box->type)) {
            if ((best_idx < 0) || (box->score > best_score)) {
                best_idx = (int)index;
                best_score = box->score;
            }
        }
    }

    return best_idx;
}

static void update_tracking_summary_from_result(const DetectionResult_t *result, int selected_idx)
{
    subboard_tracking_summary_t next;
    bool has_selected = (selected_idx >= 0) && ((uint32_t)selected_idx < result->count);
    bool raw_meta_valid = false;

    memset(&next, 0, sizeof(next));
    next.frame_id = result->frame_id;
    next.selected_idx = has_selected ? (uint8_t)selected_idx : 0xFFu;
    next.count = (uint8_t)((result->count > 255u) ? 255u : result->count);

    if (has_selected) {
        decode_tracker_meta_from_box(&next.tracker_state_raw,
                                     &next.tracker_flags_raw,
                                     &raw_meta_valid,
                                     result,
                                     &result->boxes[selected_idx],
                                     true);
    }

    if (!raw_meta_valid && raw_tracking_state_is_meaningful(result, has_selected)) {
        next.tracker_state_raw = result->tracker_state;
        next.tracker_flags_raw = result->tracker_flags;
        raw_meta_valid = true;
    }

    if (raw_meta_valid) {
        next.tracking_flags |= SUBBOARD_TRACKING_FLAG_RAW_STATE_VALID;
    }

    if (has_selected) {
        const DetectionBox_t *box = &result->boxes[selected_idx];

        next.valid = 1u;
        next.cx = to_i16_clamped((box->x1 + box->x2) / 2);
        next.cy = to_i16_clamped((box->y1 + box->y2) / 2);
        next.x1 = to_i16_clamped(box->x1);
        next.y1 = to_i16_clamped(box->y1);
        next.x2 = to_i16_clamped(box->x2);
        next.y2 = to_i16_clamped(box->y2);
        next.vx = box->vx;
        next.vy = box->vy;
        next.confidence = box->kf_confidence;
        next.target_id = box->track_id;
        next.miss_count = box->miss_count;
        next.tracking_flags |= SUBBOARD_TRACKING_FLAG_HAS_TARGET;

        if (raw_meta_valid) {
            if (next.tracker_state_raw == DETECTION_TRACKER_RAW_PREDICTED) {
                next.tracking_flags |= SUBBOARD_TRACKING_FLAG_PREDICTED;
            }
            if (next.tracker_state_raw == DETECTION_TRACKER_RAW_LOST) {
                next.tracking_flags |= SUBBOARD_TRACKING_FLAG_LOST;
            }
        } else if ((box->miss_count > 0u) || ((result->tracker_flags & TRACKER_FLAG_COASTING) != 0u)) {
            next.tracking_flags |= SUBBOARD_TRACKING_FLAG_PREDICTED;
        }
    }

    if (!has_selected && ((result->tracker_flags & TRACKER_FLAG_COASTING) != 0u)) {
        next.tracking_flags |= SUBBOARD_TRACKING_FLAG_PREDICTED;
    }

    if (!has_selected && (result->tracker_state == TRACKER_STATE_LOST)) {
        next.tracking_flags |= SUBBOARD_TRACKING_FLAG_LOST;
    }

    next.tracking_state = normalize_tracking_state(result->tracker_state, has_selected);
    s_latest_tracking = next;
}

static void update_from_box(const DetectionBox_t *box, uint8_t count, uint8_t selected_idx)
{
    subboard_detection_result_t next;

    if (box == NULL) {
        subboard_detection_adapter_clear();
        return;
    }

    memset(&next, 0, sizeof(next));
    next.valid = 1u;
    next.count = count;
    next.type = box->type;
    next.gesture_type = gesture_type_from_detection_type(detection_type_from_raw(box->type));
    next.selected_idx = selected_idx;
    next.x1 = to_i16_clamped(box->x1);
    next.y1 = to_i16_clamped(box->y1);
    next.x2 = to_i16_clamped(box->x2);
    next.y2 = to_i16_clamped(box->y2);
    next.cx = to_i16_clamped((box->x1 + box->x2) / 2);
    next.cy = to_i16_clamped((box->y1 + box->y2) / 2);
    next.vx = box->vx;
    next.vy = box->vy;
    next.face_id = box->track_id;
    next.confidence = score_to_u8(box->score);

    s_latest_result = next;
}

void subboard_detection_adapter_reset(void)
{
    memset(&s_latest_result, 0, sizeof(s_latest_result));
    memset(&s_latest_tracking, 0, sizeof(s_latest_tracking));
    s_proto_warned = false;
    s_compat_logged = false;
}

void subboard_detection_adapter_clear(void)
{
    memset(&s_latest_result, 0, sizeof(s_latest_result));
    memset(&s_latest_tracking, 0, sizeof(s_latest_tracking));
}

void subboard_detection_adapter_update_from_multi(const DetectionResult_t *result)
{
    int best_idx;

    if (!is_valid_detection_result_for_subboard(result)) {
        if ((result != NULL) && !s_proto_warned) {
            SUB_LOG_WARN("[SUB-DSP] drop result: magic=0x%08lX version=0x%04lX count=%lu\r\n",
                         (unsigned long)result->magic,
                         (unsigned long)result->version,
                         (unsigned long)result->count);
            s_proto_warned = true;
        }
        subboard_detection_adapter_clear();
        return;
    }

    if (((result->version >> 8) == 0x02u) && !s_compat_logged) {
        SUB_LOG_DEBUG("[SUB-DSP] compatible protocol mode: DSP version=0x%04lX\r\n",
                      (unsigned long)result->version);
        s_compat_logged = true;
    }

    best_idx = find_best_publishable_box(result);
    if (best_idx < 0) {
        subboard_detection_adapter_clear();
        s_latest_result.count = (uint8_t)((result->count > 255u) ? 255u : result->count);
        update_tracking_summary_from_result(result, -1);
        return;
    }

    update_from_box(&result->boxes[best_idx],
                    (uint8_t)((result->count > 255u) ? 255u : result->count),
                    (uint8_t)best_idx);
    update_tracking_summary_from_result(result, best_idx);
}

void subboard_detection_adapter_update_from_single(const DetectionBox_t *box)
{
    subboard_tracking_summary_t next;
    bool raw_meta_valid = false;
    DetectionResult_t fake_result;

    if ((box == NULL) || !is_publishable_detection_type(box->type)) {
        subboard_detection_adapter_clear();
        return;
    }

    update_from_box(box, 1u, 0u);

    memset(&next, 0, sizeof(next));
    next.valid = 1u;
    next.tracking_state = SUBBOARD_TRACKING_STATE_IDLE;
    next.tracking_flags = SUBBOARD_TRACKING_FLAG_HAS_TARGET;
    next.selected_idx = 0u;
    next.cx = to_i16_clamped((box->x1 + box->x2) / 2);
    next.cy = to_i16_clamped((box->y1 + box->y2) / 2);
    next.x1 = to_i16_clamped(box->x1);
    next.y1 = to_i16_clamped(box->y1);
    next.x2 = to_i16_clamped(box->x2);
    next.y2 = to_i16_clamped(box->y2);
    next.vx = box->vx;
    next.vy = box->vy;
    next.count = 1u;
    next.confidence = box->kf_confidence;
    next.target_id = box->track_id;
    next.miss_count = box->miss_count;

    memset(&fake_result, 0, sizeof(fake_result));
    fake_result.version = DETECTION_PROTOCOL_VERSION;
    decode_tracker_meta_from_box(&next.tracker_state_raw,
                                 &next.tracker_flags_raw,
                                 &raw_meta_valid,
                                 &fake_result,
                                 box,
                                 true);
    if (raw_meta_valid) {
        next.tracking_flags |= SUBBOARD_TRACKING_FLAG_RAW_STATE_VALID;
        if (next.tracker_state_raw == DETECTION_TRACKER_RAW_PREDICTED) {
            next.tracking_flags |= SUBBOARD_TRACKING_FLAG_PREDICTED;
        }
        if (next.tracker_state_raw == DETECTION_TRACKER_RAW_LOST) {
            next.tracking_flags |= SUBBOARD_TRACKING_FLAG_LOST;
        }
    }

    if ((next.tracking_flags & SUBBOARD_TRACKING_FLAG_RAW_STATE_VALID) == 0u && box->miss_count > 0u) {
        next.tracking_flags |= SUBBOARD_TRACKING_FLAG_PREDICTED;
    }
    s_latest_tracking = next;
}

bool subboard_detection_adapter_get_latest(subboard_detection_result_t *out_result)
{
    if (out_result == NULL) {
        return false;
    }

    *out_result = s_latest_result;
    return true;
}

bool subboard_detection_adapter_get_latest_tracking(subboard_tracking_summary_t *out_summary)
{
    if (out_summary == NULL) {
        return false;
    }

    *out_summary = s_latest_tracking;
    return true;
}