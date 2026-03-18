#include "app_gimbal_debug.h"

#include <stdbool.h>
#include <stddef.h>

#include "app_gimbal_control.h"
#include "master_log.h"

#ifndef MASTER_GIMBAL_DEBUG_BOOT_DEMO
#define MASTER_GIMBAL_DEBUG_BOOT_DEMO 0
#endif

#ifndef MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS
#define MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS 2500u
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
static uint32_t s_last_step_ms = 0u;
static uint32_t s_step_index = 0u;

static void gimbal_debug_run_step(uint32_t step_index)
{
    const gimbal_debug_step_t *step;

    if (step_index >= (sizeof(s_debug_steps) / sizeof(s_debug_steps[0]))) {
        return;
    }

    step = &s_debug_steps[step_index];
    MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] step=%lu preset=%s\r\n",
                    (unsigned long)step_index,
                    app_gimbal_control_preset_name(step->preset));
    (void)app_gimbal_control_apply_preset(step->preset);
}

void app_gimbal_debug_init(app_gimbal_debug_millis_fn_t millis_fn)
{
    s_millis_fn = millis_fn;
    s_step_index = 0u;
    s_last_step_ms = 0u;
    s_demo_finished = false;

#if MASTER_GIMBAL_DEBUG_BOOT_DEMO
    s_demo_enabled = true;
    MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] boot demo enabled, interval=%u ms\r\n",
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

    if (app_gimbal_control_is_tracking()) {
        s_demo_finished = true;
        MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] boot demo stopped because tracking mode is active\r\n");
        return;
    }

    now_ms = s_millis_fn();
    if ((s_step_index > 0u) && ((now_ms - s_last_step_ms) < MASTER_GIMBAL_DEBUG_STEP_INTERVAL_MS)) {
        return;
    }

    if (s_step_index >= (sizeof(s_debug_steps) / sizeof(s_debug_steps[0]))) {
        s_demo_finished = true;
        MASTER_LOG_INFO("[MASTER][GIMBAL][DEBUG] boot demo completed\r\n");
        return;
    }

    gimbal_debug_run_step(s_step_index);
    s_last_step_ms = now_ms;
    s_step_index++;
}