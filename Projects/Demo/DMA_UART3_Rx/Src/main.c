#include "s300.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "rcc.h"
#include "dma.h"
#include "uart.h"

static uint8_t rx_buffer[32];

int main(void)
{
    board_debug_uart_init(); /* UART3 用于 printf 重定向 */
    printf("[DMA_UART3_Rx] start (UART3 is printf too)\n");
    rcc_set_cortex_m4_sys_clock(1, 0, 1, true); /* 打开 DMA0 */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    /* 将 UART3 置为 DMA 模式，RX 触发阈值 1 字节，开启 FIFO */
    uart_set_fifo(UART_IDX3, (uart_fifo_t)(UART_FIFO_RX_1B | UART_FIFO_TX_1_2 | UART_FIFO_DMAMODE | UART_FIFO_TX_RESET | UART_FIFO_RX_RESET | UART_FIFO_EN));
    memset(rx_buffer, 0, sizeof(rx_buffer));
    if (dma_init(DMA_IDX0) != 0)
    {
        printf("dma_init failed\n");
        for (;;) __WFI();
    }
    /* 配置通道0：外设(UART3 RBR) -> 内存，源保持，目的递增，8bit 宽度 */
    dma_set_std(DMA_IDX0, 0, (uint32_t)&UART3->RBR_THR_DLL, (uint32_t)rx_buffer, 16, DMA_WIDTH_8);
    /* UART 接收建议突发=1，避免一次握手读取多字节导致读到0 */
    dma_set_burst(DMA_IDX0, 0, DMA_MSIZE_1, DMA_MSIZE_1);
    dma_set_increment(DMA_IDX0, 0, DMA_ADDR_KEEP, DMA_ADDR_INC);
    dma_set_transfer_type(DMA_IDX0, 0, DMA_TR_TYPE_P2M_FD);
    /* 绑定握手：源为 UART3_RX；目的为内存无需编号 */
    dma_set_handshaking(DMA_IDX0, 0, 6 /* 假定 UART3_RX 为 6，需与芯片手册映射一致 */, 0xF);
    printf("Please type 16 bytes on UART3...\n");
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
