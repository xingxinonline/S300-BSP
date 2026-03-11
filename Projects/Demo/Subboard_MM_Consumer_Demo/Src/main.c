#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "psram.h"
#include "rcc.h"
#include "s300.h"
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
static bool g_master_mm_requested = false;
static bool g_master_mm_granted = false;

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

static int start_mm_consumer(void)
{
    int ret;

    if (g_mm_started) {
        return 0;
    }

    printf("[SUB-MM] starting MM-only consumer path\r\n");
    printf("[SUB-MM] OV5640 init skipped; LCD init skipped\r\n");

    ret = rcc_init_mm_pll(8, 400, 0, 3, 2);
    if (ret != RCC_STATUS_OK) {
        printf("[SUB-MM] rcc_init_mm_pll failed=%d\r\n", ret);
        return -1;
    }

    init_psram(4, 1);
    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);
    dump_mm_video_regs("after init_video");

    g_mm_started = true;
    return 0;
}

int main(void)
{
    uint32_t last_heartbeat_ms;
    uint32_t last_debug_ms;
    uint8_t last_logged_state;

    board_init();

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        while (1) {
        }
    }

    printf("\r\n===========================================\r\n");
    printf("  S300 Subboard MM Consumer Demo\r\n");
    printf("  MM-only video bring-up, no DSP\r\n");
    printf("===========================================\r\n");

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
    last_debug_ms = millis();
    last_logged_state = 0xFFu;

    while (1) {
        uint8_t cmd = subboard_startup_i2c_get_command();
        uint8_t request_ack = subboard_startup_i2c_get_request_ack();

        if ((millis() - last_heartbeat_ms) >= 1000u) {
            subboard_startup_i2c_bump_heartbeat();
            last_heartbeat_ms = millis();
        }

        if ((millis() - last_debug_ms) >= 1000u) {
            subboard_startup_i2c_debug_stats_t stats;

            subboard_startup_i2c_get_debug_stats(&stats);
            printf("[SUB-MM][I2CDBG] reg00=%02X reg01=%02X reg03=%02X reg04=%02X reg05=%02X reg07=%02X\r\n",
                   subboard_startup_i2c_peek_reg(SUBBOARD_STARTUP_REG_STATUS),
                   subboard_startup_i2c_peek_reg(SUBBOARD_STARTUP_REG_SYS_STATE),
                   subboard_startup_i2c_peek_reg(SUBBOARD_STARTUP_REG_HEARTBEAT),
                   subboard_startup_i2c_peek_reg(SUBBOARD_STARTUP_REG_PROTO_VER),
                   subboard_startup_i2c_peek_reg(SUBBOARD_STARTUP_REG_REQUEST),
                   subboard_startup_i2c_peek_reg(SUBBOARD_STARTUP_REG_REQUEST_ACK));
            printf("[SUB-MM][I2CDBG] start=%lu rx=%lu rd=%lu done=%lu stop=%lu restart=%lu reg=%u tx=%u\r\n",
                   (unsigned long)stats.start_det_count,
                   (unsigned long)stats.rx_full_count,
                   (unsigned long)stats.rd_req_count,
                   (unsigned long)stats.rx_done_count,
                   (unsigned long)stats.stop_det_count,
                   (unsigned long)stats.restart_det_count,
                   (unsigned)stats.current_reg_addr,
                   (unsigned)stats.current_tx_index);
            last_debug_ms = millis();
        }

        if (!g_master_mm_requested &&
            (g_public_state == SUBBOARD_STARTUP_STATE_WAIT_VIDEO)) {
            subboard_startup_i2c_set_request(SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE, 0u);
            enter_public_state(SUBBOARD_STARTUP_STATE_WAIT_MASTER_MM);
            g_master_mm_requested = true;
            printf("[SUB-MM] REQUEST MASTER_MM_ENABLE posted\r\n");
        }

        if (!g_master_mm_granted &&
            g_master_mm_requested &&
            (request_ack == SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE)) {
            g_master_mm_granted = true;
            subboard_startup_i2c_clear_request();
            printf("[SUB-MM] REQUEST MASTER_MM_ENABLE acknowledged by master\r\n");
        }

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
            printf("[SUB-MM] CMD START_DSP not supported in MM-only demo\r\n");
            subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_UNSUPPORTED_CMD);
            subboard_startup_i2c_set_command_result(cmd, SUBBOARD_STARTUP_RESULT_NOT_SUPPORTED);
            subboard_startup_i2c_clear_command();
            break;

        case SUBBOARD_STARTUP_CMD_CLEAR_ERROR:
            printf("[SUB-MM] CMD CLEAR_ERROR\r\n");
            subboard_startup_i2c_set_error_code(SUBBOARD_STARTUP_ERR_NONE);
            if (g_public_state == SUBBOARD_STARTUP_STATE_ERROR) {
                enter_public_state(SUBBOARD_STARTUP_STATE_WAIT_VIDEO);
                g_master_mm_requested = false;
                g_master_mm_granted = false;
                subboard_startup_i2c_clear_request();
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