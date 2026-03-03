/**
 * @file    main.c
 * @brief   KWS CM4 轮询版 Demo
 * @note    参考 SDK cortex-m4-null/app/main.c 与 app/kws_dsp.c 的处理流程
 */

#include "s300.h"
#include "board.h"
#include "audio_app.h"
#include "gpio.h"
#include "rcc.h"
#include "kws_dsp.h"
#include <stdio.h>

__attribute__((noinline)) void dsp_firmware_load_point(void)
{
    __asm volatile("nop");
}

static void i2s_kws_loop_polling(void)
{
    kws_dsp_run_polling();
}

static void kws_mic_switch_gpio_init(void)
{
#if defined(BOARD_AUDIO_CODEC_ES7210)
    gpio_set_function(GPIOA, 31u, FUNCTION_2);
    gpio_set_direction(GPIOA, 31u, 0u);
    gpio_set_mode(GPIOA, 31u, GPIO_DOWN);
    printf("[Main] MIC switch GPIO31=%u\r\n", gpio_get_value(GPIOA, 31u) ? 1u : 0u);
#endif
}

void DMA0_IRQHandler(void)
{
    kws_dsp_dma0_irq_handler();
}

int main(void)
{
    int ret;

    board_init();
    printf("\r\n========================================\r\n");
    printf("  S300 KWS CM4 Demo (Polling Mode)\r\n");
    printf("========================================\r\n");

    kws_mic_switch_gpio_init();

    ret = audio_init();
    if (ret != 0)
    {
        printf("[Main] Audio init failed: %d\r\n", ret);
        while (1)
        {
            __WFI();
        }
    }

    printf("[Main] Initializing DSP PLL...\r\n");
    ret = rcc_init_dsp_pll(6, 800, 0, 2, 2);
    if (ret != 0)
    {
        printf("[Main] DSP PLL init failed: %d\r\n", ret);
        while (1)
        {
            __WFI();
        }
    }

    printf("[Main] Resetting DSP...\r\n");
    rcc_set_dsp_warm_reset(true);
    RCC->CM4_SYS_SOFT_RSTN |= 1u;

    dsp_firmware_load_point();

    printf("[Main] Starting DSP...\r\n");
    rcc_set_dsp_warm_reset(false);

    for (volatile int i = 0; i < 100000; i++)
    {
    }

    printf("[Main] DSP in: 0x%08lX, KWS: 0x%08lX\r\n",
           (unsigned long)kws_dsp_get_audio_in_addr(),
           (unsigned long)kws_dsp_get_cmd_addr());

    i2s_kws_loop_polling();
    return 0;
}
