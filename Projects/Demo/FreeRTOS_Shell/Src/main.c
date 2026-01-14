#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "board.h"
#include "uart.h"
#include "rcc.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#ifndef UART_DEBUG_IDX
#define UART_DEBUG_IDX BOARD_UART_DEBUG_IDX
#endif

static void uart_putc(char c)
{
    write_uart(UART_DEBUG_IDX, UARTTYPE_STD_SERIAL, (uint8_t)c);
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uart_puts(buf);
}

static int uart_getc_blocking(void)
{
    return (int)(uart_read(UART_DEBUG_IDX, UARTTYPE_STD_SERIAL) & 0xFFu);
}

static void shell_task(void *arg)
{
    (void)arg;
    char line[128];
    for(;;)
    {
        uart_puts("\r\n> ");
        size_t len = 0;
        memset(line, 0, sizeof(line));
        for(;;)
        {
            int ch = uart_getc_blocking();
            if (ch == '\r' || ch == '\n')
            {
                uart_puts("\r\n");
                break;
            }
            else if ((ch == 0x7F || ch == '\b'))
            {
                if (len > 0) { len--; uart_puts("\b \b"); }
                continue;
            }
            else if (len < sizeof(line) - 1)
            {
                line[len++] = (char)ch;
                uart_putc((char)ch);
            }
        }
        /* Parse */
        if (len == 0) continue;
        if (strcmp(line, "help") == 0)
        {
            uart_puts("Commands:\r\n");
            uart_puts("  help       - Show this help\r\n");
            uart_puts("  tasks      - List tasks\r\n");
            uart_puts("  tick       - Show tick count\r\n");
            uart_puts("  reboot     - Software reset\r\n");
        }
        else if (strcmp(line, "tick") == 0)
        {
            uart_printf("tick=%lu\r\n", (unsigned long)xTaskGetTickCount());
        }
        else if (strcmp(line, "reboot") == 0)
        {
            uart_puts("Rebooting...\r\n");
            NVIC_SystemReset();
        }
        else if (strcmp(line, "tasks") == 0)
        {
#if (configUSE_TRACE_FACILITY == 1) && (configUSE_STATS_FORMATTING_FUNCTIONS == 1)
            /* vTaskList needs a large enough buffer */
            char buf[512];
            vTaskList(buf);
            uart_puts("Name          State Prio Stack Num\r\n");
            uart_puts("-----------------------------------\r\n");
            uart_puts(buf);
#else
            uart_puts("Task list not enabled. Enable configUSE_TRACE_FACILITY and configUSE_STATS_FORMATTING_FUNCTIONS.\r\n");
#endif
        }
        else
        {
            uart_puts("Unknown command. Type 'help'.\r\n");
        }
    }
}

static void blinky_task(void *arg)
{
    (void)arg;
    for(;;)
    {
        uart_puts("blink\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* FreeRTOS hooks */
void vAssertCalled(const char* file, int line)
{
    (void)file; (void)line;
    uart_printf("assert: %s:%d\r\n", file, line);
    taskDISABLE_INTERRUPTS();
    for(;;){}
}

void vApplicationMallocFailedHook(void)
{
    uart_puts("Malloc failed!\r\n");
    taskDISABLE_INTERRUPTS();
    for(;;){}
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    uart_printf("Stack overflow: %s\r\n", pcTaskName);
    taskDISABLE_INTERRUPTS();
    for(;;){}
}

int main(void)
{
    board_init();
    uart_puts("\r\nS300 FreeRTOS + Shell demo\r\n");

    /* Print system clock info once at startup */
    SystemCoreClockUpdate();
    uint32_t sys_hz = rcc_get_clock(RCC_CLOCK_SYSTEM);
    uint32_t apb0_hz = rcc_get_clock(RCC_CLOCK_APB0);
    uint32_t apb1_hz = rcc_get_clock(RCC_CLOCK_APB1);
    uart_printf("Clock: SYS=%lu Hz, APB0=%lu Hz, APB1=%lu Hz\r\n",
                (unsigned long)sys_hz,
                (unsigned long)apb0_hz,
                (unsigned long)apb1_hz);

    xTaskCreate(shell_task, "shell", 512, NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(blinky_task, "blink", 256, NULL, tskIDLE_PRIORITY + 1, NULL);

    vTaskStartScheduler();

    for(;;){}
}
