#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "board.h"
#include "s300.h"
#include "s300_memmap.h"
#include "uart.h"
#include "uart_s300.h"
#include "mailbox.h"

#ifndef UART_DEBUG_IDX
#define UART_DEBUG_IDX 3u
#endif

static inline int uart_rx_ready(void)
{
    /* USR bit3 (RX FIFO not empty) */
    S300_UART_TypeDef *U = UART3; /* BOARD 默认使用 UART3 */
    return (U->USR & 0x8u) ? 1 : 0;
}

static int readline(char *buf, size_t maxlen)
{
    size_t idx = 0;
    while (idx + 1 < maxlen)
    {
        uint16_t ch = read_uart(UART_DEBUG_IDX, UARTTYPE_STD_SERIAL);
        if (ch == '\r') continue;
        if (ch == '\n') break;
        buf[idx++] = (char)ch;
    }
    buf[idx] = '\0';
    return (int)idx;
}

static void print_help(void)
{
    printf("\nMailbox test commands:\n");
    printf("  h                   : help\n");
    printf("  i [thresh] [mask]   : init mailbox (default thresh=4 mask=3)\n");
    printf("  w <hex32>           : send one word to DSP (write MAILBOX_BASE)\n");
    printf("  g                   : get one word if available (from DSP_MAILBOX_BASE)\n");
    printf("  e <hex32>           : m4<->dsp echo one word (send & wait back)\n");
    printf("  b <n>               : burst send 0..n-1 (and try echo back)\n");
    printf("\nNotes: RX monitor prints whenever DSP->M4 data arrives.\n\n");
}

static void monitor_mailbox_rx(void)
{
    // /* 若 DSP->M4 有数据（即 DSP_MAILBOX_BASE 非空），读出并打印 */
    // while (mailbox_sta_empty_flag_is(DSP_MAILBOX_BASE, 0) == 0)
    // {
    //     uint32_t v = read_mailbox(DSP_MAILBOX_BASE);
    //     printf("RX[DSP]: 0x%08lx\n", (unsigned long)v);
    // }
}

int main(void)
{
    board_init();
    printf("\n=== S300 Mailbox Test ===\n");

    /* 默认初始化：阈值=4，中断使能=SIT|RIT（可按需调整/不使用中断） */
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_SIT_EN | MAILBOX_IRQ_RIT_EN);
    // init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_SIT_EN | MAILBOX_IRQ_RIT_EN);

    print_help();

    char line[64];
    while (1)
    {
        /* 先检查 Mailbox 是否有接收数据 */
        monitor_mailbox_rx();

        /* 读取一行命令（阻塞），再执行 */
        printf("> ");
        int n = readline(line, sizeof(line));
        if (n <= 0) continue;

        /* 解析命令 */
        if (line[0] == 'h')
        {
            print_help();
        }
        else if (line[0] == 'i')
        {
            unsigned thresh = 4, mask = 3;
            (void)sscanf(line, "i %u %u", &thresh, &mask);
            init_mailbox(MAILBOX_BASE, (uint8_t)(thresh & 0xFF), (uint8_t)(mask & 0x7));
            init_mailbox(DSP_MAILBOX_BASE, (uint8_t)(thresh & 0xFF), (uint8_t)(mask & 0x7));
            printf("init: thresh=%u, mask=0x%x\n", thresh, mask);
        }
        else if (line[0] == 'w')
        {
            unsigned long val = 0;
            if (sscanf(line, "w %lx", &val) == 1)
            {
                if (write_mailbox(MAILBOX_BASE, (uint32_t)val) == 0)
                    printf("TX[M4->DSP]: 0x%08lx\n", val);
                else
                    printf("TX timeout\n");
            }
            else
            {
                printf("usage: w <hex32>\n");
            }
        }
        else if (line[0] == 'g')
        {
            if (mailbox_sta_empty_flag_is(DSP_MAILBOX_BASE, 0) == 0)
            {
                uint32_t v = read_mailbox(DSP_MAILBOX_BASE);
                printf("RX[DSP]: 0x%08lx\n", (unsigned long)v);
            }
            else
            {
                printf("no data\n");
            }
        }
        else if (line[0] == 'e')
        {
            unsigned long val = 0;
            if (sscanf(line, "e %lx", &val) == 1)
            {
                int rc = m4_dsp_one_data((uint32_t)val);
                printf("echo one rc=%d\n", rc);
            }
            else
            {
                printf("usage: e <hex32>\n");
            }
        }
        else if (line[0] == 'b')
        {
            unsigned nsend = 0;
            if (sscanf(line, "b %u", &nsend) == 1 && nsend > 0)
            {
                if (nsend > 64) nsend = 64; /* 防止阻塞太久 */
                /* 生成序列 0..nsend-1 并发送，然后尝试回读 */
                for (unsigned i = 0; i < nsend; ++i)
                {
                    if (write_mailbox(MAILBOX_BASE, i) != 0)
                    {
                        printf("TX timeout at %u\n", i);
                        break;
                    }
                }
                /* 尝试回读（若 DSP 回写） */
                for (unsigned i = 0; i < nsend; ++i)
                {
                    if (mailbox_sta_empty_flag_is(DSP_MAILBOX_BASE, 0) == 0)
                    {
                        uint32_t v = read_mailbox(DSP_MAILBOX_BASE);
                        printf("RX[DSP]: 0x%08lx\n", (unsigned long)v);
                    }
                }
                printf("burst done (%u)\n", nsend);
            }
            else
            {
                printf("usage: b <count>\n");
            }
        }
        else
        {
            printf("unknown cmd: %s\n", line);
            print_help();
        }
    }
}
