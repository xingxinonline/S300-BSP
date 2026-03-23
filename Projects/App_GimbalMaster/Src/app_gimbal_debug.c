#include "app_gimbal_debug.h"

#include <stdbool.h>
#include <stddef.h>

#include "app_gimbal_control.h"
#include "app_runtime_state.h"
#include "master_log.h"

#ifndef MASTER_GIMBAL_DEBUG_BOOT_DEMO
#define MASTER_GIMBAL_DEBUG_BOOT_DEMO 0
#endif

#ifndef MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS
#define MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS 2500u
#endif

#ifndef MASTER_GIMBAL_DEBUG_START_DELAY_MS
#define MASTER_GIMBAL_DEBUG_START_DELAY_MS 1500u
#endif

typedef struct {
    app_gimbal_preset_t preset;
} gimbal_debug_step_t;

static const gimbal_debug_step_t s_debug_steps[] = {
    { APP_GIMBAL_PRESET_CENTER_PREVIEW },
    { APP_GIMBAL_PRESET_LEFT_PREVIEW },
    { APP_GIMBAL_PRESET_RIGHT_PREVIEW },
    { APP_GIMBAL_PRESET_UP_PREVIEW },
    { APP_GIMBAL_PRESET_CENTER_PREVIEW },
    { APP_GIMBAL_PRESET_PARKING },
};

static app_gimbal_debug_millis_fn_t s_millis_fn = NULL;
static bool s_demo_enabled = false;
static bool s_demo_finished = false;
static bool s_demo_started = false;
static bool s_step_status_logged = false;
static uint32_t s_demo_start_ms = 0u;
static uint32_t s_last_step_ms = 0u;
static uint32_t s_step_index = 0u;

static void gimbal_debug_log_status(const char *reason)
{
    app_gimbal_control_status_t status;

    if (!app_gimbal_control_read_status(&status)) {
        MASTER_LOG_WARN("[MASTER][GIMBAL][DEBUG] status(%s) read failed ready=%u\r\n",
                        reason != NULL ? reason : "unknown",
                        status.ready ? 1u : 0u);
        return;
    }

    MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] status(%s) yaw=%d(%.1fdeg) pitch=%d(%.1fdeg) ready=%u\r\n",
                    reason != NULL ? reason : "unknown",
                    (int)status.yaw_position,
                    (double)status.yaw_angle_deg,
                    (int)status.pitch_position,
                    (double)status.pitch_angle_deg,
                    status.ready ? 1u : 0u);
}

static void gimbal_debug_run_step(uint32_t step_index)
{
    const gimbal_debug_step_t *step;

    if (step_index >= (sizeof(s_debug_steps) / sizeof(s_debug_steps[0]))) {
        return;
    }

    step = &s_debug_steps[step_index];
    MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] step=%lu/%lu preset=%s\r\n",
                    (unsigned long)step_index,
                    (unsigned long)(sizeof(s_debug_steps) / sizeof(s_debug_steps[0])),
                    app_gimbal_control_preset_name(step->preset));
    (void)app_gimbal_control_apply_preset(step->preset);
}

void app_gimbal_debug_init(app_gimbal_debug_millis_fn_t millis_fn)
{
    s_millis_fn = millis_fn;
    s_step_index = 0u;
    s_last_step_ms = 0u;
    s_demo_start_ms = 0u;
    s_demo_finished = false;
    s_demo_started = false;
    s_step_status_logged = false;

#if MASTER_GIMBAL_DEBUG_BOOT_DEMO
    s_demo_enabled = true;
    MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] boot demo enabled, start_delay=%u ms interval=%u ms\r\n",
                    (unsigned)MASTER_GIMBAL_DEBUG_START_DELAY_MS,
                    (unsigned)MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS);
#else
    s_demo_enabled = false;
#endif
}

void app_gimbal_debug_tick(void)
{
    uint32_t now_ms;

    if (!s_demo_enabled || s_demo_finished || (s_millis_fn == NULL)) {
        return;
    }

    if (app_runtime_state_is_tracking()) {
        s_demo_finished = true;
        MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] boot demo stopped because tracking mode is active\r\n");
        return;
    }

    now_ms = s_millis_fn();
    if (!s_demo_started) {
        s_demo_started = true;
        s_demo_start_ms = now_ms;
        MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] boot demo armed\r\n");
    }

    if ((s_step_index == 0u) && ((now_ms - s_demo_start_ms) < MASTER_GIMBAL_DEBUG_START_DELAY_MS)) {
        return;
    }

    if ((s_step_index > 0u) && !s_step_status_logged) {
        gimbal_debug_log_status("step-settle");
        s_step_status_logged = true;
    }

    if ((s_step_index > 0u) && ((now_ms - s_last_step_ms) < MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS)) {
        return;
    }

    if (s_step_index >= (sizeof(s_debug_steps) / sizeof(s_debug_steps[0]))) {
        s_demo_finished = true;
        gimbal_debug_log_status("boot-demo-complete");
        MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] boot demo completed elapsed=%lu ms\r\n",
                        (unsigned long)(now_ms - s_demo_start_ms));
        return;
    }

    gimbal_debug_run_step(s_step_index);
    s_last_step_ms = now_ms;
    s_step_index++;
    s_step_status_logged = false;
}
