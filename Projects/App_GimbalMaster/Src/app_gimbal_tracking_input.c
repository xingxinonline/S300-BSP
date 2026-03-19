#include "app_gimbal_tracking_input.h"

#include <string.h>

#include "app_gimbal_control.h"
#include "app_runtime_state.h"
#include "board.h"
#include "detection_proto.h"
#include "master_log.h"

#define MASTER_TRACK_INPUT_LOG_INTERVAL_MS   1000u
#define MASTER_TRACK_INPUT_FREEZE_MS          600u
#define MASTER_TRACK_INPUT_SOURCE_TIMEOUT_MS  500u
#define MASTER_TRACK_INPUT_IDLE_HOLD_MS       420u
#define MASTER_TRACK_INPUT_IDLE_MIN_CONF      96u
#define MASTER_TRACK_INPUT_TRACKING_MIN_CONF  70u
#define MASTER_TRACK_INPUT_DZ_X               0.08f
#define MASTER_TRACK_INPUT_DZ_Y               0.10f

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

static void build_output(app_gimbal_tracking_input_output_t *out_output)
{
    app_gimbal_tracking_input_output_t actual_output;
    bool predicted = false;
    bool command_pending = false;
    bool lost = false;
    bool tracking_active;
    bool valid;
    bool summary_fresh;
    bool result_fresh;
    bool use_tracking_summary;
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
    actual_output.predicted = predicted;
    actual_output.command_pending = command_pending;
    actual_output.effective_view = effective_tracking_view(tracking_active, valid);

    if (valid) {
        float half_w = ((float)BOARD_DISPLAY_WIDTH) * 0.5f;
        float half_h = ((float)BOARD_DISPLAY_HEIGHT) * 0.5f;

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

        actual_output.error_x = actual_output.target_cx - actual_output.screen_cx;
        actual_output.error_y = actual_output.target_cy - actual_output.screen_cy;

        actual_output.norm_error_x = clamp_f32((float)actual_output.error_x / half_w, -1.0f, 1.0f);
        actual_output.norm_error_y = clamp_f32((float)actual_output.error_y / half_h, -1.0f, 1.0f);
        actual_output.yaw_cmd = -soft_deadzone(actual_output.norm_error_x, MASTER_TRACK_INPUT_DZ_X);
        actual_output.pitch_cmd = -soft_deadzone(actual_output.norm_error_y, MASTER_TRACK_INPUT_DZ_Y);

        if (tracking_active && !predicted) {
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
        out_output->effective_view = APP_GIMBAL_TRACK_VIEW_TARGET_READY;
        return;
    }

    if (!tracking_active) {
        idle_latch_clear();
    }

    actual_output.lost = tracking_active && (lost || !valid);
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

    if (output == NULL) {
        return;
    }

    now_ms = tracking_input_now_ms();
    should_log = (output->tracking_active != s_last_tracking_active) ||
                 (output->valid != s_last_valid) ||
                 (output->frozen != s_last_frozen) ||
                 (output->freeze_timed_out != s_last_timeout) ||
                 (output->effective_view != s_last_effective_view);

    if (!should_log && output->tracking_active) {
        should_log = ((s_last_log_ms == 0u) ||
                     ((uint32_t)(now_ms - s_last_log_ms) >= MASTER_TRACK_INPUT_LOG_INTERVAL_MS));
    }

    if (!should_log) {
        return;
    }

    MASTER_LOG_INFO("[MASTER][TRACK-IN] card1 mode=%s source=%s effective=%s raw_tracker=%s raw_flags=0x%02X target=%u frame=%lu count=%u selected=%u id=%u conf=%u miss=%u cx=%ld cy=%ld err_x=%ld err_y=%ld box=%ldx%ld pred=%u lost=%u pending=%u frozen=%u timeout=%u ycmd=%.3f pcmd=%.3f\r\n",
                    track_mode_name(output->tracking_active),
                    tracking_source_name(output->from_tracking_summary),
                    app_gimbal_tracking_input_view_name(output->effective_view),
                    raw_tracker_state_label(output),
                    (unsigned)output->tracker_flags_raw,
                    output->valid ? 1u : 0u,
                    (unsigned long)output->frame_id,
                    (unsigned)output->count,
                    (unsigned)output->selected_idx,
                    (unsigned)output->target_id,
                    (unsigned)output->confidence,
                    (unsigned)output->miss_count,
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