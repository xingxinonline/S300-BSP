#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "board.h"
#include "uart.h"
#include "uart_s300.h"
#include "uart_cmd.h"

#ifndef UART_DEBUG_IDX
#define UART_DEBUG_IDX BOARD_UART_DEBUG_IDX
#endif

static inline S300_UART_TypeDef * dbg_uart_dev(void)
{
    switch (UART_DEBUG_IDX)
    {
    case 0: return UART0;
    case 1: return UART1;
    case 2: return UART2;
    default: return UART3;
    }
}

void uart_cmd_poll(void)
{
    S300_UART_TypeDef *U = dbg_uart_dev();
    static char     s_buf[64];
    static uint8_t  s_len = 0;
    while (U->USR & 0x8u)
    {
        uint8_t ch = (uint8_t)U->RBR_THR_DLL;
        write_uart(UART_DEBUG_IDX, UARTTYPE_STD_SERIAL, ch);
        if (ch == '\r' || ch == '\n')
        {
            if (s_len > 0)
            {
                s_buf[s_len] = '\0';
                /* Command parsing removed */
                s_len = 0;
            }
            if (ch == '\r') write_uart(UART_DEBUG_IDX, UARTTYPE_STD_SERIAL, '\n');
        }
        else if (ch == 0x08 || ch == 0x7F)
        {
            if (s_len > 0) s_len--;
        }
        else if ((size_t)s_len + 1U < sizeof(s_buf))
        {
            s_buf[s_len++] = (char)ch;
        }
        else { s_len = 0; }
    }
}
