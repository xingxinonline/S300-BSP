#include "s300.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "rcc.h"
#include "dma.h"
#include "uart.h"

/* 发送缓冲区：填充 'Z' 共 1024 字节，通过 DMA 从内存搬到 UART3 THR 寄存器 */
static uint8_t tx_buffer[1024];

static uart_idx_t demo_uart_idx(void)
{
    return (uart_idx_t)BOARD_DEBUG_UART_IDX;
}

static volatile uint32_t *demo_uart_thr(void)
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

static uint16_t demo_uart_tx_handshake(void)
{
    switch (demo_uart_idx())
    {
    case UART_IDX0:
        return EM_HAND_UART0_TX;
    case UART_IDX1:
        return EM_HAND_UART1_TX;
    case UART_IDX2:
        return EM_HAND_UART2_TX;
    case UART_IDX3:
    default:
        return EM_HAND_UART3_TX;
    }
}

int main(void)
{
    board_init();
    printf("[DMA_UART_Tx] start on UART%u\n", (unsigned)demo_uart_idx());

    rcc_set_cortex_m4_sys_clock(1, 0, 1, true);
    uart_set_fifo(demo_uart_idx(), (uart_fifo_t)(UART_FIFO_RX_1B | UART_FIFO_TX_1_2 | UART_FIFO_DMAMODE | UART_FIFO_TX_RESET | UART_FIFO_RX_RESET | UART_FIFO_EN));

    memset(tx_buffer, 'Z', sizeof(tx_buffer));
    if (dma_init(DMA_IDX0) != 0)
    {
        printf("dma_init failed\n");
        for (;;) __WFI();
    }

    dma_set_std(DMA_IDX0, 0, (uint32_t)tx_buffer, (uint32_t)demo_uart_thr(), sizeof(tx_buffer), DMA_WIDTH_8);
    dma_set_burst(DMA_IDX0, 0, DMA_MSIZE_1, DMA_MSIZE_1);
    dma_set_increment(DMA_IDX0, 0, DMA_ADDR_INC, DMA_ADDR_KEEP);
    dma_set_transfer_type(DMA_IDX0, 0, DMA_TR_TYPE_M2P_FD);

    dma_set_handshaking(DMA_IDX0, 0, EM_HAND_NULL, demo_uart_tx_handshake());
    dma_start(DMA_IDX0, 0);
    while (dma_is_busy(DMA_IDX0, 0)) { /* busy wait */ }
    printf("DMA UART Tx done, sent %u bytes of 'Z' on UART%u\n", (unsigned)sizeof(tx_buffer), (unsigned)demo_uart_idx());
    for (;;)
    {
        __WFI();
    }
}
