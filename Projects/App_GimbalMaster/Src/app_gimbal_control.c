#include "app_gimbal_control.h"

#include "master_log.h"

static bool s_tracking = false;

void app_gimbal_control_init(void)
{
    s_tracking = false;
}

app_gimbal_control_result_t app_gimbal_control_set_tracking(bool enabled)
{
    if (s_tracking == enabled) {
        MASTER_LOG_DEBUG("[MASTER][GIMBAL] tracking already %u\r\n", enabled ? 1u : 0u);
        return APP_GIMBAL_CONTROL_NO_CHANGE;
    }

    s_tracking = enabled;
    MASTER_LOG_INFO("[MASTER][GIMBAL] tracking %s\r\n", enabled ? "START" : "STOP");
    return APP_GIMBAL_CONTROL_ACCEPTED;
}

bool app_gimbal_control_is_tracking(void)
{
    return s_tracking;
}