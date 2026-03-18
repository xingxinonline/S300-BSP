#include "app_gimbal_control.h"

#include <stddef.h>

#include "board.h"
#include "bus_servo.h"
#include "master_log.h"
#include "s300.h"

#define GIMBAL_YAW_SERVO_ID              6u
#define GIMBAL_PITCH_SERVO_ID            4u

#define GIMBAL_YAW_MIN_PULSE             125u
#define GIMBAL_YAW_MAX_PULSE             875u
#define GIMBAL_YAW_MIN_ANGLE_DEG         (-90.0f)
#define GIMBAL_YAW_MAX_ANGLE_DEG         (90.0f)

#define GIMBAL_PITCH_MIN_PULSE           125u
#define GIMBAL_PITCH_MAX_PULSE           500u
#define GIMBAL_PITCH_MIN_ANGLE_DEG       (-90.0f)
#define GIMBAL_PITCH_MAX_ANGLE_DEG       (0.0f)

#define GIMBAL_ENABLE_LOAD_DELAY_LOOPS   40000u
#define GIMBAL_INTER_COMMAND_DELAY_LOOPS 40000u
#define GIMBAL_TRACKING_MOVE_TIME_MS     700u
#define GIMBAL_PARKING_MOVE_TIME_MS      700u
#define GIMBAL_PREVIEW_MOVE_TIME_MS      700u

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

static bool s_tracking = false;
static bool s_ready = false;
static bus_servo_t s_servo;
static float s_last_yaw_angle_deg = 0.0f;
static float s_last_pitch_angle_deg = GIMBAL_PITCH_MIN_ANGLE_DEG;

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

static void gimbal_delay_loops(uint32_t loops)
{
    volatile uint32_t index;

    for (index = 0u; index < loops; index++) {
        __NOP();
    }
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

static bool gimbal_read_positions(int16_t *yaw_position, int16_t *pitch_position)
{
    int ret;

    if (!s_ready) {
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

    gimbal_delay_loops(GIMBAL_INTER_COMMAND_DELAY_LOOPS);

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
        return;
    }

    MASTER_LOG_INFO("[MASTER][GIMBAL] status(%s) yaw=%d(%.1fdeg) pitch=%d(%.1fdeg) tracking=%u\r\n",
                    reason != NULL ? reason : "unknown",
                    (int)yaw_position,
                    yaw_pulse_to_angle_deg(yaw_position),
                    (int)pitch_position,
                    pitch_pulse_to_angle_deg(pitch_position),
                    s_tracking ? 1u : 0u);
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

    gimbal_delay_loops(GIMBAL_INTER_COMMAND_DELAY_LOOPS);

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

void app_gimbal_control_init(void)
{
    int ret;

    s_tracking = false;

    ret = board_servo_init(&s_servo);
    if (ret != 0) {
        s_ready = false;
        MASTER_LOG_WARN("[MASTER][GIMBAL] servo controller init failed=%d\r\n", ret);
        return;
    }

    gimbal_delay_loops(GIMBAL_ENABLE_LOAD_DELAY_LOOPS);
    (void)bus_servo_set_load(&s_servo, GIMBAL_YAW_SERVO_ID, true);
    gimbal_delay_loops(GIMBAL_INTER_COMMAND_DELAY_LOOPS);
    (void)bus_servo_set_load(&s_servo, GIMBAL_PITCH_SERVO_ID, true);

    s_ready = true;
    MASTER_LOG_INFO("[MASTER][GIMBAL] servo controller ready, yaw=%u pitch=%u\r\n",
                    (unsigned)GIMBAL_YAW_SERVO_ID,
                    (unsigned)GIMBAL_PITCH_SERVO_ID);

    if (app_gimbal_control_apply_preset(APP_GIMBAL_PRESET_PARKING) == APP_GIMBAL_CONTROL_ACCEPTED) {
        MASTER_LOG_INFO("[MASTER][GIMBAL] parking pose applied at startup\r\n");
        gimbal_log_status("startup");
    }
}

app_gimbal_control_result_t app_gimbal_control_set_tracking(bool enabled)
{
    if (s_tracking == enabled) {
        MASTER_LOG_DEBUG("[MASTER][GIMBAL] tracking already %u\r\n", enabled ? 1u : 0u);
        return APP_GIMBAL_CONTROL_NO_CHANGE;
    }

    if (!s_ready) {
        MASTER_LOG_WARN("[MASTER][GIMBAL] tracking request ignored, servo backend unavailable\r\n");
        return APP_GIMBAL_CONTROL_NO_CHANGE;
    }

    if (enabled) {
        if (app_gimbal_control_apply_preset(APP_GIMBAL_PRESET_TRACKING) != APP_GIMBAL_CONTROL_ACCEPTED) {
            return APP_GIMBAL_CONTROL_NO_CHANGE;
        }
    } else {
        if (app_gimbal_control_apply_preset(APP_GIMBAL_PRESET_PARKING) != APP_GIMBAL_CONTROL_ACCEPTED) {
            return APP_GIMBAL_CONTROL_NO_CHANGE;
        }
    }

    s_tracking = enabled;
    MASTER_LOG_INFO("[MASTER][GIMBAL] tracking %s, pose=%s\r\n",
                    enabled ? "START" : "STOP",
                    app_gimbal_control_preset_name(enabled ? APP_GIMBAL_PRESET_TRACKING : APP_GIMBAL_PRESET_PARKING));
    gimbal_log_status(enabled ? "tracking-start" : "tracking-stop");
    return APP_GIMBAL_CONTROL_ACCEPTED;
}

app_gimbal_control_result_t app_gimbal_control_move_to_angles(float yaw_angle_deg,
                                                              float pitch_angle_deg,
                                                              uint16_t time_ms)
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

    MASTER_LOG_INFO("[MASTER][GIMBAL] move yaw=%.1fdeg pitch=%.1fdeg time=%u\r\n",
                    s_last_yaw_angle_deg,
                    s_last_pitch_angle_deg,
                    (unsigned)time_ms);
    gimbal_log_status("move-to-angles");
    return APP_GIMBAL_CONTROL_ACCEPTED;
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
    status->tracking = s_tracking;
    status->yaw_position_valid = positions_valid;
    status->pitch_position_valid = positions_valid;
    status->yaw_position = yaw_position;
    status->pitch_position = pitch_position;
    status->yaw_angle_deg = positions_valid ? yaw_pulse_to_angle_deg(yaw_position) : s_last_yaw_angle_deg;
    status->pitch_angle_deg = positions_valid ? pitch_pulse_to_angle_deg(pitch_position) : s_last_pitch_angle_deg;
    return positions_valid;
}

bool app_gimbal_control_is_tracking(void)
{
    return s_tracking;
}