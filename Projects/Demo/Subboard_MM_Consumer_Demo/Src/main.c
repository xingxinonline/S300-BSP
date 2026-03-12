#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "control_proto.h"
#include "psram.h"
#include "rcc.h"
#include "s300.h"
#include "subboard_dsp_ctrl.h"
#include "subboard_startup_i2c.h"
#include "subboard_startup_proto.h"
#include "video.h"

#if BOARD_CAMERA_FORMAT == 0
#define APP_CAM_FMT CAMREA_RGB565
#else
#define APP_CAM_FMT CAMREA_YUV422
#endif

static volatile uint32_t g_tick_ms = 0u;
static uint8_t g_public_state = SUBBOARD_STARTUP_STATE_BOOT;
static bool g_mm_started = false;
static bool g_dsp_pll_started = false;
static bool g_master_mm_requested = false;
static bool g_master_mm_granted = false;
static uint8_t g_active_request = SUBBOARD_STARTUP_REQ_NONE;
static subboard_detection_result_t g_last_published_result;

static void refresh_dsp_resource_flags(void)
{
    uint8_t resource_flags = 0u;

    if (g_master_mm_granted) {
        resource_flags |= CONTROL_RESOURCE_CAMERA_READY;
        resource_flags |= CONTROL_RESOURCE_LCD_READY;
    }

    if (g_mm_started) {
        resource_flags |= CONTROL_RESOURCE_MM_READY;
    }

    subboard_dsp_ctrl_set_resource_flags(resource_flags);
}

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t millis(void)
{
    return g_tick_ms;
}

static const char *public_state_name(uint8_t state)
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

static void enter_public_state(uint8_t next_state)
{
    if (g_public_state != next_state) {
        printf("[SUB-MM] STATE %s -> %s\r\n",
               public_state_name(g_public_state),
               public_state_name(next_state));
        g_public_state = next_state;
        subboard_startup_i2c_set_public_state(next_state);
    }
}

static void dump_mm_video_regs(const char *tag)
{
    uint32_t reg_20 = REG32(DSP_VIDEO_SS_BASE + 0x20u);
    uint32_t reg_28 = REG32(DSP_VIDEO_SS_BASE + 0x28u);

    printf("[SUB-MM] %s disp=%ux%u snap=%ux%u reg20=0x%08lX reg28=0x%08lX\r\n",
           tag,
           (unsigned)DISP_IMAGE_WIDTH,
           (unsigned)DISP_IMAGE_HEIGHT,
           (unsigned)SNAP_IMAGE_WIDTH,
           (unsigned)SNAP_IMAGE_HEIGHT,
           (unsigned long)reg_20,
           (unsigned long)reg_28);
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

static void trigger_local_mm_runtime_enable(void)
{
    if (!g_mm_started) {
        printf("[SUB-MM] skip local MM runtime enable: MM consumer not started\r\n");
        return;
    }

    trigger_core_reg_update();

#if defined(BOARD_LCD_SPI_ENABLE_ON_INIT) && (BOARD_LCD_SPI_ENABLE_ON_INIT == 0)
    printf("[SUB-MM] applied local MM runtime enable: core only, local LCD SPI path disabled\r\n");
#else
    trigger_spi_reg_update();
    printf("[SUB-MM] applied local MM runtime enable: core + lcd spi\r\n");
#endif
}

__attribute__((used, noinline)) void subboard_dsp_image_load_point(void)
{
    __asm volatile("" ::: "memory");
}

static int start_mm_consumer(void)
{
    int ret;

    if (g_mm_started) {
        return 0;
    }

    printf("[SUB-MM] starting MM consumer path\r\n");
    printf("[SUB-MM] OV5640 init skipped; MM/SPI config mirrored from master, LCD SPI output disabled\r\n");

    ret = rcc_init_mm_pll(8, 400, 0, 3, 2);
    if (ret != RCC_STATUS_OK) {
        printf("[SUB-MM] rcc_init_mm_pll failed=%d\r\n", ret);
        return -1;
    }

    init_psram(4, 1);
    subboard_dsp_image_load_point();
    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);
    dump_mm_video_regs("after init_video");

    g_mm_started = true;
    subboard_dsp_ctrl_set_mm_ready(true);
    refresh_dsp_resource_flags();
    return 0;
}

static int ensure_dsp_pll_started(void)
{
    int ret;

    if (g_dsp_pll_started) {
        return 0;
    }

    ret = rcc_init_dsp_pll(6, 800, 0, 2, 2);
    if (ret != RCC_STATUS_OK) {
        printf("[SUB-MM] rcc_init_dsp_pll failed=%d\r\n", ret);
        return -1;
    }

    g_dsp_pll_started = true;
    printf("[SUB-MM] DSP PLL ready\r\n");
    return 0;
}

static void reset_request_path(void)
{
    g_master_mm_requested = false;
    g_master_mm_granted = false;
    g_active_request = SUBBOARD_STARTUP_REQ_NONE;
    refresh_dsp_resource_flags();
    subboard_startup_i2c_clear_request();
}

static void post_next_master_request(void)
{
    uint8_t request = SUBBOARD_STARTUP_REQ_NONE;

    if (g_active_request != SUBBOARD_STARTUP_REQ_NONE) {
        return;
    }

    if (!g_master_mm_requested &&
        (g_public_state == SUBBOARD_STARTUP_STATE_WAIT_VIDEO)) {
        request = SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE;
        subboard_startup_i2c_set_request(request, 0u);
        enter_public_state(SUBBOARD_STARTUP_STATE_WAIT_MASTER_MM);
        g_master_mm_requested = true;
        g_active_request = request;
        printf("[SUB-MM] REQUEST %s posted\r\n", request_name(request));
        return;
    }

    request = subboard_dsp_ctrl_peek_master_request();
    if (request != SUBBOARD_STARTUP_REQ_NONE) {
        if (request == SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME) {
            trigger_local_mm_runtime_enable();
        }

        subboard_startup_i2c_set_request(request, 0u);
        g_active_request = request;
        printf("[SUB-MM] REQUEST %s posted from DSP runtime notify\r\n",
               request_name(request));
    }
}

static void consume_request_ack(uint8_t request_ack)
{
    if ((g_active_request == SUBBOARD_STARTUP_REQ_NONE) ||
        (request_ack != g_active_request)) {
        return;
    }

    if (g_active_request == SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE) {
        g_master_mm_granted = true;
        refresh_dsp_resource_flags();
    } else {
        subboard_dsp_ctrl_complete_master_request(g_active_request);
    }

    printf("[SUB-MM] REQUEST %s acknowledged by master\r\n",
           request_name(g_active_request));
    subboard_startup_i2c_clear_request();
    g_active_request = SUBBOARD_STARTUP_REQ_NONE;
}

static void sync_public_state_from_dsp(void)
{
    uint8_t dsp_state;

    if (!subboard_dsp_ctrl_is_active()) {
        return;
    }

    dsp_state = subboard_dsp_ctrl_get_public_state();
    if (dsp_state == SUBBOARD_STARTUP_STATE_ERROR) {
        subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_DSP_START_FAIL);
        enter_public_state(SUBBOARD_STARTUP_STATE_ERROR);
        return;
    }

    if ((dsp_state != SUBBOARD_STARTUP_STATE_MM_READY) &&
        (g_public_state != dsp_state)) {
        enter_public_state(dsp_state);
    }
}

int main(void)
{
    uint32_t last_heartbeat_ms;
    uint8_t last_logged_state;

    board_init();

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        while (1) {
        }
    }

    printf("\r\n===========================================\r\n");
    printf("  S300 Subboard MM Consumer Demo\r\n");
    printf("  MM consumer bring-up with optional DSP\r\n");
    printf("===========================================\r\n");

    subboard_dsp_ctrl_init(millis);
    refresh_dsp_resource_flags();
    subboard_startup_i2c_update_result(&g_last_published_result);

    if (subboard_startup_i2c_init(SUBBOARD_STARTUP_SLAVE_ADDR_CARD1) != 0) {
        printf("[SUB-MM] i2c slave init failed\r\n");
        while (1) {
        }
    }

    subboard_startup_i2c_set_status(SUBBOARD_STARTUP_STATUS_NONE);
    subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_NONE);

    enter_public_state(SUBBOARD_STARTUP_STATE_I2C_READY);
    enter_public_state(SUBBOARD_STARTUP_STATE_WAIT_VIDEO);

    printf("[SUB-MM] I2C slave ready: addr=0x%02X proto=0x%02X\r\n",
           SUBBOARD_STARTUP_SLAVE_ADDR_CARD1,
           SUBBOARD_STARTUP_PROTO_VER);

    last_heartbeat_ms = millis();
    last_logged_state = 0xFFu;

    while (1) {
        uint8_t cmd = subboard_startup_i2c_get_command();
        uint8_t request_ack = subboard_startup_i2c_get_request_ack();
        subboard_detection_result_t latest_result;

        subboard_dsp_ctrl_tick();

        if (subboard_dsp_ctrl_get_latest_result(&latest_result) &&
            (memcmp(&latest_result, &g_last_published_result, sizeof(latest_result)) != 0)) {
            subboard_startup_i2c_update_result(&latest_result);
            g_last_published_result = latest_result;

            if (latest_result.valid != 0u) {
                printf("[SUB-MM] face result: count=%u conf=%u box=(%d,%d)-(%d,%d)\r\n",
                       (unsigned)latest_result.count,
                       (unsigned)latest_result.confidence,
                       (int)latest_result.x1,
                       (int)latest_result.y1,
                       (int)latest_result.x2,
                       (int)latest_result.y2);
            }
        }

        if ((millis() - last_heartbeat_ms) >= 1000u) {
            subboard_startup_i2c_bump_heartbeat();
            last_heartbeat_ms = millis();
        }

        consume_request_ack(request_ack);
        sync_public_state_from_dsp();
        post_next_master_request();

        if (last_logged_state != g_public_state) {
            printf("[SUB-MM] public_state=%s\r\n", public_state_name(g_public_state));
            last_logged_state = g_public_state;
        }

        if (cmd == SUBBOARD_STARTUP_CMD_NONE) {
            continue;
        }

        switch (cmd) {
        case SUBBOARD_STARTUP_CMD_PING:
            printf("[SUB-MM] CMD PING\r\n");
            subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_OK);
            subboard_startup_i2c_clear_command();
            break;

        case SUBBOARD_STARTUP_CMD_PREPARE_VIDEO:
            printf("[SUB-MM] CMD PREPARE_VIDEO_CONSUMER arg=0x%02X\r\n",
                   subboard_startup_i2c_get_command_arg());

            if (!g_master_mm_granted) {
                printf("[SUB-MM] reject PREPARE_VIDEO: waiting master MM grant\r\n");
                subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_BUSY);
                subboard_startup_i2c_clear_command();
                break;
            }

            if ((g_public_state != SUBBOARD_STARTUP_STATE_WAIT_MASTER_MM) &&
                (g_public_state != SUBBOARD_STARTUP_STATE_WAIT_VIDEO) &&
                (g_public_state != SUBBOARD_STARTUP_STATE_I2C_READY)) {
                subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_INVALID_STATE);
                subboard_startup_i2c_clear_command();
                break;
            }

            if (start_mm_consumer() == 0) {
                subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_NONE);
                enter_public_state(SUBBOARD_STARTUP_STATE_MM_READY);
                subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_OK);
            } else {
                subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_MM_INIT_FAIL);
                enter_public_state(SUBBOARD_STARTUP_STATE_ERROR);
                subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_MM_FAILED);
            }
            subboard_startup_i2c_clear_command();
            break;

        case SUBBOARD_STARTUP_CMD_START_DSP:
            printf("[SUB-MM] CMD START_DSP arg=0x%02X\r\n",
                   subboard_startup_i2c_get_command_arg());

            if (!g_mm_started || !g_master_mm_granted) {
                printf("[SUB-MM] reject START_DSP: MM path not ready\r\n");
                subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_BUSY);
                subboard_startup_i2c_clear_command();
                break;
            }

            if (subboard_dsp_ctrl_is_active()) {
                printf("[SUB-MM] reject START_DSP: DSP control already active\r\n");
                subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_BUSY);
                subboard_startup_i2c_clear_command();
                break;
            }

            if (ensure_dsp_pll_started() != 0) {
                subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_DSP_START_FAIL);
                enter_public_state(SUBBOARD_STARTUP_STATE_ERROR);
                subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_DSP_FAILED);
                subboard_startup_i2c_clear_command();
                break;
            }

            if (subboard_dsp_ctrl_start() == 0) {
                subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_NONE);
                enter_public_state(SUBBOARD_STARTUP_STATE_DSP_STARTING);
                subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_OK);
            } else {
                subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_DSP_START_FAIL);
                enter_public_state(SUBBOARD_STARTUP_STATE_ERROR);
                subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_DSP_FAILED);
            }
            subboard_startup_i2c_clear_command();
            break;

        case SUBBOARD_STARTUP_CMD_CLEAR_ERROR:
            printf("[SUB-MM] CMD CLEAR_ERROR\r\n");
            subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_NONE);
            subboard_dsp_ctrl_reset();
            subboard_dsp_ctrl_set_mm_ready(g_mm_started);
            refresh_dsp_resource_flags();
            if (g_public_state == SUBBOARD_STARTUP_STATE_ERROR) {
                if (g_mm_started && g_master_mm_granted) {
                    enter_public_state(SUBBOARD_STARTUP_STATE_MM_READY);
                } else {
                    enter_public_state(SUBBOARD_STARTUP_STATE_WAIT_VIDEO);
                    reset_request_path();
                }
            }
            subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_OK);
            subboard_startup_i2c_clear_command();
            break;

        case SUBBOARD_STARTUP_CMD_STOP_PIPELINE:
        default:
            printf("[SUB-MM] CMD 0x%02X not supported\r\n", cmd);
            subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_UNSUPPORTED_CMD);
            subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_NOT_SUPPORTED);
            subboard_startup_i2c_clear_command();
            break;
        }
    }
}