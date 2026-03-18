#include "app_card1_result_handler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "detection_proto.h"
#include "master_log.h"

#define CARD1_OVERLAY_CLEAR_DEBOUNCE_MS 120u

static app_card1_result_handler_ops_t s_ops;
static bool s_ops_ready = false;
static bool s_overlay_active = false;
static uint32_t s_overlay_invalid_since_ms = 0u;
static subboard_detection_result_t s_last_result;

static uint32_t card1_result_handler_now_ms(void)
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

int app_card1_result_handler_init(const app_card1_result_handler_ops_t *ops)
{
    if ((ops == NULL) || (ops->millis_fn == NULL)) {
        return -1;
    }

    s_ops = *ops;
    s_ops_ready = true;
    app_card1_result_handler_reset();
    return 0;
}

void app_card1_result_handler_reset(void)
{
    s_overlay_active = false;
    s_overlay_invalid_since_ms = 0u;
    memset(&s_last_result, 0, sizeof(s_last_result));

    if (s_ops_ready && (s_ops.overlay_clear != NULL)) {
        s_ops.overlay_clear();
    }
}

void app_card1_result_handler_tick(void)
{
    if (!s_ops_ready) {
        return;
    }

    if (s_ops.overlay_tick != NULL) {
        s_ops.overlay_tick();
    }

    if (s_overlay_active && (s_ops.overlay_is_active != NULL) && !s_ops.overlay_is_active()) {
        MASTER_LOG_INFO("[MASTER][CARD1] overlay cleared\r\n");
        s_overlay_active = false;
        s_overlay_invalid_since_ms = 0u;
    }
}

void app_card1_result_handler_handle_result(const subboard_detection_result_t *result)
{
    bool displayable;
    uint32_t now_ms;

    if (!s_ops_ready || (result == NULL)) {
        return;
    }

    displayable = result_is_displayable(result);
    now_ms = card1_result_handler_now_ms();

    if (displayable) {
        s_overlay_invalid_since_ms = 0u;
    } else if (s_overlay_active) {
        if (s_overlay_invalid_since_ms == 0u) {
            s_overlay_invalid_since_ms = now_ms;
        }

        if ((uint32_t)(now_ms - s_overlay_invalid_since_ms) >= CARD1_OVERLAY_CLEAR_DEBOUNCE_MS) {
            if (s_ops.overlay_clear != NULL) {
                s_ops.overlay_clear();
            }
            if (s_overlay_active && (s_ops.overlay_is_active != NULL) && !s_ops.overlay_is_active()) {
                MASTER_LOG_INFO("[MASTER][CARD1] overlay cleared\r\n");
                s_overlay_active = false;
            }
            s_overlay_invalid_since_ms = 0u;
        }
    }

    if (memcmp(result, &s_last_result, sizeof(*result)) == 0) {
        return;
    }

    s_last_result = *result;
    if (displayable) {
        if (s_ops.overlay_draw != NULL) {
            s_ops.overlay_draw(result);
        }
        if (!s_overlay_active) {
            MASTER_LOG_INFO("[MASTER][CARD1] overlay shown: type=%s count=%u conf=%u box=(%d,%d)-(%d,%d) id=%u\r\n",
                            detection_type_label(result->type),
                            (unsigned)result->count,
                            (unsigned)result->confidence,
                            (int)result->x1,
                            (int)result->y1,
                            (int)result->x2,
                            (int)result->y2,
                            (unsigned)result->face_id);
            s_overlay_active = true;
        }
        MASTER_LOG_DEBUG("[MASTER][CARD1] detection result: type=%s count=%u conf=%u box=(%d,%d)-(%d,%d) id=%u\r\n",
                         detection_type_label(result->type),
                         (unsigned)result->count,
                         (unsigned)result->confidence,
                         (int)result->x1,
                         (int)result->y1,
                         (int)result->x2,
                         (int)result->y2,
                         (unsigned)result->face_id);
    } else {
        MASTER_LOG_DEBUG("[MASTER][CARD1] detection result cleared\r\n");
    }
}