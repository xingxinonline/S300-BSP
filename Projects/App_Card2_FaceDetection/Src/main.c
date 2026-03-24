#include <stdint.h>

#include "board.h"
#include "psram.h"
#include "rcc.h"
#include "s300.h"
#include "subboard_mm_app.h"

static volatile uint32_t g_tick_ms = 0u;

__attribute__((used, noinline)) void card2_dsp_image_load_point(void)
{
    __asm volatile("" ::: "memory");
}

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
    board_init();

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        while (1) {
        }
    }

    if (rcc_init_mm_pll(8, 400, 0, 3, 2) != RCC_STATUS_OK) {
        while (1) {
        }
    }

    if (rcc_init_dsp_pll(6, 800, 0, 2, 2) != RCC_STATUS_OK) {
        while (1) {
        }
    }

    if (init_psram(4, 1) != 0) {
        while (1) {
        }
    }
    card2_dsp_image_load_point();

    if (subboard_mm_app_init(millis) != 0) {
        while (1) {
        }
    }

    while (1) {
        subboard_mm_app_tick();
    }
}