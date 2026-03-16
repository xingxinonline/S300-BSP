#include "subboard_mm_app_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "psram.h"
#include "rcc.h"
#include "s300.h"
#include "subboard_dsp_ctrl.h"
#include "subboard_log.h"
#include "subboard_startup_i2c.h"
#include "subboard_startup_proto.h"
#include "video.h"
#include "video_config.h"

#ifndef REG32
#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#endif

#if BOARD_CAMERA_FORMAT == 0
#define APP_CAM_FMT CAMREA_RGB565
#else
#define APP_CAM_FMT CAMREA_YUV422
#endif

static void dump_mm_video_regs(const char *tag)
{
#if SUBBOARD_LOG_LEVEL >= SUBBOARD_LOG_LEVEL_DEBUG
    uint32_t reg_20 = REG32(DSP_VIDEO_SS_BASE + 0x20u);
    uint32_t reg_28 = REG32(DSP_VIDEO_SS_BASE + 0x28u);

    SUB_LOG_DEBUG("[CARD1] %s disp=%ux%u snap=%ux%u reg20=0x%08lX reg28=0x%08lX\r\n",
                  tag,
                  (unsigned)DISP_IMAGE_WIDTH,
                  (unsigned)DISP_IMAGE_HEIGHT,
                  (unsigned)SNAP_IMAGE_WIDTH,
                  (unsigned)SNAP_IMAGE_HEIGHT,
                  (unsigned long)reg_20,
                  (unsigned long)reg_28);
#else
    (void)tag;
#endif
}

__attribute__((used, noinline)) static void subboard_dsp_image_load_point(void)
{
    __asm volatile("" ::: "memory");
}

int subboard_mm_app_start_mm_consumer(SubboardMmAppContext *ctx)
{
    int ret;

    if ((ctx == NULL) || ctx->mm_started) {
        return 0;
    }

    SUB_LOG_INFO("[CARD1] starting MM consumer path\r\n");
#if defined(BOARD_LCD_SPI_ENABLE_ON_INIT) && (BOARD_LCD_SPI_ENABLE_ON_INIT == 0)
    SUB_LOG_INFO("[CARD1] OV5640 init skipped; MM/SPI config mirrored from master, LCD SPI output disabled\r\n");
#else
    SUB_LOG_INFO("[CARD1] OV5640 init skipped; MM/SPI config mirrored from master, LCD SPI output enabled\r\n");
#endif

    ret = rcc_init_mm_pll(8, 400, 0, 3, 2);
    if (ret != RCC_STATUS_OK) {
        SUB_LOG_WARN("[CARD1] rcc_init_mm_pll failed=%d\r\n", ret);
        return -1;
    }

    init_psram(4, 1);
    subboard_dsp_image_load_point();
    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);
    dump_mm_video_regs("after init_video");

    ctx->mm_started = true;
    subboard_dsp_ctrl_set_mm_ready(true);
    subboard_mm_app_refresh_dsp_resource_flags(ctx);
    return 0;
}

int subboard_mm_app_ensure_dsp_pll_started(SubboardMmAppContext *ctx)
{
    int ret;

    if ((ctx == NULL) || ctx->dsp_pll_started) {
        return 0;
    }

    ret = rcc_init_dsp_pll(6, 800, 0, 2, 2);
    if (ret != RCC_STATUS_OK) {
        SUB_LOG_WARN("[CARD1] rcc_init_dsp_pll failed=%d\r\n", ret);
        return -1;
    }

    ctx->dsp_pll_started = true;
    SUB_LOG_INFO("[CARD1] DSP PLL ready\r\n");
    return 0;
}

static void finish_command(uint8_t cmd, uint8_t result)
{
    subboard_startup_i2c_set_command_result(cmd, result);
    subboard_startup_i2c_clear_command();
}

static void handle_ping_cmd(uint8_t cmd)
{
    SUB_LOG_DEBUG("[CARD1] CMD PING\r\n");
    finish_command(cmd, SUBBOARD_STARTUP_RESULT_OK);
}

static void handle_prepare_video_cmd(SubboardMmAppContext *ctx, uint8_t cmd)
{
    SUB_LOG_INFO("[CARD1] CMD PREPARE_VIDEO_CONSUMER arg=0x%02X\r\n",
                 subboard_startup_i2c_get_command_arg());

    if (!ctx->master_mm_granted) {
        SUB_LOG_WARN("[CARD1] reject PREPARE_VIDEO: waiting master MM grant\r\n");
        finish_command(cmd, SUBBOARD_STARTUP_RESULT_BUSY);
        return;
    }

    if ((ctx->public_state != SUBBOARD_STARTUP_STATE_WAIT_MASTER_MM) &&
        (ctx->public_state != SUBBOARD_STARTUP_STATE_WAIT_VIDEO) &&
        (ctx->public_state != SUBBOARD_STARTUP_STATE_I2C_READY)) {
        finish_command(cmd, SUBBOARD_STARTUP_RESULT_INVALID_STATE);
        return;
    }

    if (subboard_mm_app_start_mm_consumer(ctx) == 0) {
        subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_NONE);
        subboard_mm_app_enter_public_state(ctx, SUBBOARD_STARTUP_STATE_MM_READY);
        finish_command(cmd, SUBBOARD_STARTUP_RESULT_OK);
        return;
    }

    subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_MM_INIT_FAIL);
    subboard_mm_app_enter_public_state(ctx, SUBBOARD_STARTUP_STATE_ERROR);
    finish_command(cmd, SUBBOARD_STARTUP_RESULT_MM_FAILED);
}

static void handle_start_dsp_cmd(SubboardMmAppContext *ctx, uint8_t cmd)
{
    SUB_LOG_INFO("[CARD1] CMD START_DSP arg=0x%02X\r\n",
                 subboard_startup_i2c_get_command_arg());

    if (!ctx->mm_started || !ctx->master_mm_granted) {
        SUB_LOG_WARN("[CARD1] reject START_DSP: MM path not ready\r\n");
        finish_command(cmd, SUBBOARD_STARTUP_RESULT_BUSY);
        return;
    }

    if (subboard_dsp_ctrl_is_active()) {
        SUB_LOG_WARN("[CARD1] reject START_DSP: DSP control already active\r\n");
        finish_command(cmd, SUBBOARD_STARTUP_RESULT_BUSY);
        return;
    }

    if (subboard_mm_app_ensure_dsp_pll_started(ctx) != 0) {
        subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_DSP_START_FAIL);
        subboard_mm_app_enter_public_state(ctx, SUBBOARD_STARTUP_STATE_ERROR);
        finish_command(cmd, SUBBOARD_STARTUP_RESULT_DSP_FAILED);
        return;
    }

    if (subboard_dsp_ctrl_start() == 0) {
        subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_NONE);
        subboard_mm_app_enter_public_state(ctx, SUBBOARD_STARTUP_STATE_DSP_STARTING);
        finish_command(cmd, SUBBOARD_STARTUP_RESULT_OK);
        return;
    }

    subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_DSP_START_FAIL);
    subboard_mm_app_enter_public_state(ctx, SUBBOARD_STARTUP_STATE_ERROR);
    finish_command(cmd, SUBBOARD_STARTUP_RESULT_DSP_FAILED);
}

static void handle_clear_error_cmd(SubboardMmAppContext *ctx, uint8_t cmd)
{
    SUB_LOG_INFO("[CARD1] CMD CLEAR_ERROR\r\n");
    subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_NONE);
    subboard_dsp_ctrl_reset();
    subboard_dsp_ctrl_set_mm_ready(ctx->mm_started);
    subboard_mm_app_refresh_dsp_resource_flags(ctx);

    if (ctx->public_state == SUBBOARD_STARTUP_STATE_ERROR) {
        if (ctx->mm_started && ctx->master_mm_granted) {
            subboard_mm_app_enter_public_state(ctx, SUBBOARD_STARTUP_STATE_MM_READY);
        } else {
            subboard_mm_app_enter_public_state(ctx, SUBBOARD_STARTUP_STATE_WAIT_VIDEO);
            subboard_mm_app_reset_request_path(ctx);
        }
    }

    finish_command(cmd, SUBBOARD_STARTUP_RESULT_OK);
}

static void handle_unsupported_cmd(uint8_t cmd)
{
    SUB_LOG_WARN("[CARD1] CMD 0x%02X not supported\r\n", cmd);
    subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_UNSUPPORTED_CMD);
    finish_command(cmd, SUBBOARD_STARTUP_RESULT_NOT_SUPPORTED);
}

void subboard_mm_app_handle_command(SubboardMmAppContext *ctx, uint8_t cmd)
{
    if ((ctx == NULL) || (cmd == SUBBOARD_STARTUP_CMD_NONE)) {
        return;
    }

    switch (cmd) {
    case SUBBOARD_STARTUP_CMD_PING:
        handle_ping_cmd(cmd);
        break;

    case SUBBOARD_STARTUP_CMD_PREPARE_VIDEO:
        handle_prepare_video_cmd(ctx, cmd);
        break;

    case SUBBOARD_STARTUP_CMD_START_DSP:
        handle_start_dsp_cmd(ctx, cmd);
        break;

    case SUBBOARD_STARTUP_CMD_CLEAR_ERROR:
        handle_clear_error_cmd(ctx, cmd);
        break;

    case SUBBOARD_STARTUP_CMD_STOP_PIPELINE:
    default:
        handle_unsupported_cmd(cmd);
        break;
    }
}