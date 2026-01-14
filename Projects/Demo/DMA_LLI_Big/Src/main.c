#include "s300.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "rcc.h"
#include "dma.h"

/*
 * Demo: Large memory-to-memory transfer using DMA Linked List (LLI)
 * Example: RGB565 frame 320x240 => 320 * 240 * 2 = 153600 bytes
 * Goal: exceed single-block limit (BLOCK_TS) by chaining multiple LLI entries.
 */

#define PIX_W  320u
#define PIX_H  240u
#define BPP    2u /* RGB565 */
#define TOTAL_BYTES   (PIX_W * PIX_H * BPP) /* 153600 bytes */

/* Choose transfer width:
 * - For memory-to-memory, 32-bit is faster (fewer transfers, better throughput) if addresses/length are 4-byte aligned.
 * - For memory-to-peripheral to a 16-bit FIFO, set to EM_TR_WIDTH_16_BIT and configure handshake + DINC=INC, SINC=INC(or KEEP if needed).
 */
#define WIDTH         EM_TR_WIDTH_32_BIT /* change to EM_TR_WIDTH_16_BIT for 16-bit peripheral */

/* Compute bytes per transfer based on WIDTH */
#define TR_BYTES      ((WIDTH)==EM_TR_WIDTH_32_BIT ? 4u : ((WIDTH)==EM_TR_WIDTH_16_BIT ? 2u : 1u))
/* Max bytes per LLI block under BLOCK_TS(max=4095) */
#define BYTES_PER_BLOCK (4095u * TR_BYTES)

static uint8_t src_buffer[TOTAL_BYTES];
static uint8_t dst_buffer[TOTAL_BYTES];

/* Reserve enough LLI items: ceil(TOTAL_BYTES / BYTES_PER_BLOCK) */
#define MAX_LLIS (((TOTAL_BYTES) + BYTES_PER_BLOCK - 1u) / BYTES_PER_BLOCK)
static dma_lli_t llis[MAX_LLIS] __attribute__((aligned(8)));

static void fill_pattern(uint8_t *buf, size_t n)
{
    /* Simple repeating pattern */
    for (size_t i = 0; i < n; ++i) buf[i] = (uint8_t)(0xA5 ^ (i & 0xFF));
}

int main(void)
{
    board_debug_uart_init();
    printf("[DMA_LLI_Big] start RGB565 %ux%u, TOTAL=%u bytes, block=%u bytes, tr=%u bytes\n",
        (unsigned)PIX_W, (unsigned)PIX_H, (unsigned)TOTAL_BYTES, (unsigned)BYTES_PER_BLOCK, (unsigned)TR_BYTES);

    /* Enable DMA0 clock (and required APB dividers) */
    rcc_set_cortex_m4_sys_clock(0, 0, 1, true); /* AON=0, DMA1=0, DMA0=1, enable */

    /* Prepare buffers */
    fill_pattern(src_buffer, sizeof(src_buffer));
    memset(dst_buffer, 0, sizeof(dst_buffer));

    if (dma_init(DMA_IDX0) != 0)
    {
        printf("dma_init failed\n");
        for (;;) __WFI();
    }

    /* Baseline config to program CFG registers etc. The CTL/SAR/DAR will be overridden by LLI. */
    set_dma_std(EM_DMA0, 0,
                (uint32_t)src_buffer,
                (uint32_t)dst_buffer,
                16, /* dummy small length */
                WIDTH);
    set_dma_std_increment(EM_DMA0, 0, EM_ADDRESS_INC, EM_ADDRESS_INC);
    set_dma_std_transfer_bitwidth(EM_DMA0, 0, WIDTH, WIDTH);

    /* Build LLI chain */
    uint32_t remaining = TOTAL_BYTES;
    uint32_t offset = 0;
    uint32_t items = 0;
    while (remaining > 0)
    {
        uint32_t blk = (remaining > BYTES_PER_BLOCK) ? BYTES_PER_BLOCK : remaining;
        /* Ensure length is multiple of transfer width (4) */
        blk &= ~((uint32_t)3u);
        if (blk == 0) break;

    dma_set_link_unit(&llis[items],
                          (uint32_t)(uintptr_t)(src_buffer + offset),
                          (uint32_t)(uintptr_t)(dst_buffer + offset),
                          blk,
                          (dma_width_t)WIDTH);
        /* Link will be set below after we know total count */
        llis[items].LLP = 0u;
    /* Ensure LLP continues after this block by keeping LLP_EN in the CTL loaded from LLI */
    llis[items].CTL_L |= (1u << DMA_CTL_LLP_DST_EN_Pos) | (1u << DMA_CTL_LLP_SRC_EN_Pos);

        offset += blk;
        remaining -= blk;
        ++items;
    }

    /* Chain LLIs */
    for (uint32_t i = 0; i + 1 < items; ++i)
    {
        llis[i].LLP = (uint32_t)(uintptr_t)&llis[i + 1];
    }

    /* Program channel to use LLI chain */
    if (set_dma_link(EM_DMA0, 0, &llis[0]) != 0)
    {
        printf("set_dma_link failed\n");
        for (;;) __WFI();
    }

    /* Start and wait */
    set_dma_start(EM_DMA0, 0);
    while (is_dma_busy(EM_DMA0, 0)) { /* busy wait */ }

    int diff = memcmp(src_buffer, dst_buffer, sizeof(src_buffer));
    if (diff == 0)
        printf("LLI transfer done, memcmp=0 (OK), dst[last]=0x%02X, LLI count=%u\n",
               dst_buffer[TOTAL_BYTES - 1], (unsigned)items);
    else
    {
        size_t i;
        for (i = 0; i < sizeof(src_buffer); ++i) if (src_buffer[i] != dst_buffer[i]) break;
        printf("LLI transfer done, memcmp!=0 (FAIL), first diff at %u: src=0x%02X dst=0x%02X, LLI count=%u\n",
            (unsigned)i, src_buffer[i], dst_buffer[i], (unsigned)items);
    }

    for (;;)
    {
        __WFI();
    }
}
