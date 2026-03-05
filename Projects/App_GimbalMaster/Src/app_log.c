#include "app_log.h"

#include <stdio.h>
#include <stdarg.h>

#include "board.h"
#include "uart.h"
#include "s300.h"
#include "FreeRTOS.h"
#include "task.h"

#ifndef APP_LOG_BUFFER_SIZE
#define APP_LOG_BUFFER_SIZE 192U
#endif

static uint8_t g_app_log_uart_idx = BOARD_UART_DEBUG_IDX;
static char g_app_log_printf_buf[APP_LOG_BUFFER_SIZE];

typedef struct {
    UBaseType_t saved_mask;
    uint8_t from_isr;
} app_log_lock_ctx_t;

static app_log_lock_ctx_t app_log_lock(void)
{
    app_log_lock_ctx_t ctx;

    ctx.from_isr = (__get_IPSR() != 0u) ? 1u : 0u;
    if (ctx.from_isr != 0u) {
        ctx.saved_mask = taskENTER_CRITICAL_FROM_ISR();
    } else {
        taskENTER_CRITICAL();
        ctx.saved_mask = 0u;
    }

    return ctx;
}

static void app_log_unlock(app_log_lock_ctx_t ctx)
{
    if (ctx.from_isr != 0u) {
        taskEXIT_CRITICAL_FROM_ISR(ctx.saved_mask);
    } else {
        taskEXIT_CRITICAL();
    }
}

void app_log_init(uint8_t uart_idx)
{
    g_app_log_uart_idx = uart_idx;
}

void app_log_putc(char c)
{
    app_log_lock_ctx_t ctx = app_log_lock();
    write_uart(g_app_log_uart_idx, UARTTYPE_STD_SERIAL, (uint8_t)c);
    app_log_unlock(ctx);
}

void app_log_puts(const char *s)
{
    app_log_lock_ctx_t ctx;

    if (s == NULL) {
        return;
    }

    ctx = app_log_lock();
    while (*s) {
        write_uart(g_app_log_uart_idx, UARTTYPE_STD_SERIAL, (uint8_t)(*s));
        s++;
    }
    app_log_unlock(ctx);
}

void app_log_printf(const char *fmt, ...)
{
    va_list ap;
    app_log_lock_ctx_t ctx;
    const char *s;

    if (fmt == NULL) {
        return;
    }

    ctx = app_log_lock();
    va_start(ap, fmt);
    vsnprintf(g_app_log_printf_buf, sizeof(g_app_log_printf_buf), fmt, ap);
    va_end(ap);

    s = g_app_log_printf_buf;
    while (*s != '\0') {
        write_uart(g_app_log_uart_idx, UARTTYPE_STD_SERIAL, (uint8_t)(*s));
        s++;
    }

    app_log_unlock(ctx);
}
