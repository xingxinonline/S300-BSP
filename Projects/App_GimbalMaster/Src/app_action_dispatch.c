#include "app_action_dispatch.h"

#include <stddef.h>

#include "app_action_executor.h"
#include "app_runtime_state.h"
#include "app_status_light.h"
#include "master_log.h"

#define APP_ACTION_DEBOUNCE_PHOTO_CHUNKS         48u
#define APP_ACTION_DEBOUNCE_RECORD_START_CHUNKS  120u
#define APP_ACTION_DEBOUNCE_RECORD_STOP_CHUNKS   120u
#define APP_ACTION_DEBOUNCE_TRACK_START_CHUNKS   120u
#define APP_ACTION_DEBOUNCE_TRACK_STOP_CHUNKS    120u
#define APP_ACTION_TRACK_TOGGLE_GUARD_CHUNKS     120u
#define APP_ACTION_DEBOUNCE_FILL_LIGHT_CHUNKS    90u

#define KWS_KEYWORD_PHOTO          1u
#define KWS_KEYWORD_RECORD_START   2u
#define KWS_KEYWORD_RECORD_STOP    3u
#define KWS_KEYWORD_TRACK_START    4u
#define KWS_KEYWORD_TRACK_STOP     5u
#define KWS_KEYWORD_FILL_LIGHT_ON  6u
#define KWS_KEYWORD_FILL_LIGHT_OFF 7u

typedef enum {
    APP_ACTION_RESULT_IGNORED = 0,
    APP_ACTION_RESULT_APPLIED,
    APP_ACTION_RESULT_SIDE_EFFECT_ONLY,
} app_action_result_t;

static uint32_t s_last_kws_action_chunk[APP_ACTION_FILL_LIGHT_OFF + 1u];

static const char *app_action_name(app_action_type_t type)
{
    switch (type) {
    case APP_ACTION_PHOTO: return "PHOTO";
    case APP_ACTION_RECORD_START: return "RECORD_START";
    case APP_ACTION_RECORD_STOP: return "RECORD_STOP";
    case APP_ACTION_TRACK_START: return "TRACK_START";
    case APP_ACTION_TRACK_STOP: return "TRACK_STOP";
    case APP_ACTION_FILL_LIGHT_ON: return "FILL_LIGHT_ON";
    case APP_ACTION_FILL_LIGHT_OFF: return "FILL_LIGHT_OFF";
    case APP_ACTION_NONE:
    default:
        return "NONE";
    }
}

static const char *app_action_source_name(app_action_source_t source)
{
    switch (source) {
    case APP_ACTION_SOURCE_KWS: return "KWS";
    case APP_ACTION_SOURCE_GESTURE: return "GESTURE";
    case APP_ACTION_SOURCE_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

static const char *app_action_result_name(app_action_result_t result)
{
    switch (result) {
    case APP_ACTION_RESULT_APPLIED: return "APPLIED";
    case APP_ACTION_RESULT_SIDE_EFFECT_ONLY: return "SIDE_EFFECT_ONLY";
    case APP_ACTION_RESULT_IGNORED:
    default:
        return "IGNORED";
    }
}

static uint32_t kws_action_cooldown_chunks(app_action_type_t type)
{
    switch (type) {
    case APP_ACTION_PHOTO: return APP_ACTION_DEBOUNCE_PHOTO_CHUNKS;
    case APP_ACTION_RECORD_START: return APP_ACTION_DEBOUNCE_RECORD_START_CHUNKS;
    case APP_ACTION_RECORD_STOP: return APP_ACTION_DEBOUNCE_RECORD_STOP_CHUNKS;
    case APP_ACTION_TRACK_START: return APP_ACTION_DEBOUNCE_TRACK_START_CHUNKS;
    case APP_ACTION_TRACK_STOP: return APP_ACTION_DEBOUNCE_TRACK_STOP_CHUNKS;
    case APP_ACTION_FILL_LIGHT_ON:
    case APP_ACTION_FILL_LIGHT_OFF:
        return APP_ACTION_DEBOUNCE_FILL_LIGHT_CHUNKS;
    case APP_ACTION_NONE:
    default:
        return 0u;
    }
}

static bool should_suppress_kws_action(const app_action_t *action)
{
    uint32_t cooldown_chunks;
    uint32_t last_chunk;
    uint32_t delta;

    if ((action == NULL) ||
        (action->source != APP_ACTION_SOURCE_KWS) ||
        (action->type == APP_ACTION_NONE) ||
        (action->type > APP_ACTION_FILL_LIGHT_OFF)) {
        return false;
    }

    cooldown_chunks = kws_action_cooldown_chunks(action->type);
    if (cooldown_chunks == 0u) {
        return false;
    }

    last_chunk = s_last_kws_action_chunk[action->type];
    if (last_chunk == 0u) {
        return false;
    }

    delta = action->chunk_idx - last_chunk;
    return delta < cooldown_chunks;
}

static bool should_suppress_opposite_track_kws_action(const app_action_t *action)
{
    app_action_type_t opposite_type;
    uint32_t last_chunk;
    uint32_t delta;

    if ((action == NULL) ||
        (action->source != APP_ACTION_SOURCE_KWS)) {
        return false;
    }

    if (action->type == APP_ACTION_TRACK_START) {
        opposite_type = APP_ACTION_TRACK_STOP;
    } else if (action->type == APP_ACTION_TRACK_STOP) {
        opposite_type = APP_ACTION_TRACK_START;
    } else {
        return false;
    }

    last_chunk = s_last_kws_action_chunk[opposite_type];
    if (last_chunk == 0u) {
        return false;
    }

    delta = action->chunk_idx - last_chunk;
    return delta < APP_ACTION_TRACK_TOGGLE_GUARD_CHUNKS;
}

static void remember_kws_action(const app_action_t *action)
{
    if ((action == NULL) ||
        (action->source != APP_ACTION_SOURCE_KWS) ||
        (action->type == APP_ACTION_NONE) ||
        (action->type > APP_ACTION_FILL_LIGHT_OFF)) {
        return;
    }

    s_last_kws_action_chunk[action->type] = action->chunk_idx;
}

static void log_action_result(const app_action_t *action,
                              app_action_result_t action_result,
                              app_action_execute_result_t execute_result,
                              app_runtime_event_t event,
                              app_runtime_event_result_t event_result)
{
    if (action == NULL) {
        return;
    }

    MASTER_LOG_INFO("[MASTER][ACTION] action=%s source=%s source_id=%u result=%s execute=%s event=%s event_result=%s\r\n",
                    app_action_name(action->type),
                    app_action_source_name(action->source),
                    (unsigned)action->source_id,
                    app_action_result_name(action_result),
                    app_action_execute_result_name(execute_result),
                    app_runtime_event_name(event),
                    app_runtime_event_result_name(event_result));
}

static void sync_status_outputs(const app_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    app_status_light_set_recording(snapshot->recording);
    app_status_light_set_tracking(snapshot->tracking);
    app_status_light_set_fill_light(snapshot->fill_light_enabled);
}

static app_runtime_event_result_t apply_runtime_event(app_runtime_event_t event,
                                                      app_runtime_snapshot_t *snapshot)
{
    app_runtime_event_result_t result = app_runtime_state_apply_event(event);

    if (result == APP_RUNTIME_EVENT_APPLIED) {
        app_runtime_state_get_snapshot(snapshot);
        sync_status_outputs(snapshot);
    }

    return result;
}

static app_runtime_event_t runtime_event_for_action(const app_action_t *action)
{
    if (action == NULL) {
        return APP_RUNTIME_EVENT_NONE;
    }

    switch (action->type) {
    case APP_ACTION_PHOTO:
        return APP_RUNTIME_EVENT_PHOTO;
    case APP_ACTION_RECORD_START:
        return APP_RUNTIME_EVENT_RECORD_START;
    case APP_ACTION_RECORD_STOP:
        return APP_RUNTIME_EVENT_RECORD_STOP;
    case APP_ACTION_TRACK_START:
        return APP_RUNTIME_EVENT_TRACK_START;
    case APP_ACTION_TRACK_STOP:
        return APP_RUNTIME_EVENT_TRACK_STOP;
    case APP_ACTION_FILL_LIGHT_ON:
        return APP_RUNTIME_EVENT_FILL_LIGHT_ON;
    case APP_ACTION_FILL_LIGHT_OFF:
        return APP_RUNTIME_EVENT_FILL_LIGHT_OFF;
    case APP_ACTION_NONE:
    default:
        return APP_RUNTIME_EVENT_NONE;
    }
}

static app_status_light_feedback_t feedback_for_action(const app_action_t *action)
{
    if (action == NULL) {
        return APP_STATUS_LIGHT_FEEDBACK_NONE;
    }

    switch (action->type) {
    case APP_ACTION_PHOTO:
        return APP_STATUS_LIGHT_FEEDBACK_PHOTO;

    case APP_ACTION_RECORD_START:
        return APP_STATUS_LIGHT_FEEDBACK_RECORD_START;

    case APP_ACTION_RECORD_STOP:
        return APP_STATUS_LIGHT_FEEDBACK_RECORD_STOP;

    case APP_ACTION_TRACK_START:
        return APP_STATUS_LIGHT_FEEDBACK_TRACK_START;

    case APP_ACTION_TRACK_STOP:
        return APP_STATUS_LIGHT_FEEDBACK_TRACK_STOP;

    case APP_ACTION_FILL_LIGHT_ON:
        return APP_STATUS_LIGHT_FEEDBACK_FILL_LIGHT_ON;

    case APP_ACTION_FILL_LIGHT_OFF:
        return APP_STATUS_LIGHT_FEEDBACK_FILL_LIGHT_OFF;

    default:
        return APP_STATUS_LIGHT_FEEDBACK_NONE;
    }
}

void app_action_dispatch(const app_action_t *action)
{
    app_action_result_t action_result = APP_ACTION_RESULT_IGNORED;
    app_action_execute_result_t execute_result = APP_ACTION_EXECUTE_IGNORED;
    app_runtime_event_t runtime_event;
    app_runtime_event_result_t runtime_result = APP_RUNTIME_EVENT_NO_CHANGE;
    app_runtime_snapshot_t runtime_snapshot;
    app_runtime_event_result_t preview_result;

    if (action == NULL) {
        return;
    }

    if (should_suppress_kws_action(action)) {
        MASTER_LOG_DEBUG("[MASTER][ACTION] suppress repeated kws action=%s source_id=%u chunk=%lu\r\n",
                         app_action_name(action->type),
                         (unsigned)action->source_id,
                         (unsigned long)action->chunk_idx);
        return;
    }

    if (should_suppress_opposite_track_kws_action(action)) {
        MASTER_LOG_INFO("[MASTER][ACTION] suppress opposite kws action=%s source_id=%u chunk=%lu guard=%lu\r\n",
                        app_action_name(action->type),
                        (unsigned)action->source_id,
                        (unsigned long)action->chunk_idx,
                        (unsigned long)APP_ACTION_TRACK_TOGGLE_GUARD_CHUNKS);
        return;
    }

    runtime_event = runtime_event_for_action(action);
    preview_result = app_runtime_state_preview_event(runtime_event, &runtime_snapshot);

    switch (action->type) {
    case APP_ACTION_PHOTO:
        execute_result = app_action_execute(action);
        if (execute_result == APP_ACTION_EXECUTE_DONE) {
            runtime_result = apply_runtime_event(runtime_event, &runtime_snapshot);
            action_result = APP_ACTION_RESULT_SIDE_EFFECT_ONLY;
        }
        break;

    case APP_ACTION_RECORD_START:
        if (preview_result == APP_RUNTIME_EVENT_APPLIED) {
            execute_result = app_action_execute(action);
            if (execute_result == APP_ACTION_EXECUTE_DONE) {
                runtime_result = apply_runtime_event(runtime_event, &runtime_snapshot);
                action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
                    APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
            }
        }
        break;

    case APP_ACTION_RECORD_STOP:
        if (preview_result == APP_RUNTIME_EVENT_APPLIED) {
            execute_result = app_action_execute(action);
            if (execute_result == APP_ACTION_EXECUTE_DONE) {
                runtime_result = apply_runtime_event(runtime_event, &runtime_snapshot);
                action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
                    APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
            }
        }
        break;

    case APP_ACTION_TRACK_START:
        if (preview_result == APP_RUNTIME_EVENT_APPLIED) {
            execute_result = app_action_execute(action);
            if (execute_result == APP_ACTION_EXECUTE_DONE) {
                runtime_result = apply_runtime_event(runtime_event, &runtime_snapshot);
                action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
                    APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
            }
        }
        break;

    case APP_ACTION_TRACK_STOP:
        if (preview_result == APP_RUNTIME_EVENT_APPLIED) {
            execute_result = app_action_execute(action);
            if (execute_result == APP_ACTION_EXECUTE_DONE) {
                runtime_result = apply_runtime_event(runtime_event, &runtime_snapshot);
                action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
                    APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
            }
        }
        break;

    case APP_ACTION_FILL_LIGHT_ON:
        if (preview_result == APP_RUNTIME_EVENT_APPLIED) {
            execute_result = app_action_execute(action);
            if (execute_result == APP_ACTION_EXECUTE_DONE) {
                runtime_result = apply_runtime_event(runtime_event, &runtime_snapshot);
                action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
                    APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
            }
        }
        break;

    case APP_ACTION_FILL_LIGHT_OFF:
        if (preview_result == APP_RUNTIME_EVENT_APPLIED) {
            execute_result = app_action_execute(action);
            if (execute_result == APP_ACTION_EXECUTE_DONE) {
                runtime_result = apply_runtime_event(runtime_event, &runtime_snapshot);
                action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
                    APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
            }
        }
        break;

    case APP_ACTION_NONE:
    default:
        break;
    }

    if (action->type != APP_ACTION_NONE) {
        app_status_light_feedback_t feedback = feedback_for_action(action);

        if ((feedback != APP_STATUS_LIGHT_FEEDBACK_NONE) && (action_result != APP_ACTION_RESULT_IGNORED)) {
            app_status_light_notify_feedback(feedback);
        }
    }

    if (action_result != APP_ACTION_RESULT_IGNORED) {
        remember_kws_action(action);
    }

    if (action->type != APP_ACTION_NONE) {
        log_action_result(action, action_result, execute_result, runtime_event, runtime_result);
    }
}

void app_action_dispatch_kws_keyword(uint8_t keyword_idx, uint8_t confidence, uint32_t chunk_idx)
{
    app_action_t action = {
        .type = APP_ACTION_NONE,
        .source = APP_ACTION_SOURCE_KWS,
        .source_id = keyword_idx,
        .confidence = confidence,
        .chunk_idx = chunk_idx,
    };

    switch (keyword_idx) {
    case KWS_KEYWORD_PHOTO:
        action.type = APP_ACTION_PHOTO;
        app_action_dispatch(&action);
        break;

    case KWS_KEYWORD_RECORD_START:
        action.type = APP_ACTION_RECORD_START;
        app_action_dispatch(&action);
        break;

    case KWS_KEYWORD_RECORD_STOP:
        action.type = APP_ACTION_RECORD_STOP;
        app_action_dispatch(&action);
        break;

    case KWS_KEYWORD_TRACK_START:
        action.type = APP_ACTION_TRACK_START;
        app_action_dispatch(&action);
        break;

    case KWS_KEYWORD_TRACK_STOP:
        action.type = APP_ACTION_TRACK_STOP;
        app_action_dispatch(&action);
        break;

    case KWS_KEYWORD_FILL_LIGHT_ON:
        action.type = APP_ACTION_FILL_LIGHT_ON;
        app_action_dispatch(&action);
        break;

    case KWS_KEYWORD_FILL_LIGHT_OFF:
        action.type = APP_ACTION_FILL_LIGHT_OFF;
        app_action_dispatch(&action);
        break;

    default:
        break;
    }
}