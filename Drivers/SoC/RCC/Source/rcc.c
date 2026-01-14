#include <stdint.h>
#include <stdbool.h>
#include "../Include/rcc.h"

/* Small delay helper */
static inline void rcc_delay(volatile uint32_t n)
{
    while (n--) __asm volatile("nop");
}

/* -------- PLL helpers -------- */
static int rcc_wait_pll_lock(volatile uint32_t *lock_reg, uint32_t mask, uint32_t timeout)
{
    while (((*lock_reg & mask) == 0u) && timeout)
    {
        timeout--;
    }
    return timeout ? RCC_STATUS_OK : RCC_STATUS_ERROR_PLL;
}

int rcc_init_cortex_m4_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2)
{
    uint32_t temp;
    uint32_t timeout = 1000u;
    /* switch system clk sel to HSE before touch PLL */
    temp = RCC->CM4_SYS_CLK_SEL;
    temp &= ~0x3u;
    temp |= 1u;
    RCC->CM4_SYS_CLK_SEL = temp;
    rcc_delay(20);
    /* program PLL */
    temp = 0
           | (0u << 31) /* PD */
           | (0u << 30) /* DACPD */
           | (1u << 29) /* DSMPD */
           | (0u << 28) /* FOUTPOSTDIVPD */
           | (0u << 27) /* FOUT4PHASEPD */
           | (0u << 26) /* FOUTVCOPD */
           | (1u << 25) /* BYPASS */
           | ((postdiv2 & 0x7u) << 15)
           | ((postdiv1 & 0x7u) << 12)
           | (fbdiv & 0x0FFFu);
    RCC->CM4_PLL_CTL2 = temp;
    temp = ((uint32_t)(refdiv & 0x3Fu) << 24) | (frac & 0xFFFFFFu);
    RCC->CM4_PLL_CTL = temp;
    if (rcc_wait_pll_lock(&RCC->CM4_PLL_LOCK_STATUS, 1u, timeout) != RCC_STATUS_OK)
        return RCC_STATUS_ERROR_PLL;
    RCC->CM4_PLL_CTL2 &= ~(1u << 25); /* clear BYPASS */
    temp = RCC->CM4_SYS_CLK_SEL;
    temp &= ~0x3u;
    temp |= 0x2u;
    RCC->CM4_SYS_CLK_SEL = temp;
    rcc_delay(100);
    return RCC_STATUS_OK;
}

int rcc_init_audio_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2)
{
    uint32_t temp;
    uint32_t timeout = 1000u;
    temp = RCC->CM4_SYS_CLK_SEL;
    temp &= ~0xCu;
    temp |= 0x4u;
    RCC->CM4_SYS_CLK_SEL = temp;
    rcc_delay(20);
    temp = 0
           | (0u << 31) | (0u << 30) | (1u << 29) | (0u << 28) | (0u << 27) | (0u << 26) | (1u << 25)
           | ((postdiv2 & 0x7u) << 15) | ((postdiv1 & 0x7u) << 12) | (fbdiv & 0x0FFFu);
    RCC->CM4_AUDIO_PLL_CTL2 = temp;
    temp = ((uint32_t)(refdiv & 0x3Fu) << 24) | (frac & 0xFFFFFFu);
    RCC->CM4_AUDIO_PLL_CTL = temp;
    if (rcc_wait_pll_lock(&RCC->CM4_PLL_LOCK_STATUS, 2u, timeout) != RCC_STATUS_OK)
        return RCC_STATUS_ERROR_PLL;
    RCC->CM4_AUDIO_PLL_CTL2 &= ~(1u << 25);
    temp = RCC->CM4_SYS_CLK_SEL;
    temp &= ~0xCu;
    temp |= 0x8u;
    RCC->CM4_SYS_CLK_SEL = temp;
    rcc_delay(100);
    return RCC_STATUS_OK;
}

int rcc_init_gmac_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2)
{
    uint32_t temp;
    uint32_t timeout = 1000u;
    temp = 0
           | (0u << 31) | (0u << 30) | (1u << 29) | (0u << 28) | (0u << 27) | (0u << 26) | (1u << 25)
           | ((postdiv2 & 0x7u) << 15) | ((postdiv1 & 0x7u) << 12) | (fbdiv & 0x0FFFu);
    RCC->CM4_ETH_PLL_CTL2 = temp;
    temp = ((uint32_t)(refdiv & 0x3Fu) << 24) | (frac & 0xFFFFFFu);
    RCC->CM4_ETH_PLL_CTL = temp;
    if (rcc_wait_pll_lock(&RCC->CM4_PLL_LOCK_STATUS, 4u, timeout) != RCC_STATUS_OK)
        return RCC_STATUS_ERROR_PLL;
    RCC->CM4_ETH_PLL_CTL2 &= ~(1u << 25);
    return RCC_STATUS_OK;
}

int rcc_init_mm_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2)
{
    uint32_t temp;
    uint32_t timeout = 1000u;
    temp = DSP_RCC->DSP_SYS_CLK_SEL;
    temp &= ~0x30u;
    temp |= 0x10u;
    DSP_RCC->DSP_SYS_CLK_SEL = temp;
    rcc_delay(20);
    temp = 0
           | (0u << 31) | (0u << 30) | (1u << 29) | (0u << 28) | (0u << 27) | (0u << 26) | (1u << 25)
           | ((postdiv2 & 0x7u) << 15) | ((postdiv1 & 0x7u) << 12) | (fbdiv & 0x0FFFu);
    DSP_RCC->DSP_MM_PLL_CTL2 = temp;
    temp = ((uint32_t)(refdiv & 0x3Fu) << 24) | (frac & 0xFFFFFFu);
    DSP_RCC->DSP_MM_PLL_CTL = temp;
    if (rcc_wait_pll_lock(&DSP_RCC->DSP_PLOCK_STATUS, 2u, timeout) != RCC_STATUS_OK)
        return RCC_STATUS_ERROR_PLL;
    DSP_RCC->DSP_MM_PLL_CTL2 &= ~(1u << 25);
    temp = DSP_RCC->DSP_SYS_CLK_SEL;
    temp &= ~0x30u;
    temp |= 0x20u;
    DSP_RCC->DSP_SYS_CLK_SEL = temp;
    rcc_delay(100);
    return RCC_STATUS_OK;
}

int rcc_init_dsp_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2)
{
    uint32_t temp;
    uint32_t timeout = 1000u;
    temp = DSP_RCC->DSP_SYS_CLK_SEL;
    temp &= ~0x3u;
    temp |= 1u;
    DSP_RCC->DSP_SYS_CLK_SEL = temp;
    rcc_delay(20);
    temp = 0
           | (0u << 31) | (0u << 30) | (1u << 29) | (0u << 28) | (0u << 27) | (0u << 26) | (1u << 25)
           | ((postdiv2 & 0x7u) << 15) | ((postdiv1 & 0x7u) << 12) | (fbdiv & 0x0FFFu);
    DSP_RCC->DSP_PLL_CTRL2 = temp;
    temp = ((uint32_t)(refdiv & 0x3Fu) << 24) | (frac & 0xFFFFFFu);
    DSP_RCC->DSP_PLL_CTRL = temp;
    if (rcc_wait_pll_lock(&DSP_RCC->DSP_PLOCK_STATUS, 1u, timeout) != RCC_STATUS_OK)
        return RCC_STATUS_ERROR_PLL;
    DSP_RCC->DSP_PLL_CTRL2 &= ~(1u << 25);
    temp = DSP_RCC->DSP_SYS_CLK_SEL;
    temp &= ~0x3u;
    temp |= 0x2u;
    DSP_RCC->DSP_SYS_CLK_SEL = temp;
    rcc_delay(100);
    return RCC_STATUS_OK;
}

/* -------- CM4 clock gates/dividers/resets -------- */
void rcc_set_cortex_m4_sys_clock(uint8_t aon, uint8_t dma1, uint8_t dma0, rcc_bool_t en)
{
    uint32_t mask = ((uint32_t)(aon & 1u) << 8) | ((uint32_t)(dma1 & 1u) << 4) | ((uint32_t)(dma0 & 1u) << 3);
    if (en) RCC->CM4_SYS_CLK_EN |= mask;
    else RCC->CM4_SYS_CLK_EN &= ~mask;
}

void rcc_set_cortex_m4_apb0_clock(rcc_cm4_apb0_t apb, rcc_bool_t en)
{
    if (en) RCC->CM4_APB0_CLK_EN |= apb;
    else RCC->CM4_APB0_CLK_EN &= ~apb;
}

void rcc_set_cortex_m4_apb1_clock(rcc_cm4_apb1_t apb, rcc_bool_t en)
{
    if (en) RCC->CM4_APB1_CLK_EN |= apb;
    else RCC->CM4_APB1_CLK_EN &= ~apb;
}

void rcc_set_cortex_m4_ahb_clock(rcc_cm4_ahb_t ahb, rcc_bool_t en)
{
    if (en) RCC->CM4_AHB_CLK_EN |= ahb;
    else RCC->CM4_AHB_CLK_EN &= ~ahb;
}

void rcc_set_apb_clock_div(uint8_t num, uint8_t div)
{
    if (num)
    {
        RCC->CM4_APB_CLK_DIV = (RCC->CM4_APB_CLK_DIV & ~0xF0u) | (((uint32_t)div & 0xFu) << 4);
    }
    else
    {
        RCC->CM4_APB_CLK_DIV = (RCC->CM4_APB_CLK_DIV & ~0x0Fu) | ((uint32_t)div & 0xFu);
    }
}

void rcc_set_cortex_m4_core_reset(rcc_bool_t en)
{
    if (en) RCC->CM4_SYS_SOFT_RSTN &= ~1u;
    else RCC->CM4_SYS_SOFT_RSTN |= 1u;
}

void rcc_set_cortex_m4_sys_reset(uint8_t aon, uint8_t dma1, uint8_t dma0, rcc_bool_t en)
{
    uint32_t mask = ((uint32_t)(aon & 1u) << 8) | ((uint32_t)(dma1 & 1u) << 4) | ((uint32_t)(dma0 & 1u) << 3);
    if (en) RCC->CM4_SYS_RST_CTL &= ~mask;
    else RCC->CM4_SYS_RST_CTL |= mask;
}

void rcc_set_cortex_m4_apb0_reset(rcc_cm4_apb0_t apb, rcc_bool_t en)
{
    if (en) RCC->CM4_APB0_RST_CTL &= ~apb;
    else RCC->CM4_APB0_RST_CTL |= apb;
}

void rcc_set_cortex_m4_apb1_reset(rcc_cm4_apb1_t apb, rcc_bool_t en)
{
    if (en) RCC->CM4_APB1_RST_CTL &= ~apb;
    else RCC->CM4_APB1_RST_CTL |= apb;
}

void rcc_set_cortex_m4_ahb_reset(rcc_cm4_ahb_t ahb, rcc_bool_t en)
{
    if (en) RCC->CM4_AHB_RST_CTL &= ~ahb;
    else RCC->CM4_AHB_RST_CTL |= ahb;
}

/* -------- Audio / watchdog / timers / SDIO / GMAC -------- */
void rcc_set_audio_clock(uint8_t num, rcc_bool_t en)
{
    uint32_t m = (1u << (num & 1u));
    if (en) RCC->CM4_AUDIO_PERF_CLK_EN |= m;
    else RCC->CM4_AUDIO_PERF_CLK_EN &= ~m;
}

void rcc_set_audio_reset(uint8_t num, rcc_bool_t en)
{
    uint32_t m = (1u << (num & 1u));
    if (en) RCC->CM4_AUDIO_RSTN_CTL &= ~m;
    else RCC->CM4_AUDIO_RSTN_CTL |= m;
}

void rcc_set_wdg3_reset(rcc_bool_t en)
{
    uint32_t m = (1u << 7);
    if (en) RCC->CM4_WDG3_RCC_CTL &= ~m;
    else RCC->CM4_WDG3_RCC_CTL |= m;
}

void rcc_set_timer_wdg_32k_clock(uint8_t timer2, uint8_t wdg3, rcc_bool_t en)
{
    uint32_t m = ((uint32_t)(timer2 & 1u) << 1) | ((uint32_t)(wdg3 & 1u));
    if (en) RCC->CM4_WDG3_RCC_CTL |= m;
    else RCC->CM4_WDG3_RCC_CTL &= ~m;
}

void rcc_set_timer_clock(uint8_t num, rcc_bool_t en)
{
    uint32_t m = (1u << (num & 7u));
    if (en) RCC->CM4_TW_CLK_CTL |= m;
    else RCC->CM4_TW_CLK_CTL &= ~m;
}

void rcc_set_wdg_clock(uint8_t num, rcc_bool_t en)
{
    uint32_t m = (1u << ((num & 3u) + 6u));
    if (en) RCC->CM4_TW_CLK_CTL |= m;
    else RCC->CM4_TW_CLK_CTL &= ~m;
}

void rcc_set_mem_clock(uint8_t bus, uint8_t rom, uint8_t sram0, uint8_t sram1, rcc_bool_t en)
{
    uint32_t m = ((uint32_t)(bus & 1u) << 3) | ((uint32_t)(rom & 1u) << 2) | ((uint32_t)(sram0 & 1u) << 1) | ((uint32_t)(sram1 & 1u));
    if (en) RCC->CM4_MEM_CLK_CTL |= m;
    else RCC->CM4_MEM_CLK_CTL &= ~m;
}

void rcc_set_mem_reset(uint8_t bus, uint8_t sram0, uint8_t sram1, rcc_bool_t en)
{
    uint32_t m = ((uint32_t)(bus & 1u) << 3) | ((uint32_t)(sram0 & 1u) << 1) | ((uint32_t)(sram1 & 1u));
    if (en) RCC->CM4_MEM_RST_CTL &= ~m;
    else RCC->CM4_MEM_RST_CTL |= m;
}

void rcc_set_gmac_clock(uint8_t div, rcc_bool_t en)
{
    if (en) RCC->CM4_ETH_CLK_DIV = (((uint32_t)div & 0xFu) << 28) | 1u;
    else RCC->CM4_ETH_CLK_DIV &= ~1u;
}

void rcc_set_i2s_clock(uint8_t div)
{
    RCC->CM4_I2S_CLK_DIV = ((uint32_t)div & 0xFu);
}

void rcc_set_sdio_clock(uint8_t num, uint8_t delay, uint8_t div, int diven, uint8_t sample, uint8_t drv)
{
    uint32_t divctl = (((uint32_t)delay & 7u) << 3) | ((uint32_t)div & 7u);
    uint32_t cclk = (diven ? 0x1u : 0u) | (((uint32_t)sample & 3u) << 1) | (((uint32_t)drv & 3u) << 3);
    if ((num & 1u) == 1u)
    {
        RCC->CM4_SDIO1_CLK_DIV_CTL = divctl;
        RCC->CM4_SDIO_CLK_SEL = (RCC->CM4_SDIO_CLK_SEL & ~0xFF00u) | (cclk << 8);
    }
    else
    {
        RCC->CM4_SDIO0_CLK_DIV_CTL = divctl;
        RCC->CM4_SDIO_CLK_SEL = (RCC->CM4_SDIO_CLK_SEL & ~0x00FFu) | (cclk);
    }
}

/* -------- DSP/MM domain -------- */
void rcc_set_npu_clock_div(uint8_t div)
{
    DSP_RCC->DSP_PIM_NPU_CLK_DIV = div;
}

void rcc_set_pim_clock_div(uint8_t div)
{
    DSP_RCC->DSP_PIM_NPU_CLK_DIV = div;
}

void rcc_set_mm_clock_enable(rcc_bool_t en)
{
    if (en) DSP_RCC->DSP_MM_PERF_CLKEN |= 1u;
    else DSP_RCC->DSP_MM_PERF_CLKEN = 0u;
}

void rcc_set_mm_reset(rcc_bool_t reset)
{
    if (reset) DSP_RCC->DSP_MM_RSTN_CTL = 0u;
    else DSP_RCC->DSP_MM_RSTN_CTL |= 0x11u;
}

void rcc_set_dsp_reset(rcc_bool_t reset)
{
    if (reset) DSP_RCC->DSP_CEVA_RST_CTRL = 0u;
    else DSP_RCC->DSP_CEVA_RST_CTRL |= 1u;
}

void rcc_set_dsp_warm_reset(rcc_bool_t reset)
{
    if (reset && (DSP_RCC->DSP_CEVA_STATUS & 1u)) DSP_RCC->DSP_WARM_RSTN = 0u;
    else DSP_RCC->DSP_WARM_RSTN = 1u;
}

void rcc_set_dsp_peripheral_reset(uint32_t reset_mask, rcc_bool_t en)
{
    if (en) DSP_RCC->DSP_PERF_RSTN_CTL &= ~reset_mask;
    else DSP_RCC->DSP_PERF_RSTN_CTL |= reset_mask;
}

void rcc_set_dsp_system_reset(uint32_t reset_mask, rcc_bool_t en)
{
    if (en) DSP_RCC->DSP_RSTN_CTL &= ~reset_mask;
    else DSP_RCC->DSP_RSTN_CTL |= reset_mask;
}

void rcc_set_dsp_peripheral_clock(uint32_t clock_mask, rcc_bool_t en)
{
    if (en) DSP_RCC->DSP_PERF_CLK_EN |= clock_mask;
    else DSP_RCC->DSP_PERF_CLK_EN &= ~clock_mask;
}

void rcc_set_dsp_system_clock(uint32_t clock_mask, rcc_bool_t en)
{
    if (en) DSP_RCC->DSP_SYS_CLK_EN |= clock_mask;
    else DSP_RCC->DSP_SYS_CLK_EN &= ~clock_mask;
}

/* -------- Clock calculation -------- */
static uint32_t rcc_pll_calc(uint32_t ctl2, uint32_t ctl)
{
    if ((ctl2 & (1u << 31)) != 0u) return 0u; /* PD */
    uint32_t postdiv2 = (ctl2 >> 15) & 0x7u;
    uint32_t postdiv1 = (ctl2 >> 12) & 0x7u;
    uint32_t fbdiv    = (ctl2 & 0x0FFFu);
    uint32_t refdiv   = (ctl >> 24) & 0x3Fu;
    uint32_t frac     = (ctl & 0xFFFFFFu);
    if (refdiv == 0u || postdiv1 == 0u || postdiv2 == 0u) return 0u;
    /* Use 64-bit to maintain precision: fvco = (HSE/refdiv)*(fbdiv + frac/2^24) */
    uint64_t base = ((uint64_t)HSE_CLOCK_HZ * (uint64_t)fbdiv << 24) + ((uint64_t)HSE_CLOCK_HZ * (uint64_t)frac);
    uint64_t fvco_q24 = base / (uint64_t)refdiv; /* Q24 */
    uint64_t fpost_q24 = fvco_q24 / ((uint64_t)postdiv1 * (uint64_t)postdiv2 * 2ull);
    return (uint32_t)(fpost_q24 >> 24);
}

uint32_t rcc_get_clock(rcc_clock_t clock)
{
    uint32_t ret = 0, sel, div;
    switch (clock)
    {
    case RCC_CLOCK_SYSTEM:
    case RCC_CLOCK_AHB:
        sel = RCC->CM4_SYS_CLK_SEL & 0x3u;
        ret = (sel == 1u) ? HSE_CLOCK_HZ : rcc_pll_calc(RCC->CM4_PLL_CTL2, RCC->CM4_PLL_CTL);
        break;
    case RCC_CLOCK_AUDIO:
        sel = (RCC->CM4_SYS_CLK_SEL >> 2) & 0x3u;
        ret = (sel == 1u) ? HSE_CLOCK_HZ : rcc_pll_calc(RCC->CM4_AUDIO_PLL_CTL2, RCC->CM4_AUDIO_PLL_CTL);
        div = (RCC->CM4_I2S_CLK_DIV & 0x0Fu);
        ret = ret / (2u * (div + 1u));
        break;
    case RCC_CLOCK_MM:
        sel = (DSP_RCC->DSP_SYS_CLK_SEL >> 4) & 0x3u;
        ret = (sel == 1u) ? HSE_CLOCK_HZ : rcc_pll_calc(DSP_RCC->DSP_MM_PLL_CTL2, DSP_RCC->DSP_MM_PLL_CTL);
        break;
    case RCC_CLOCK_DSP:
        sel = (DSP_RCC->DSP_SYS_CLK_SEL & 0x3u);
        ret = (sel == 1u) ? HSE_CLOCK_HZ : rcc_pll_calc(DSP_RCC->DSP_PLL_CTRL2, DSP_RCC->DSP_PLL_CTRL);
        break;
    case RCC_CLOCK_PWM:
        sel = (RCC->CM4_SYS_CLK_SEL >> 6) & 0x3u;
        ret = (sel == 1u) ? HSE_CLOCK_HZ : rcc_pll_calc(RCC->CM4_PLL_CTL2, RCC->CM4_PLL_CTL);
        break;
    case RCC_CLOCK_GMAC:
        ret = rcc_pll_calc(RCC->CM4_ETH_PLL_CTL2, RCC->CM4_ETH_PLL_CTL) / 2u;
        break;
    case RCC_CLOCK_APB0:
        sel = RCC->CM4_SYS_CLK_SEL & 0x3u;
        ret = (sel == 1u) ? HSE_CLOCK_HZ : rcc_pll_calc(RCC->CM4_PLL_CTL2, RCC->CM4_PLL_CTL);
        div = (RCC->CM4_APB_CLK_DIV & 0x0Fu);
        ret = ret / (div + 1u);
        break;
    case RCC_CLOCK_APB1:
        sel = RCC->CM4_SYS_CLK_SEL & 0x3u;
        ret = (sel == 1u) ? HSE_CLOCK_HZ : rcc_pll_calc(RCC->CM4_PLL_CTL2, RCC->CM4_PLL_CTL);
        div = (RCC->CM4_APB_CLK_DIV >> 4) & 0x0Fu;
        ret = ret / (div + 1u);
        break;
    case RCC_CLOCK_32K:
        ret = 32768u;
        break;
    default:
        ret = 0;
        break;
    }
    return ret;
}
