#include "s300.h"
#include <stdio.h>
#include <stdbool.h>
#include "uart.h"
#include "board.h"

/* Use the debug UART configured in board.h */
#ifndef UART_ECHO_IDX
    #define UART_ECHO_IDX BOARD_DEBUG_UART_IDX
#endif

int main(void)
{
    /* Board init: clock + debug UART (already configured in board_init) */
    board_init();
    printf("[UART3_Echo] started\r\n");

    /* simple echo loop */
    for (;;)
    {
        uint16_t d = read_uart(UART_ECHO_IDX, UARTTYPE_STD_SERIAL);
        write_uart(UART_ECHO_IDX, UARTTYPE_STD_SERIAL, d);
    }
}
