#ifndef S300_I2S_S300_H
#define S300_I2S_S300_H

#ifdef __cplusplus
extern "C" {
#endif

#include "s300.h"

/* I2S register map derived from legacy offsets */
typedef struct
{
    volatile uint32_t IER;        /* 0x000: Enable */
    volatile uint32_t IRER;       /* 0x004: Rx block enable */
    volatile uint32_t ITER;       /* 0x008: Tx block enable */
    volatile uint32_t CER;        /* 0x00C: Clock enable */
    volatile uint32_t CCR;        /* 0x010: Clock config */
    volatile uint32_t RXFFR;      /* 0x014: Rx FIFO flush */
    volatile uint32_t TXFFR;      /* 0x018: Tx FIFO flush */
    volatile uint32_t SR;         /* 0x01C: Status */
    volatile uint32_t LRBR0;      /* 0x020: Left Rx buffer 0 / Left Tx holding 0 */
    volatile uint32_t RRBR0;      /* 0x024: Right Rx buffer 0 / Right Tx holding 0 */
    volatile uint32_t RER0;       /* 0x028: Rx channel enable 0 */
    volatile uint32_t TER0;       /* 0x02C: Tx channel enable 0 */
    volatile uint32_t RCR0;       /* 0x030: Rx config 0 (word length) */
    volatile uint32_t TCR0;       /* 0x034: Tx config 0 (word length) */
    volatile uint32_t ISR0;       /* 0x038: Interrupt status 0 */
    volatile uint32_t IMR0;       /* 0x03C: Interrupt mask 0 */
    volatile uint32_t ROR0;       /* 0x040: Rx overrun 0 */
    volatile uint32_t TOR0;       /* 0x044: Tx overrun 0 */
    volatile uint32_t RFCR0;      /* 0x048: Rx FIFO ctrl 0 */
    volatile uint32_t TFCR0;      /* 0x04C: Tx FIFO ctrl 0 */
    volatile uint32_t RFF0;       /* 0x050: Rx FIFO level 0 */
    volatile uint32_t TFF0;       /* 0x054: Tx FIFO level 0 */
    uint32_t _RSV0[0x1C0/4 - 0x058/4]; /* 0x058 .. 0x1BF */
    volatile uint32_t RXDMA;      /* 0x1C0: Rx DMA data port */
    volatile uint32_t RRXDMA;     /* 0x1C4: Rx DMA alt? */
    volatile uint32_t TXDMA;      /* 0x1C8: Tx DMA data port */
    volatile uint32_t RTXDMA;     /* 0x1CC: Tx DMA alt? */
    uint32_t _RSV1[0x1F0/4 - 0x1D0/4]; /* 0x1D0 .. 0x1EF */
    volatile uint32_t COMP_PARA2; /* 0x1F0 */
    volatile uint32_t COMP_PARA1; /* 0x1F4 */
    volatile uint32_t COMP_VERSION; /* 0x1F8 */
    volatile uint32_t COMP_TYPE;  /* 0x1FC */
    volatile uint32_t DMACR;      /* 0x200: DMA control */
    volatile uint32_t RXDMA_CH0;  /* 0x204: Rx DMA channel select 0 */
    uint32_t _RSV2[0x214/4 - 0x208/4];
    volatile uint32_t TXDMA_CH0;  /* 0x214: Tx DMA channel select 0 */
    uint32_t _RSV3[0x224/4 - 0x218/4];
    volatile uint32_t RSLOT0;     /* 0x224: Rx slot 0 */
    /* TSLOT0 shares the same address as RSLOT0 in legacy macros */
} S300_I2S_TypeDef;

#define I2S0 ((S300_I2S_TypeDef *)I2S0_BASE)
#define I2S1 ((S300_I2S_TypeDef *)I2S1_BASE)

#ifdef __cplusplus
}
#endif

#endif /* S300_I2S_S300_H */
