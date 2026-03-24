#include "app_card2_result_handler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "detection_proto.h"
#include "master_log.h"

#define CARD2_RESULT_LOG_MIN_INTERVAL_MS 1500u

static app_card2_result_handler_ops_t s_ops;
static bool s_ops_ready = false;
static bool s_result_active = false;
static uint32_t s_last_result_log_ms = 0u;
static subboard_detection_result_t s_last_result;
static app_card2_verify_snapshot_t s_last_snapshot;
static bool s_have_snapshot = false;

const char *app_card2_result_handler_verify_state_name(uint8_t verify_state)
{
    switch (verify_state) {
    case SUBBOARD_FACE_VERIFY_STATE_WAIT_ANCHOR: return "WAIT_ANCHOR";
    case SUBBOARD_FACE_VERIFY_STATE_UNCERTAIN: return "UNCERTAIN";
    case SUBBOARD_FACE_VERIFY_STATE_MATCH: return "MATCH";
    case SUBBOARD_FACE_VERIFY_STATE_NO_MATCH: return "NO_MATCH";
    case SUBBOARD_FACE_VERIFY_STATE_NONE:
    default:
        return "NONE";
    }
}

static uint32_t card2_result_handler_now_ms(void)
{
    return (s_ops.millis_fn != NULL) ? s_ops.millis_fn() : 0u;
}

static const char *detection_type_label(uint8_t raw_type)
{
    return detection_type_name(detection_type_from_raw(raw_type));
}

static bool result_is_displayable(const subboard_detection_result_t *result)
{
    if (result == NULL) {
        return false;
    }

    return (result->valid != 0u) &&
           (result->count != 0u) &&
           (result->confidence != 0u) &&
           (result->x2 > result->x1) &&
           (result->y2 > result->y1);
}

int app_card2_result_handler_init(const app_card2_result_handler_ops_t *ops)
{
    if ((ops == NULL) || (ops->millis_fn == NULL)) {
        return -1;
    }

    s_ops = *ops;
    s_ops_ready = true;
    app_card2_result_handler_reset();
    return 0;
}

void app_card2_result_handler_reset(void)
{
    s_result_active = false;
    s_last_result_log_ms = 0u;
    memset(&s_last_result, 0, sizeof(s_last_result));
    memset(&s_last_snapshot, 0, sizeof(s_last_snapshot));
    s_have_snapshot = false;
}

void app_card2_result_handler_tick(void)
{
    (void)s_ops_ready;
}

void app_card2_result_handler_handle_result(const subboard_detection_result_t *result)
{
    bool displayable;
    uint32_t now_ms;
    uint8_t verify_state;
    uint8_t verify_score;
    uint8_t verify_flags;

    if (!s_ops_ready || (result == NULL)) {
        return;
    }

    displayable = result_is_displayable(result);
    now_ms = card2_result_handler_now_ms();
    verify_state = subboard_detection_result_get_verify_state(result);
    verify_score = subboard_detection_result_get_verify_score(result);
    verify_flags = subboard_detection_result_get_verify_flags(result);

    if (memcmp(result, &s_last_result, sizeof(*result)) == 0) {
        return;
    }

    s_last_result = *result;
    s_last_snapshot.updated_ms = now_ms;
    s_last_snapshot.detection_active = displayable;
    s_last_snapshot.verify_state = verify_state;
    s_last_snapshot.verify_score = verify_score;
    s_last_snapshot.verify_flags = verify_flags;
    s_last_snapshot.face_id = result->face_id;
    s_last_snapshot.confidence = result->confidence;
    s_last_snapshot.cx = result->cx;
    s_last_snapshot.cy = result->cy;
    s_last_snapshot.x1 = result->x1;
    s_last_snapshot.y1 = result->y1;
    s_last_snapshot.x2 = result->x2;
    s_last_snapshot.y2 = result->y2;
    s_have_snapshot = true;

    if (displayable) {
        if (!s_result_active) {
            if ((s_last_result_log_ms == 0u) ||
                ((uint32_t)(now_ms - s_last_result_log_ms) >= CARD2_RESULT_LOG_MIN_INTERVAL_MS)) {
                MASTER_LOG_INFO("[MASTER][CARD2] result active: type=%s count=%u conf=%u box=(%d,%d)-(%d,%d) id=%u verify=%s score=%u flags=0x%02X\r\n",
                                detection_type_label(result->type),
                                (unsigned)result->count,
                                (unsigned)result->confidence,
                                (int)result->x1,
                                (int)result->y1,
                                (int)result->x2,
                                (int)result->y2,
                                (unsigned)result->face_id,
                                app_card2_result_handler_verify_state_name(verify_state),
                                (unsigned)verify_score,
                                (unsigned)verify_flags);
                s_last_result_log_ms = now_ms;
            }
            s_result_active = true;
        }
        MASTER_LOG_DEBUG("[MASTER][CARD2] detection result: type=%s count=%u conf=%u box=(%d,%d)-(%d,%d) id=%u verify=%s score=%u flags=0x%02X\r\n",
                         detection_type_label(result->type),
                         (unsigned)result->count,
                         (unsigned)result->confidence,
                         (int)result->x1,
                         (int)result->y1,
                         (int)result->x2,
                         (int)result->y2,
                         (unsigned)result->face_id,
                         app_card2_result_handler_verify_state_name(verify_state),
                         (unsigned)verify_score,
                         (unsigned)verify_flags);
    } else {
        if (s_result_active) {
            if ((s_last_result_log_ms == 0u) ||
                ((uint32_t)(now_ms - s_last_result_log_ms) >= CARD2_RESULT_LOG_MIN_INTERVAL_MS)) {
                MASTER_LOG_INFO("[MASTER][CARD2] result idle verify=%s score=%u flags=0x%02X\r\n",
                                app_card2_result_handler_verify_state_name(verify_state),
                                (unsigned)verify_score,
                                (unsigned)verify_flags);
                s_last_result_log_ms = now_ms;
            }
            s_result_active = false;
        }
        MASTER_LOG_DEBUG("[MASTER][CARD2] detection result cleared verify=%s score=%u flags=0x%02X\r\n",
                         app_card2_result_handler_verify_state_name(verify_state),
                         (unsigned)verify_score,
                         (unsigned)verify_flags);
    }
}

bool app_card2_result_handler_get_snapshot(app_card2_verify_snapshot_t *out_snapshot)
{
    if ((out_snapshot == NULL) || !s_have_snapshot) {
        return false;
    }

    *out_snapshot = s_last_snapshot;
    return true;
}