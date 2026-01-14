#ifndef S300_RCC_S300_H
#define S300_RCC_S300_H

#ifdef __cplusplus
extern "C" {
#endif

#include "s300.h"

typedef struct
{
    volatile uint32_t CM4_SYS_CLK_SEL;       /* 0x0000 */
    volatile uint32_t CM4_SYS_CLK_EN;        /* 0x0004 */
    volatile uint32_t CM4_APB0_CLK_EN;       /* 0x0008 */
    volatile uint32_t CM4_APB1_CLK_EN;       /* 0x000C */
    volatile uint32_t CM4_AHB_CLK_EN;        /* 0x0010 */
    volatile uint32_t CM4_APB_CLK_DIV;       /* 0x0014 */
    volatile uint32_t CM4_SYS_SOFT_RSTN;     /* 0x0018 */
    volatile uint32_t CM4_SYS_RST_CTL;       /* 0x001C */
    volatile uint32_t CM4_APB0_RST_CTL;      /* 0x0020 */
    volatile uint32_t CM4_APB1_RST_CTL;      /* 0x0024 */
    volatile uint32_t CM4_AHB_RST_CTL;       /* 0x0028 */
    volatile uint32_t CM4_AUDIO_PERF_CLK_EN; /* 0x002C */
    volatile uint32_t CM4_AUDIO_RSTN_CTL;    /* 0x0030 */
    volatile uint32_t CM4_MM_PERF_CLK_EN;    /* 0x0034 */
    volatile uint32_t CM4_MM_RSTN_CTL;       /* 0x0038 */
    volatile uint32_t CM4_WDG3_RCC_CTL;      /* 0x003C */
    volatile uint32_t CM4_TW_CLK_CTL;        /* 0x0040 */
    volatile uint32_t CM4_MEM_CLK_CTL;       /* 0x0044 */
    volatile uint32_t CM4_MEM_RST_CTL;       /* 0x0048 */
    volatile uint32_t CM4_PLL_CTL;           /* 0x004C */
    volatile uint32_t CM4_PLL_CTL2;          /* 0x0050 */
    volatile uint32_t CM4_AUDIO_PLL_CTL;     /* 0x0054 */
    volatile uint32_t CM4_AUDIO_PLL_CTL2;    /* 0x0058 */
    volatile uint32_t CM4_MM_PLL_CTL;        /* 0x005C */
    volatile uint32_t CM4_MM_PLL_CTL2;       /* 0x0060 */
    volatile uint32_t CM4_I2S_CLK_DIV;       /* 0x0064 */
    volatile uint32_t CM4_ETH_PLL_CTL;       /* 0x0068 */
    volatile uint32_t CM4_ETH_PLL_CTL2;      /* 0x006C */
    volatile uint32_t CM4_ETH_CLK_DIV;       /* 0x0070 */
    volatile uint32_t CM4_PLL_LOCK_STATUS;   /* 0x0074 */
    volatile uint32_t CM4_SDIO0_CLK_DIV_CTL; /* 0x0078 */
    volatile uint32_t CM4_SDIO1_CLK_DIV_CTL; /* 0x007C */
    volatile uint32_t CM4_SDIO_CLK_SEL;      /* 0x0080 */
} S300_RCC_TypeDef;

typedef struct
{
    volatile uint32_t DSP_SYS_CLK_EN;        /* 0x0000 */
    volatile uint32_t DSP_PERF_CLK_EN;       /* 0x0004 */
    volatile uint32_t DSP_RSTN_CTL;          /* 0x0008 */
    volatile uint32_t DSP_PERF_RSTN_CTL;     /* 0x000C */
    volatile uint32_t DSP_WARM_RSTN;         /* 0x0010 */
    volatile uint32_t DSP_SYS_CLK_SEL;       /* 0x0014 */
    volatile uint32_t DSP_CEVA_RST_CTRL;     /* 0x0018 */
    volatile uint32_t DSP_PLL_CTRL;          /* 0x001C */
    volatile uint32_t DSP_PLL_CTRL2;         /* 0x0020 */
    volatile uint32_t DSP_PLOCK_STATUS;      /* 0x0024 */
    volatile uint32_t DSP_CEVA_STATUS;       /* 0x0028 */
    uint32_t RESERVED_0x2C;                  /* 0x002C */
    volatile uint32_t DSP_PIM_NPU_CLK_DIV;   /* 0x0030 */
    volatile uint32_t DSP_MM_PLL_CTL;        /* 0x0034 */
    volatile uint32_t DSP_MM_PLL_CTL2;       /* 0x0038 */
    volatile uint32_t DSP_MM_PERF_CLKEN;     /* 0x003C */
    volatile uint32_t DSP_MM_RSTN_CTL;       /* 0x0040 */
    volatile uint32_t DSP_WARMRST_STS;       /* 0x0044 */
} S300_DSP_RCC_TypeDef;

#define RCC        ((S300_RCC_TypeDef *)RCC_BASE)
#define DSP_RCC    ((S300_DSP_RCC_TypeDef *)DSP_RCC_BASE)

#ifdef __cplusplus
}
#endif

#endif /* S300_RCC_S300_H */
