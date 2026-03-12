#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include "uart.h"
#include "board.h"

// 默认使用 board.h 中定义的调试串口索引
#ifndef BOARD_UART_DEBUG_IDX
    #define BOARD_UART_DEBUG_IDX 0
#endif

// 默认如果 board.h 没定义开关，认为是开启的
#ifndef BOARD_DEBUG_UART_ENABLE
    #ifdef BOARD_UART3_DEBUG_ENABLE
        #define BOARD_DEBUG_UART_ENABLE BOARD_UART3_DEBUG_ENABLE
    #else
        #define BOARD_DEBUG_UART_ENABLE 1
    #endif
#endif

// 定义为弱函数，允许特定板子覆盖此函数（例如输出到 LCD 或 ITM）
__attribute__((weak)) int _write(int fd, char *pBuffer, int size)
{
    (void)fd;

#if BOARD_DEBUG_UART_ENABLE
    for (int i = 0; i < size; i++)
    {
        write_uart(BOARD_UART_DEBUG_IDX, UARTTYPE_STD_SERIAL, (uint8_t)pBuffer[i]);
    }
#else
    (void)pBuffer;
    (void)size;
#endif

    return size;
}
