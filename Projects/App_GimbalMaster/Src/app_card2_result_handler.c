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
}

void app_card2_result_handler_tick(void)
{
    (void)s_ops_ready;
}

void app_card2_result_handler_handle_result(const subboard_detection_result_t *result)
{
    bool displayable;
    uint32_t now_ms;

    if (!s_ops_ready || (result == NULL)) {
        return;
    }

    displayable = result_is_displayable(result);
    now_ms = card2_result_handler_now_ms();

    if (memcmp(result, &s_last_result, sizeof(*result)) == 0) {
        return;
    }

    s_last_result = *result;
    if (displayable) {
        if (!s_result_active) {
            if ((s_last_result_log_ms == 0u) ||
                ((uint32_t)(now_ms - s_last_result_log_ms) >= CARD2_RESULT_LOG_MIN_INTERVAL_MS)) {
                MASTER_LOG_INFO("[MASTER][CARD2] result active: type=%s count=%u conf=%u box=(%d,%d)-(%d,%d) id=%u\r\n",
                                detection_type_label(result->type),
                                (unsigned)result->count,
                                (unsigned)result->confidence,
                                (int)result->x1,
                                (int)result->y1,
                                (int)result->x2,
                                (int)result->y2,
                                (unsigned)result->face_id);
                s_last_result_log_ms = now_ms;
            }
            s_result_active = true;
        }
        MASTER_LOG_DEBUG("[MASTER][CARD2] detection result: type=%s count=%u conf=%u box=(%d,%d)-(%d,%d) id=%u\r\n",
                         detection_type_label(result->type),
                         (unsigned)result->count,
                         (unsigned)result->confidence,
                         (int)result->x1,
                         (int)result->y1,
                         (int)result->x2,
                         (int)result->y2,
                         (unsigned)result->face_id);
    } else {
        if (s_result_active) {
            if ((s_last_result_log_ms == 0u) ||
                ((uint32_t)(now_ms - s_last_result_log_ms) >= CARD2_RESULT_LOG_MIN_INTERVAL_MS)) {
                MASTER_LOG_INFO("[MASTER][CARD2] result idle\r\n");
                s_last_result_log_ms = now_ms;
            }
            s_result_active = false;
        }
        MASTER_LOG_DEBUG("[MASTER][CARD2] detection result cleared\r\n");
    }
}