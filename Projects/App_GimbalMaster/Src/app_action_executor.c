#include "app_action_executor.h"

#include <stddef.h>

#include "app_card1_subboard.h"
#include "app_fill_light.h"
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
        if (app_card1_subboard_request_track_start() != 0) {
            return APP_ACTION_EXECUTE_IGNORED;
        }

        return (app_gimbal_control_set_tracking(true) == APP_GIMBAL_CONTROL_ACCEPTED) ?
            APP_ACTION_EXECUTE_DONE : APP_ACTION_EXECUTE_FAILED;

    case APP_ACTION_TRACK_STOP:
        if (app_card1_subboard_is_running() &&
            (app_card1_subboard_request_track_stop() != 0)) {
            return APP_ACTION_EXECUTE_IGNORED;
        }

        return (app_gimbal_control_set_tracking(false) == APP_GIMBAL_CONTROL_ACCEPTED) ?
            APP_ACTION_EXECUTE_DONE : APP_ACTION_EXECUTE_FAILED;

    case APP_ACTION_FILL_LIGHT_ON:
        return (app_fill_light_set_enabled(true) == 0) ?
            APP_ACTION_EXECUTE_DONE : APP_ACTION_EXECUTE_FAILED;

    case APP_ACTION_FILL_LIGHT_OFF:
        return (app_fill_light_set_enabled(false) == 0) ?
            APP_ACTION_EXECUTE_DONE : APP_ACTION_EXECUTE_FAILED;

    case APP_ACTION_NONE:
    default:
        return APP_ACTION_EXECUTE_IGNORED;
    }
}