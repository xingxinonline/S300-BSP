#include "s300.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "rcc.h"
#include "dma.h"
#include "uart.h"

static uint8_t rx_buffer[32];

static uart_idx_t demo_uart_idx(void)
{
    return (uart_idx_t)BOARD_DEBUG_UART_IDX;
}

static volatile uint32_t *demo_uart_rbr(void)
{
    switch (demo_uart_idx())
    {
    case UART_IDX0:
        return &UART0->RBR_THR_DLL;
    case UART_IDX1:
        return &UART1->RBR_THR_DLL;
    case UART_IDX2:
        return &UART2->RBR_THR_DLL;
    case UART_IDX3:
    default:
        return &UART3->RBR_THR_DLL;
    }
}

static uint16_t demo_uart_rx_handshake(void)
{
    switch (demo_uart_idx())
    {
    case UART_IDX0:
        return EM_HAND_UART0_RX;
    case UART_IDX1:
        return EM_HAND_UART1_RX;
    case UART_IDX2:
        return EM_HAND_UART2_RX;
    case UART_IDX3:
    default:
        return EM_HAND_UART3_RX;
    }
}

int main(void)
{
    board_init();
    printf("[DMA_UART_Rx] start on UART%u\n", (unsigned)demo_uart_idx());

    rcc_set_cortex_m4_sys_clock(1, 0, 1, true);
    uart_set_fifo(demo_uart_idx(), (uart_fifo_t)(UART_FIFO_RX_1B | UART_FIFO_TX_1_2 | UART_FIFO_DMAMODE | UART_FIFO_TX_RESET | UART_FIFO_RX_RESET | UART_FIFO_EN));
    memset(rx_buffer, 0, sizeof(rx_buffer));
    if (dma_init(DMA_IDX0) != 0)
    {
        printf("dma_init failed\n");
        for (;;) __WFI();
    }

    /* 使用板级调试 UART 做 DMA 接收，避免额外依赖板型专用引脚配置。 */
    dma_set_std(DMA_IDX0, 0, (uint32_t)demo_uart_rbr(), (uint32_t)rx_buffer, 16, DMA_WIDTH_8);
    dma_set_burst(DMA_IDX0, 0, DMA_MSIZE_1, DMA_MSIZE_1);
    dma_set_increment(DMA_IDX0, 0, DMA_ADDR_KEEP, DMA_ADDR_INC);
    dma_set_transfer_type(DMA_IDX0, 0, DMA_TR_TYPE_P2M_FD);

    /* 绑定 DMA 握手到当前调试 UART 的 RX 通道，目的端为内存无需握手。 */
    dma_set_handshaking(DMA_IDX0, 0, demo_uart_rx_handshake(), EM_HAND_NULL);
    printf("Please type 16 bytes on UART%u...\n", (unsigned)demo_uart_idx());

    dma_start(DMA_IDX0, 0);
    while (dma_is_busy(DMA_IDX0, 0)) { /* busy wait */ }
    printf("Received (%d): ", 16);
    for (int i = 0; i < 16; ++i) putchar((char)rx_buffer[i]);
    printf("\n");
    for (;;)
    {
        __WFI();
    }
}
