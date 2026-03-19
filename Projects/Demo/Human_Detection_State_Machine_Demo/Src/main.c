#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "human_detection_app.h"
#include "perf.h"
#include "psram.h"
#include "rcc.h"
#include "s300.h"

static volatile uint32_t g_tick_ms = 0;
volatile uint32_t g_cpu_total_ticks = 0;
volatile uint32_t g_cpu_idle_ticks = 0;
volatile uint8_t g_cpu_in_idle = 0;

void SysTick_Handler(void)
{
    g_tick_ms++;
    g_cpu_total_ticks++;
    if (g_cpu_in_idle) {
        g_cpu_idle_ticks++;
    }
}

static uint32_t millis(void)
{
    return g_tick_ms;
}

__attribute__((used, noinline)) void hd_state_machine_dsp_image_load_point(void)
{
    __asm volatile("nop");
}

int main(void)
{
    board_init();
    printf("\r\n[S300][HD-SM] Booting...\r\n");

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        printf("[S300][HD-SM][ERR] SysTick_Config failed\r\n");
        while (1) {
            __NOP();
        }
    }

    rcc_init_mm_pll(8, 400, 0, 3, 2);
    rcc_init_dsp_pll(6, 800, 0, 2, 2);

    init_psram(4, 1);
    hd_state_machine_dsp_image_load_point();
    human_detection_app_init(millis);

    while (1) {
        human_detection_app_tick();
        g_cpu_in_idle = 1;
        {
            uint32_t start_ms = millis();
            while ((uint32_t)(millis() - start_ms) < 2u) {
            }
        }
        g_cpu_in_idle = 0;
    }
}