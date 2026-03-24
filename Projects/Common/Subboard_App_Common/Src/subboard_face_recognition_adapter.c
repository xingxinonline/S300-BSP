#include "subboard_face_recognition_adapter.h"

#include <stdint.h>
#include <string.h>

#include "board.h"
#include "subboard_app_identity.h"
#include "subboard_face_recognition_proto.h"
#include "subboard_log.h"

#define SUBBOARD_FR_TEMPLATE_SLOT_COUNT            3u
#define SUBBOARD_FR_MATCH_THRESHOLD_SCORE         62
#define SUBBOARD_FR_UNCERTAIN_THRESHOLD_SCORE     52
#define SUBBOARD_FR_TEMPLATE_ENROLL_MIN_CONF      70u
#define SUBBOARD_FR_TEMPLATE_DUPLICATE_SCORE      88

static subboard_detection_result_t s_latest_result;
static subboard_tracking_summary_t s_latest_tracking;
static bool s_template_valid[SUBBOARD_FR_TEMPLATE_SLOT_COUNT];
static uint8_t s_template_quality[SUBBOARD_FR_TEMPLATE_SLOT_COUNT];
static int8_t s_template_features[SUBBOARD_FR_TEMPLATE_SLOT_COUNT][SUBBOARD_FR_FEATURE_DIMENSION];
static uint8_t s_template_count = 0u;
static bool s_proto_warned = false;

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
    int32_t value;

    if (score <= 1.0f) {
        value = (int32_t)(score * 100.0f + 0.5f);
    } else {
        value = (int32_t)(score + 0.5f);
    }

    if (value < 0) {
        value = 0;
    }
    if (value > 100) {
        value = 100;
    }
    return (uint8_t)value;
}

static int calculate_single_similarity(const int8_t *feature1, const int8_t *feature2)
{
    int32_t dot = 0;
    uint32_t index;

    for (index = 0u; index < SUBBOARD_FR_FEATURE_DIMENSION; index++) {
        dot += (int32_t)feature1[index] * (int32_t)feature2[index];
    }

    dot = (dot * 100) / 16129;
    if (dot < 0) {
        dot = 0;
    }
    if (dot > 100) {
        dot = 100;
    }
    return (int)dot;
}

static void clear_feature_templates(void)
{
    memset(s_template_valid, 0, sizeof(s_template_valid));
    memset(s_template_quality, 0, sizeof(s_template_quality));
    memset(s_template_features, 0, sizeof(s_template_features));
    s_template_count = 0u;
}

static int find_best_template_similarity(const int8_t *feature, uint8_t *out_slot)
{
    int best_similarity = -1;
    uint32_t slot;

    if (out_slot != NULL) {
        *out_slot = 0u;
    }

    if (feature == NULL) {
        return -1;
    }

    for (slot = 0u; slot < SUBBOARD_FR_TEMPLATE_SLOT_COUNT; slot++) {
        int similarity;

        if (!s_template_valid[slot]) {
            continue;
        }

        similarity = calculate_single_similarity(s_template_features[slot], feature);
        if (similarity > best_similarity) {
            best_similarity = similarity;
            if (out_slot != NULL) {
                *out_slot = (uint8_t)slot;
            }
        }
    }

    return best_similarity;
}

static bool try_enroll_feature_template(const int8_t *feature, uint8_t confidence, int best_similarity)
{
    uint32_t slot;

    if ((feature == NULL) || (confidence < SUBBOARD_FR_TEMPLATE_ENROLL_MIN_CONF)) {
        return false;
    }

    if ((s_template_count != 0u) && (best_similarity >= SUBBOARD_FR_TEMPLATE_DUPLICATE_SCORE)) {
        return false;
    }

    for (slot = 0u; slot < SUBBOARD_FR_TEMPLATE_SLOT_COUNT; slot++) {
        if (s_template_valid[slot]) {
            continue;
        }

        memcpy(s_template_features[slot], feature, SUBBOARD_FR_FEATURE_DIMENSION);
        s_template_valid[slot] = true;
        s_template_quality[slot] = confidence;
        s_template_count++;
        SUB_LOG_INFO(SUBBOARD_APP_TAG " face template enrolled slot=%lu quality=%u total=%u\r\n",
                     (unsigned long)slot,
                     (unsigned)confidence,
                     (unsigned)s_template_count);
        return true;
    }

    return false;
}

static bool result_is_valid(const subboard_fr_detection_result_t *result)
{
    return (result != NULL) &&
           (result->magic == SUBBOARD_FR_RESULT_MAGIC) &&
           (result->version >= SUBBOARD_FR_PROTOCOL_VERSION) &&
           (result->count <= SUBBOARD_FR_MAX_DETECTION_COUNT);
}

static bool selected_face_is_valid(const subboard_fr_detection_result_t *result)
{
    return (result != NULL) &&
           (result->selected_idx >= 0) &&
           ((uint32_t)result->selected_idx < result->count);
}

static bool result_has_valid_feature(const subboard_fr_detection_result_t *result)
{
    return selected_face_is_valid(result) &&
           ((result->feature_flags & SUBBOARD_FR_RESULT_FLAG_HAS_FEATURE) != 0u) &&
           (result->feature_dim == SUBBOARD_FR_FEATURE_DIMENSION);
}

static int find_best_face_index(const subboard_fr_detection_result_t *result)
{
    uint32_t index;
    int best_index = -1;
    int64_t best_center_distance = 0;
    int32_t frame_center_x = (int32_t)BOARD_DISPLAY_WIDTH / 2;
    int32_t frame_center_y = (int32_t)BOARD_DISPLAY_HEIGHT / 2;

    if (selected_face_is_valid(result)) {
        return result->selected_idx;
    }

    if (result == NULL) {
        return -1;
    }

    for (index = 0u; index < result->count; index++) {
        const subboard_fr_detection_box_t *box = &result->boxes[index];
        int32_t box_center_x;
        int32_t box_center_y;
        int32_t dx;
        int32_t dy;
        int64_t center_distance;

        if (box->type != 1u) {
            continue;
        }

        box_center_x = (box->x1 + box->x2) / 2;
        box_center_y = (box->y1 + box->y2) / 2;
        dx = box_center_x - frame_center_x;
        dy = box_center_y - frame_center_y;
        center_distance = ((int64_t)dx * (int64_t)dx) + ((int64_t)dy * (int64_t)dy);

        if ((best_index < 0) ||
            (center_distance < best_center_distance) ||
            ((center_distance == best_center_distance) &&
             (box->score > result->boxes[(uint32_t)best_index].score))) {
            best_index = (int)index;
            best_center_distance = center_distance;
        }
    }

    return best_index;
}

static void update_tracking_summary(const subboard_fr_detection_result_t *result, int face_index)
{
    memset(&s_latest_tracking, 0, sizeof(s_latest_tracking));

    if (result == NULL) {
        return;
    }

    s_latest_tracking.frame_id = result->frame_id;
    s_latest_tracking.count = (uint8_t)((result->count > 255u) ? 255u : result->count);
    s_latest_tracking.selected_idx = (int8_t)((face_index >= 0) ? face_index : result->selected_idx);

    if ((face_index >= 0) && ((uint32_t)face_index < result->count)) {
        const subboard_fr_detection_box_t *box = &result->boxes[face_index];

        s_latest_tracking.valid = 1u;
        s_latest_tracking.tracking_state = SUBBOARD_TRACKING_STATE_IDLE;
        s_latest_tracking.tracking_flags = SUBBOARD_TRACKING_FLAG_HAS_TARGET;
        s_latest_tracking.cx = to_i16_clamped((box->x1 + box->x2) / 2);
        s_latest_tracking.cy = to_i16_clamped((box->y1 + box->y2) / 2);
        s_latest_tracking.x1 = to_i16_clamped(box->x1);
        s_latest_tracking.y1 = to_i16_clamped(box->y1);
        s_latest_tracking.x2 = to_i16_clamped(box->x2);
        s_latest_tracking.y2 = to_i16_clamped(box->y2);
        s_latest_tracking.vx = box->vx;
        s_latest_tracking.vy = box->vy;
        s_latest_tracking.confidence = box->kf_confidence;
        s_latest_tracking.target_id = box->track_id;
    }
}

static void update_latest_result(const subboard_fr_detection_result_t *result, int face_index)
{
    subboard_detection_result_t next;
    uint8_t verify_state = SUBBOARD_FACE_VERIFY_STATE_NONE;
    uint8_t verify_flags = 0u;
    uint8_t verify_score = 0u;

    memset(&next, 0, sizeof(next));

    if ((result == NULL) || (face_index < 0) || ((uint32_t)face_index >= result->count)) {
        s_latest_result = next;
        return;
    }

    {
        const subboard_fr_detection_box_t *box = &result->boxes[face_index];
        bool feature_valid = result_has_valid_feature(result);
        int best_similarity = -1;

        next.valid = 1u;
        next.count = (uint8_t)((result->count > 255u) ? 255u : result->count);
        next.type = box->type;
        next.selected_idx = (uint8_t)face_index;
        next.x1 = to_i16_clamped(box->x1);
        next.y1 = to_i16_clamped(box->y1);
        next.x2 = to_i16_clamped(box->x2);
        next.y2 = to_i16_clamped(box->y2);
        next.cx = to_i16_clamped((box->x1 + box->x2) / 2);
        next.cy = to_i16_clamped((box->y1 + box->y2) / 2);
        next.vx = box->vx;
        next.vy = box->vy;
        next.face_id = 0u;
        next.confidence = score_to_u8(box->score);

        if (s_template_count != 0u) {
            verify_flags |= SUBBOARD_DETECTION_VERIFY_FLAG_ANCHOR_VALID;
        }
        if (feature_valid) {
            verify_flags |= SUBBOARD_DETECTION_VERIFY_FLAG_FEATURE_VALID;
            best_similarity = find_best_template_similarity(result->feature_vector, NULL);

            if ((s_template_count < SUBBOARD_FR_TEMPLATE_SLOT_COUNT) &&
                try_enroll_feature_template(result->feature_vector, next.confidence, best_similarity)) {
                verify_flags |= SUBBOARD_DETECTION_VERIFY_FLAG_ANCHOR_VALID;
                best_similarity = find_best_template_similarity(result->feature_vector, NULL);
            }
        }

        if (s_template_count == 0u) {
            verify_state = SUBBOARD_FACE_VERIFY_STATE_WAIT_ANCHOR;
        } else if (!feature_valid) {
            verify_state = SUBBOARD_FACE_VERIFY_STATE_UNCERTAIN;
        } else {
            verify_score = (uint8_t)((best_similarity < 0) ? 0 : best_similarity);
            if (best_similarity >= SUBBOARD_FR_MATCH_THRESHOLD_SCORE) {
                verify_state = SUBBOARD_FACE_VERIFY_STATE_MATCH;
                next.face_id = 1u;
            } else if (best_similarity >= SUBBOARD_FR_UNCERTAIN_THRESHOLD_SCORE) {
                verify_state = SUBBOARD_FACE_VERIFY_STATE_UNCERTAIN;
            } else {
                verify_state = SUBBOARD_FACE_VERIFY_STATE_NO_MATCH;
            }
        }
    }

    subboard_detection_result_set_verify_state(&next, verify_state);
    subboard_detection_result_set_verify_score(&next, verify_score);
    subboard_detection_result_set_verify_flags(&next, verify_flags);
    s_latest_result = next;
}

void subboard_face_recognition_adapter_reset(void)
{
    memset(&s_latest_result, 0, sizeof(s_latest_result));
    memset(&s_latest_tracking, 0, sizeof(s_latest_tracking));
    clear_feature_templates();
    s_proto_warned = false;
}

void subboard_face_recognition_adapter_clear(void)
{
    memset(&s_latest_result, 0, sizeof(s_latest_result));
    memset(&s_latest_tracking, 0, sizeof(s_latest_tracking));
}

void subboard_face_recognition_adapter_reset_session(void)
{
    clear_feature_templates();
    memset(&s_latest_result, 0, sizeof(s_latest_result));
    subboard_detection_result_set_verify_state(&s_latest_result, SUBBOARD_FACE_VERIFY_STATE_WAIT_ANCHOR);
    SUB_LOG_INFO(SUBBOARD_APP_TAG " face verify session reset\r\n");
}

void subboard_face_recognition_adapter_update_from_multi(const void *result_payload)
{
    const subboard_fr_detection_result_t *result = (const subboard_fr_detection_result_t *)result_payload;
    int face_index;

    if (!result_is_valid(result)) {
        if ((result != NULL) && !s_proto_warned) {
            SUB_LOG_WARN(SUBBOARD_APP_TAG " FR result drop: magic=0x%08lX version=0x%04lX count=%lu\r\n",
                         (unsigned long)result->magic,
                         (unsigned long)result->version,
                         (unsigned long)result->count);
            s_proto_warned = true;
        }
        subboard_face_recognition_adapter_clear();
        return;
    }

    face_index = find_best_face_index(result);
    update_tracking_summary(result, face_index);
    update_latest_result(result, face_index);
}

bool subboard_face_recognition_adapter_get_latest(subboard_detection_result_t *out_result)
{
    if (out_result == NULL) {
        return false;
    }

    *out_result = s_latest_result;
    return true;
}

bool subboard_face_recognition_adapter_get_latest_tracking(subboard_tracking_summary_t *out_summary)
{
    if (out_summary == NULL) {
        return false;
    }

    *out_summary = s_latest_tracking;
    return true;
}