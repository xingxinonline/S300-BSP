#include <stdio.h>
#include <stdint.h>

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

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t millis(void)
{
    return g_tick_ms;
}

int main(void)
{
    uint32_t last_log_ms;
    card1_detection_result_t result;

    board_init();

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        while (1) {
        }
    }

    printf("\r\n[CARD1] Human Detection Slave Boot\r\n");

    rcc_init_mm_pll(8, 400, 0, 3, 2);
    rcc_init_dsp_pll(6, 800, 0, 2, 2);
    init_psram(4, 1);

    /* 子板不初始化 OV5640：主板完成初始化后由 CPLD 分发视频到子板 DVP。 */
    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);

    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    set_dsp_warm_reset(true);
    (void)write_mailbox(MAILBOX_BASE, 0x5A5A5A5Au);
    (void)write_mailbox(MAILBOX_BASE, MAILBOX_CMD_START_TRACK);

    card1_detection_init();
    if (card1_i2c_slave_init(CARD1_I2C_ADDR) != 0) {
        printf("[CARD1] i2c slave init failed\r\n");
        while (1) {
        }
    }

    printf("[CARD1] I2C slave ready: addr=0x%02X\r\n", CARD1_I2C_ADDR);

    last_log_ms = millis();

    while (1) {
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
