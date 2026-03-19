#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

#include "human_tracking_target.h"
#include "video.h"

#define HT_GIMBAL_ERR_X_DEADZONE      0.08f
#define HT_GIMBAL_ERR_Y_DEADZONE      0.10f
#define HT_GIMBAL_LOST_FREEZE_MS      600u

static bool s_last_logged_has_target = false;
static uint8_t s_last_tracker_state = 0xFFu;
static uint8_t s_last_tracker_flags = 0xFFu;
static uint8_t s_last_primary_track_id = 0xFFu;
static int32_t s_last_selected_idx = -2;
static uint32_t s_last_target_frame_id = 0u;
static bool s_last_tracking_active = false;
static human_tracking_target_view_t s_last_effective_view = (human_tracking_target_view_t)0xFFu;
static uint32_t s_last_valid_control_ms = 0u;
static float s_last_valid_yaw_cmd = 0.0f;
static float s_last_valid_pitch_cmd = 0.0f;
static bool s_has_last_valid_control = false;
static bool s_freeze_active = false;
static bool s_freeze_timed_out = false;

static float clamp_f32(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static float soft_deadzone(float error, float deadzone)
{
    float half_deadzone = deadzone * 0.5f;
    float abs_error = fabsf(error);

    if (abs_error < half_deadzone) {
        return 0.0f;
    }

    if (abs_error < deadzone) {
        float t = (abs_error - half_deadzone) / half_deadzone;
        return copysignf(t * abs_error, error);
    }

    return error;
}

static human_tracking_target_view_t effective_tracking_view(const human_tracking_overlay_status_t *status,
                                                            bool tracking_active)
{
    if (tracking_active) {
        return ((status != 0) && status->has_target) ?
               HT_TARGET_VIEW_FOLLOWING :
               HT_TARGET_VIEW_FOLLOWING_LOST;
    }

    if ((status != 0) && status->has_target) {
        return HT_TARGET_VIEW_TARGET_READY;
    }

    return HT_TARGET_VIEW_IDLE;
}

void human_tracking_target_reset(void)
{
    s_last_logged_has_target = false;
    s_last_tracker_state = 0xFFu;
    s_last_tracker_flags = 0xFFu;
    s_last_primary_track_id = 0xFFu;
    s_last_selected_idx = -2;
    s_last_target_frame_id = 0u;
    s_last_tracking_active = false;
    s_last_effective_view = (human_tracking_target_view_t)0xFFu;
    s_last_valid_control_ms = 0u;
    s_last_valid_yaw_cmd = 0.0f;
    s_last_valid_pitch_cmd = 0.0f;
    s_has_last_valid_control = false;
    s_freeze_active = false;
    s_freeze_timed_out = false;
}

void human_tracking_target_capture(bool tracking_active, human_tracking_target_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0) {
        return;
    }

    human_tracking_overlay_set_tracking_active(tracking_active);
    human_tracking_overlay_get_status(&out_snapshot->overlay);
    out_snapshot->tracking_active = tracking_active;
    out_snapshot->effective_view = effective_tracking_view(&out_snapshot->overlay, tracking_active);
}

bool human_tracking_target_consume_snapshot(const human_tracking_target_snapshot_t *snapshot,
                                            uint32_t frame_log_every_n)
{
    bool should_log;

    if (snapshot == 0) {
        return false;
    }

    should_log = (snapshot->overlay.has_target != s_last_logged_has_target) ||
                 (snapshot->overlay.selected_idx != s_last_selected_idx) ||
                 (snapshot->overlay.tracker_state != s_last_tracker_state) ||
                 (snapshot->overlay.tracker_flags != s_last_tracker_flags) ||
                 (snapshot->overlay.primary_track_id != s_last_primary_track_id) ||
                 (snapshot->tracking_active != s_last_tracking_active) ||
                 (snapshot->effective_view != s_last_effective_view);

    if (!should_log && (snapshot->overlay.frame_id != 0u) && (s_last_target_frame_id != 0u) &&
        ((snapshot->overlay.frame_id - s_last_target_frame_id) >= frame_log_every_n)) {
        should_log = true;
    }

    if (should_log) {
        s_last_logged_has_target = snapshot->overlay.has_target;
        s_last_selected_idx = snapshot->overlay.selected_idx;
        s_last_target_frame_id = snapshot->overlay.frame_id;
        s_last_tracker_state = snapshot->overlay.tracker_state;
        s_last_tracker_flags = snapshot->overlay.tracker_flags;
        s_last_primary_track_id = snapshot->overlay.primary_track_id;
        s_last_tracking_active = snapshot->tracking_active;
        s_last_effective_view = snapshot->effective_view;
    }

    return should_log;
}

const char *human_tracking_target_view_name(human_tracking_target_view_t view)
{
    switch (view) {
    case HT_TARGET_VIEW_TARGET_READY: return "TARGET_READY";
    case HT_TARGET_VIEW_FOLLOWING: return "FOLLOWING";
    case HT_TARGET_VIEW_FOLLOWING_LOST: return "FOLLOWING_LOST";
    case HT_TARGET_VIEW_IDLE:
    default:
        return "IDLE";
    }
}

void human_tracking_target_fill_gimbal_output(const human_tracking_target_snapshot_t *snapshot,
                                              human_tracking_gimbal_target_t *out_target)
{
    if ((snapshot == 0) || (out_target == 0)) {
        return;
    }

    out_target->frame_id = snapshot->overlay.frame_id;
    out_target->timestamp_ms = snapshot->overlay.timestamp_ms;
    out_target->screen_w = DISP_IMAGE_WIDTH;
    out_target->screen_h = DISP_IMAGE_HEIGHT;
    out_target->screen_cx = (int32_t)DISP_IMAGE_WIDTH / 2;
    out_target->screen_cy = (int32_t)DISP_IMAGE_HEIGHT / 2;
    out_target->target_x1 = snapshot->overlay.primary_x1;
    out_target->target_y1 = snapshot->overlay.primary_y1;
    out_target->target_x2 = snapshot->overlay.primary_x2;
    out_target->target_y2 = snapshot->overlay.primary_y2;
    out_target->target_cx = (snapshot->overlay.primary_x1 + snapshot->overlay.primary_x2) / 2;
    out_target->target_cy = (snapshot->overlay.primary_y1 + snapshot->overlay.primary_y2) / 2;
    out_target->box_w = snapshot->overlay.primary_x2 - snapshot->overlay.primary_x1;
    out_target->box_h = snapshot->overlay.primary_y2 - snapshot->overlay.primary_y1;
    if (out_target->box_w < 0) {
        out_target->box_w = 0;
    }
    if (out_target->box_h < 0) {
        out_target->box_h = 0;
    }
    out_target->box_area = (uint32_t)(out_target->box_w * out_target->box_h);
    out_target->error_x = out_target->target_cx - out_target->screen_cx;
    out_target->error_y = out_target->target_cy - out_target->screen_cy;
    out_target->track_id = snapshot->overlay.primary_track_id;
    out_target->score_pct = snapshot->overlay.primary_score_pct;
    out_target->miss_count = snapshot->overlay.primary_miss_count;
    out_target->valid = snapshot->overlay.has_target;
    out_target->tracking_active = snapshot->tracking_active;
    out_target->predicted = snapshot->overlay.has_target && (snapshot->overlay.primary_miss_count > 0u);
    out_target->lost = snapshot->tracking_active && !snapshot->overlay.has_target;
    out_target->effective_view = snapshot->effective_view;

    if (!out_target->valid) {
        out_target->target_x1 = 0;
        out_target->target_y1 = 0;
        out_target->target_x2 = 0;
        out_target->target_y2 = 0;
        out_target->target_cx = out_target->screen_cx;
        out_target->target_cy = out_target->screen_cy;
        out_target->box_w = 0;
        out_target->box_h = 0;
        out_target->box_area = 0u;
        out_target->error_x = 0;
        out_target->error_y = 0;
    }
}

void human_tracking_target_fill_gimbal_control(const human_tracking_target_snapshot_t *snapshot,
                                               uint32_t now_ms,
                                               human_tracking_gimbal_control_t *out_control)
{
    bool can_refresh_freeze_baseline;

    if ((snapshot == 0) || (out_control == 0)) {
        return;
    }

    human_tracking_target_fill_gimbal_output(snapshot, &out_control->target);
    out_control->norm_error_x = 0.0f;
    out_control->norm_error_y = 0.0f;
    out_control->mapped_error_x = 0.0f;
    out_control->mapped_error_y = 0.0f;
    out_control->yaw_cmd = 0.0f;
    out_control->pitch_cmd = 0.0f;
    out_control->freeze_age_ms = 0u;
    out_control->yaw_in_deadzone = true;
    out_control->pitch_in_deadzone = true;
    out_control->frozen = false;
    out_control->freeze_timed_out = false;

    can_refresh_freeze_baseline = out_control->target.valid &&
                                 snapshot->tracking_active &&
                                 !out_control->target.predicted;

    if (out_control->target.valid) {
        float half_w = ((float)out_control->target.screen_w) * 0.5f;
        float half_h = ((float)out_control->target.screen_h) * 0.5f;

        if (half_w < 1.0f) {
            half_w = 1.0f;
        }
        if (half_h < 1.0f) {
            half_h = 1.0f;
        }

        out_control->norm_error_x = clamp_f32((float)out_control->target.error_x / half_w, -1.0f, 1.0f);
        out_control->norm_error_y = clamp_f32((float)out_control->target.error_y / half_h, -1.0f, 1.0f);
        out_control->mapped_error_x = soft_deadzone(out_control->norm_error_x, HT_GIMBAL_ERR_X_DEADZONE);
        out_control->mapped_error_y = soft_deadzone(out_control->norm_error_y, HT_GIMBAL_ERR_Y_DEADZONE);
        out_control->yaw_in_deadzone = (out_control->mapped_error_x == 0.0f);
        out_control->pitch_in_deadzone = (out_control->mapped_error_y == 0.0f);

        /* 当前约定: 目标在右侧/下侧时，建议云台向反方向修正。 */
        out_control->yaw_cmd = -out_control->mapped_error_x;
        out_control->pitch_cmd = -out_control->mapped_error_y;

        if (can_refresh_freeze_baseline) {
            s_last_valid_control_ms = now_ms;
            s_last_valid_yaw_cmd = out_control->yaw_cmd;
            s_last_valid_pitch_cmd = out_control->pitch_cmd;
            s_has_last_valid_control = true;
        }

        if (s_freeze_active || s_freeze_timed_out) {
            printf("[HT-CTRL] freeze cleared frame=%lu valid=%u pred=%u mode=%u\r\n",
                   (unsigned long)out_control->target.frame_id,
                   (unsigned)out_control->target.valid,
                   (unsigned)out_control->target.predicted,
                   (unsigned)snapshot->tracking_active);
            s_freeze_active = false;
            s_freeze_timed_out = false;
        }
        return;
    }

    if (snapshot->tracking_active && s_has_last_valid_control) {
        uint32_t freeze_age_ms = now_ms - s_last_valid_control_ms;

        out_control->freeze_age_ms = freeze_age_ms;
        if (freeze_age_ms <= HT_GIMBAL_LOST_FREEZE_MS) {
            out_control->frozen = true;
            out_control->yaw_cmd = s_last_valid_yaw_cmd;
            out_control->pitch_cmd = s_last_valid_pitch_cmd;
            out_control->yaw_in_deadzone = false;
            out_control->pitch_in_deadzone = false;
            if (!s_freeze_active) {
                printf("[HT-CTRL] freeze enter frame=%lu age_ms=%lu ycmd=%.3f pcmd=%.3f\r\n",
                       (unsigned long)out_control->target.frame_id,
                       (unsigned long)freeze_age_ms,
                       (double)out_control->yaw_cmd,
                       (double)out_control->pitch_cmd);
                s_freeze_active = true;
            }
            s_freeze_timed_out = false;
        } else {
            out_control->freeze_timed_out = true;
            out_control->yaw_cmd = 0.0f;
            out_control->pitch_cmd = 0.0f;
            out_control->yaw_in_deadzone = true;
            out_control->pitch_in_deadzone = true;
            if (!s_freeze_timed_out) {
                printf("[HT-CTRL] freeze timeout frame=%lu age_ms=%lu -> zero cmd\r\n",
                       (unsigned long)out_control->target.frame_id,
                       (unsigned long)freeze_age_ms);
                s_freeze_timed_out = true;
            }
            s_freeze_active = false;
        }
        return;
    }

    if (s_freeze_active || s_freeze_timed_out) {
        printf("[HT-CTRL] freeze cleared frame=%lu valid=%u pred=%u mode=%u\r\n",
               (unsigned long)out_control->target.frame_id,
               (unsigned)out_control->target.valid,
               (unsigned)out_control->target.predicted,
               (unsigned)snapshot->tracking_active);
        s_freeze_active = false;
        s_freeze_timed_out = false;
    }
}