#include "app_log.h"

#include <stdio.h>
#include <stdarg.h>

#include "board.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"

#ifndef APP_LOG_BUFFER_SIZE
#define APP_LOG_BUFFER_SIZE 192U
#endif

static uint8_t g_app_log_uart_idx = BOARD_UART_DEBUG_IDX;

static void app_log_lock(void)
{
    taskENTER_CRITICAL();
}

static void app_log_unlock(void)
{
    taskEXIT_CRITICAL();
}

void app_log_init(uint8_t uart_idx)
{
    g_app_log_uart_idx = uart_idx;
}

void app_log_putc(char c)
{
    app_log_lock();
    write_uart(g_app_log_uart_idx, UARTTYPE_STD_SERIAL, (uint8_t)c);
    app_log_unlock();
}

void app_log_puts(const char *s)
{
    if (s == NULL) {
        return;
    }

    app_log_lock();
    while (*s) {
        write_uart(g_app_log_uart_idx, UARTTYPE_STD_SERIAL, (uint8_t)(*s));
        s++;
    }
    app_log_unlock();
}

void app_log_printf(const char *fmt, ...)
{
    char buf[APP_LOG_BUFFER_SIZE];
    va_list ap;

    if (fmt == NULL) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    app_log_puts(buf);
}
