#include "app_card3_result_handler.h"

#include <stdbool.h>
#include <stddef.h>

#include "app_action_dispatch.h"
#include "app_runtime_state.h"
#include "detection_proto.h"
#include "master_log.h"

#define CARD3_GESTURE_TYPE_NONE   0u
#define CARD3_GESTURE_TYPE_PALM   1u
#define CARD3_GESTURE_TYPE_PEACE  2u

#define CARD3_GESTURE_MIN_CONFIDENCE  60u
#define CARD3_GESTURE_REARM_MS        800u

static app_card3_result_handler_millis_fn_t s_millis_fn = NULL;
static DetectionType_t s_latched_gesture = DETECTION_TYPE_UNKNOWN;
static uint32_t s_last_dispatch_ms = 0u;

static uint32_t card3_now_ms(void)
{
    return (s_millis_fn != NULL) ? s_millis_fn() : 0u;
}

static const char *card3_gesture_name(DetectionType_t type)
{
    return detection_type_name(type);
}

static DetectionType_t card3_map_gesture_type(const subboard_detection_result_t *result)
{
    DetectionType_t result_type;

    if (result == NULL) {
        return DETECTION_TYPE_UNKNOWN;
    }

    result_type = detection_type_from_raw(result->type);
    if (result_type == DETECTION_TYPE_PALM || result_type == DETECTION_TYPE_PEACE) {
        return result_type;
    }

    switch (result->gesture_type) {
    case CARD3_GESTURE_TYPE_PALM:
        return DETECTION_TYPE_PALM;
    case CARD3_GESTURE_TYPE_PEACE:
        return DETECTION_TYPE_PEACE;
    case CARD3_GESTURE_TYPE_NONE:
    default:
        return DETECTION_TYPE_UNKNOWN;
    }
}

static bool card3_result_has_valid_gesture(const subboard_detection_result_t *result,
                                           DetectionType_t *gesture_type)
{
    DetectionType_t mapped_gesture;

    if ((result == NULL) || (gesture_type == NULL)) {
        return false;
    }

    if ((result->valid == 0u) || (result->count == 0u) || (result->confidence < CARD3_GESTURE_MIN_CONFIDENCE)) {
        return false;
    }

    mapped_gesture = card3_map_gesture_type(result);
    if (!detection_type_is_gesture(mapped_gesture) || (mapped_gesture == DETECTION_TYPE_GESTURE)) {
        return false;
    }

    *gesture_type = mapped_gesture;
    return true;
}

static void card3_dispatch_gesture_action(DetectionType_t gesture_type, uint8_t confidence)
{
    app_action_t action = {
        .type = APP_ACTION_NONE,
        .source = APP_ACTION_SOURCE_GESTURE,
        .source_id = (uint8_t)gesture_type,
        .confidence = confidence,
        .chunk_idx = 0u,
    };

    switch (gesture_type) {
    case DETECTION_TYPE_PALM:
        action.type = app_runtime_state_is_tracking() ? APP_ACTION_TRACK_STOP : APP_ACTION_TRACK_START;
        break;

    case DETECTION_TYPE_PEACE:
        action.type = APP_ACTION_PHOTO;
        break;

    default:
        return;
    }

    MASTER_LOG_INFO("[MASTER][CARD3] gesture=%s conf=%u -> action=%u\r\n",
                    card3_gesture_name(gesture_type),
                    (unsigned)confidence,
                    (unsigned)action.type);
    app_action_dispatch(&action);
}

void app_card3_result_handler_init(app_card3_result_handler_millis_fn_t millis_fn)
{
    s_millis_fn = millis_fn;
    s_latched_gesture = DETECTION_TYPE_UNKNOWN;
    s_last_dispatch_ms = 0u;
}

void app_card3_result_handler_handle_result(const subboard_detection_result_t *result)
{
    DetectionType_t gesture_type = DETECTION_TYPE_UNKNOWN;
    uint32_t now_ms;

    if (!card3_result_has_valid_gesture(result, &gesture_type)) {
        s_latched_gesture = DETECTION_TYPE_UNKNOWN;
        return;
    }

    if (gesture_type == s_latched_gesture) {
        return;
    }

    now_ms = card3_now_ms();
    if (((uint32_t)(now_ms - s_last_dispatch_ms) < CARD3_GESTURE_REARM_MS) &&
        (s_latched_gesture != DETECTION_TYPE_UNKNOWN)) {
        s_latched_gesture = gesture_type;
        return;
    }

    card3_dispatch_gesture_action(gesture_type, result->confidence);
    s_latched_gesture = gesture_type;
    s_last_dispatch_ms = now_ms;
}

void app_card3_result_handler_notify_offline(void)
{
    s_latched_gesture = DETECTION_TYPE_UNKNOWN;
}