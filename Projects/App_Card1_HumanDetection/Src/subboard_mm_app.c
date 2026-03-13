#include "subboard_mm_app.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "s300.h"
#include "subboard_dsp_ctrl.h"
#include "subboard_log.h"
#include "subboard_mm_app_internal.h"
#include "subboard_startup_i2c.h"
#include "subboard_startup_proto.h"

static SubboardMmAppContext s_ctx = {
    .public_state = SUBBOARD_STARTUP_STATE_BOOT,
    .active_request = SUBBOARD_STARTUP_REQ_NONE,
    .last_logged_state = 0xFFu,
};

static uint32_t millis(void)
{
    return subboard_mm_app_millis(&s_ctx);
}

int subboard_mm_app_init(uint32_t (*get_millis_fn)(void))
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.get_millis = get_millis_fn;
    s_ctx.public_state = SUBBOARD_STARTUP_STATE_BOOT;
    s_ctx.active_request = SUBBOARD_STARTUP_REQ_NONE;
    s_ctx.last_logged_state = 0xFFu;

    SUB_LOG_INFO("\r\n===========================================\r\n");
    SUB_LOG_INFO("  App_Card1_HumanDetection\r\n");
    SUB_LOG_INFO("  Card1 human-detection subboard app\r\n");
    SUB_LOG_INFO("===========================================\r\n");

    subboard_dsp_ctrl_init(millis);
    subboard_mm_app_refresh_dsp_resource_flags(&s_ctx);
    subboard_startup_i2c_update_result(&s_ctx.last_published_result);

    if (subboard_startup_i2c_init(SUBBOARD_STARTUP_SLAVE_ADDR_CARD1) != 0) {
        SUB_LOG_WARN("[CARD1] i2c slave init failed\r\n");
        return -1;
    }

    subboard_startup_i2c_set_status(SUBBOARD_STARTUP_STATUS_NONE);
    subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_NONE);

    subboard_mm_app_enter_public_state(&s_ctx, SUBBOARD_STARTUP_STATE_I2C_READY);
    subboard_mm_app_enter_public_state(&s_ctx, SUBBOARD_STARTUP_STATE_WAIT_VIDEO);

    SUB_LOG_INFO("[CARD1] I2C slave ready: addr=0x%02X proto=0x%02X\r\n",
                 SUBBOARD_STARTUP_SLAVE_ADDR_CARD1,
                 SUBBOARD_STARTUP_PROTO_VER);

    s_ctx.last_heartbeat_ms = millis();
    s_ctx.last_logged_state = 0xFFu;
    return 0;
}

void subboard_mm_app_tick(void)
{
    uint8_t cmd = subboard_startup_i2c_get_command();
    uint8_t request_ack = subboard_startup_i2c_get_request_ack();

    subboard_dsp_ctrl_tick();
    subboard_mm_app_publish_latest_result(&s_ctx);
    subboard_mm_app_bump_heartbeat_if_needed(&s_ctx);
    subboard_mm_app_consume_request_ack(&s_ctx, request_ack);
    subboard_mm_app_sync_public_state_from_dsp(&s_ctx);
    subboard_mm_app_post_next_master_request(&s_ctx);
    subboard_mm_app_log_public_state_if_needed(&s_ctx);
    subboard_mm_app_handle_command(&s_ctx, cmd);
}