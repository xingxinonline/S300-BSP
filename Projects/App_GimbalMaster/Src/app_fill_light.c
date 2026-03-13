#include "app_fill_light.h"

#include <stdbool.h>
#include <stdint.h>

#include "camera_ov5640.h"
#include "master_log.h"
#include "ov5640.h"

static i2c_soft_t *s_camera_i2c = 0;
static uint8_t s_camera_addr = 0u;
static bool s_fill_light_initialized = false;
static bool s_fill_light_enabled = false;

int app_fill_light_init(void)
{
    if (!camera_ov5640_get_context(&s_camera_i2c, &s_camera_addr)) {
        s_fill_light_initialized = false;
        MASTER_LOG_WARN("[MASTER][FILL] camera context unavailable\r\n");
        return -1;
    }

    s_fill_light_initialized = true;
    s_fill_light_enabled = false;
    MASTER_LOG_INFO("[MASTER][FILL] ready on ov5640 addr=0x%02X\r\n", s_camera_addr);
    return 0;
}

int app_fill_light_set_enabled(bool enabled)
{
    int ret;

    if (!s_fill_light_initialized) {
        if (app_fill_light_init() != 0) {
            return -1;
        }
    }

    if (s_fill_light_enabled == enabled) {
        return 0;
    }

    ret = ov5640_set_light(s_camera_i2c, s_camera_addr, enabled);
    if (ret != 0) {
        MASTER_LOG_WARN("[MASTER][FILL] set=%u failed=%d\r\n", enabled ? 1u : 0u, ret);
        return ret;
    }

    s_fill_light_enabled = enabled;
    MASTER_LOG_INFO("[MASTER][FILL] %s\r\n", enabled ? "ON" : "OFF");
    return 0;
}

bool app_fill_light_is_enabled(void)
{
    return s_fill_light_enabled;
}