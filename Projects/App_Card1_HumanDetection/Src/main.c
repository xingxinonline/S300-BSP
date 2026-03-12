#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "s300.h"
#include "board.h"
#include "rcc.h"
#include "video.h"
#include "psram.h"
#include "mailbox.h"
#include "mailbox_proto.h"

#include "card1_detection.h"
#include "card1_i2c_slave.h"

#if BOARD_CAMERA_FORMAT == 0
#define APP_CAM_FMT CAMREA_RGB565
#else
#define APP_CAM_FMT CAMREA_YUV422
#endif

static volatile uint32_t g_tick_ms;

#ifndef MAILBOX_HANDSHAKE_INIT
#define MAILBOX_HANDSHAKE_INIT 0x5A5A5A5Au
#endif

#ifndef MAILBOX_HANDSHAKE_ACK
#define MAILBOX_HANDSHAKE_ACK 0xA5A5A5A5u
#endif

#ifndef DSP_HANDSHAKE_TIMEOUT_MS
#define DSP_HANDSHAKE_TIMEOUT_MS 1000u
#endif

#ifndef DSP_HANDSHAKE_RETRY_MS
#define DSP_HANDSHAKE_RETRY_MS 100u
#endif

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t millis(void)
{
    return g_tick_ms;
}

static void card1_dump_mm_video_regs(const char *tag)
{
    uint32_t reg_20;
    uint32_t reg_28;
    uint32_t reg_30;
    uint32_t reg_40;
    uint32_t reg_58;

    reg_20 = REG32(DSP_VIDEO_SS_BASE + 0x20);
    reg_28 = REG32(DSP_VIDEO_SS_BASE + 0x28);
    reg_30 = REG32(DSP_VIDEO_SS_BASE + 0x30);
    reg_40 = REG32(DSP_VIDEO_SS_BASE + 0x40);
    reg_58 = REG32(DSP_VIDEO_SS_BASE + 0x58);

    printf("[CARD1][MMDBG] %s macro disp=%ux%u snap=%ux%u\r\n",
        tag,
        (unsigned)DISP_IMAGE_WIDTH,
        (unsigned)DISP_IMAGE_HEIGHT,
        (unsigned)SNAP_IMAGE_WIDTH,
        (unsigned)SNAP_IMAGE_HEIGHT);
    printf("[CARD1][MMDBG] %s reg20=0x%08lX disp=%lux%lu\r\n",
        tag,
        (unsigned long)reg_20,
        (unsigned long)(reg_20 & 0xFFFFu),
        (unsigned long)((reg_20 >> 16) & 0xFFFFu));
    printf("[CARD1][MMDBG] %s reg28=0x%08lX snap=%lux%lu\r\n",
        tag,
        (unsigned long)reg_28,
        (unsigned long)(reg_28 & 0xFFFFu),
        (unsigned long)((reg_28 >> 16) & 0xFFFFu));
    printf("[CARD1][MMDBG] %s reg30=0x%08lX reg40=0x%08lX reg58=0x%08lX\r\n",
        tag,
        (unsigned long)reg_30,
        (unsigned long)reg_40,
        (unsigned long)reg_58);
}

static int card1_wait_handshake_done(void)
{
    uint32_t t0 = millis();
    uint32_t next_init_ms = t0;

    while ((uint32_t)(millis() - t0) < DSP_HANDSHAKE_TIMEOUT_MS) {
        uint32_t now_ms = millis();

        if ((int32_t)(now_ms - next_init_ms) >= 0) {
            (void)write_mailbox(MAILBOX_BASE, MAILBOX_HANDSHAKE_INIT);
            next_init_ms = now_ms + DSP_HANDSHAKE_RETRY_MS;
        }

        if (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
            uint32_t msg = read_mailbox(MAILBOX_BASE);
            if (msg == MAILBOX_HANDSHAKE_ACK) {
                printf("[CARD1] DSP handshake ACK received\r\n");
                return 0;
            }
            if (msg == MAILBOX_HANDSHAKE_INIT) {
                (void)write_mailbox(MAILBOX_BASE, MAILBOX_HANDSHAKE_ACK);
                printf("[CARD1] DSP handshake INIT received, ACK sent\r\n");
                return 0;
            }
        }
    }

    printf("[CARD1] DSP handshake timeout (%ums)\r\n", (unsigned)DSP_HANDSHAKE_TIMEOUT_MS);
    return -1;
}

static int card1_start_mm_dsp(void)
{
    rcc_init_mm_pll(8, 400, 0, 3, 2);
    rcc_init_dsp_pll(6, 800, 0, 2, 2);
    init_psram(4, 1);

    /* 子板不初始化 OV5640：仅消费主板经 CPLD 分发过来的视频流。 */
    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);
    card1_dump_mm_video_regs("after init_video");

    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);

    /* Follow the same DSP boot sequence used by working demos. */
    set_dsp_warm_reset(true);
    RCC->CM4_SYS_SOFT_RSTN |= 1u;

    for (volatile int i = 0; i < 100000; i++) {
    }

    /* Release warm reset before handshake. */
    set_dsp_warm_reset(false);

    for (volatile int i = 0; i < 100000; i++) {
    }

    if (card1_wait_handshake_done() != 0) {
        return -1;
    }

    card1_dump_mm_video_regs("after handshake");

    (void)write_mailbox(MAILBOX_BASE, MAILBOX_CMD_START_TRACK);

    return 0;
}

int main(void)
{
    uint32_t last_wait_log_ms;
    uint32_t last_log_ms;
    uint8_t sys_state;
    uint8_t cmd;
    uint8_t started;
    uint8_t dsp_msg_seen;
    uint32_t start_retry_ms;
    card1_detection_result_t result;

    board_init();

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        while (1) {
        }
    }

    printf("\r\n[CARD1] Human Detection Slave Boot\r\n");

    card1_detection_init();
    memset(&result, 0, sizeof(result));

    if (card1_i2c_slave_init(CARD1_I2C_ADDR) != 0) {
        printf("[CARD1] i2c slave init failed\r\n");
        while (1) {
        }
    }

    sys_state = CARD1_SYS_STATE_BOOT | CARD1_SYS_STATE_I2C_READY;
    card1_i2c_slave_set_system_state(sys_state);

    printf("[CARD1] I2C slave ready: addr=0x%02X\r\n", CARD1_I2C_ADDR);
    printf("[CARD1] waiting host cmd: START_MM_DSP (%u)\r\n", (unsigned int)CARD1_CMD_START_MM_DSP);

    last_wait_log_ms = millis();
    last_log_ms = millis();
    started = 0u;
    dsp_msg_seen = 0u;
    start_retry_ms = millis();

    while (1) {
        cmd = card1_i2c_slave_get_command();

        if (started == 0u) {
            if (cmd == CARD1_CMD_START_MM_DSP) {
                printf("[CARD1] host cmd START_MM_DSP received\r\n");
                if (card1_start_mm_dsp() == 0) {
                    started = 1u;
                    dsp_msg_seen = 0u;
                    start_retry_ms = millis();
                    sys_state |= (CARD1_SYS_STATE_MM_READY |
                                  CARD1_SYS_STATE_DSP_READY |
                                  CARD1_SYS_STATE_RUNNING);
                    card1_i2c_slave_set_system_state(sys_state);
                    card1_i2c_slave_set_command_ack(cmd);
                    card1_i2c_slave_clear_command();
                    printf("[CARD1] MM+DSP started\r\n");
                } else {
                    card1_i2c_slave_set_command_ack(cmd);
                    card1_i2c_slave_clear_command();
                    printf("[CARD1] MM+DSP start failed\r\n");
                }
            } else if (cmd == CARD1_CMD_STOP_MM_DSP) {
                card1_i2c_slave_set_command_ack(cmd);
                card1_i2c_slave_clear_command();
            }

            if ((millis() - last_wait_log_ms) >= 1000u) {
                printf("[CARD1] waiting host start cmd...\r\n");
                last_wait_log_ms = millis();
            }
            continue;
        }

        if (cmd == CARD1_CMD_STOP_MM_DSP) {
            (void)write_mailbox(MAILBOX_BASE, MAILBOX_CMD_STOP_TRACK);
            started = 0u;
            dsp_msg_seen = 0u;
            sys_state &= (uint8_t)~CARD1_SYS_STATE_RUNNING;
            card1_i2c_slave_set_system_state(sys_state);
            card1_i2c_slave_set_command_ack(cmd);
            card1_i2c_slave_clear_command();
            memset(&result, 0, sizeof(result));
            card1_i2c_slave_update_result(&result);
            printf("[CARD1] host cmd STOP_MM_DSP received\r\n");
            continue;
        }

        if (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
            dsp_msg_seen = 1u;
        } else if ((dsp_msg_seen == 0u) && ((millis() - start_retry_ms) >= 1000u)) {
            (void)write_mailbox(MAILBOX_BASE, MAILBOX_CMD_START_TRACK);
            start_retry_ms = millis();
            printf("[CARD1] no dsp msg yet, resend START_TRACK\r\n");
        }

        card1_detection_poll();
        if (card1_detection_get_latest(&result)) {
            card1_i2c_slave_update_result(&result);
        }

        if ((millis() - last_log_ms) >= 1000u) {
            printf("[CARD1] valid=%u cnt=%u conf=%u cx=%d cy=%d\r\n",
                   (unsigned int)result.valid,
                   (unsigned int)result.count,
                   (unsigned int)result.confidence,
                   (int)result.cx,
                   (int)result.cy);
            last_log_ms = millis();
        }
    }
}
