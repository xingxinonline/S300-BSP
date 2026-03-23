#include "app_gimbal_control.h"

#include <stddef.h>

#include "board.h"
#include "bus_servo.h"
#include "master_log.h"
#include "s300.h"

#define GIMBAL_YAW_SERVO_ID              6u
#define GIMBAL_PITCH_SERVO_ID            4u
#define GIMBAL_AUX_SERVO_2_ID            2u
#define GIMBAL_AUX_SERVO_3_ID            3u
#define GIMBAL_AUX_SERVO_5_ID            5u

#define GIMBAL_SERVO_NEUTRAL_PULSE       500u

#define GIMBAL_YAW_MIN_PULSE             125u
#define GIMBAL_YAW_MAX_PULSE             875u
#define GIMBAL_YAW_MIN_ANGLE_DEG         (-90.0f)
#define GIMBAL_YAW_MAX_ANGLE_DEG         (90.0f)

#define GIMBAL_PITCH_MIN_PULSE           125u
#define GIMBAL_PITCH_MAX_PULSE           500u
#define GIMBAL_PITCH_MIN_ANGLE_DEG       (-90.0f)
#define GIMBAL_PITCH_MAX_ANGLE_DEG       (0.0f)

#define GIMBAL_TRACKING_MOVE_TIME_MS     700u
#define GIMBAL_PARKING_MOVE_TIME_MS      700u
#define GIMBAL_PREVIEW_MOVE_TIME_MS      700u
#define GIMBAL_TRACKING_STOP_MOVE_TIME_MS 1100u
#define GIMBAL_STARTUP_INIT_MOVE_TIME_MS  1200u
#define GIMBAL_TRACK_INPUT_LOG_INTERVAL_MS 1000u

#ifndef MASTER_GIMBAL_STATUS_READBACK_ENABLE
#define MASTER_GIMBAL_STATUS_READBACK_ENABLE 0
#endif

#ifndef MASTER_GIMBAL_TRACK_CONTROL_ENABLE
#define MASTER_GIMBAL_TRACK_CONTROL_ENABLE 1
#endif

#define GIMBAL_TRACK_CONTROL_INTERVAL_MS       35u
#define GIMBAL_TRACK_CONTROL_MOVE_TIME_MS      35u
#define GIMBAL_TRACK_CONTROL_LOG_INTERVAL_MS   1000u
#define GIMBAL_TRACK_CONTROL_MIN_CMD           0.03f
#define GIMBAL_TRACK_CONTROL_YAW_DEADZONE      0.03f
#define GIMBAL_TRACK_CONTROL_PITCH_DEADZONE    0.025f
#define GIMBAL_TRACK_CONTROL_YAW_ALPHA         0.45f
#define GIMBAL_TRACK_CONTROL_PITCH_ALPHA       0.40f
#define GIMBAL_TRACK_CONTROL_YAW_ALPHA_FAST    0.34f
#define GIMBAL_TRACK_CONTROL_PITCH_ALPHA_FAST  0.24f
#define GIMBAL_TRACK_CONTROL_YAW_MIN_MOVE_DEG  0.06f
#define GIMBAL_TRACK_CONTROL_PITCH_MIN_MOVE_DEG 0.05f
#define GIMBAL_TRACK_CONTROL_PREDICTED_GAIN    0.72f
#define GIMBAL_TRACK_CONTROL_LOW_CONF_GAIN     0.85f
#define GIMBAL_TRACK_CONTROL_LOW_CONF_THRESH   80u
#define GIMBAL_TRACK_CONTROL_FAST_CMD_LOW      0.25f
#define GIMBAL_TRACK_CONTROL_FAST_CMD_HIGH     0.75f
#define GIMBAL_TRACK_CONTROL_YAW_FAST_RESP_MAX 1.28f
#define GIMBAL_TRACK_CONTROL_PITCH_FAST_RESP_MAX 1.30f
#define GIMBAL_TRACK_CONTROL_YAW_STEP_DEG      2.8f
#define GIMBAL_TRACK_CONTROL_PITCH_STEP_DEG    1.8f
#define GIMBAL_TRACK_CONTROL_YAW_REVERSAL_DAMP 0.58f
#define GIMBAL_TRACK_CONTROL_REVERSAL_THRESHOLD_DEG 0.60f
#define GIMBAL_TRACK_CONTROL_PITCH_REVERSAL_DAMP 0.65f
#define GIMBAL_TRACK_CONTROL_PITCH_REVERSAL_THRESHOLD_DEG 0.20f
#define GIMBAL_TRACK_CONTROL_FAST_START_CMD    0.18f

#define GIMBAL_STARTUP_RESET_MOVE_TIME_MS      1400u
#define GIMBAL_STARTUP_PREPARE_MOVE_TIME_MS    2200u
#define GIMBAL_STARTUP_POWERUP_DELAY_MS        500u
#define GIMBAL_STARTUP_SERVO_SETTLE_MS         10u
#define GIMBAL_STARTUP_GIMBAL_READY_DELAY_MS   100u
#define GIMBAL_MOVE_INTER_COMMAND_DELAY_MS     5u

typedef struct {
    app_gimbal_preset_t preset;
    float yaw_angle_deg;
    float pitch_angle_deg;
    uint16_t move_time_ms;
    const char *name;
} app_gimbal_preset_desc_t;

static const app_gimbal_preset_desc_t s_gimbal_presets[] = {
    { APP_GIMBAL_PRESET_PARKING,        0.0f,  -90.0f, GIMBAL_PARKING_MOVE_TIME_MS,  "parking" },
    { APP_GIMBAL_PRESET_TRACKING,       0.0f,  -45.0f, GIMBAL_TRACKING_MOVE_TIME_MS, "tracking" },
    { APP_GIMBAL_PRESET_CENTER_PREVIEW, 0.0f,  -45.0f, GIMBAL_PREVIEW_MOVE_TIME_MS,  "center-preview" },
    { APP_GIMBAL_PRESET_LEFT_PREVIEW,  -45.0f, -45.0f, GIMBAL_PREVIEW_MOVE_TIME_MS,  "left-preview" },
    { APP_GIMBAL_PRESET_RIGHT_PREVIEW,  45.0f, -45.0f, GIMBAL_PREVIEW_MOVE_TIME_MS,  "right-preview" },
    { APP_GIMBAL_PRESET_UP_PREVIEW,     0.0f,  -20.0f, GIMBAL_PREVIEW_MOVE_TIME_MS,  "up-preview" },
};

static int gimbal_move_raw(uint16_t yaw_pulse, uint16_t pitch_pulse, uint16_t time_ms);

static bool s_ready = false;
static bus_servo_t s_servo;
static float s_last_yaw_angle_deg = 0.0f;
static float s_last_pitch_angle_deg = GIMBAL_PITCH_MIN_ANGLE_DEG;
static bool s_have_tracking_observation = false;
static app_gimbal_tracking_observation_t s_tracking_observation;
static uint32_t s_last_tracking_observation_log_ms = 0u;
static bool s_last_tracking_observation_valid = false;
static bool s_last_tracking_observation_frozen = false;
static bool s_last_tracking_observation_timeout = false;
static uint32_t s_last_tracking_control_ms = 0u;
static uint32_t s_last_tracking_control_log_ms = 0u;
static bool s_last_tracking_drive_active = false;
static float s_filtered_tracking_yaw_cmd = 0.0f;
static float s_filtered_tracking_pitch_cmd = 0.0f;
static bool s_tracking_cmd_filter_initialized = false;
static float s_last_tracking_yaw_delta_deg = 0.0f;
static float s_last_tracking_pitch_delta_deg = 0.0f;
static float s_pending_yaw_delta_deg = 0.0f;
static float s_pending_pitch_delta_deg = 0.0f;

static app_gimbal_control_result_t gimbal_apply_preset_with_time(app_gimbal_preset_t preset,
                                                                 uint16_t move_time_ms,
                                                                 bool verbose_log,
                                                                 const char *status_reason);

static uint32_t gimbal_tracking_observation_now_ms(void)
{
    return s_tracking_observation.updated_ms;
}

static const app_gimbal_preset_desc_t *gimbal_find_preset(app_gimbal_preset_t preset)
{
    uint32_t index;

    for (index = 0u; index < (sizeof(s_gimbal_presets) / sizeof(s_gimbal_presets[0])); index++) {
        if (s_gimbal_presets[index].preset == preset) {
            return &s_gimbal_presets[index];
        }
    }

    return NULL;
}

static void gimbal_delay_ms(uint32_t ms)
{
    volatile uint32_t count;

    count = ms * (SystemCoreClock / 10000u);
    while (count-- > 0u) {
        __NOP();
    }
}

static bool gimbal_prepare_startup_pose(uint8_t servo_id, float angle_deg, uint16_t move_time_ms)
{
    return bus_servo_move_prepare(&s_servo, servo_id, angle_deg, move_time_ms) == 0;
}

static bool gimbal_start_prepared_motion(uint8_t servo_id)
{
    return bus_servo_move_start(&s_servo, servo_id) == 0;
}

static void gimbal_init_aux_servo(uint8_t servo_id)
{
    (void)gimbal_prepare_startup_pose(servo_id, 120.0f, GIMBAL_STARTUP_PREPARE_MOVE_TIME_MS);
    gimbal_delay_ms(GIMBAL_STARTUP_SERVO_SETTLE_MS);
    (void)bus_servo_set_load(&s_servo, servo_id, true);
    gimbal_delay_ms(GIMBAL_STARTUP_SERVO_SETTLE_MS);
    (void)gimbal_start_prepared_motion(servo_id);
    gimbal_delay_ms(GIMBAL_STARTUP_SERVO_SETTLE_MS);
}

static bool gimbal_startup_reset_all_servos(void)
{
    if (!gimbal_prepare_startup_pose(GIMBAL_YAW_SERVO_ID, 0.0f, GIMBAL_STARTUP_PREPARE_MOVE_TIME_MS) ||
        !gimbal_prepare_startup_pose(GIMBAL_PITCH_SERVO_ID, 0.0f, GIMBAL_STARTUP_PREPARE_MOVE_TIME_MS)) {
        if (gimbal_move_raw(GIMBAL_SERVO_NEUTRAL_PULSE,
                            GIMBAL_SERVO_NEUTRAL_PULSE,
                            GIMBAL_STARTUP_RESET_MOVE_TIME_MS) != 0) {
            return false;
        }

        s_last_yaw_angle_deg = 0.0f;
        s_last_pitch_angle_deg = 0.0f;
        return true;
    }

    if (!gimbal_start_prepared_motion(GIMBAL_YAW_SERVO_ID) ||
        !gimbal_start_prepared_motion(GIMBAL_PITCH_SERVO_ID)) {
        if (gimbal_move_raw(GIMBAL_SERVO_NEUTRAL_PULSE,
                            GIMBAL_SERVO_NEUTRAL_PULSE,
                            GIMBAL_STARTUP_RESET_MOVE_TIME_MS) != 0) {
            return false;
        }
    }

    s_last_yaw_angle_deg = 0.0f;
    s_last_pitch_angle_deg = 0.0f;
    return true;
}

static bool gimbal_prepare_main_axes_before_load(void)
{
    if (!gimbal_prepare_startup_pose(GIMBAL_YAW_SERVO_ID, 0.0f, GIMBAL_STARTUP_PREPARE_MOVE_TIME_MS)) {
        return false;
    }

    if (!gimbal_prepare_startup_pose(GIMBAL_PITCH_SERVO_ID, 0.0f, GIMBAL_STARTUP_PREPARE_MOVE_TIME_MS)) {
        return false;
    }

    return true;
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

static float yaw_pulse_to_angle_deg(int16_t pulse)
{
    return GIMBAL_YAW_MIN_ANGLE_DEG +
           ((float)(pulse - (int16_t)GIMBAL_YAW_MIN_PULSE) *
            (GIMBAL_YAW_MAX_ANGLE_DEG - GIMBAL_YAW_MIN_ANGLE_DEG) /
            (float)(GIMBAL_YAW_MAX_PULSE - GIMBAL_YAW_MIN_PULSE));
}

static float pitch_pulse_to_angle_deg(int16_t pulse)
{
    return GIMBAL_PITCH_MIN_ANGLE_DEG +
           ((float)(pulse - (int16_t)GIMBAL_PITCH_MIN_PULSE) *
            (GIMBAL_PITCH_MAX_ANGLE_DEG - GIMBAL_PITCH_MIN_ANGLE_DEG) /
            (float)(GIMBAL_PITCH_MAX_PULSE - GIMBAL_PITCH_MIN_PULSE));
}

static uint16_t yaw_angle_deg_to_pulse(float angle_deg)
{
    float clamped = clamp_f32(angle_deg, GIMBAL_YAW_MIN_ANGLE_DEG, GIMBAL_YAW_MAX_ANGLE_DEG);

    return (uint16_t)(GIMBAL_YAW_MIN_PULSE +
                      ((clamped - GIMBAL_YAW_MIN_ANGLE_DEG) *
                       (float)(GIMBAL_YAW_MAX_PULSE - GIMBAL_YAW_MIN_PULSE) /
                       (GIMBAL_YAW_MAX_ANGLE_DEG - GIMBAL_YAW_MIN_ANGLE_DEG)));
}

static uint16_t pitch_angle_deg_to_pulse(float angle_deg)
{
    float clamped = clamp_f32(angle_deg, GIMBAL_PITCH_MIN_ANGLE_DEG, GIMBAL_PITCH_MAX_ANGLE_DEG);

    return (uint16_t)(GIMBAL_PITCH_MIN_PULSE +
                      ((clamped - GIMBAL_PITCH_MIN_ANGLE_DEG) *
                       (float)(GIMBAL_PITCH_MAX_PULSE - GIMBAL_PITCH_MIN_PULSE) /
                       (GIMBAL_PITCH_MAX_ANGLE_DEG - GIMBAL_PITCH_MIN_ANGLE_DEG)));
}

static bool gimbal_status_readback_enabled(void)
{
    return MASTER_GIMBAL_STATUS_READBACK_ENABLE != 0;
}

static float abs_f32(float value)
{
    return (value < 0.0f) ? -value : value;
}

static float max_f32(float left, float right)
{
    return (left > right) ? left : right;
}

static float gimbal_apply_deadzone(float value, float deadzone)
{
    return (abs_f32(value) < deadzone) ? 0.0f : value;
}

static float gimbal_tracking_response_ratio(float cmd_mag)
{
    if (cmd_mag <= GIMBAL_TRACK_CONTROL_FAST_CMD_LOW) {
        return 0.0f;
    }

    if (cmd_mag >= GIMBAL_TRACK_CONTROL_FAST_CMD_HIGH) {
        return 1.0f;
    }

    return (cmd_mag - GIMBAL_TRACK_CONTROL_FAST_CMD_LOW) /
           (GIMBAL_TRACK_CONTROL_FAST_CMD_HIGH - GIMBAL_TRACK_CONTROL_FAST_CMD_LOW);
}

static bool gimbal_delta_reversed(float current_delta_deg, float previous_delta_deg)
{
    if ((abs_f32(current_delta_deg) < GIMBAL_TRACK_CONTROL_REVERSAL_THRESHOLD_DEG) ||
        (abs_f32(previous_delta_deg) < GIMBAL_TRACK_CONTROL_REVERSAL_THRESHOLD_DEG)) {
        return false;
    }

    return ((current_delta_deg > 0.0f) && (previous_delta_deg < 0.0f)) ||
           ((current_delta_deg < 0.0f) && (previous_delta_deg > 0.0f));
}

static float gimbal_accumulate_small_delta(float delta_deg,
                                           float min_move_deg,
                                           float *pending_delta_deg)
{
    float combined_delta_deg;

    if (pending_delta_deg == NULL) {
        return delta_deg;
    }

    combined_delta_deg = delta_deg + *pending_delta_deg;
    if (abs_f32(combined_delta_deg) < min_move_deg) {
        *pending_delta_deg = combined_delta_deg;
        return 0.0f;
    }

    *pending_delta_deg = 0.0f;
    return combined_delta_deg;
}

static float gimbal_tracking_gain_scale(const app_gimbal_tracking_observation_t *observation)
{
    float gain = 1.0f;

    if (observation == NULL) {
        return gain;
    }

    if (observation->predicted) {
        gain *= GIMBAL_TRACK_CONTROL_PREDICTED_GAIN;
    }

    if (observation->confidence < GIMBAL_TRACK_CONTROL_LOW_CONF_THRESH) {
        gain *= GIMBAL_TRACK_CONTROL_LOW_CONF_GAIN;
    }

    return gain;
}

static const char *gimbal_tracking_source_name(bool from_tracking_summary)
{
    return from_tracking_summary ? "summary" : "result-fallback";
}

static bool gimbal_read_positions(int16_t *yaw_position, int16_t *pitch_position)
{
    int ret;

    if (!s_ready || !gimbal_status_readback_enabled()) {
        return false;
    }

    if (yaw_position != NULL) {
        ret = bus_servo_read_position(&s_servo, GIMBAL_YAW_SERVO_ID, yaw_position);
        if (ret != 0) {
            MASTER_LOG_WARN("[MASTER][GIMBAL] yaw read failed id=%u err=%d\r\n",
                            (unsigned)GIMBAL_YAW_SERVO_ID,
                            ret);
            return false;
        }
    }

    gimbal_delay_ms(GIMBAL_MOVE_INTER_COMMAND_DELAY_MS);

    if (pitch_position != NULL) {
        ret = bus_servo_read_position(&s_servo, GIMBAL_PITCH_SERVO_ID, pitch_position);
        if (ret != 0) {
            MASTER_LOG_WARN("[MASTER][GIMBAL] pitch read failed id=%u err=%d\r\n",
                            (unsigned)GIMBAL_PITCH_SERVO_ID,
                            ret);
            return false;
        }
    }

    return true;
}

static void gimbal_log_status(const char *reason)
{
    int16_t yaw_position = 0;
    int16_t pitch_position = 0;

    if (!gimbal_read_positions(&yaw_position, &pitch_position)) {
        MASTER_LOG_INFO("[MASTER][GIMBAL] status(%s) yaw=%.1fdeg pitch=%.1fdeg cached=1\r\n",
                        reason != NULL ? reason : "unknown",
                        (double)s_last_yaw_angle_deg,
                        (double)s_last_pitch_angle_deg);
        return;
    }

    MASTER_LOG_INFO("[MASTER][GIMBAL] status(%s) yaw=%d(%.1fdeg) pitch=%d(%.1fdeg)\r\n",
                    reason != NULL ? reason : "unknown",
                    (int)yaw_position,
                    yaw_pulse_to_angle_deg(yaw_position),
                    (int)pitch_position,
                    pitch_pulse_to_angle_deg(pitch_position));
}

static int gimbal_move_raw(uint16_t yaw_pulse, uint16_t pitch_pulse, uint16_t time_ms)
{
    int ret;

    ret = bus_servo_move_raw(&s_servo, GIMBAL_YAW_SERVO_ID, yaw_pulse, time_ms);
    if (ret != 0) {
        MASTER_LOG_WARN("[MASTER][GIMBAL] yaw move failed id=%u pulse=%u err=%d\r\n",
                        (unsigned)GIMBAL_YAW_SERVO_ID,
                        (unsigned)yaw_pulse,
                        ret);
        return ret;
    }

    gimbal_delay_ms(GIMBAL_MOVE_INTER_COMMAND_DELAY_MS);

    ret = bus_servo_move_raw(&s_servo, GIMBAL_PITCH_SERVO_ID, pitch_pulse, time_ms);
    if (ret != 0) {
        MASTER_LOG_WARN("[MASTER][GIMBAL] pitch move failed id=%u pulse=%u err=%d\r\n",
                        (unsigned)GIMBAL_PITCH_SERVO_ID,
                        (unsigned)pitch_pulse,
                        ret);
        return ret;
    }

    return 0;
}

static app_gimbal_control_result_t gimbal_apply_angles(float yaw_angle_deg,
                                                       float pitch_angle_deg,
                                                       uint16_t time_ms,
                                                       bool verbose_log,
                                                       const char *status_reason)
{
    uint16_t yaw_pulse;
    uint16_t pitch_pulse;

    if (!s_ready) {
        MASTER_LOG_WARN("[MASTER][GIMBAL] move request ignored, servo backend unavailable\r\n");
        return APP_GIMBAL_CONTROL_NO_CHANGE;
    }

    yaw_pulse = yaw_angle_deg_to_pulse(yaw_angle_deg);
    pitch_pulse = pitch_angle_deg_to_pulse(pitch_angle_deg);

    if (gimbal_move_raw(yaw_pulse, pitch_pulse, time_ms) != 0) {
        return APP_GIMBAL_CONTROL_NO_CHANGE;
    }

    s_last_yaw_angle_deg = clamp_f32(yaw_angle_deg, GIMBAL_YAW_MIN_ANGLE_DEG, GIMBAL_YAW_MAX_ANGLE_DEG);
    s_last_pitch_angle_deg = clamp_f32(pitch_angle_deg, GIMBAL_PITCH_MIN_ANGLE_DEG, GIMBAL_PITCH_MAX_ANGLE_DEG);

    if (verbose_log) {
        MASTER_LOG_INFO("[MASTER][GIMBAL] move yaw=%.1fdeg pitch=%.1fdeg time=%u\r\n",
                        (double)s_last_yaw_angle_deg,
                        (double)s_last_pitch_angle_deg,
                        (unsigned)time_ms);
        gimbal_log_status(status_reason != NULL ? status_reason : "move-to-angles");
    }

    return APP_GIMBAL_CONTROL_ACCEPTED;
}

static bool tracking_observation_can_drive(const app_gimbal_tracking_observation_t *observation)
{
    if ((observation == NULL) || !s_ready) {
        return false;
    }

    if (!observation->tracking_active || observation->freeze_timed_out) {
        return false;
    }

    if (!(observation->valid || observation->frozen)) {
        return false;
    }

    return (abs_f32(observation->yaw_cmd) >= GIMBAL_TRACK_CONTROL_MIN_CMD) ||
           (abs_f32(observation->pitch_cmd) >= GIMBAL_TRACK_CONTROL_MIN_CMD);
}

static void gimbal_apply_tracking_drive(const app_gimbal_tracking_observation_t *observation)
{
    float next_yaw_deg;
    float next_pitch_deg;
    float pitch_filter_alpha;
    float drive_gain;
    float cmd_mag;
    float response_ratio;
    float yaw_filter_alpha;
    float yaw_response_boost;
    float pitch_response_boost;
    float yaw_cmd;
    float pitch_cmd;
    float yaw_delta_deg;
    float pitch_delta_deg;
    uint32_t now_ms;
    bool drive_active;
    bool fast_start;
    bool should_log;

#if !MASTER_GIMBAL_TRACK_CONTROL_ENABLE
    (void)observation;
    return;
#else
    if (observation == NULL) {
        return;
    }

    now_ms = observation->updated_ms;
    drive_active = tracking_observation_can_drive(observation);

    if (!drive_active) {
        if (s_last_tracking_drive_active) {
            MASTER_LOG_INFO("[MASTER][GIMBAL] track drive hold active=%u valid=%u frozen=%u timeout=%u\r\n",
                            observation->tracking_active ? 1u : 0u,
                            observation->valid ? 1u : 0u,
                            observation->frozen ? 1u : 0u,
                            observation->freeze_timed_out ? 1u : 0u);
        }
        s_last_tracking_drive_active = false;
        s_filtered_tracking_yaw_cmd = 0.0f;
        s_filtered_tracking_pitch_cmd = 0.0f;
        s_tracking_cmd_filter_initialized = false;
        s_last_tracking_yaw_delta_deg = 0.0f;
        s_last_tracking_pitch_delta_deg = 0.0f;
        s_pending_yaw_delta_deg = 0.0f;
        s_pending_pitch_delta_deg = 0.0f;
        return;
    }

    if ((s_last_tracking_control_ms != 0u) &&
        ((uint32_t)(now_ms - s_last_tracking_control_ms) < GIMBAL_TRACK_CONTROL_INTERVAL_MS)) {
        return;
    }

    yaw_cmd = gimbal_apply_deadzone(clamp_f32(observation->yaw_cmd, -1.0f, 1.0f),
                                    GIMBAL_TRACK_CONTROL_YAW_DEADZONE);
    pitch_cmd = gimbal_apply_deadzone(clamp_f32(observation->pitch_cmd, -1.0f, 1.0f),
                                      GIMBAL_TRACK_CONTROL_PITCH_DEADZONE);
    drive_gain = gimbal_tracking_gain_scale(observation);
    yaw_cmd *= drive_gain;
    pitch_cmd *= drive_gain;
    cmd_mag = max_f32(abs_f32(yaw_cmd), abs_f32(pitch_cmd));
    response_ratio = gimbal_tracking_response_ratio(cmd_mag);
    yaw_response_boost = 1.0f + (response_ratio * (GIMBAL_TRACK_CONTROL_YAW_FAST_RESP_MAX - 1.0f));
    pitch_response_boost = 1.0f + (response_ratio * (GIMBAL_TRACK_CONTROL_PITCH_FAST_RESP_MAX - 1.0f));
    yaw_filter_alpha = GIMBAL_TRACK_CONTROL_YAW_ALPHA -
                       (response_ratio * (GIMBAL_TRACK_CONTROL_YAW_ALPHA - GIMBAL_TRACK_CONTROL_YAW_ALPHA_FAST));
    pitch_filter_alpha = GIMBAL_TRACK_CONTROL_PITCH_ALPHA -
                         (response_ratio * (GIMBAL_TRACK_CONTROL_PITCH_ALPHA - GIMBAL_TRACK_CONTROL_PITCH_ALPHA_FAST));
    fast_start = (!s_tracking_cmd_filter_initialized) ||
                 ((!s_last_tracking_drive_active) && (cmd_mag >= GIMBAL_TRACK_CONTROL_FAST_START_CMD));

    if (fast_start) {
        s_filtered_tracking_yaw_cmd = yaw_cmd;
        s_filtered_tracking_pitch_cmd = pitch_cmd;
        s_tracking_cmd_filter_initialized = true;
    } else {
        s_filtered_tracking_yaw_cmd = (yaw_filter_alpha * s_filtered_tracking_yaw_cmd) +
                                      ((1.0f - yaw_filter_alpha) * yaw_cmd);
        s_filtered_tracking_pitch_cmd = (pitch_filter_alpha * s_filtered_tracking_pitch_cmd) +
                                        ((1.0f - pitch_filter_alpha) * pitch_cmd);
    }

    yaw_delta_deg = s_filtered_tracking_yaw_cmd * GIMBAL_TRACK_CONTROL_YAW_STEP_DEG * yaw_response_boost;
    pitch_delta_deg = s_filtered_tracking_pitch_cmd * GIMBAL_TRACK_CONTROL_PITCH_STEP_DEG * pitch_response_boost;

    if (gimbal_delta_reversed(yaw_delta_deg, s_last_tracking_yaw_delta_deg)) {
        yaw_delta_deg *= GIMBAL_TRACK_CONTROL_YAW_REVERSAL_DAMP;
    }

    if ((abs_f32(pitch_delta_deg) >= GIMBAL_TRACK_CONTROL_PITCH_REVERSAL_THRESHOLD_DEG) &&
        (abs_f32(s_last_tracking_pitch_delta_deg) >= GIMBAL_TRACK_CONTROL_PITCH_REVERSAL_THRESHOLD_DEG) &&
        (((pitch_delta_deg > 0.0f) && (s_last_tracking_pitch_delta_deg < 0.0f)) ||
         ((pitch_delta_deg < 0.0f) && (s_last_tracking_pitch_delta_deg > 0.0f)))) {
        pitch_delta_deg *= GIMBAL_TRACK_CONTROL_PITCH_REVERSAL_DAMP;
    }

    yaw_delta_deg = gimbal_accumulate_small_delta(yaw_delta_deg,
                                                  GIMBAL_TRACK_CONTROL_YAW_MIN_MOVE_DEG,
                                                  &s_pending_yaw_delta_deg);
    pitch_delta_deg = gimbal_accumulate_small_delta(pitch_delta_deg,
                                                    GIMBAL_TRACK_CONTROL_PITCH_MIN_MOVE_DEG,
                                                    &s_pending_pitch_delta_deg);

    if (abs_f32(yaw_delta_deg) < GIMBAL_TRACK_CONTROL_YAW_MIN_MOVE_DEG) {
        yaw_delta_deg = 0.0f;
    }
    if (abs_f32(pitch_delta_deg) < GIMBAL_TRACK_CONTROL_PITCH_MIN_MOVE_DEG) {
        pitch_delta_deg = 0.0f;
    }

    next_yaw_deg = clamp_f32(s_last_yaw_angle_deg + yaw_delta_deg,
                             GIMBAL_YAW_MIN_ANGLE_DEG,
                             GIMBAL_YAW_MAX_ANGLE_DEG);
    next_pitch_deg = clamp_f32(s_last_pitch_angle_deg + pitch_delta_deg,
                               GIMBAL_PITCH_MIN_ANGLE_DEG,
                               GIMBAL_PITCH_MAX_ANGLE_DEG);

    if ((abs_f32(next_yaw_deg - s_last_yaw_angle_deg) < 0.01f) &&
        (abs_f32(next_pitch_deg - s_last_pitch_angle_deg) < 0.01f)) {
        s_last_tracking_drive_active = true;
        return;
    }

    if (gimbal_apply_angles(next_yaw_deg,
                            next_pitch_deg,
                            GIMBAL_TRACK_CONTROL_MOVE_TIME_MS,
                            false,
                            NULL) != APP_GIMBAL_CONTROL_ACCEPTED) {
        return;
    }

    should_log = !s_last_tracking_drive_active ||
                 (s_last_tracking_control_log_ms == 0u) ||
                 ((uint32_t)(now_ms - s_last_tracking_control_log_ms) >= GIMBAL_TRACK_CONTROL_LOG_INTERVAL_MS);
    if (should_log) {
        MASTER_LOG_INFO("[MASTER][GIMBAL] track drive valid=%u pred=%u conf=%u gain=%.2f resp=(%.2f,%.2f) frozen=%u timeout=%u cmd=(%.3f,%.3f) delta=(%.2f,%.2f) aim=(%.1f,%.1f)\r\n",
                        observation->valid ? 1u : 0u,
                observation->predicted ? 1u : 0u,
                (unsigned)observation->confidence,
                (double)drive_gain,
                (double)yaw_response_boost,
                (double)pitch_response_boost,
                        observation->frozen ? 1u : 0u,
                        observation->freeze_timed_out ? 1u : 0u,
                        (double)s_filtered_tracking_yaw_cmd,
                        (double)s_filtered_tracking_pitch_cmd,
                        (double)yaw_delta_deg,
                        (double)pitch_delta_deg,
                        (double)s_last_yaw_angle_deg,
                        (double)s_last_pitch_angle_deg);
        s_last_tracking_control_log_ms = now_ms;
    }

    s_last_tracking_yaw_delta_deg = yaw_delta_deg;
    s_last_tracking_pitch_delta_deg = pitch_delta_deg;
    s_last_tracking_control_ms = now_ms;
    s_last_tracking_drive_active = true;
#endif
}

const char *app_gimbal_control_preset_name(app_gimbal_preset_t preset)
{
    const app_gimbal_preset_desc_t *preset_desc = gimbal_find_preset(preset);

    if (preset_desc == NULL) {
        return "unknown";
    }

    return preset_desc->name;
}

app_gimbal_control_result_t app_gimbal_control_apply_preset(app_gimbal_preset_t preset)
{
    const app_gimbal_preset_desc_t *preset_desc = gimbal_find_preset(preset);

    if (preset_desc == NULL) {
        MASTER_LOG_WARN("[MASTER][GIMBAL] preset request ignored, preset=%u unsupported\r\n",
                        (unsigned)preset);
        return APP_GIMBAL_CONTROL_NO_CHANGE;
    }

    return app_gimbal_control_move_to_angles(preset_desc->yaw_angle_deg,
                                             preset_desc->pitch_angle_deg,
                                             preset_desc->move_time_ms);
}

static app_gimbal_control_result_t gimbal_apply_preset_with_time(app_gimbal_preset_t preset,
                                                                 uint16_t move_time_ms,
                                                                 bool verbose_log,
                                                                 const char *status_reason)
{
    const app_gimbal_preset_desc_t *preset_desc = gimbal_find_preset(preset);

    if (preset_desc == NULL) {
        MASTER_LOG_WARN("[MASTER][GIMBAL] preset request ignored, preset=%u unsupported\r\n",
                        (unsigned)preset);
        return APP_GIMBAL_CONTROL_NO_CHANGE;
    }

    return gimbal_apply_angles(preset_desc->yaw_angle_deg,
                               preset_desc->pitch_angle_deg,
                               move_time_ms,
                               verbose_log,
                               status_reason);
}

void app_gimbal_control_init(void)
{
    int ret;

    ret = board_servo_init(&s_servo);
    if (ret != 0) {
        s_ready = false;
        MASTER_LOG_WARN("[MASTER][GIMBAL] servo controller init failed=%d\r\n", ret);
        return;
    }

    gimbal_delay_ms(GIMBAL_STARTUP_POWERUP_DELAY_MS);
    gimbal_init_aux_servo(GIMBAL_AUX_SERVO_2_ID);
    gimbal_init_aux_servo(GIMBAL_AUX_SERVO_3_ID);
    gimbal_init_aux_servo(GIMBAL_AUX_SERVO_5_ID);

    (void)gimbal_prepare_main_axes_before_load();
    gimbal_delay_ms(GIMBAL_STARTUP_SERVO_SETTLE_MS);

    (void)bus_servo_set_load(&s_servo, GIMBAL_YAW_SERVO_ID, true);
    gimbal_delay_ms(GIMBAL_STARTUP_SERVO_SETTLE_MS);
    (void)bus_servo_set_load(&s_servo, GIMBAL_PITCH_SERVO_ID, true);
    gimbal_delay_ms(GIMBAL_STARTUP_GIMBAL_READY_DELAY_MS);

    s_ready = true;
    MASTER_LOG_INFO("[MASTER][GIMBAL] servo controller ready, yaw=%u pitch=%u\r\n",
                    (unsigned)GIMBAL_YAW_SERVO_ID,
                    (unsigned)GIMBAL_PITCH_SERVO_ID);

    if (gimbal_startup_reset_all_servos()) {
        MASTER_LOG_INFO("[MASTER][GIMBAL] all servos reset at startup\r\n");
        gimbal_log_status("startup-reset");
    }

    if (gimbal_apply_preset_with_time(APP_GIMBAL_PRESET_CENTER_PREVIEW,
                                      GIMBAL_STARTUP_INIT_MOVE_TIME_MS,
                                      true,
                                      "move-to-angles") == APP_GIMBAL_CONTROL_ACCEPTED) {
        MASTER_LOG_INFO("[MASTER][GIMBAL] demo init pose applied at startup\r\n");
        gimbal_log_status("startup");
    }
}

app_gimbal_control_result_t app_gimbal_control_set_tracking(bool enabled)
{
    if (!s_ready) {
        MASTER_LOG_WARN("[MASTER][GIMBAL] tracking request ignored, servo backend unavailable\r\n");
        return APP_GIMBAL_CONTROL_NO_CHANGE;
    }

    if (enabled) {
        if (app_gimbal_control_apply_preset(APP_GIMBAL_PRESET_TRACKING) != APP_GIMBAL_CONTROL_ACCEPTED) {
            return APP_GIMBAL_CONTROL_NO_CHANGE;
        }
    } else {
        if (gimbal_apply_preset_with_time(APP_GIMBAL_PRESET_CENTER_PREVIEW,
                                          GIMBAL_TRACKING_STOP_MOVE_TIME_MS,
                                          true,
                                          "move-to-angles") != APP_GIMBAL_CONTROL_ACCEPTED) {
            return APP_GIMBAL_CONTROL_NO_CHANGE;
        }
    }

    s_last_tracking_control_ms = 0u;
    s_last_tracking_control_log_ms = 0u;
    s_last_tracking_drive_active = false;

    MASTER_LOG_INFO("[MASTER][GIMBAL] tracking %s, pose=%s\r\n",
                    enabled ? "START" : "STOP",
                    app_gimbal_control_preset_name(enabled ? APP_GIMBAL_PRESET_TRACKING : APP_GIMBAL_PRESET_CENTER_PREVIEW));
    gimbal_log_status(enabled ? "tracking-start" : "tracking-stop");
    return APP_GIMBAL_CONTROL_ACCEPTED;
}

app_gimbal_control_result_t app_gimbal_control_move_to_angles(float yaw_angle_deg,
                                                              float pitch_angle_deg,
                                                              uint16_t time_ms)
{
    return gimbal_apply_angles(yaw_angle_deg,
                               pitch_angle_deg,
                               time_ms,
                               true,
                               "move-to-angles");
}

bool app_gimbal_control_read_status(app_gimbal_control_status_t *status)
{
    int16_t yaw_position = 0;
    int16_t pitch_position = 0;
    bool positions_valid;

    if (status == NULL) {
        return false;
    }

    positions_valid = gimbal_read_positions(&yaw_position, &pitch_position);

    status->ready = s_ready;
    status->yaw_position_valid = positions_valid;
    status->pitch_position_valid = positions_valid;
    status->yaw_position = yaw_position;
    status->pitch_position = pitch_position;
    status->yaw_angle_deg = positions_valid ? yaw_pulse_to_angle_deg(yaw_position) : s_last_yaw_angle_deg;
    status->pitch_angle_deg = positions_valid ? pitch_pulse_to_angle_deg(pitch_position) : s_last_pitch_angle_deg;
    return positions_valid;
}

void app_gimbal_control_observe_tracking_input(const app_gimbal_tracking_observation_t *observation)
{
    uint32_t now_ms;
    bool should_log;

    if (observation == NULL) {
        return;
    }

    s_tracking_observation = *observation;
    s_have_tracking_observation = true;
    now_ms = gimbal_tracking_observation_now_ms();

    gimbal_apply_tracking_drive(observation);

    should_log = (observation->valid != s_last_tracking_observation_valid) ||
                 (observation->frozen != s_last_tracking_observation_frozen) ||
                 (observation->freeze_timed_out != s_last_tracking_observation_timeout);

    if (!should_log && observation->tracking_active) {
        should_log = ((s_last_tracking_observation_log_ms == 0u) ||
                     ((uint32_t)(now_ms - s_last_tracking_observation_log_ms) >= GIMBAL_TRACK_INPUT_LOG_INTERVAL_MS));
    }

    if (!should_log) {
        return;
    }

    MASTER_LOG_INFO("[MASTER][GIMBAL] track input source=%s active=%u valid=%u lost=%u frozen=%u timeout=%u err=(%ld,%ld) box=%ldx%ld cmd=(%.3f,%.3f) conf=%u\r\n",
                    gimbal_tracking_source_name(observation->from_tracking_summary),
                    observation->tracking_active ? 1u : 0u,
                    observation->valid ? 1u : 0u,
                    observation->lost ? 1u : 0u,
                    observation->frozen ? 1u : 0u,
                    observation->freeze_timed_out ? 1u : 0u,
                    (long)observation->error_x,
                    (long)observation->error_y,
                    (long)observation->box_w,
                    (long)observation->box_h,
                    (double)observation->yaw_cmd,
                    (double)observation->pitch_cmd,
                    (unsigned)observation->confidence);

    s_last_tracking_observation_valid = observation->valid;
    s_last_tracking_observation_frozen = observation->frozen;
    s_last_tracking_observation_timeout = observation->freeze_timed_out;
    s_last_tracking_observation_log_ms = now_ms;
}

bool app_gimbal_control_get_tracking_observation(app_gimbal_tracking_observation_t *observation)
{
    if ((observation == NULL) || !s_have_tracking_observation) {
        return false;
    }

    *observation = s_tracking_observation;
    return true;
}