#include "app_gimbal_tracking_input.h"

#include <string.h>

#include "app_card2_result_handler.h"
#include "app_gimbal_control.h"
#include "app_runtime_state.h"
#include "board.h"
#include "detection_proto.h"
#include "master_log.h"

#define MASTER_TRACK_INPUT_LOG_INTERVAL_MS   1500u
#define MASTER_TRACK_INPUT_FREEZE_MS          600u
#define MASTER_TRACK_INPUT_SOURCE_TIMEOUT_MS  500u
#define MASTER_TRACK_INPUT_CARD2_TIMEOUT_MS   500u
#define MASTER_TRACK_INPUT_IDLE_HOLD_MS       420u
#define MASTER_TRACK_INPUT_IDLE_MIN_CONF      96u
#define MASTER_TRACK_INPUT_TRACKING_MIN_CONF  70u
#define MASTER_TRACK_INPUT_LONG_LOST_VERIFY_TIMEOUT_MS 800u
#define MASTER_TRACK_INPUT_CARD2_MATCH_CONFIRM_FRAMES 2u
#define MASTER_TRACK_INPUT_CARD2_NOMATCH_CONFIRM_FRAMES 2u
#define MASTER_TRACK_INPUT_REACQUIRED_STABLE_FRAMES 3u
#define MASTER_TRACK_INPUT_DZ_X               0.05f
#define MASTER_TRACK_INPUT_DZ_Y               0.06f
#define MASTER_TRACK_INPUT_ADAPTIVE_ERR_LOW_PX    10.0f
#define MASTER_TRACK_INPUT_ADAPTIVE_ERR_HIGH_PX   40.0f
#define MASTER_TRACK_INPUT_GAIN_MIN_X            0.70f
#define MASTER_TRACK_INPUT_GAIN_MAX_X            1.35f
#define MASTER_TRACK_INPUT_GAIN_MIN_Y            0.75f
#define MASTER_TRACK_INPUT_GAIN_MAX_Y            1.25f
#define MASTER_TRACK_INPUT_BOX_RATIO_LARGE       0.25f
#define MASTER_TRACK_INPUT_BOX_RATIO_SMALL       0.05f
#define MASTER_TRACK_INPUT_BOX_GAIN_NEAR         0.55f
#define MASTER_TRACK_INPUT_BOX_GAIN_FAR          1.15f
#define MASTER_TRACK_INPUT_EDGE_RATIO            0.15f
#define MASTER_TRACK_INPUT_EDGE_GAIN_MIN         0.35f

static app_gimbal_tracking_input_millis_fn_t s_millis_fn = NULL;
static bool s_ready = false;
static bool s_have_last_result = false;
static bool s_have_tracking_summary = false;
static subboard_detection_result_t s_last_result;
static subboard_tracking_summary_t s_last_tracking_summary;
static uint32_t s_last_result_ms = 0u;
static uint32_t s_last_tracking_summary_ms = 0u;
static uint32_t s_last_valid_control_ms = 0u;
static float s_last_valid_yaw_cmd = 0.0f;
static float s_last_valid_pitch_cmd = 0.0f;
static bool s_has_last_valid_control = false;
static bool s_last_tracking_active = false;
static bool s_last_valid = false;
static bool s_last_frozen = false;
static bool s_last_timeout = false;
static app_gimbal_track_view_t s_last_effective_view = (app_gimbal_track_view_t)0xFF;
static uint32_t s_last_log_ms = 0u;
static app_gimbal_tracking_input_output_t s_last_output;
static bool s_have_idle_latch = false;
static uint32_t s_last_idle_latch_ms = 0u;
static app_gimbal_tracking_input_output_t s_idle_latch_output;
static app_gimbal_follow_state_t s_follow_state = APP_GIMBAL_FOLLOW_STATE_IDLE;
static app_gimbal_follow_state_t s_last_logged_follow_state = (app_gimbal_follow_state_t)0xFF;
static uint32_t s_follow_session_id = 0u;
static uint8_t s_last_locked_track_id = 0u;
static uint32_t s_verify_deadline_ms = 0u;
static uint8_t s_last_verify_state = SUBBOARD_FACE_VERIFY_STATE_NONE;
static uint8_t s_last_verify_score = 0u;
static uint8_t s_reacquired_stable_frames = 0u;
static uint8_t s_card2_match_confirm_frames = 0u;
static uint8_t s_card2_no_match_confirm_frames = 0u;

static uint32_t tracking_input_now_ms(void)
{
    return (s_millis_fn != NULL) ? s_millis_fn() : 0u;
}

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

static float abs_f32(float value)
{
    return (value < 0.0f) ? -value : value;
}

static float with_sign_f32(float magnitude, float sign_source)
{
    return (sign_source < 0.0f) ? -magnitude : magnitude;
}

static float lerp_f32(float from, float to, float t)
{
    return from + ((to - from) * t);
}

static float soft_deadzone(float error, float deadzone)
{
    float half_deadzone = deadzone * 0.5f;
    float abs_error = abs_f32(error);

    if (abs_error < half_deadzone) {
        return 0.0f;
    }
    if (abs_error < deadzone) {
        float t = (abs_error - half_deadzone) / half_deadzone;
        return with_sign_f32(t * abs_error, error);
    }
    return error;
}

static float adaptive_gain_from_error(float error_px, float gain_min, float gain_max)
{
    float abs_error_px = abs_f32(error_px);

    if (abs_error_px <= MASTER_TRACK_INPUT_ADAPTIVE_ERR_LOW_PX) {
        return gain_min;
    }

    if (abs_error_px >= MASTER_TRACK_INPUT_ADAPTIVE_ERR_HIGH_PX) {
        return gain_max;
    }

    return lerp_f32(gain_min,
                    gain_max,
                    (abs_error_px - MASTER_TRACK_INPUT_ADAPTIVE_ERR_LOW_PX) /
                    (MASTER_TRACK_INPUT_ADAPTIVE_ERR_HIGH_PX - MASTER_TRACK_INPUT_ADAPTIVE_ERR_LOW_PX));
}

static float gain_from_box_size(int32_t box_w, int32_t box_h)
{
    float box_ratio;

    if ((box_w <= 0) || (box_h <= 0)) {
        return 1.0f;
    }

    box_ratio = ((float)box_w * (float)box_h) /
                ((float)BOARD_DISPLAY_WIDTH * (float)BOARD_DISPLAY_HEIGHT);

    if (box_ratio >= MASTER_TRACK_INPUT_BOX_RATIO_LARGE) {
        return MASTER_TRACK_INPUT_BOX_GAIN_NEAR;
    }

    if (box_ratio <= MASTER_TRACK_INPUT_BOX_RATIO_SMALL) {
        return MASTER_TRACK_INPUT_BOX_GAIN_FAR;
    }

    return lerp_f32(MASTER_TRACK_INPUT_BOX_GAIN_FAR,
                    MASTER_TRACK_INPUT_BOX_GAIN_NEAR,
                    (box_ratio - MASTER_TRACK_INPUT_BOX_RATIO_SMALL) /
                    (MASTER_TRACK_INPUT_BOX_RATIO_LARGE - MASTER_TRACK_INPUT_BOX_RATIO_SMALL));
}

static float gain_from_edge_distance(int32_t target_coord, int32_t image_size)
{
    float edge_margin;
    float target_coord_f;

    if (image_size <= 0) {
        return 1.0f;
    }

    edge_margin = (float)image_size * MASTER_TRACK_INPUT_EDGE_RATIO;
    target_coord_f = (float)target_coord;

    if (target_coord_f < edge_margin) {
        return clamp_f32(MASTER_TRACK_INPUT_EDGE_GAIN_MIN +
                         ((1.0f - MASTER_TRACK_INPUT_EDGE_GAIN_MIN) * (target_coord_f / edge_margin)),
                         MASTER_TRACK_INPUT_EDGE_GAIN_MIN,
                         1.0f);
    }

    if (target_coord_f > ((float)image_size - edge_margin)) {
        return clamp_f32(MASTER_TRACK_INPUT_EDGE_GAIN_MIN +
                         ((1.0f - MASTER_TRACK_INPUT_EDGE_GAIN_MIN) *
                          (((float)image_size - target_coord_f) / edge_margin)),
                         MASTER_TRACK_INPUT_EDGE_GAIN_MIN,
                         1.0f);
    }

    return 1.0f;
}

static bool result_is_trackable(const subboard_detection_result_t *result)
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

static bool tracking_summary_has_target(const subboard_tracking_summary_t *summary)
{
    if (summary == NULL) {
        return false;
    }

    return (summary->valid != 0u) &&
           ((summary->tracking_flags & SUBBOARD_TRACKING_FLAG_HAS_TARGET) != 0u) &&
           (summary->x2 > summary->x1) &&
           (summary->y2 > summary->y1);
}

static bool tracking_summary_is_predicted(const subboard_tracking_summary_t *summary)
{
    if (summary == NULL) {
        return false;
    }

    if ((summary->valid != 0u) &&
        ((summary->tracking_flags & SUBBOARD_TRACKING_FLAG_HAS_TARGET) != 0u) &&
        ((summary->tracking_flags & SUBBOARD_TRACKING_FLAG_RAW_STATE_VALID) != 0u)) {
        return summary->tracker_state_raw == DETECTION_TRACKER_RAW_PREDICTED;
    }

    return (summary->tracking_flags & SUBBOARD_TRACKING_FLAG_PREDICTED) != 0u;
}

static bool tracking_summary_is_lost(const subboard_tracking_summary_t *summary)
{
    if (summary == NULL) {
        return false;
    }

    if ((summary->valid != 0u) &&
        ((summary->tracking_flags & SUBBOARD_TRACKING_FLAG_HAS_TARGET) != 0u) &&
        ((summary->tracking_flags & SUBBOARD_TRACKING_FLAG_RAW_STATE_VALID) != 0u)) {
        return summary->tracker_state_raw == DETECTION_TRACKER_RAW_LOST;
    }

    return ((summary->tracking_flags & SUBBOARD_TRACKING_FLAG_LOST) != 0u) ||
           (summary->tracking_state == SUBBOARD_TRACKING_STATE_FOLLOWING_LOST);
}

static bool tracking_summary_command_pending(const subboard_tracking_summary_t *summary)
{
    return (summary != NULL) &&
           ((summary->tracking_flags & SUBBOARD_TRACKING_FLAG_CMD_PENDING) != 0u);
}

static bool tracking_summary_is_short_lost(const subboard_tracking_summary_t *summary)
{
    return (summary != NULL) && (summary->selected_idx == -2);
}

static bool tracking_summary_is_long_lost(const subboard_tracking_summary_t *summary)
{
    return (summary != NULL) && (summary->selected_idx == -3);
}

static app_gimbal_track_view_t effective_tracking_view(bool tracking_active, bool has_target)
{
    if (tracking_active) {
        return has_target ? APP_GIMBAL_TRACK_VIEW_FOLLOWING : APP_GIMBAL_TRACK_VIEW_FOLLOWING_LOST;
    }

    if (has_target) {
        return APP_GIMBAL_TRACK_VIEW_TARGET_READY;
    }

    return APP_GIMBAL_TRACK_VIEW_IDLE;
}

static const char *track_mode_name(bool tracking_active)
{
    return tracking_active ? "FOLLOWING" : "IDLE";
}

static const char *tracking_source_name(bool use_tracking_summary)
{
    return use_tracking_summary ? "summary" : "result-fallback";
}

const char *app_gimbal_tracking_input_follow_state_name(app_gimbal_follow_state_t state)
{
    switch (state) {
    case APP_GIMBAL_FOLLOW_STATE_TRACKING: return "TRACKING";
    case APP_GIMBAL_FOLLOW_STATE_SHORT_LOST: return "SHORT_LOST";
    case APP_GIMBAL_FOLLOW_STATE_LONG_LOST_WAIT_VERIFY: return "LONG_LOST_WAIT_VERIFY";
    case APP_GIMBAL_FOLLOW_STATE_REACQUIRED: return "REACQUIRED";
    case APP_GIMBAL_FOLLOW_STATE_LOST: return "LOST";
    case APP_GIMBAL_FOLLOW_STATE_IDLE:
    default:
        return "IDLE";
    }
}

static const char *verify_state_name(uint8_t verify_state)
{
    return app_card2_result_handler_verify_state_name(verify_state);
}

static const char *raw_tracker_state_name(uint8_t tracker_state)
{
    switch (tracker_state) {
    case DETECTION_TRACKER_RAW_DISABLED: return "DISABLED";
    case DETECTION_TRACKER_RAW_TRACKED: return "TRACKED";
    case DETECTION_TRACKER_RAW_PREDICTED: return "PREDICTED";
    case DETECTION_TRACKER_RAW_LOST: return "LOST";
    default: return "UNKNOWN";
    }
}

static const char *raw_tracker_state_label(const app_gimbal_tracking_input_output_t *output)
{
    if ((output == NULL) || !output->raw_state_valid) {
        return "UNKNOWN";
    }

    return raw_tracker_state_name(output->tracker_state_raw);
}

static bool source_is_fresh(uint32_t now_ms, uint32_t source_ms)
{
    return (source_ms != 0u) &&
           ((uint32_t)(now_ms - source_ms) <= MASTER_TRACK_INPUT_SOURCE_TIMEOUT_MS);
}

static uint8_t current_source_confidence(bool use_tracking_summary)
{
    return use_tracking_summary ? s_last_tracking_summary.confidence : s_last_result.confidence;
}

static bool idle_latch_is_fresh(uint32_t now_ms)
{
    return s_have_idle_latch &&
           ((uint32_t)(now_ms - s_last_idle_latch_ms) <= MASTER_TRACK_INPUT_IDLE_HOLD_MS);
}

static void idle_latch_refresh(const app_gimbal_tracking_input_output_t *output)
{
    if (output == NULL) {
        return;
    }

    s_idle_latch_output = *output;
    s_have_idle_latch = true;
    s_last_idle_latch_ms = output->updated_ms;
}

static void idle_latch_clear(void)
{
    s_have_idle_latch = false;
    s_last_idle_latch_ms = 0u;
    memset(&s_idle_latch_output, 0, sizeof(s_idle_latch_output));
}

static void populate_tracking_commands(app_gimbal_tracking_input_output_t *output)
{
    float half_w;
    float half_h;
    float base_yaw_cmd;
    float base_pitch_cmd;
    float box_gain;
    float yaw_gain;
    float pitch_gain;

    if ((output == NULL) || !output->valid) {
        return;
    }

    half_w = ((float)BOARD_DISPLAY_WIDTH) * 0.5f;
    half_h = ((float)BOARD_DISPLAY_HEIGHT) * 0.5f;

    output->error_x = output->target_cx - output->screen_cx;
    output->error_y = output->target_cy - output->screen_cy;
    output->norm_error_x = clamp_f32((float)output->error_x / half_w, -1.0f, 1.0f);
    output->norm_error_y = clamp_f32((float)output->error_y / half_h, -1.0f, 1.0f);
    base_yaw_cmd = -soft_deadzone(output->norm_error_x, MASTER_TRACK_INPUT_DZ_X);
    base_pitch_cmd = -soft_deadzone(output->norm_error_y, MASTER_TRACK_INPUT_DZ_Y);
    box_gain = gain_from_box_size(output->box_w, output->box_h);
    yaw_gain = adaptive_gain_from_error((float)output->error_x,
                                        MASTER_TRACK_INPUT_GAIN_MIN_X,
                                        MASTER_TRACK_INPUT_GAIN_MAX_X) *
               box_gain *
               gain_from_edge_distance(output->target_cx, (int32_t)BOARD_DISPLAY_WIDTH);
    pitch_gain = adaptive_gain_from_error((float)output->error_y,
                                          MASTER_TRACK_INPUT_GAIN_MIN_Y,
                                          MASTER_TRACK_INPUT_GAIN_MAX_Y) *
                 box_gain *
                 gain_from_edge_distance(output->target_cy, (int32_t)BOARD_DISPLAY_HEIGHT);
    output->yaw_cmd = clamp_f32(base_yaw_cmd * yaw_gain, -1.0f, 1.0f);
    output->pitch_cmd = clamp_f32(base_pitch_cmd * pitch_gain, -1.0f, 1.0f);
}

static void fill_output_from_card2_verify(app_gimbal_tracking_input_output_t *output,
                                          const app_card2_verify_snapshot_t *snapshot,
                                          uint32_t now_ms)
{
    if ((output == NULL) || (snapshot == NULL)) {
        return;
    }

    memset(output, 0, sizeof(*output));
    output->updated_ms = now_ms;
    output->screen_w = BOARD_DISPLAY_WIDTH;
    output->screen_h = BOARD_DISPLAY_HEIGHT;
    output->screen_cx = (int32_t)BOARD_DISPLAY_WIDTH / 2;
    output->screen_cy = (int32_t)BOARD_DISPLAY_HEIGHT / 2;
    output->target_cx = output->screen_cx;
    output->target_cy = output->screen_cy;
    output->tracking_active = true;
    output->valid = snapshot->detection_active;
    output->from_tracking_summary = false;
    output->using_card2_verify = true;
    output->target_id = snapshot->face_id;
    output->confidence = snapshot->confidence;
    output->verify_state = snapshot->verify_state;
    output->verify_score = snapshot->verify_score;
    output->verify_flags = snapshot->verify_flags;
    output->target_x1 = snapshot->x1;
    output->target_y1 = snapshot->y1;
    output->target_x2 = snapshot->x2;
    output->target_y2 = snapshot->y2;
    output->target_cx = snapshot->cx;
    output->target_cy = snapshot->cy;
    output->box_w = snapshot->x2 - snapshot->x1;
    output->box_h = snapshot->y2 - snapshot->y1;
    output->effective_view = effective_tracking_view(true, output->valid);

    if (output->valid) {
        populate_tracking_commands(output);
    }
}

static void enter_follow_state(app_gimbal_follow_state_t next_state)
{
    if (s_follow_state == next_state) {
        return;
    }

    if ((next_state == APP_GIMBAL_FOLLOW_STATE_TRACKING) ||
        (next_state == APP_GIMBAL_FOLLOW_STATE_IDLE) ||
        (next_state == APP_GIMBAL_FOLLOW_STATE_LOST)) {
        s_verify_deadline_ms = 0u;
    }

    if ((next_state == APP_GIMBAL_FOLLOW_STATE_LONG_LOST_WAIT_VERIFY) ||
        (next_state == APP_GIMBAL_FOLLOW_STATE_REACQUIRED)) {
        s_verify_deadline_ms = tracking_input_now_ms() + MASTER_TRACK_INPUT_LONG_LOST_VERIFY_TIMEOUT_MS;
    }

    s_follow_state = next_state;
}

static void reset_follow_state(void)
{
    s_follow_state = APP_GIMBAL_FOLLOW_STATE_IDLE;
    s_last_logged_follow_state = (app_gimbal_follow_state_t)0xFF;
    s_last_locked_track_id = 0u;
    s_verify_deadline_ms = 0u;
    s_last_verify_state = SUBBOARD_FACE_VERIFY_STATE_NONE;
    s_last_verify_score = 0u;
    s_reacquired_stable_frames = 0u;
    s_card2_match_confirm_frames = 0u;
    s_card2_no_match_confirm_frames = 0u;
}

static void update_card2_verify_confirmation(const app_card2_verify_snapshot_t *card2_snapshot,
                                             bool card2_fresh,
                                             bool *out_match_confirmed,
                                             bool *out_no_match_confirmed)
{
    bool match_confirmed = false;
    bool no_match_confirmed = false;

    if (card2_fresh && (card2_snapshot != NULL) && card2_snapshot->detection_active) {
        if (card2_snapshot->verify_state == SUBBOARD_FACE_VERIFY_STATE_MATCH) {
            if (s_card2_match_confirm_frames < 255u) {
                s_card2_match_confirm_frames++;
            }
            s_card2_no_match_confirm_frames = 0u;
        } else if (card2_snapshot->verify_state == SUBBOARD_FACE_VERIFY_STATE_NO_MATCH) {
            if (s_card2_no_match_confirm_frames < 255u) {
                s_card2_no_match_confirm_frames++;
            }
            s_card2_match_confirm_frames = 0u;
        } else {
            s_card2_match_confirm_frames = 0u;
            s_card2_no_match_confirm_frames = 0u;
        }
    } else {
        s_card2_match_confirm_frames = 0u;
        s_card2_no_match_confirm_frames = 0u;
    }

    match_confirmed = (s_card2_match_confirm_frames >= MASTER_TRACK_INPUT_CARD2_MATCH_CONFIRM_FRAMES);
    no_match_confirmed = (s_card2_no_match_confirm_frames >= MASTER_TRACK_INPUT_CARD2_NOMATCH_CONFIRM_FRAMES);

    if (out_match_confirmed != NULL) {
        *out_match_confirmed = match_confirmed;
    }
    if (out_no_match_confirmed != NULL) {
        *out_no_match_confirmed = no_match_confirmed;
    }
}

static bool card2_snapshot_can_drive_reacquire(const app_card2_verify_snapshot_t *card2_snapshot,
                                               bool card2_fresh)
{
    if (!card2_fresh || (card2_snapshot == NULL) || !card2_snapshot->detection_active) {
        return false;
    }

    return (card2_snapshot->verify_state == SUBBOARD_FACE_VERIFY_STATE_MATCH) ||
           (card2_snapshot->verify_state == SUBBOARD_FACE_VERIFY_STATE_UNCERTAIN) ||
           (card2_snapshot->verify_state == SUBBOARD_FACE_VERIFY_STATE_WAIT_ANCHOR);
}

static bool verify_deadline_reached(uint32_t now_ms)
{
    return (s_verify_deadline_ms != 0u) &&
           ((uint32_t)(now_ms - s_verify_deadline_ms) < 0x80000000u);
}

static bool update_follow_state_machine(bool tracking_active,
                                        bool summary_fresh,
                                        bool card1_valid,
                                        bool card1_lost,
                                        int32_t selected_idx,
                                        uint8_t target_id,
                                        const app_card2_verify_snapshot_t *card2_snapshot,
                                        bool card2_fresh)
{
    uint32_t now_ms = tracking_input_now_ms();
    bool use_card2_verify = false;
    bool card2_match_confirmed = false;
    bool card2_no_match_confirmed = false;

    if (!tracking_active) {
        enter_follow_state(APP_GIMBAL_FOLLOW_STATE_IDLE);
        s_reacquired_stable_frames = 0u;
        s_card2_match_confirm_frames = 0u;
        s_card2_no_match_confirm_frames = 0u;
        return false;
    }

    if (!s_last_tracking_active) {
        s_follow_session_id++;
        s_reacquired_stable_frames = 0u;
        s_last_locked_track_id = 0u;
        s_card2_match_confirm_frames = 0u;
        s_card2_no_match_confirm_frames = 0u;
        enter_follow_state(APP_GIMBAL_FOLLOW_STATE_TRACKING);
    }

    update_card2_verify_confirmation(card2_snapshot,
                                     card2_fresh,
                                     &card2_match_confirmed,
                                     &card2_no_match_confirmed);

    if (card2_fresh && (card2_snapshot != NULL)) {
        s_last_verify_state = card2_snapshot->verify_state;
        s_last_verify_score = card2_snapshot->verify_score;
    }

    if (card1_valid) {
        s_last_locked_track_id = target_id;

        if ((s_follow_state == APP_GIMBAL_FOLLOW_STATE_SHORT_LOST) ||
            (s_follow_state == APP_GIMBAL_FOLLOW_STATE_LONG_LOST_WAIT_VERIFY) ||
            (s_follow_state == APP_GIMBAL_FOLLOW_STATE_REACQUIRED) ||
            (s_follow_state == APP_GIMBAL_FOLLOW_STATE_LOST)) {
            if (s_reacquired_stable_frames < 255u) {
                s_reacquired_stable_frames++;
            }

            if (s_reacquired_stable_frames >= MASTER_TRACK_INPUT_REACQUIRED_STABLE_FRAMES) {
                enter_follow_state(APP_GIMBAL_FOLLOW_STATE_TRACKING);
                s_reacquired_stable_frames = 0u;
            } else {
                enter_follow_state(APP_GIMBAL_FOLLOW_STATE_REACQUIRED);
            }
        } else {
            s_reacquired_stable_frames = 0u;
            enter_follow_state(APP_GIMBAL_FOLLOW_STATE_TRACKING);
        }

        return false;
    }

    s_reacquired_stable_frames = 0u;

    if (summary_fresh && tracking_summary_is_short_lost(&s_last_tracking_summary)) {
        enter_follow_state(APP_GIMBAL_FOLLOW_STATE_SHORT_LOST);
        return false;
    }

    if (summary_fresh && tracking_summary_is_long_lost(&s_last_tracking_summary)) {
        enter_follow_state(APP_GIMBAL_FOLLOW_STATE_LONG_LOST_WAIT_VERIFY);

        if (card2_match_confirmed) {
            enter_follow_state(APP_GIMBAL_FOLLOW_STATE_REACQUIRED);
            return true;
        }

        if (card2_no_match_confirmed || verify_deadline_reached(now_ms)) {
            enter_follow_state(APP_GIMBAL_FOLLOW_STATE_LOST);
        }
        return false;
    }

    if (s_follow_state == APP_GIMBAL_FOLLOW_STATE_REACQUIRED) {
        if (card2_no_match_confirmed) {
            enter_follow_state(APP_GIMBAL_FOLLOW_STATE_LOST);
            return false;
        }

        if (card2_snapshot_can_drive_reacquire(card2_snapshot, card2_fresh)) {
            use_card2_verify = true;
        }

        if (!use_card2_verify && verify_deadline_reached(now_ms)) {
            enter_follow_state(APP_GIMBAL_FOLLOW_STATE_LOST);
        }
        return use_card2_verify;
    }

    if (card1_lost || summary_fresh || (selected_idx == -1)) {
        enter_follow_state(APP_GIMBAL_FOLLOW_STATE_LOST);
    }

    return false;
}

static void build_output(app_gimbal_tracking_input_output_t *out_output)
{
    app_gimbal_tracking_input_output_t actual_output;
    app_card2_verify_snapshot_t card2_snapshot;
    bool predicted = false;
    bool command_pending = false;
    bool lost = false;
    bool tracking_active;
    bool valid;
    bool summary_fresh;
    bool result_fresh;
    bool use_tracking_summary;
    bool card2_fresh;
    bool use_card2_verify;
    uint8_t source_confidence;
    uint32_t now_ms;

    if (out_output == NULL) {
        return;
    }

    memset(&actual_output, 0, sizeof(actual_output));
    actual_output.screen_w = BOARD_DISPLAY_WIDTH;
    actual_output.screen_h = BOARD_DISPLAY_HEIGHT;
    actual_output.screen_cx = (int32_t)BOARD_DISPLAY_WIDTH / 2;
    actual_output.screen_cy = (int32_t)BOARD_DISPLAY_HEIGHT / 2;
    actual_output.target_cx = actual_output.screen_cx;
    actual_output.target_cy = actual_output.screen_cy;

    now_ms = tracking_input_now_ms();
    tracking_active = app_runtime_state_is_tracking();
    summary_fresh = s_have_tracking_summary && source_is_fresh(now_ms, s_last_tracking_summary_ms);
    result_fresh = s_have_last_result && source_is_fresh(now_ms, s_last_result_ms);
    card2_fresh = app_card2_result_handler_get_snapshot(&card2_snapshot) &&
                  ((uint32_t)(now_ms - card2_snapshot.updated_ms) <= MASTER_TRACK_INPUT_CARD2_TIMEOUT_MS);
    use_tracking_summary = summary_fresh;

    valid = use_tracking_summary ?
        tracking_summary_has_target(&s_last_tracking_summary) :
        (result_fresh && result_is_trackable(&s_last_result));
    source_confidence = valid ? current_source_confidence(use_tracking_summary) : 0u;

    if (!tracking_active && valid && (source_confidence < MASTER_TRACK_INPUT_IDLE_MIN_CONF)) {
        valid = false;
    }

    if (tracking_active && !use_tracking_summary && valid &&
        (source_confidence < MASTER_TRACK_INPUT_TRACKING_MIN_CONF)) {
        valid = false;
    }

    if (summary_fresh) {
        predicted = tracking_summary_is_predicted(&s_last_tracking_summary);
        command_pending = tracking_summary_command_pending(&s_last_tracking_summary);
        lost = tracking_summary_is_lost(&s_last_tracking_summary);
    }

    actual_output.updated_ms = now_ms;
    actual_output.tracking_active = tracking_active;
    actual_output.from_tracking_summary = use_tracking_summary;
    actual_output.valid = valid;
    actual_output.predicted = tracking_active && predicted;
    actual_output.command_pending = tracking_active && command_pending;
    actual_output.effective_view = effective_tracking_view(tracking_active, valid);
    actual_output.follow_state = s_follow_state;
    actual_output.follow_session_id = s_follow_session_id;
    actual_output.verify_state = card2_fresh ? card2_snapshot.verify_state : SUBBOARD_FACE_VERIFY_STATE_NONE;
    actual_output.verify_score = card2_fresh ? card2_snapshot.verify_score : 0u;
    actual_output.verify_flags = card2_fresh ? card2_snapshot.verify_flags : 0u;

    if (valid) {
        if (use_tracking_summary) {
            actual_output.frame_id = s_last_tracking_summary.frame_id;
            actual_output.target_x1 = s_last_tracking_summary.x1;
            actual_output.target_y1 = s_last_tracking_summary.y1;
            actual_output.target_x2 = s_last_tracking_summary.x2;
            actual_output.target_y2 = s_last_tracking_summary.y2;
            actual_output.target_cx = s_last_tracking_summary.cx;
            actual_output.target_cy = s_last_tracking_summary.cy;
            actual_output.box_w = s_last_tracking_summary.x2 - s_last_tracking_summary.x1;
            actual_output.box_h = s_last_tracking_summary.y2 - s_last_tracking_summary.y1;
            actual_output.vx = s_last_tracking_summary.vx;
            actual_output.vy = s_last_tracking_summary.vy;
            actual_output.count = s_last_tracking_summary.count;
            actual_output.selected_idx = s_last_tracking_summary.selected_idx;
            actual_output.target_id = s_last_tracking_summary.target_id;
            actual_output.confidence = s_last_tracking_summary.confidence;
            actual_output.tracking_state = s_last_tracking_summary.tracking_state;
            actual_output.miss_count = s_last_tracking_summary.miss_count;
            actual_output.tracker_state_raw = s_last_tracking_summary.tracker_state_raw;
            actual_output.tracker_flags_raw = s_last_tracking_summary.tracker_flags_raw;
            actual_output.raw_state_valid =
                ((s_last_tracking_summary.tracking_flags & SUBBOARD_TRACKING_FLAG_RAW_STATE_VALID) != 0u);
        } else {
            actual_output.target_id = s_last_result.face_id;
            actual_output.target_x1 = s_last_result.x1;
            actual_output.target_y1 = s_last_result.y1;
            actual_output.target_x2 = s_last_result.x2;
            actual_output.target_y2 = s_last_result.y2;
            actual_output.target_cx = s_last_result.cx;
            actual_output.target_cy = s_last_result.cy;
            actual_output.box_w = s_last_result.x2 - s_last_result.x1;
            actual_output.box_h = s_last_result.y2 - s_last_result.y1;
            actual_output.vx = s_last_result.vx;
            actual_output.vy = s_last_result.vy;
            actual_output.count = s_last_result.count;
            actual_output.selected_idx = s_last_result.selected_idx;
            actual_output.confidence = s_last_result.confidence;
        }
        populate_tracking_commands(&actual_output);
    }

    use_card2_verify = update_follow_state_machine(tracking_active,
                                                   summary_fresh,
                                                   valid,
                                                   lost,
                                                   actual_output.selected_idx,
                                                   actual_output.target_id,
                                                   &card2_snapshot,
                                                   card2_fresh);

    actual_output.follow_state = s_follow_state;
    actual_output.follow_session_id = s_follow_session_id;
    actual_output.verify_state = card2_fresh ? card2_snapshot.verify_state : SUBBOARD_FACE_VERIFY_STATE_NONE;
    actual_output.verify_score = card2_fresh ? card2_snapshot.verify_score : 0u;
    actual_output.verify_flags = card2_fresh ? card2_snapshot.verify_flags : 0u;

    if (use_card2_verify) {
        fill_output_from_card2_verify(&actual_output, &card2_snapshot, now_ms);
        actual_output.follow_state = s_follow_state;
        actual_output.follow_session_id = s_follow_session_id;
    }

    if (actual_output.valid) {
        if (tracking_active && !actual_output.predicted) {
            s_last_valid_control_ms = now_ms;
            s_last_valid_yaw_cmd = actual_output.yaw_cmd;
            s_last_valid_pitch_cmd = actual_output.pitch_cmd;
            s_has_last_valid_control = true;
        }

        if (!tracking_active) {
            idle_latch_refresh(&actual_output);
        } else {
            idle_latch_clear();
        }

        actual_output.lost = (s_follow_state == APP_GIMBAL_FOLLOW_STATE_SHORT_LOST) ||
                             (s_follow_state == APP_GIMBAL_FOLLOW_STATE_LONG_LOST_WAIT_VERIFY) ||
                             (s_follow_state == APP_GIMBAL_FOLLOW_STATE_LOST);
        *out_output = actual_output;
        return;
    }

    if (!tracking_active && idle_latch_is_fresh(now_ms)) {
        *out_output = s_idle_latch_output;
        out_output->updated_ms = now_ms;
        out_output->tracking_active = false;
        out_output->from_tracking_summary = s_idle_latch_output.from_tracking_summary;
        out_output->predicted = false;
        out_output->lost = false;
        out_output->command_pending = false;
        out_output->frozen = false;
        out_output->freeze_timed_out = false;
        out_output->follow_state = APP_GIMBAL_FOLLOW_STATE_IDLE;
        out_output->effective_view = APP_GIMBAL_TRACK_VIEW_TARGET_READY;
        return;
    }

    if (!tracking_active) {
        idle_latch_clear();
    }

    actual_output.follow_state = s_follow_state;
    actual_output.follow_session_id = s_follow_session_id;
    actual_output.lost = tracking_active &&
                         ((s_follow_state != APP_GIMBAL_FOLLOW_STATE_TRACKING) &&
                          (s_follow_state != APP_GIMBAL_FOLLOW_STATE_REACQUIRED));
    if (tracking_active && s_has_last_valid_control) {
        uint32_t freeze_age_ms = now_ms - s_last_valid_control_ms;

        if (freeze_age_ms <= MASTER_TRACK_INPUT_FREEZE_MS) {
            actual_output.frozen = true;
            actual_output.yaw_cmd = s_last_valid_yaw_cmd;
            actual_output.pitch_cmd = s_last_valid_pitch_cmd;
        } else {
            actual_output.freeze_timed_out = true;
        }
    }

    *out_output = actual_output;
}

static void maybe_log_output(const app_gimbal_tracking_input_output_t *output)
{
    uint32_t now_ms;
    bool should_log;
    bool periodic_log_allowed;

    if (output == NULL) {
        return;
    }

    now_ms = tracking_input_now_ms();
    should_log = (output->tracking_active != s_last_tracking_active) ||
                 (output->valid != s_last_valid) ||
                 (output->frozen != s_last_frozen) ||
                 (output->freeze_timed_out != s_last_timeout) ||
                 (output->effective_view != s_last_effective_view) ||
                 (output->follow_state != s_last_logged_follow_state);

    periodic_log_allowed = output->tracking_active && output->valid;

    if (!should_log && periodic_log_allowed) {
        should_log = ((s_last_log_ms == 0u) ||
                     ((uint32_t)(now_ms - s_last_log_ms) >= MASTER_TRACK_INPUT_LOG_INTERVAL_MS));
    }

    if (!should_log) {
        return;
    }

    MASTER_LOG_INFO("[MASTER][TRACK-IN] card1 mode=%s source=%s follow=%s session=%lu effective=%s raw_tracker=%s raw_flags=0x%02X target=%u frame=%lu count=%u selected=%ld id=%u conf=%u miss=%u verify=%s vscore=%u vflags=0x%02X cx=%ld cy=%ld err_x=%ld err_y=%ld box=%ldx%ld pred=%u lost=%u pending=%u frozen=%u timeout=%u ycmd=%.3f pcmd=%.3f\r\n",
                    track_mode_name(output->tracking_active),
                    output->using_card2_verify ? "card2-verify" : tracking_source_name(output->from_tracking_summary),
                    app_gimbal_tracking_input_follow_state_name(output->follow_state),
                    (unsigned long)output->follow_session_id,
                    app_gimbal_tracking_input_view_name(output->effective_view),
                    raw_tracker_state_label(output),
                    (unsigned)output->tracker_flags_raw,
                    output->valid ? 1u : 0u,
                    (unsigned long)output->frame_id,
                    (unsigned)output->count,
                    (long)output->selected_idx,
                    (unsigned)output->target_id,
                    (unsigned)output->confidence,
                    (unsigned)output->miss_count,
                    verify_state_name(output->verify_state),
                    (unsigned)output->verify_score,
                    (unsigned)output->verify_flags,
                    (long)output->target_cx,
                    (long)output->target_cy,
                    (long)output->error_x,
                    (long)output->error_y,
                    (long)output->box_w,
                    (long)output->box_h,
                    output->predicted ? 1u : 0u,
                    output->lost ? 1u : 0u,
                    output->command_pending ? 1u : 0u,
                    output->frozen ? 1u : 0u,
                    output->freeze_timed_out ? 1u : 0u,
                    (double)output->yaw_cmd,
                    (double)output->pitch_cmd);

    s_last_tracking_active = output->tracking_active;
    s_last_valid = output->valid;
    s_last_frozen = output->frozen;
    s_last_timeout = output->freeze_timed_out;
    s_last_effective_view = output->effective_view;
    s_last_logged_follow_state = output->follow_state;
    s_last_log_ms = now_ms;
}

static void publish_output_to_gimbal_control(const app_gimbal_tracking_input_output_t *output)
{
    app_gimbal_tracking_observation_t observation;

    if (output == NULL) {
        return;
    }

    memset(&observation, 0, sizeof(observation));
    observation.updated_ms = output->updated_ms;
    observation.target_cx = output->target_cx;
    observation.target_cy = output->target_cy;
    observation.error_x = output->error_x;
    observation.error_y = output->error_y;
    observation.box_w = output->box_w;
    observation.box_h = output->box_h;
    observation.confidence = output->confidence;
    observation.valid = output->valid;
    observation.tracking_active = output->tracking_active;
    observation.from_tracking_summary = output->from_tracking_summary;
    observation.predicted = output->predicted;
    observation.lost = output->lost;
    observation.frozen = output->frozen;
    observation.freeze_timed_out = output->freeze_timed_out;
    observation.yaw_cmd = output->yaw_cmd;
    observation.pitch_cmd = output->pitch_cmd;

    app_gimbal_control_observe_tracking_input(&observation);
}

int app_gimbal_tracking_input_init(app_gimbal_tracking_input_millis_fn_t millis_fn)
{
    if (millis_fn == NULL) {
        return -1;
    }

    s_millis_fn = millis_fn;
    s_ready = true;
    app_gimbal_tracking_input_reset();
    return 0;
}

void app_gimbal_tracking_input_reset(void)
{
    s_have_last_result = false;
    s_have_tracking_summary = false;
    memset(&s_last_result, 0, sizeof(s_last_result));
    memset(&s_last_tracking_summary, 0, sizeof(s_last_tracking_summary));
    s_last_result_ms = 0u;
    s_last_tracking_summary_ms = 0u;
    s_last_valid_control_ms = 0u;
    s_last_valid_yaw_cmd = 0.0f;
    s_last_valid_pitch_cmd = 0.0f;
    s_has_last_valid_control = false;
    s_last_tracking_active = false;
    s_last_valid = false;
    s_last_frozen = false;
    s_last_timeout = false;
    s_last_effective_view = (app_gimbal_track_view_t)0xFF;
    s_last_log_ms = 0u;
    memset(&s_last_output, 0, sizeof(s_last_output));
    s_last_output.screen_w = BOARD_DISPLAY_WIDTH;
    s_last_output.screen_h = BOARD_DISPLAY_HEIGHT;
    s_last_output.screen_cx = (int32_t)BOARD_DISPLAY_WIDTH / 2;
    s_last_output.screen_cy = (int32_t)BOARD_DISPLAY_HEIGHT / 2;
    s_last_output.target_cx = s_last_output.screen_cx;
    s_last_output.target_cy = s_last_output.screen_cy;
    s_follow_session_id = 0u;
    reset_follow_state();
    idle_latch_clear();
}

void app_gimbal_tracking_input_tick(void)
{
    if (!s_ready) {
        return;
    }

    build_output(&s_last_output);
    publish_output_to_gimbal_control(&s_last_output);
    maybe_log_output(&s_last_output);
}

void app_gimbal_tracking_input_handle_card1_result(const subboard_detection_result_t *result)
{
    if (!s_ready || (result == NULL)) {
        return;
    }

    s_last_result = *result;
    s_have_last_result = true;
    s_last_result_ms = tracking_input_now_ms();
}

void app_gimbal_tracking_input_handle_card1_tracking(const subboard_tracking_summary_t *summary)
{
    if (!s_ready || (summary == NULL)) {
        return;
    }

    s_last_tracking_summary = *summary;
    s_have_tracking_summary = true;
    s_last_tracking_summary_ms = tracking_input_now_ms();
}

void app_gimbal_tracking_input_get_output(app_gimbal_tracking_input_output_t *out_output)
{
    if (out_output == NULL) {
        return;
    }

    *out_output = s_last_output;
}

const char *app_gimbal_tracking_input_view_name(app_gimbal_track_view_t view)
{
    switch (view) {
    case APP_GIMBAL_TRACK_VIEW_TARGET_READY: return "TARGET_READY";
    case APP_GIMBAL_TRACK_VIEW_FOLLOWING: return "FOLLOWING";
    case APP_GIMBAL_TRACK_VIEW_FOLLOWING_LOST: return "FOLLOWING_LOST";
    case APP_GIMBAL_TRACK_VIEW_IDLE:
    default:
        return "IDLE";
    }
}

const char *app_gimbal_tracking_input_raw_tracker_name(uint8_t tracker_state_raw)
{
    return raw_tracker_state_name(tracker_state_raw);
}