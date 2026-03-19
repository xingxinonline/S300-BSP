#include "app_card1_result_handler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "detection_proto.h"
#include "app_gimbal_tracking_input.h"
#include "master_log.h"

#define CARD1_OVERLAY_LOG_MIN_INTERVAL_MS 1500u

static app_card1_result_handler_ops_t s_ops;
static bool s_ops_ready = false;
static bool s_overlay_active = false;
static uint32_t s_last_overlay_log_ms = 0u;
static subboard_detection_result_t s_last_result;

static uint32_t card1_result_handler_now_ms(void)
{
    return (s_ops.millis_fn != NULL) ? s_ops.millis_fn() : 0u;
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
    if (app_gimbal_tracking_input_init(ops->millis_fn) != 0) {
        s_ops_ready = false;
        return -1;
    }
    app_card1_result_handler_reset();
    return 0;
}

void app_card1_result_handler_reset(void)
{
    s_overlay_active = false;
    s_last_overlay_log_ms = 0u;
    memset(&s_last_result, 0, sizeof(s_last_result));
    app_gimbal_tracking_input_reset();

    if (s_ops_ready && (s_ops.overlay_clear != NULL)) {
        s_ops.overlay_clear();
    }
}

void app_card1_result_handler_tick(void)
{
    app_gimbal_tracking_input_output_t output;
    bool overlay_now_active = false;
    uint32_t now_ms;

    if (!s_ops_ready) {
        return;
    }

    now_ms = card1_result_handler_now_ms();

    app_gimbal_tracking_input_tick();

    if (s_ops.overlay_tick != NULL) {
        s_ops.overlay_tick();
    }

    if (s_ops.overlay_is_active != NULL) {
        overlay_now_active = s_ops.overlay_is_active();
    }

    app_gimbal_tracking_input_get_output(&output);
    if (!s_overlay_active && overlay_now_active) {
        if ((s_last_overlay_log_ms == 0u) ||
            ((uint32_t)(now_ms - s_last_overlay_log_ms) >= CARD1_OVERLAY_LOG_MIN_INTERVAL_MS)) {
            MASTER_LOG_INFO("[MASTER][CARD1][OVL] source=%s frame=%lu target=%u selected=%u effective=%s raw_tracker=%s raw_flags=0x%02X id=%u conf=%u miss=%u pred=%u box=(%ld,%ld)-(%ld,%ld)\r\n",
                            output.from_tracking_summary ? "summary" : "result-fallback",
                            (unsigned long)output.frame_id,
                            output.valid ? 1u : 0u,
                            (unsigned)output.selected_idx,
                            app_gimbal_tracking_input_view_name(output.effective_view),
                            output.raw_state_valid ? app_gimbal_tracking_input_raw_tracker_name(output.tracker_state_raw) : "UNKNOWN",
                            (unsigned)output.tracker_flags_raw,
                            (unsigned)output.target_id,
                            (unsigned)output.confidence,
                            (unsigned)output.miss_count,
                            output.predicted ? 1u : 0u,
                            (long)output.target_x1,
                            (long)output.target_y1,
                            (long)output.target_x2,
                            (long)output.target_y2);
            s_last_overlay_log_ms = now_ms;
        }
    } else if (s_overlay_active && !overlay_now_active) {
        if ((s_last_overlay_log_ms == 0u) ||
            ((uint32_t)(now_ms - s_last_overlay_log_ms) >= CARD1_OVERLAY_LOG_MIN_INTERVAL_MS)) {
            MASTER_LOG_INFO("[MASTER][CARD1][OVL] cleared source=%s frame=%lu target=%u selected=%u effective=%s raw_tracker=%s raw_flags=0x%02X id=%u conf=%u miss=%u pred=%u\r\n",
                            output.from_tracking_summary ? "summary" : "result-fallback",
                            (unsigned long)output.frame_id,
                            output.valid ? 1u : 0u,
                            (unsigned)output.selected_idx,
                            app_gimbal_tracking_input_view_name(output.effective_view),
                            output.raw_state_valid ? app_gimbal_tracking_input_raw_tracker_name(output.tracker_state_raw) : "UNKNOWN",
                            (unsigned)output.tracker_flags_raw,
                            (unsigned)output.target_id,
                            (unsigned)output.confidence,
                            (unsigned)output.miss_count,
                            output.predicted ? 1u : 0u);
            s_last_overlay_log_ms = now_ms;
        }
    }

    s_overlay_active = overlay_now_active;
}

void app_card1_result_handler_handle_result(const subboard_detection_result_t *result)
{
    if (!s_ops_ready || (result == NULL)) {
        return;
    }

    app_gimbal_tracking_input_handle_card1_result(result);

    if (memcmp(result, &s_last_result, sizeof(*result)) == 0) {
        return;
    }

    s_last_result = *result;

    if (result_is_displayable(result)) {
        if (s_ops.overlay_draw != NULL) {
            s_ops.overlay_draw(result);
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