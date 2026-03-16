#include "app_action_executor.h"

#include <stddef.h>

#include "app_gimbal_control.h"
#include "app_media_control.h"
#include "master_log.h"

const char *app_action_execute_result_name(app_action_execute_result_t result)
{
    switch (result) {
    case APP_ACTION_EXECUTE_DONE:
        return "DONE";
    case APP_ACTION_EXECUTE_DEFERRED:
        return "DEFERRED";
    case APP_ACTION_EXECUTE_FAILED:
        return "FAILED";
    case APP_ACTION_EXECUTE_IGNORED:
    default:
        return "IGNORED";
    }
}

app_action_execute_result_t app_action_execute(const app_action_t *action)
{
    app_media_control_result_t media_result;

    if (action == NULL) {
        return APP_ACTION_EXECUTE_IGNORED;
    }

    switch (action->type) {
    case APP_ACTION_PHOTO:
        media_result = app_media_control_trigger_photo();
        if (media_result == APP_MEDIA_CONTROL_ACCEPTED) {
            return APP_ACTION_EXECUTE_DONE;
        }
        if (media_result == APP_MEDIA_CONTROL_FAILED) {
            return APP_ACTION_EXECUTE_FAILED;
        }
        return APP_ACTION_EXECUTE_IGNORED;

    case APP_ACTION_RECORD_START:
        media_result = app_media_control_set_recording(true);
        if (media_result == APP_MEDIA_CONTROL_ACCEPTED) {
            return APP_ACTION_EXECUTE_DONE;
        }
        if (media_result == APP_MEDIA_CONTROL_FAILED) {
            return APP_ACTION_EXECUTE_FAILED;
        }
        return APP_ACTION_EXECUTE_IGNORED;

    case APP_ACTION_RECORD_STOP:
        media_result = app_media_control_set_recording(false);
        if (media_result == APP_MEDIA_CONTROL_ACCEPTED) {
            return APP_ACTION_EXECUTE_DONE;
        }
        if (media_result == APP_MEDIA_CONTROL_FAILED) {
            return APP_ACTION_EXECUTE_FAILED;
        }
        return APP_ACTION_EXECUTE_IGNORED;

    case APP_ACTION_TRACK_START:
        return (app_gimbal_control_set_tracking(true) == APP_GIMBAL_CONTROL_ACCEPTED) ?
            APP_ACTION_EXECUTE_DONE : APP_ACTION_EXECUTE_IGNORED;

    case APP_ACTION_TRACK_STOP:
        return (app_gimbal_control_set_tracking(false) == APP_GIMBAL_CONTROL_ACCEPTED) ?
            APP_ACTION_EXECUTE_DONE : APP_ACTION_EXECUTE_IGNORED;

    case APP_ACTION_FILL_LIGHT_ON:
    case APP_ACTION_FILL_LIGHT_OFF:
        return APP_ACTION_EXECUTE_DONE;

    case APP_ACTION_KWS_HIT:
    case APP_ACTION_NONE:
    default:
        return APP_ACTION_EXECUTE_IGNORED;
    }
}