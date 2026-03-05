#include "card1_detection.h"

#include "mailbox.h"
#include "mailbox_proto.h"
#include "detection_proto.h"

#include <string.h>

static card1_detection_result_t g_latest;

static int16_t to_i16_clamped(int32_t v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

static uint8_t score_to_u8(float score)
{
    int32_t v = (int32_t)(score * 100.0f + 0.5f);
    if (v < 0) {
        v = 0;
    }
    if (v > 100) {
        v = 100;
    }
    return (uint8_t)v;
}

static int find_best_human(const DetectionResult_t *result)
{
    int best_idx = -1;
    float best_score = 0.0f;

    if (result->selected_idx >= 0 &&
        result->selected_idx < (int32_t)result->count &&
        result->boxes[result->selected_idx].type == (uint8_t)DETECTION_TYPE_PERSON) {
        return result->selected_idx;
    }

    for (uint32_t i = 0; i < result->count; i++) {
        const DetectionBox_t *box = &result->boxes[i];
        if (box->type != (uint8_t)DETECTION_TYPE_PERSON) {
            continue;
        }
        if (best_idx < 0 || box->score > best_score) {
            best_idx = (int)i;
            best_score = box->score;
        }
    }

    return best_idx;
}

static void update_from_multi(const DetectionResult_t *result)
{
    card1_detection_result_t next;
    int idx;
    const DetectionBox_t *box;

    if (!DETECTION_RESULT_IS_VALID(result)) {
        memset(&g_latest, 0, sizeof(g_latest));
        return;
    }

    idx = find_best_human(result);
    if (idx < 0) {
        memset(&g_latest, 0, sizeof(g_latest));
        g_latest.count = (uint8_t)((result->count > 255u) ? 255u : result->count);
        return;
    }

    box = &result->boxes[idx];
    memset(&next, 0, sizeof(next));

    next.valid = 1u;
    next.count = (uint8_t)((result->count > 255u) ? 255u : result->count);
    next.type = (uint8_t)DETECTION_TYPE_PERSON;
    next.selected_idx = (uint8_t)idx;
    next.x1 = to_i16_clamped(box->x1);
    next.y1 = to_i16_clamped(box->y1);
    next.x2 = to_i16_clamped(box->x2);
    next.y2 = to_i16_clamped(box->y2);
    next.cx = to_i16_clamped((box->x1 + box->x2) / 2);
    next.cy = to_i16_clamped((box->y1 + box->y2) / 2);
    next.vx = box->vx;
    next.vy = box->vy;
    next.gesture_type = 0u;
    next.face_id = box->track_id;
    next.confidence = score_to_u8(box->score);

    g_latest = next;
}

static void update_from_single(const DetectionBox_t *box)
{
    card1_detection_result_t next;

    if (box == NULL || box->type != (uint8_t)DETECTION_TYPE_PERSON) {
        memset(&g_latest, 0, sizeof(g_latest));
        return;
    }

    memset(&next, 0, sizeof(next));
    next.valid = 1u;
    next.count = 1u;
    next.type = (uint8_t)DETECTION_TYPE_PERSON;
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

    g_latest = next;
}

void card1_detection_init(void)
{
    memset(&g_latest, 0, sizeof(g_latest));
}

void card1_detection_poll(void)
{
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t offset = MAILBOX_GET_PAYLOAD(msg);
        uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)offset;

        if (type == MAILBOX_MSG_TYPE_MULTI) {
            update_from_multi((const DetectionResult_t *)addr);
        } else if (type == MAILBOX_MSG_TYPE_SINGLE) {
            update_from_single((const DetectionBox_t *)addr);
        } else if (type == MAILBOX_MSG_TYPE_NO_RESULT) {
            memset(&g_latest, 0, sizeof(g_latest));
        }
    }
}

bool card1_detection_get_latest(card1_detection_result_t *out)
{
    if (out == NULL) {
        return false;
    }

    *out = g_latest;
    return true;
}
