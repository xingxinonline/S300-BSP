#include <stdint.h>

#include "board.h"
#include "s300.h"
#include "subboard_mm_app.h"

static volatile uint32_t g_tick_ms = 0u;

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

    if (subboard_mm_app_init(millis) != 0) {
        while (1) {
        }
    }

    while (1) {
        subboard_mm_app_tick();
    }
}
