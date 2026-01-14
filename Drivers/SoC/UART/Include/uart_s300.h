#ifndef S300_UART_S300_H
#define S300_UART_S300_H

#ifdef __cplusplus
extern "C" {
#endif

#include "s300.h"

/* CMSIS-style UART register map (DW_apb_uart-like) */
typedef struct
{
    volatile uint32_t RBR_THR_DLL;   /* 0x00 RBR/THR/DLL */
    volatile uint32_t IER_DLH;       /* 0x04 IER/DLH     */
    volatile uint32_t IIR_FCR;       /* 0x08 IIR/FCR     */
    volatile uint32_t LCR;           /* 0x0C */
    volatile uint32_t MCR;           /* 0x10 */
    volatile uint32_t LSR;           /* 0x14 */
    volatile uint32_t MSR;           /* 0x18 */
    volatile uint32_t SCR;           /* 0x1C */
    volatile uint32_t LPDLL;         /* 0x20 */
    volatile uint32_t LPDLH;         /* 0x24 */
    uint32_t RESERVED0[2];           /* 0x28,0x2C */
    volatile uint32_t SRBR_STHR[16]; /* 0x30..0x6C */
    volatile uint32_t FAR;           /* 0x70 */
    volatile uint32_t TFR;           /* 0x74 */
    volatile uint32_t RFW;           /* 0x78 */
    volatile uint32_t USR;           /* 0x7C */
    volatile uint32_t TFL;           /* 0x80 */
    volatile uint32_t RFL;           /* 0x84 */
    volatile uint32_t SRR;           /* 0x88 */
    volatile uint32_t SRTS;          /* 0x8C */
    volatile uint32_t SBCR;          /* 0x90 */
    volatile uint32_t SDMAM;         /* 0x94 */
    volatile uint32_t SFE;           /* 0x98 */
    volatile uint32_t SRT;           /* 0x9C */
    volatile uint32_t STET;          /* 0xA0 */
    volatile uint32_t HTX;           /* 0xA4 */
    volatile uint32_t DMASA;         /* 0xA8 */
    volatile uint32_t TCR;           /* 0xAC */
    volatile uint32_t DE_EN;         /* 0xB0 */
    volatile uint32_t RE_EN;         /* 0xB4 */
    volatile uint32_t DET;           /* 0xB8 */
    volatile uint32_t TAT;           /* 0xBC */
    volatile uint32_t DLF;           /* 0xC0 */
    volatile uint32_t RAR;           /* 0xC4 */
    volatile uint32_t TAR;           /* 0xC8 */
    volatile uint32_t LCR_EXT;       /* 0xCC */
    volatile uint32_t REG_TIMEOUT_RST; /* 0xD4 */
    volatile uint32_t CPR;           /* 0xF4 */
    volatile uint32_t UCV;           /* 0xF8 */
    volatile uint32_t CTR;           /* 0xFC */
} S300_UART_TypeDef;

#define UART0        ((S300_UART_TypeDef *)UART0_BASE)
#define UART1        ((S300_UART_TypeDef *)UART1_BASE)
#define UART2        ((S300_UART_TypeDef *)UART2_BASE)
#define UART3        ((S300_UART_TypeDef *)UART3_BASE)

#ifdef __cplusplus
}
#endif

#endif /* S300_UART_S300_H */
