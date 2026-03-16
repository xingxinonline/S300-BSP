#include "app_runtime_state.h"

#include "master_log.h"

static bool s_recording = false;
static bool s_tracking = false;
static bool s_fill_light = false;
static app_runtime_state_t s_state = APP_RUNTIME_STATE_IDLE;

static app_runtime_state_t compose_state(void);

static void refresh_state(void)
{
    app_runtime_state_t next_state = compose_state();

    if (next_state != s_state) {
        MASTER_LOG_INFO("[MASTER][APP] runtime_state %s -> %s\r\n",
                        app_runtime_state_name(s_state),
                        app_runtime_state_name(next_state));
        s_state = next_state;
    }
}

static app_runtime_state_t compose_state(void)
{
    if (s_recording && s_tracking) {
        return APP_RUNTIME_STATE_TRACKING_RECORDING;
    }

    if (s_recording) {
        return APP_RUNTIME_STATE_RECORDING;
    }

    if (s_tracking) {
        return APP_RUNTIME_STATE_TRACKING;
    }

    return APP_RUNTIME_STATE_IDLE;
}

const char *app_runtime_state_name(app_runtime_state_t state)
{
    switch (state) {
    case APP_RUNTIME_STATE_IDLE: return "IDLE";
    case APP_RUNTIME_STATE_RECORDING: return "RECORDING";
    case APP_RUNTIME_STATE_TRACKING: return "TRACKING";
    case APP_RUNTIME_STATE_TRACKING_RECORDING: return "TRACKING_RECORDING";
    default: return "UNKNOWN";
    }
}

const char *app_runtime_event_name(app_runtime_event_t event)
{
    switch (event) {
    case APP_RUNTIME_EVENT_PHOTO: return "PHOTO";
    case APP_RUNTIME_EVENT_RECORD_START: return "RECORD_START";
    case APP_RUNTIME_EVENT_RECORD_STOP: return "RECORD_STOP";
    case APP_RUNTIME_EVENT_TRACK_START: return "TRACK_START";
    case APP_RUNTIME_EVENT_TRACK_STOP: return "TRACK_STOP";
    case APP_RUNTIME_EVENT_FILL_LIGHT_ON: return "FILL_LIGHT_ON";
    case APP_RUNTIME_EVENT_FILL_LIGHT_OFF: return "FILL_LIGHT_OFF";
    case APP_RUNTIME_EVENT_NONE:
    default:
        return "NONE";
    }
}

const char *app_runtime_event_result_name(app_runtime_event_result_t result)
{
    switch (result) {
    case APP_RUNTIME_EVENT_APPLIED:
        return "APPLIED";
    case APP_RUNTIME_EVENT_NO_CHANGE:
    default:
        return "NO_CHANGE";
    }
}

static app_runtime_event_result_t set_recording(bool enabled, app_runtime_event_t event)
{
    (void)event;

    if (s_recording == enabled) {
        MASTER_LOG_DEBUG("[MASTER][APP] event %s ignored: recording=%u\r\n",
                         app_runtime_event_name(event),
                         enabled ? 1u : 0u);
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }

    s_recording = enabled;
    refresh_state();
    return APP_RUNTIME_EVENT_APPLIED;
}

static app_runtime_event_result_t set_tracking(bool enabled, app_runtime_event_t event)
{
    (void)event;

    if (s_tracking == enabled) {
        MASTER_LOG_DEBUG("[MASTER][APP] event %s ignored: tracking=%u\r\n",
                         app_runtime_event_name(event),
                         enabled ? 1u : 0u);
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }

    s_tracking = enabled;
    refresh_state();
    return APP_RUNTIME_EVENT_APPLIED;
}

static app_runtime_event_result_t set_fill_light(bool enabled, app_runtime_event_t event)
{
    (void)event;

    if (s_fill_light == enabled) {
        MASTER_LOG_DEBUG("[MASTER][APP] event %s ignored: fill_light=%u\r\n",
                         app_runtime_event_name(event),
                         enabled ? 1u : 0u);
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }

    s_fill_light = enabled;
    return APP_RUNTIME_EVENT_APPLIED;
}

void app_runtime_state_reset(void)
{
    s_recording = false;
    s_tracking = false;
    s_fill_light = false;
    s_state = APP_RUNTIME_STATE_IDLE;
}

app_runtime_event_result_t app_runtime_state_apply_event(app_runtime_event_t event)
{
    switch (event) {
    case APP_RUNTIME_EVENT_PHOTO:
        return APP_RUNTIME_EVENT_NO_CHANGE;

    case APP_RUNTIME_EVENT_RECORD_START:
        return set_recording(true, event);

    case APP_RUNTIME_EVENT_RECORD_STOP:
        return set_recording(false, event);

    case APP_RUNTIME_EVENT_TRACK_START:
        return set_tracking(true, event);

    case APP_RUNTIME_EVENT_TRACK_STOP:
        return set_tracking(false, event);

    case APP_RUNTIME_EVENT_FILL_LIGHT_ON:
        return set_fill_light(true, event);

    case APP_RUNTIME_EVENT_FILL_LIGHT_OFF:
        return set_fill_light(false, event);

    case APP_RUNTIME_EVENT_NONE:
    default:
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }
}

app_runtime_state_t app_runtime_state_get(void)
{
    return s_state;
}

bool app_runtime_state_is_recording(void)
{
    return s_recording;
}

bool app_runtime_state_is_tracking(void)
{
    return s_tracking;
}

bool app_runtime_state_is_fill_light_enabled(void)
{
    return s_fill_light;
}