#include "app_runtime_state.h"

#include <string.h>

#include "master_log.h"

static app_runtime_snapshot_t s_snapshot = {
    .state = APP_RUNTIME_STATE_IDLE,
    .recording = false,
    .tracking = false,
    .fill_light_enabled = false,
};

static app_runtime_state_t compose_state(const app_runtime_snapshot_t *snapshot);
static app_runtime_event_result_t apply_event_to_snapshot(app_runtime_event_t event,
                                                          app_runtime_snapshot_t *snapshot);

static void refresh_state(const app_runtime_snapshot_t *previous_snapshot)
{
    app_runtime_state_t next_state;

    if (previous_snapshot == NULL) {
        return;
    }

    next_state = compose_state(&s_snapshot);

    if (next_state != s_snapshot.state) {
        MASTER_LOG_INFO("[MASTER][APP] runtime_state %s -> %s\r\n",
                        app_runtime_state_name(s_snapshot.state),
                        app_runtime_state_name(next_state));
    }

    s_snapshot.state = next_state;

    if (previous_snapshot->fill_light_enabled != s_snapshot.fill_light_enabled) {
        MASTER_LOG_INFO("[MASTER][APP] fill_light %u -> %u\r\n",
                        previous_snapshot->fill_light_enabled ? 1u : 0u,
                        s_snapshot.fill_light_enabled ? 1u : 0u);
    }
}

static app_runtime_state_t compose_state(const app_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return APP_RUNTIME_STATE_IDLE;
    }

    if (snapshot->recording && snapshot->tracking) {
        return APP_RUNTIME_STATE_TRACKING_RECORDING;
    }

    if (snapshot->recording) {
        return APP_RUNTIME_STATE_RECORDING;
    }

    if (snapshot->tracking) {
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

static app_runtime_event_result_t set_recording(bool enabled)
{
    if (s_snapshot.recording == enabled) {
        MASTER_LOG_DEBUG("[MASTER][APP] recording already %u\r\n",
                         enabled ? 1u : 0u);
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }

    s_snapshot.recording = enabled;
    return APP_RUNTIME_EVENT_APPLIED;
}

static app_runtime_event_result_t set_tracking(bool enabled)
{
    if (s_snapshot.tracking == enabled) {
        MASTER_LOG_DEBUG("[MASTER][APP] tracking already %u\r\n",
                         enabled ? 1u : 0u);
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }

    s_snapshot.tracking = enabled;
    return APP_RUNTIME_EVENT_APPLIED;
}

static app_runtime_event_result_t set_fill_light(bool enabled)
{
    if (s_snapshot.fill_light_enabled == enabled) {
        MASTER_LOG_DEBUG("[MASTER][APP] fill_light already %u\r\n",
                         enabled ? 1u : 0u);
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }

    s_snapshot.fill_light_enabled = enabled;
    return APP_RUNTIME_EVENT_APPLIED;
}

static app_runtime_event_result_t apply_event_to_snapshot(app_runtime_event_t event,
                                                          app_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }

    switch (event) {
    case APP_RUNTIME_EVENT_PHOTO:
        return APP_RUNTIME_EVENT_NO_CHANGE;

    case APP_RUNTIME_EVENT_RECORD_START:
        if (snapshot->recording) {
            return APP_RUNTIME_EVENT_NO_CHANGE;
        }
        snapshot->recording = true;
        break;

    case APP_RUNTIME_EVENT_RECORD_STOP:
        if (!snapshot->recording) {
            return APP_RUNTIME_EVENT_NO_CHANGE;
        }
        snapshot->recording = false;
        break;

    case APP_RUNTIME_EVENT_TRACK_START:
        if (snapshot->tracking) {
            return APP_RUNTIME_EVENT_NO_CHANGE;
        }
        snapshot->tracking = true;
        break;

    case APP_RUNTIME_EVENT_TRACK_STOP:
        if (!snapshot->tracking) {
            return APP_RUNTIME_EVENT_NO_CHANGE;
        }
        snapshot->tracking = false;
        break;

    case APP_RUNTIME_EVENT_FILL_LIGHT_ON:
        if (snapshot->fill_light_enabled) {
            return APP_RUNTIME_EVENT_NO_CHANGE;
        }
        snapshot->fill_light_enabled = true;
        break;

    case APP_RUNTIME_EVENT_FILL_LIGHT_OFF:
        if (!snapshot->fill_light_enabled) {
            return APP_RUNTIME_EVENT_NO_CHANGE;
        }
        snapshot->fill_light_enabled = false;
        break;

    case APP_RUNTIME_EVENT_NONE:
    default:
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }

    snapshot->state = compose_state(snapshot);
    return APP_RUNTIME_EVENT_APPLIED;
}

void app_runtime_state_reset(void)
{
    s_snapshot.state = APP_RUNTIME_STATE_IDLE;
    s_snapshot.recording = false;
    s_snapshot.tracking = false;
    s_snapshot.fill_light_enabled = false;
}

app_runtime_event_result_t app_runtime_state_preview_event(app_runtime_event_t event,
                                                           app_runtime_snapshot_t *next_snapshot)
{
    app_runtime_snapshot_t preview = s_snapshot;
    app_runtime_event_result_t result = apply_event_to_snapshot(event, &preview);

    if (next_snapshot != NULL) {
        *next_snapshot = preview;
    }

    return result;
}

app_runtime_event_result_t app_runtime_state_apply_event(app_runtime_event_t event)
{
    app_runtime_snapshot_t previous_snapshot = s_snapshot;
    app_runtime_event_result_t result;

    switch (event) {
    case APP_RUNTIME_EVENT_PHOTO:
        return APP_RUNTIME_EVENT_NO_CHANGE;

    case APP_RUNTIME_EVENT_RECORD_START:
        return set_recording(true);

    case APP_RUNTIME_EVENT_RECORD_STOP:
        return set_recording(false);

    case APP_RUNTIME_EVENT_TRACK_START:
        return set_tracking(true);

    case APP_RUNTIME_EVENT_TRACK_STOP:
        return set_tracking(false);

    case APP_RUNTIME_EVENT_FILL_LIGHT_ON:
        return set_fill_light(true);

    case APP_RUNTIME_EVENT_FILL_LIGHT_OFF:
        result = set_fill_light(false);
        break;

    case APP_RUNTIME_EVENT_NONE:
    default:
        return APP_RUNTIME_EVENT_NO_CHANGE;
    }

    if (result == APP_RUNTIME_EVENT_APPLIED) {
        refresh_state(&previous_snapshot);
    }

    return result;
}

app_runtime_state_t app_runtime_state_get(void)
{
    return s_snapshot.state;
}

void app_runtime_state_get_snapshot(app_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    *snapshot = s_snapshot;
}

bool app_runtime_state_is_recording(void)
{
    return s_snapshot.recording;
}

bool app_runtime_state_is_tracking(void)
{
    return s_snapshot.tracking;
}

bool app_runtime_state_is_fill_light_enabled(void)
{
    return s_snapshot.fill_light_enabled;
}