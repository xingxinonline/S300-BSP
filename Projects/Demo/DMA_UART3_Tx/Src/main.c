#include "s300.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "rcc.h"
#include "dma.h"
#include "uart.h"

/* 发送缓冲区：填充 'Z' 共 1024 字节，通过 DMA 从内存搬到 UART3 THR 寄存器 */
static uint8_t tx_buffer[1024];

int main(void)
{
    board_debug_uart_init(); /* 注意：UART3 也用于 printf 重定向 */
    printf("[DMA_UART3_Tx] start (UART3 is printf too)\n");
    /* 1) 打开 DMA0 时钟；2) 打开 UART3 时钟并初始化引脚与波特率 */
    rcc_set_cortex_m4_sys_clock(1, 0, 1, true); /* AON=1, DMA1=0, DMA0=1, enable */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    /* board_debug_uart_init 已经设置 UART3 引脚与波特率，这里仅确保 FIFO 进入 DMA 模式 */
    uart_set_fifo(UART_IDX3, (uart_fifo_t)(UART_FIFO_RX_1B | UART_FIFO_TX_1_2 | UART_FIFO_DMAMODE | UART_FIFO_TX_RESET | UART_FIFO_RX_RESET | UART_FIFO_EN));
    /* 准备源数据 */
    memset(tx_buffer, 'Z', sizeof(tx_buffer));
    /* 初始化 DMA 控制器，配置通道0：内存->外设，源递增，目的保持，8bit 宽度，握手选择 UART3_TX */
    if (dma_init(DMA_IDX0) != 0)
    {
        printf("dma_init failed\n");
        for (;;) __WFI();
    }
    dma_set_std(DMA_IDX0, 0, (uint32_t)tx_buffer, (uint32_t)&UART3->RBR_THR_DLL, sizeof(tx_buffer), DMA_WIDTH_8);
    /* 对于 UART，突发长度设为1，避免一次握手搬运多个beat */
    dma_set_burst(DMA_IDX0, 0, DMA_MSIZE_1, DMA_MSIZE_1);
    dma_set_increment(DMA_IDX0, 0, DMA_ADDR_INC, DMA_ADDR_KEEP);
    dma_set_transfer_type(DMA_IDX0, 0, DMA_TR_TYPE_M2P_FD);
    /* 绑定握手：UART3_TX 作为目的端握手；源为内存无需握手编号 */
    dma_set_handshaking(DMA_IDX0, 0, 0xF, /* none */ 7 /* 假定 UART3_TX 在 DMA 手册映射为 7，如与芯片手册不符请调整 */);
    /* 启动传输并等待完成；期间 printf 依然可用，但为避免互相抢占，尽量减少打印 */
    dma_start(DMA_IDX0, 0);
    while (dma_is_busy(DMA_IDX0, 0)) { /* busy wait */ }
    printf("DMA UART3 Tx done, sent %u bytes of 'Z'\n", (unsigned)sizeof(tx_buffer));
    for (;;)
    {
        __WFI();
    }
}
