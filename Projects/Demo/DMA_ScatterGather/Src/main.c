#include "s300.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "rcc.h"
#include "dma.h"

#define WORDS 128
static uint32_t src32[WORDS];
static uint32_t dst32[WORDS];

static void fill_seq32(uint32_t *buf, size_t n, uint32_t base)
{
    for (size_t i = 0; i < n; ++i) buf[i] = base + (uint32_t)i;
}

static void fill_zero32(uint32_t *buf, size_t n)
{
    memset(buf, 0, n * sizeof(uint32_t));
}

static int check_scatter_pattern(const uint32_t *dst, size_t total_tr, uint32_t group, uint32_t gap)
{
    /* Expect: groups of 'group' filled words, then 'gap' zeros, repeating */
    uint32_t val = 0x1000;
    size_t idx = 0;
    while (idx < total_tr)
    {
        /* filled region */
        for (uint32_t i = 0; i < group && idx < total_tr; ++i, ++idx)
        {
            if (dst[idx] != val) return -1;
            ++val;
        }
        /* gap region */
        for (uint32_t i = 0; i < gap && idx < total_tr; ++i, ++idx)
        {
            if (dst[idx] != 0) return -2;
        }
    }
    return 0;
}

static int test_dest_scatter(void)
{
    printf("[Test] Destination Scatter only\n");
    fill_seq32(src32, WORDS, 0x1000);
    fill_zero32(dst32, WORDS);

    if (dma_init(DMA_IDX0) != 0) { printf("dma_init failed\n"); return -10; }
    /* 基本配置：M2M，32bit，递增 */
    set_dma_std(EM_DMA0, 0, (uint32_t)src32, (uint32_t)dst32, 64 * sizeof(uint32_t), EM_TR_WIDTH_32_BIT);
    set_dma_std_increment(EM_DMA0, 0, EM_ADDRESS_INC, EM_ADDRESS_INC);
    set_dma_std_transfer_bitwidth(EM_DMA0, 0, EM_TR_WIDTH_32_BIT, EM_TR_WIDTH_32_BIT);

    /* 启用目标散射：每组4个word，空隙4个word */
    set_dma_dst_scatter(EM_DMA0, 0, /*DSC*/4, /*DSI*/4);
    /* 源收集关闭 */
    set_dma_src_gather(EM_DMA0, 0, 0, 0);

    set_dma_start(EM_DMA0, 0);
    while (is_dma_busy(EM_DMA0, 0)) {}

    int rc = check_scatter_pattern(dst32, 64, 4, 4);
    printf("  result: %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}

static int check_gather_result(const uint32_t *dst, size_t total_tr, uint32_t group)
{
    /* Expect: concatenation of non-zero blocks (each of length 'group') that were in src */
    uint32_t expect = 0x2000;
    for (size_t i = 0; i < total_tr; ++i)
    {
        if (dst[i] != expect) return -1;
        ++expect;
        if (((i + 1) % group) == 0)
        {
            /* next block starts at next expect sequence value */
        }
    }
    return 0;
}

static int test_source_gather(void)
{
    printf("[Test] Source Gather only\n");
    /* 构造源：每4个word为一块，填入连续值；块与块之间插入4个word的0 */
    fill_zero32(src32, WORDS);
    uint32_t val = 0x2000;
    for (size_t blk = 0; blk < 8; ++blk)
    {
        size_t start = blk * (4 + 4); /* group=4, gap=4 */
        for (size_t i = 0; i < 4; ++i) src32[start + i] = val++;
    }
    fill_zero32(dst32, WORDS);

    if (dma_init(DMA_IDX0) != 0) { printf("dma_init failed\n"); return -10; }
    /* 期望搬运 32 个 word（8 组 x 4）到连续目标 */
    set_dma_std(EM_DMA0, 0, (uint32_t)src32, (uint32_t)dst32, 32 * sizeof(uint32_t), EM_TR_WIDTH_32_BIT);
    set_dma_std_increment(EM_DMA0, 0, EM_ADDRESS_INC, EM_ADDRESS_INC);
    set_dma_std_transfer_bitwidth(EM_DMA0, 0, EM_TR_WIDTH_32_BIT, EM_TR_WIDTH_32_BIT);

    /* 启用源收集：每组4个word，间隔4个word */
    set_dma_src_gather(EM_DMA0, 0, /*SGC*/4, /*SGI*/4);
    /* 关闭目标散射 */
    set_dma_dst_scatter(EM_DMA0, 0, 0, 0);

    set_dma_start(EM_DMA0, 0);
    while (is_dma_busy(EM_DMA0, 0)) {}

    int rc = check_gather_result(dst32, 32, 4);
    printf("  result: %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}

int main(void)
{
    board_debug_uart_init();
    printf("[DMA_ScatterGather] start\n");
    /* 打开DMA0时钟（以及必要的APB分频） */
    rcc_set_cortex_m4_sys_clock(1, 0, 1, true); /* AON=1, DMA1=0, DMA0=1, enable */

    int rc1 = test_dest_scatter();
    int rc2 = test_source_gather();

    if (rc1 == 0 && rc2 == 0) printf("All tests PASS\n");
    else printf("Tests FAIL: rc1=%d rc2=%d\n", rc1, rc2);

    for(;;) { __WFI(); }
}
