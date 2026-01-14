#include "s300.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "rcc.h"
#include "dma.h"

static uint8_t src_buffer[256];
static uint8_t dst_buffer[256];
static volatile int dma_done = 0;

void DMA0_IRQHandler(void)
{
    /* 简单清中断：读取状态并写回 ClearTfr 清除，mask 不在此修改 */
    S300_DMA_TypeDef *D = DMAC0;
    uint32_t status = D->StatusTfr;
    (void)status;
    D->ClearTfr = 0xFFu;
    dma_done = 1;
}

int main(void)
{
    board_debug_uart_init();
    printf("[DMA_Interrupt] start\n");
    rcc_set_cortex_m4_sys_clock(1, 0, 1, true);
    memset(src_buffer, 0xA5, sizeof(src_buffer));
    memset(dst_buffer, 0x00, sizeof(dst_buffer));
    init_dma(EM_DMA0);
    /* 配置一次 M2M 传输，32bit 宽度，打开TFR中断 */
    set_dma_std(EM_DMA0, 1, (uint32_t)src_buffer, (uint32_t)dst_buffer, sizeof(src_buffer), EM_TR_WIDTH_32_BIT);
    set_dma_interrupt(EM_DMA0, 1, EM_DMA_INT_TFR, 1);
    NVIC_ClearPendingIRQ(DMA0_IRQn);
    NVIC_SetPriority(DMA0_IRQn, 5);
    NVIC_EnableIRQ(DMA0_IRQn);
    set_dma_start(EM_DMA0, 1);
    while (!dma_done)
    {
        __WFI();
    }
    printf("IRQ done, dst[255]=0x%02X\n", dst_buffer[255]);
    for (;;)
    {
        __WFI();
    }
}
