#include "s300.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "rcc.h"
#include "dma.h"

static uint8_t src_buffer[1024];
static uint8_t dst_buffer[1024];


int main(void)
{
    /* Board init: clock + debug UART */
    board_init();
    printf("[DMA_Ram2Ram] start\r\n");
    /* 打开DMA0时钟（以及必要的APB分频） */
    rcc_set_cortex_m4_sys_clock(1, 0, 1, true); /* AON=1, DMA1=0, DMA0=1, enable */
    memset(src_buffer, 0x5A, sizeof(src_buffer));
    memset(dst_buffer, 0x00, sizeof(dst_buffer));
    if (dma_init(DMA_IDX0) != 0)
    {
        printf("dma_init failed\n");
        for (;;) __WFI();
    }
    /* 配置一次标准内存到内存复制，16bit 宽度 */
    set_dma_std(EM_DMA0, 0, (uint32_t)src_buffer, (uint32_t)dst_buffer, sizeof(src_buffer), EM_TR_WIDTH_16_BIT);
    set_dma_start(EM_DMA0, 0);
    while (is_dma_busy(EM_DMA0, 0)) { /* busy wait */ }
    int diff = memcmp(src_buffer, dst_buffer, sizeof(src_buffer));
    if (diff == 0)
        printf("done, memcmp=0 (OK), dst[1023]=0x%02X\n", dst_buffer[1023]);
    else
    {
        size_t i;
        for (i = 0; i < sizeof(src_buffer); ++i) if (src_buffer[i] != dst_buffer[i]) break;
        printf("done, memcmp!=0 (FAIL), first diff at %u: src=0x%02X dst=0x%02X\n", (unsigned)i, src_buffer[i], dst_buffer[i]);
    }
    for (;;)
    {
        __WFI();
    }
}
