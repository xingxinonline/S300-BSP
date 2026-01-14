#ifndef S300_BSP_RCC_H
#define S300_BSP_RCC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "rcc_s300.h"

#ifndef HSE_CLOCK_HZ
#define HSE_CLOCK_HZ (24000000UL)
#endif

/* Status codes (compatible with legacy EM_STATUS_*) */
#define RCC_STATUS_OK           0
#define RCC_STATUS_ERROR_PLL   -2

/* Backward compatible boolean */
typedef bool rcc_bool_t;

/* Bit masks mapped from legacy definitions for easy porting */
typedef enum
{
    RCC_CM4_APB0_TIMER0   = (1u << 0),
    RCC_CM4_APB0_TIMER1   = (1u << 1),
    RCC_CM4_APB0_TIMER2   = (1u << 2),
    RCC_CM4_APB0_WDG0     = (1u << 3),
    RCC_CM4_APB0_WDG1     = (1u << 4),
    RCC_CM4_APB0_WDG2     = (1u << 5),
    RCC_CM4_APB0_WDG3     = (1u << 6),
    RCC_CM4_APB0_INTCTRL  = (1u << 7),
    RCC_CM4_APB0_IOMATRIX = (1u << 8),
    RCC_CM4_APB0_IOMUX    = (1u << 9),
    RCC_CM4_APB0_SCTRL    = (1u << 12),
    RCC_CM4_APB0_QSPIFLASH = (1u << 13),
    RCC_CM4_APB0_DVP      = (1u << 14),
} rcc_cm4_apb0_t;

typedef enum
{
    RCC_CM4_APB1_UART0 = (1u << 0),
    RCC_CM4_APB1_UART1 = (1u << 1),
    RCC_CM4_APB1_UART2 = (1u << 2),
    RCC_CM4_APB1_UART3 = (1u << 3),
    RCC_CM4_APB1_I2C0  = (1u << 4),
    RCC_CM4_APB1_I2C1  = (1u << 5),
    RCC_CM4_APB1_I2C2  = (1u << 6),
    RCC_CM4_APB1_I2C3  = (1u << 7),
    RCC_CM4_APB1_GPIO  = (1u << 8),
    RCC_CM4_APB1_MBOX  = (1u << 9),
    RCC_CM4_APB1_I2S0  = (1u << 11),
    RCC_CM4_APB1_I2S1  = (1u << 12),
    RCC_CM4_APB1_PWM   = (1u << 13),
    RCC_CM4_APB1_PDM   = (1u << 15),
} rcc_cm4_apb1_t;

typedef enum
{
    RCC_CM4_AHB_GMAC      = (1u << 4),
    RCC_CM4_AHB_SECRAM4K  = (1u << 5),
    RCC_CM4_AHB_PSRAM     = (1u << 6),
    RCC_CM4_AHB_QSPIFLASH = (1u << 7),
    RCC_CM4_AHB_SPI0      = (1u << 8),
    RCC_CM4_AHB_SPI1      = (1u << 9),
    RCC_CM4_AHB_LCD       = (1u << 10),
    RCC_CM4_AHB_SDIO0     = (1u << 12),
    RCC_CM4_AHB_SDIO1     = (1u << 13),
} rcc_cm4_ahb_t;

typedef enum
{
    RCC_CLOCK_SYSTEM = 0,
    RCC_CLOCK_AUDIO,
    RCC_CLOCK_MM,
    RCC_CLOCK_DSP,
    RCC_CLOCK_PWM,
    RCC_CLOCK_GMAC,
    RCC_CLOCK_APB0,
    RCC_CLOCK_APB1,
    RCC_CLOCK_AHB,
    RCC_CLOCK_32K,
} rcc_clock_t;

/* PLL init */
int rcc_init_cortex_m4_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2);
int rcc_init_audio_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2);
int rcc_init_gmac_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2);
int rcc_init_mm_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2);
int rcc_init_dsp_pll(uint16_t refdiv, uint16_t fbdiv, uint32_t frac, uint16_t postdiv1, uint16_t postdiv2);

/* CM4 clock gates/dividers/resets */
void rcc_set_cortex_m4_sys_clock(uint8_t aon, uint8_t dma1, uint8_t dma0, rcc_bool_t en);
void rcc_set_cortex_m4_apb0_clock(rcc_cm4_apb0_t apb, rcc_bool_t en);
void rcc_set_cortex_m4_apb1_clock(rcc_cm4_apb1_t apb, rcc_bool_t en);
void rcc_set_cortex_m4_ahb_clock(rcc_cm4_ahb_t ahb, rcc_bool_t en);
void rcc_set_apb_clock_div(uint8_t num, uint8_t div);
void rcc_set_cortex_m4_core_reset(rcc_bool_t en);
void rcc_set_cortex_m4_sys_reset(uint8_t aon, uint8_t dma1, uint8_t dma0, rcc_bool_t en);
void rcc_set_cortex_m4_apb0_reset(rcc_cm4_apb0_t apb, rcc_bool_t en);
void rcc_set_cortex_m4_apb1_reset(rcc_cm4_apb1_t apb, rcc_bool_t en);
void rcc_set_cortex_m4_ahb_reset(rcc_cm4_ahb_t ahb, rcc_bool_t en);

/* Audio / I2S / SDIO / GMAC helpers */
void rcc_set_audio_clock(uint8_t num, rcc_bool_t en);
void rcc_set_audio_reset(uint8_t num, rcc_bool_t en);
void rcc_set_wdg3_reset(rcc_bool_t en);
void rcc_set_timer_wdg_32k_clock(uint8_t timer2, uint8_t wdg3, rcc_bool_t en);
void rcc_set_timer_clock(uint8_t num, rcc_bool_t en);
void rcc_set_wdg_clock(uint8_t num, rcc_bool_t en);
void rcc_set_mem_clock(uint8_t bus, uint8_t rom, uint8_t sram0, uint8_t sram1, rcc_bool_t en);
void rcc_set_mem_reset(uint8_t bus, uint8_t sram0, uint8_t sram1, rcc_bool_t en);
void rcc_set_gmac_clock(uint8_t div, rcc_bool_t en);
void rcc_set_i2s_clock(uint8_t div);
void rcc_set_sdio_clock(uint8_t num, uint8_t delay, uint8_t div, int diven, uint8_t sample, uint8_t drv);

/* DSP/MM domain */
void rcc_set_npu_clock_div(uint8_t div);
void rcc_set_pim_clock_div(uint8_t div);
void rcc_set_mm_clock_enable(rcc_bool_t en);
void rcc_set_mm_reset(rcc_bool_t reset);
void rcc_set_dsp_reset(rcc_bool_t reset);
void rcc_set_dsp_warm_reset(rcc_bool_t reset);
void rcc_set_dsp_peripheral_reset(uint32_t reset_mask, rcc_bool_t en);
void rcc_set_dsp_system_reset(uint32_t reset_mask, rcc_bool_t en);
void rcc_set_dsp_peripheral_clock(uint32_t clock_mask, rcc_bool_t en);
void rcc_set_dsp_system_clock(uint32_t clock_mask, rcc_bool_t en);

/* Clock query */
uint32_t rcc_get_clock(rcc_clock_t clock);

/* -------- Legacy name aliases to ease porting (optional) -------- */
typedef rcc_cm4_apb0_t emCM4APB0;
typedef rcc_cm4_apb1_t emCM4APB1;
typedef rcc_cm4_ahb_t  emCM4AHB;
typedef rcc_clock_t    emCLOCK;

static inline int init_cortex_m4_pll(uint16_t a, uint16_t b, uint32_t c, uint16_t d, uint16_t e)
{
    return rcc_init_cortex_m4_pll(a, b, c, d, e);
}
static inline int init_audio_pll(uint16_t a, uint16_t b, uint32_t c, uint16_t d, uint16_t e)
{
    return rcc_init_audio_pll(a, b, c, d, e);
}
static inline int init_gmac_pll(uint16_t a, uint16_t b, uint32_t c, uint16_t d, uint16_t e)
{
    return rcc_init_gmac_pll(a, b, c, d, e);
}
static inline int init_mm_pll(uint16_t a, uint16_t b, uint32_t c, uint16_t d, uint16_t e)
{
    return rcc_init_mm_pll(a, b, c, d, e);
}
static inline int init_dsp_pll(uint16_t a, uint16_t b, uint32_t c, uint16_t d, uint16_t e)
{
    return rcc_init_dsp_pll(a, b, c, d, e);
}
static inline void set_cortex_m4_sys_clock(uint8_t a, uint8_t b, uint8_t c, bool en)
{
    rcc_set_cortex_m4_sys_clock(a, b, c, en);
}
static inline void set_cortex_m4_apb0_clock(emCM4APB0 apb, bool en)
{
    rcc_set_cortex_m4_apb0_clock(apb, en);
}
static inline void set_cortex_m4_apb1_clock(emCM4APB1 apb, bool en)
{
    rcc_set_cortex_m4_apb1_clock(apb, en);
}
static inline void set_cortex_m4_ahb_clock(emCM4AHB ahb, bool en)
{
    rcc_set_cortex_m4_ahb_clock(ahb, en);
}
static inline void set_apb_clock_div(uint8_t num, uint8_t div)
{
    rcc_set_apb_clock_div(num, div);
}
static inline void set_cortex_m4_core_reset(bool en)
{
    rcc_set_cortex_m4_core_reset(en);
}
static inline void set_cortex_m4_sys_reset(uint8_t a, uint8_t b, uint8_t c, bool en)
{
    rcc_set_cortex_m4_sys_reset(a, b, c, en);
}
static inline void set_cortex_m4_apb0_reset(emCM4APB0 apb, bool en)
{
    rcc_set_cortex_m4_apb0_reset(apb, en);
}
static inline void set_cortex_m4_apb1_reset(emCM4APB1 apb, bool en)
{
    rcc_set_cortex_m4_apb1_reset(apb, en);
}
static inline void set_cortex_m4_ahb_reset(emCM4AHB ahb, bool en)
{
    rcc_set_cortex_m4_ahb_reset(ahb, en);
}
static inline void set_audio_clock(uint8_t num, bool en)
{
    rcc_set_audio_clock(num, en);
}
static inline void set_audio_reset(uint8_t num, bool en)
{
    rcc_set_audio_reset(num, en);
}
static inline void set_wdg3_reset(bool en)
{
    rcc_set_wdg3_reset(en);
}
static inline void set_timer_wdg_32k_clock(uint8_t t2, uint8_t w3, bool en)
{
    rcc_set_timer_wdg_32k_clock(t2, w3, en);
}
static inline void set_timer_clock(uint8_t num, bool en)
{
    rcc_set_timer_clock(num, en);
}
static inline void set_wdg_clock(uint8_t num, bool en)
{
    rcc_set_wdg_clock(num, en);
}
static inline void set_mem_clock(uint8_t bus, uint8_t rom, uint8_t s0, uint8_t s1, bool en)
{
    rcc_set_mem_clock(bus, rom, s0, s1, en);
}
static inline void set_mem_reset(uint8_t bus, uint8_t s0, uint8_t s1, bool en)
{
    rcc_set_mem_reset(bus, s0, s1, en);
}
static inline void set_gmac_clock(uint8_t div, bool en)
{
    rcc_set_gmac_clock(div, en);
}
static inline void set_i2s_clock(uint8_t div)
{
    rcc_set_i2s_clock(div);
}
static inline void set_sdio_clock(uint8_t n, uint8_t dly, uint8_t div, int diven, uint8_t s, uint8_t drv)
{
    rcc_set_sdio_clock(n, dly, div, diven, s, drv);
}
static inline void set_npu_clock_div(uint8_t div)
{
    rcc_set_npu_clock_div(div);
}
static inline void set_pim_clock_div(uint8_t div)
{
    rcc_set_pim_clock_div(div);
}
static inline void set_mm_clock_enable(bool en)
{
    rcc_set_mm_clock_enable(en);
}
static inline void set_mm_reset(bool r)
{
    rcc_set_mm_reset(r);
}
static inline void set_dsp_reset(bool r)
{
    rcc_set_dsp_reset(r);
}
static inline void set_dsp_warm_reset(bool r)
{
    rcc_set_dsp_warm_reset(r);
}
static inline void set_dsp_peripheral_reset(uint32_t m, bool en)
{
    rcc_set_dsp_peripheral_reset(m, en);
}
static inline void set_dsp_system_reset(uint32_t m, bool en)
{
    rcc_set_dsp_system_reset(m, en);
}
static inline void set_dsp_peripheral_clock(uint32_t m, bool en)
{
    rcc_set_dsp_peripheral_clock(m, en);
}
static inline void set_dsp_system_clock(uint32_t m, bool en)
{
    rcc_set_dsp_system_clock(m, en);
}
static inline uint32_t get_clock(rcc_clock_t c)
{
    return rcc_get_clock(c);
}

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_RCC_H */
