#include "s300.h"
#include "rcc_s300.h"

uint32_t SystemCoreClock = HSE_CLOCK_HZ; /* default 24MHz */

static uint32_t cm4_pll_calc(uint32_t ctl2, uint32_t ctl)
{
    /* 复制 RCC 计算逻辑：fvco = (HSE/refdiv) * (fbdiv + frac/2^24)
       fpost = fvco / (postdiv1 * postdiv2 * 2) */
    if ((ctl2 & (1u << 31)) != 0u) return 0u; /* PD */
    uint32_t postdiv2 = (ctl2 >> 15) & 0x7u;
    uint32_t postdiv1 = (ctl2 >> 12) & 0x7u;
    uint32_t fbdiv    = (ctl2 & 0x0FFFu);
    uint32_t refdiv   = (ctl >> 24) & 0x3Fu;
    uint32_t frac     = (ctl & 0xFFFFFFu);
    if (refdiv == 0u || postdiv1 == 0u || postdiv2 == 0u) return 0u;
    uint64_t base = ((uint64_t)HSE_CLOCK_HZ * (uint64_t)fbdiv << 24) + ((uint64_t)HSE_CLOCK_HZ * (uint64_t)frac);
    uint64_t fvco_q24 = base / (uint64_t)refdiv; /* Q24 */
    uint64_t fpost_q24 = fvco_q24 / ((uint64_t)postdiv1 * (uint64_t)postdiv2 * 2ull);
    return (uint32_t)(fpost_q24 >> 24);
}

void SystemCoreClockUpdate(void)
{
    /* 依据 RCC->CM4_SYS_CLK_SEL 选择 HSE 或 CM4 PLL 作为系统时钟 */
    uint32_t sel = RCC->CM4_SYS_CLK_SEL & 0x3u;
    if (sel == 1u)
        SystemCoreClock = HSE_CLOCK_HZ;
    else
        SystemCoreClock = cm4_pll_calc(RCC->CM4_PLL_CTL2, RCC->CM4_PLL_CTL);
}

void SystemInit(void)
{
    /* Minimal: keep default clock source = HSE per reset, no PLL. */
    SystemCoreClockUpdate();
    /* Enable FPU (CP10 & CP11 full access) before any FP instruction executes */
#if defined(__FPU_PRESENT) && (__FPU_PRESENT == 1) && defined(__FPU_USED) && (__FPU_USED == 1)
    SCB->CPACR |= (0xFu << 20);
    __DSB();
    __ISB();
#endif
    /* Set vector table base address */
#if defined(SBL_BUILD) && (SBL_BUILD == 1)
    /* SBL runs from Flash, set VTOR to Flash base + SBL offset */
    SCB->VTOR = (uint32_t)0x08010000U;
#else
    /* RBL runs from SRAM, set VTOR to SRAM base where .isr_vector is linked by sram.ld */
    SCB->VTOR = (uint32_t)0x20000000U;
#endif
}
