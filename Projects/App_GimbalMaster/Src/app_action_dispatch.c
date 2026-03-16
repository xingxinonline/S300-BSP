#include "app_action_dispatch.h"

#include <stddef.h>

#include "app_action_executor.h"
#include "app_fill_light.h"
#include "app_runtime_state.h"
#include "app_status_light.h"
#include "master_log.h"

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

static const char *app_action_name(app_action_type_t type)
{
    switch (type) {
    case APP_ACTION_KWS_HIT: return "KWS_HIT";
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

static void log_action_result(const app_action_t *action,
                              app_action_result_t action_result,
                              app_action_execute_result_t execute_result,
                              app_runtime_event_t event,
                              app_runtime_event_result_t event_result)
{
    if (action == NULL) {
        return;
    }

    MASTER_LOG_INFO("[MASTER][ACTION] action=%s keyword=%u result=%s execute=%s event=%s event_result=%s\r\n",
                    app_action_name(action->type),
                    (unsigned)action->keyword_idx,
                    app_action_result_name(action_result),
                    app_action_execute_result_name(execute_result),
                    app_runtime_event_name(event),
                    app_runtime_event_result_name(event_result));
}

static void sync_status_outputs(void)
{
    app_status_light_set_recording(app_runtime_state_is_recording());
    app_status_light_set_tracking(app_runtime_state_is_tracking());
    app_status_light_set_fill_light(app_runtime_state_is_fill_light_enabled());
}

static app_runtime_event_result_t apply_runtime_event(app_runtime_event_t event)
{
    app_runtime_event_result_t result = app_runtime_state_apply_event(event);

    if (result == APP_RUNTIME_EVENT_APPLIED) {
        sync_status_outputs();
    }

    return result;
}

void app_action_dispatch(const app_action_t *action)
{
    app_action_result_t action_result = APP_ACTION_RESULT_IGNORED;
    app_action_execute_result_t execute_result = APP_ACTION_EXECUTE_IGNORED;
    app_runtime_event_t runtime_event = APP_RUNTIME_EVENT_NONE;
    app_runtime_event_result_t runtime_result = APP_RUNTIME_EVENT_NO_CHANGE;

    if (action == NULL) {
        return;
    }

    switch (action->type) {
    case APP_ACTION_KWS_HIT:
        app_status_light_notify_kws_hit(action->keyword_idx);
        action_result = APP_ACTION_RESULT_SIDE_EFFECT_ONLY;
        break;

    case APP_ACTION_PHOTO:
        execute_result = app_action_execute(action);
        runtime_event = APP_RUNTIME_EVENT_PHOTO;
        runtime_result = apply_runtime_event(runtime_event);
        action_result = APP_ACTION_RESULT_SIDE_EFFECT_ONLY;
        break;

    case APP_ACTION_RECORD_START:
        execute_result = app_action_execute(action);
        runtime_event = APP_RUNTIME_EVENT_RECORD_START;
        runtime_result = apply_runtime_event(runtime_event);
        action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
            APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
        break;

    case APP_ACTION_RECORD_STOP:
        execute_result = app_action_execute(action);
        runtime_event = APP_RUNTIME_EVENT_RECORD_STOP;
        runtime_result = apply_runtime_event(runtime_event);
        action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
            APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
        break;

    case APP_ACTION_TRACK_START:
        execute_result = app_action_execute(action);
        runtime_event = APP_RUNTIME_EVENT_TRACK_START;
        runtime_result = apply_runtime_event(runtime_event);
        action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
            APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
        break;

    case APP_ACTION_TRACK_STOP:
        execute_result = app_action_execute(action);
        runtime_event = APP_RUNTIME_EVENT_TRACK_STOP;
        runtime_result = apply_runtime_event(runtime_event);
        action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
            APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_IGNORED;
        break;

    case APP_ACTION_FILL_LIGHT_ON:
        execute_result = app_action_execute(action);
        runtime_event = APP_RUNTIME_EVENT_FILL_LIGHT_ON;
        runtime_result = apply_runtime_event(runtime_event);
        (void)app_fill_light_set_enabled(true);
        action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
            APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_SIDE_EFFECT_ONLY;
        break;

    case APP_ACTION_FILL_LIGHT_OFF:
        execute_result = app_action_execute(action);
        runtime_event = APP_RUNTIME_EVENT_FILL_LIGHT_OFF;
        runtime_result = apply_runtime_event(runtime_event);
        (void)app_fill_light_set_enabled(false);
        action_result = (runtime_result == APP_RUNTIME_EVENT_APPLIED) ?
            APP_ACTION_RESULT_APPLIED : APP_ACTION_RESULT_SIDE_EFFECT_ONLY;
        break;

    case APP_ACTION_NONE:
    default:
        break;
    }

    if (action->type != APP_ACTION_NONE) {
        log_action_result(action, action_result, execute_result, runtime_event, runtime_result);
    }
}

void app_action_dispatch_kws_keyword(uint8_t keyword_idx, uint8_t confidence, uint32_t chunk_idx)
{
    app_action_t action = {
        .type = APP_ACTION_NONE,
        .keyword_idx = keyword_idx,
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

    action.type = APP_ACTION_KWS_HIT;
    app_action_dispatch(&action);
}