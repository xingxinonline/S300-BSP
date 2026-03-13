#include "subboard_mm_app_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "control_proto.h"
#include "s300.h"
#include "subboard_log.h"
#include "subboard_dsp_ctrl.h"
#include "subboard_startup_i2c.h"
#include "subboard_startup_proto.h"

#ifndef REG32
#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#endif

#define SUBBOARD_RESULT_IDLE_DEBOUNCE_MS 120u

uint32_t subboard_mm_app_millis(const SubboardMmAppContext *ctx)
{
    return ((ctx != NULL) && (ctx->get_millis != 0)) ? ctx->get_millis() : 0u;
}

const char *subboard_mm_app_public_state_name(uint8_t state)
{
    switch (state) {
    case SUBBOARD_STARTUP_STATE_BOOT: return "BOOT";
    case SUBBOARD_STARTUP_STATE_I2C_READY: return "I2C_READY";
    case SUBBOARD_STARTUP_STATE_WAIT_VIDEO: return "WAIT_VIDEO_READY";
    case SUBBOARD_STARTUP_STATE_WAIT_MASTER_MM: return "WAIT_MASTER_MM";
    case SUBBOARD_STARTUP_STATE_MM_READY: return "MM_READY";
    case SUBBOARD_STARTUP_STATE_DSP_STARTING: return "DSP_STARTING";
    case SUBBOARD_STARTUP_STATE_DSP_READY: return "DSP_READY";
    case SUBBOARD_STARTUP_STATE_RUNNING: return "RUNNING";
    case SUBBOARD_STARTUP_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static const char *request_name(uint8_t request)
{
    switch (request) {
    case SUBBOARD_STARTUP_REQ_NONE: return "NONE";
    case SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE: return "REQUEST_MASTER_MM_ENABLE";
    case SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME: return "REQUEST_MASTER_MM_RUNTIME";
    case SUBBOARD_STARTUP_REQ_MASTER_SPI_SYNC: return "REQUEST_MASTER_SPI_SYNC";
    default: return "UNKNOWN";
    }
}

static void trigger_core_reg_update(void)
{
    REG32(DSP_VIDEO_SS_BASE + 0x70u) = 1u;
}

#if !defined(BOARD_LCD_SPI_ENABLE_ON_INIT) || (BOARD_LCD_SPI_ENABLE_ON_INIT != 0)
static void trigger_spi_reg_update(void)
{
    REG32(DSP_VIDEO_SS_BASE + 0x1E0u) = 1u;
}
#endif

static void trigger_local_mm_runtime_enable(const SubboardMmAppContext *ctx)
{
    if ((ctx == NULL) || !ctx->mm_started) {
        SUB_LOG_WARN("[SUB-MM] skip local MM runtime enable: MM consumer not started\r\n");
        return;
    }

    trigger_core_reg_update();

#if defined(BOARD_LCD_SPI_ENABLE_ON_INIT) && (BOARD_LCD_SPI_ENABLE_ON_INIT == 0)
    SUB_LOG_INFO("[SUB-MM] applied local MM runtime enable: core only, local LCD SPI path disabled\r\n");
#else
    trigger_spi_reg_update();
    SUB_LOG_INFO("[SUB-MM] applied local MM runtime enable: core + lcd spi\r\n");
#endif
}

void subboard_mm_app_refresh_dsp_resource_flags(SubboardMmAppContext *ctx)
{
    uint8_t resource_flags = 0u;

    if (ctx == NULL) {
        return;
    }

    if (ctx->master_mm_granted) {
        resource_flags |= CONTROL_RESOURCE_CAMERA_READY;
        resource_flags |= CONTROL_RESOURCE_LCD_READY;
    }

    if (ctx->mm_started) {
        resource_flags |= CONTROL_RESOURCE_MM_READY;
    }

    subboard_dsp_ctrl_set_resource_flags(resource_flags);
}

void subboard_mm_app_enter_public_state(SubboardMmAppContext *ctx, uint8_t next_state)
{
    if ((ctx == NULL) || (ctx->public_state == next_state)) {
        return;
    }

    SUB_LOG_INFO("[SUB-MM] STATE %s -> %s\r\n",
                 subboard_mm_app_public_state_name(ctx->public_state),
                 subboard_mm_app_public_state_name(next_state));
    ctx->public_state = next_state;
    subboard_startup_i2c_set_public_state(next_state);
}

void subboard_mm_app_reset_request_path(SubboardMmAppContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->master_mm_requested = false;
    ctx->master_mm_granted = false;
    ctx->active_request = SUBBOARD_STARTUP_REQ_NONE;
    subboard_mm_app_refresh_dsp_resource_flags(ctx);
    subboard_startup_i2c_clear_request();
}

void subboard_mm_app_post_next_master_request(SubboardMmAppContext *ctx)
{
    uint8_t request;

    if ((ctx == NULL) || (ctx->active_request != SUBBOARD_STARTUP_REQ_NONE)) {
        return;
    }

    if (!ctx->master_mm_requested && (ctx->public_state == SUBBOARD_STARTUP_STATE_WAIT_VIDEO)) {
        request = SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE;
        subboard_startup_i2c_set_request(request, 0u);
        subboard_mm_app_enter_public_state(ctx, SUBBOARD_STARTUP_STATE_WAIT_MASTER_MM);
        ctx->master_mm_requested = true;
        ctx->active_request = request;
        SUB_LOG_INFO("[SUB-MM] REQUEST %s posted\r\n", request_name(request));
        return;
    }

    request = subboard_dsp_ctrl_peek_master_request();
    if (request == SUBBOARD_STARTUP_REQ_NONE) {
        return;
    }

    if (request == SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME) {
        trigger_local_mm_runtime_enable(ctx);
    }

    subboard_startup_i2c_set_request(request, 0u);
    ctx->active_request = request;
    SUB_LOG_INFO("[SUB-MM] REQUEST %s posted from DSP runtime notify\r\n",
                 request_name(request));
}

void subboard_mm_app_consume_request_ack(SubboardMmAppContext *ctx, uint8_t request_ack)
{
    if ((ctx == NULL) ||
        (ctx->active_request == SUBBOARD_STARTUP_REQ_NONE) ||
        (request_ack != ctx->active_request)) {
        return;
    }

    if (ctx->active_request == SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE) {
        ctx->master_mm_granted = true;
        subboard_mm_app_refresh_dsp_resource_flags(ctx);
    } else {
        subboard_dsp_ctrl_complete_master_request(ctx->active_request);
    }

    SUB_LOG_INFO("[SUB-MM] REQUEST %s acknowledged by master\r\n",
                 request_name(ctx->active_request));
    subboard_startup_i2c_clear_request();
    ctx->active_request = SUBBOARD_STARTUP_REQ_NONE;
}

void subboard_mm_app_sync_public_state_from_dsp(SubboardMmAppContext *ctx)
{
    uint8_t dsp_state;

    if ((ctx == NULL) || !subboard_dsp_ctrl_is_active()) {
        return;
    }

    dsp_state = subboard_dsp_ctrl_get_public_state();
    if (dsp_state == SUBBOARD_STARTUP_STATE_ERROR) {
        subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_DSP_START_FAIL);
        subboard_mm_app_enter_public_state(ctx, SUBBOARD_STARTUP_STATE_ERROR);
        return;
    }

    if ((dsp_state != SUBBOARD_STARTUP_STATE_MM_READY) && (ctx->public_state != dsp_state)) {
        subboard_mm_app_enter_public_state(ctx, dsp_state);
    }
}

static bool result_is_publishable(const subboard_detection_result_t *result)
{
    if (result == NULL) {
        return false;
    }

    return (result->valid != 0u) &&
           (result->count != 0u) &&
           (result->confidence != 0u) &&
           (result->x2 > result->x1) &&
           (result->y2 > result->y1);
}

void subboard_mm_app_publish_latest_result(SubboardMmAppContext *ctx)
{
    subboard_detection_result_t latest_result;
    bool result_active;
    uint32_t now_ms;

    if ((ctx == NULL) || !subboard_dsp_ctrl_get_latest_result(&latest_result)) {
        return;
    }

    now_ms = subboard_mm_app_millis(ctx);
    result_active = result_is_publishable(&latest_result);

    if (!result_active && ctx->result_active) {
        if (ctx->result_invalid_since_ms == 0u) {
            ctx->result_invalid_since_ms = now_ms;
        }

        if ((uint32_t)(now_ms - ctx->result_invalid_since_ms) < SUBBOARD_RESULT_IDLE_DEBOUNCE_MS) {
            return;
        }
    } else {
        ctx->result_invalid_since_ms = 0u;
    }

    if (memcmp(&latest_result, &ctx->last_published_result, sizeof(latest_result)) == 0) {
        return;
    }

    subboard_startup_i2c_update_result(&latest_result);
    ctx->last_published_result = latest_result;

    if (result_active && !ctx->result_active) {
        SUB_LOG_INFO("[SUB-MM] result active: count=%u conf=%u box=(%d,%d)-(%d,%d)\r\n",
                     (unsigned)latest_result.count,
                     (unsigned)latest_result.confidence,
                     (int)latest_result.x1,
                     (int)latest_result.y1,
                     (int)latest_result.x2,
                     (int)latest_result.y2);
        ctx->result_active = true;
    } else if (!result_active && ctx->result_active) {
        SUB_LOG_INFO("[SUB-MM] result idle\r\n");
        ctx->result_active = false;
        ctx->result_invalid_since_ms = 0u;
    }

    if (result_active) {
        SUB_LOG_DEBUG("[SUB-MM] face result: count=%u conf=%u box=(%d,%d)-(%d,%d)\r\n",
                      (unsigned)latest_result.count,
                      (unsigned)latest_result.confidence,
                      (int)latest_result.x1,
                      (int)latest_result.y1,
                      (int)latest_result.x2,
                      (int)latest_result.y2);
    }
}

void subboard_mm_app_bump_heartbeat_if_needed(SubboardMmAppContext *ctx)
{
    if ((ctx != NULL) && ((subboard_mm_app_millis(ctx) - ctx->last_heartbeat_ms) >= 1000u)) {
        subboard_startup_i2c_bump_heartbeat();
        ctx->last_heartbeat_ms = subboard_mm_app_millis(ctx);
    }
}

void subboard_mm_app_log_public_state_if_needed(SubboardMmAppContext *ctx)
{
    if ((ctx != NULL) && (ctx->last_logged_state != ctx->public_state)) {
        SUB_LOG_DEBUG("[SUB-MM] public_state=%s\r\n",
                      subboard_mm_app_public_state_name(ctx->public_state));
        ctx->last_logged_state = ctx->public_state;
    }
}