/**
 * @file main.c
 * @brief Hello World 示例应用
 *
 * 这是 S300 BSP 的最小示例程序，演示基本的系统初始化和串口输出。
 * 适合作为新项目的起点模板。
 */

#include <stdio.h>
#include <stdint.h>
#include "s300.h"
#include "board.h"

/* SysTick 计数器 */
static volatile uint32_t g_tick_ms = 0;

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t millis(void)
{
    return g_tick_ms;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms);
}

int main(void)
{
    /* 板级初始化 (时钟、GPIO、UART 等) */
    board_init();

    /* 配置 SysTick 为 1ms 中断 */
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        /* SysTick 配置失败 */
        while (1);
    }

    printf("\r\n");
    printf("========================================\r\n");
    printf("  S300 Hello World Demo\r\n");
    printf("========================================\r\n");
    printf("System Clock: %lu Hz\r\n", SystemCoreClock);
    printf("\r\n");

    uint32_t count = 0;
    uint32_t last_print_ms = millis();

    while (1) {
        uint32_t now = millis();
        if ((now - last_print_ms) >= 1000U) {
            printf("[%8lu ms] Hello, World! Count: %lu\r\n", now, count++);
            last_print_ms = now;
        }
    }

    return 0;
}
