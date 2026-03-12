#include "subboard_detection_adapter.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static subboard_detection_result_t s_latest_result;
static bool s_proto_warned = false;
static bool s_v2_logged = false;

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

static bool is_face_like_type(uint8_t raw_type)
{
    DetectionType_t type = detection_type_from_raw(raw_type);
    return (type == DETECTION_TYPE_FACE) || (type == DETECTION_TYPE_UNKNOWN);
}

static int find_best_face(const DetectionResult_t *result)
{
    int best_idx = -1;
    float best_score = 0.0f;

    if ((result->selected_idx >= 0) &&
        (result->selected_idx < (int32_t)result->count) &&
        is_face_like_type(result->boxes[result->selected_idx].type)) {
        return result->selected_idx;
    }

    for (uint32_t index = 0u; index < result->count; index++) {
        const DetectionBox_t *box = &result->boxes[index];

        if (!is_face_like_type(box->type)) {
            continue;
        }

        if ((best_idx < 0) || (box->score > best_score)) {
            best_idx = (int)index;
            best_score = box->score;
        }
    }

    return best_idx;
}

void subboard_detection_adapter_reset(void)
{
    memset(&s_latest_result, 0, sizeof(s_latest_result));
    s_proto_warned = false;
    s_v2_logged = false;
}

void subboard_detection_adapter_clear(void)
{
    memset(&s_latest_result, 0, sizeof(s_latest_result));
}

void subboard_detection_adapter_update_from_multi(const DetectionResult_t *result)
{
    subboard_detection_result_t next;
    int best_idx;
    const DetectionBox_t *box;

    if (!is_valid_detection_result_for_subboard(result)) {
        if ((result != NULL) && !s_proto_warned) {
            printf("[SUB-DSP][WARN] drop result: magic=0x%08lX version=0x%04lX count=%lu\r\n",
                   (unsigned long)result->magic,
                   (unsigned long)result->version,
                   (unsigned long)result->count);
            s_proto_warned = true;
        }
        subboard_detection_adapter_clear();
        return;
    }

    if (((result->version >> 8) == 0x02u) && !s_v2_logged) {
        printf("[SUB-DSP] compatible protocol mode: DSP version=0x%04lX\r\n",
               (unsigned long)result->version);
        s_v2_logged = true;
    }

    best_idx = find_best_face(result);
    if (best_idx < 0) {
        subboard_detection_adapter_clear();
        s_latest_result.count = (uint8_t)((result->count > 255u) ? 255u : result->count);
        return;
    }

    box = &result->boxes[best_idx];
    memset(&next, 0, sizeof(next));

    next.valid = 1u;
    next.count = (uint8_t)((result->count > 255u) ? 255u : result->count);
    next.type = box->type;
    next.selected_idx = (uint8_t)best_idx;
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

void subboard_detection_adapter_update_from_single(const DetectionBox_t *box)
{
    subboard_detection_result_t next;

    if ((box == NULL) || !is_face_like_type(box->type)) {
        subboard_detection_adapter_clear();
        return;
    }

    memset(&next, 0, sizeof(next));
    next.valid = 1u;
    next.count = 1u;
    next.type = box->type;
    next.selected_idx = 0u;
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

bool subboard_detection_adapter_get_latest(subboard_detection_result_t *out_result)
{
    if (out_result == NULL) {
        return false;
    }

    *out_result = s_latest_result;
    return true;
}