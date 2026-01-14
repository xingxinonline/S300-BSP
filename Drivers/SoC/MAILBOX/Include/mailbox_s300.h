#ifndef S300_MAILBOX_S300_H
#define S300_MAILBOX_S300_H

#ifdef __cplusplus
extern "C" {
#endif

#include "s300.h"

/* CMSIS-style MAILBOX register map based on programming guide offsets */
typedef struct
{
    volatile uint32_t WRDATA;   /* 0x00: Write data FIFO */
    volatile uint32_t RESERVED1;/* 0x04: reserved */
    volatile uint32_t RDDATA;   /* 0x08: Read data FIFO */
    volatile uint32_t RESERVED2;/* 0x0C: reserved */
    volatile uint32_t STATUS;   /* 0x10: Status */
    volatile uint32_t ERROR;    /* 0x14: Error */
    volatile uint32_t SIT;      /* 0x18: Send interrupt threshold */
    volatile uint32_t RIT;      /* 0x1C: Receive interrupt threshold */
    volatile uint32_t IS;       /* 0x20: Interrupt status (write-1-to-clear) */
    volatile uint32_t IE;       /* 0x24: Interrupt enable */
    volatile uint32_t IP;       /* 0x28: Interrupt pending */
    volatile uint32_t CTRL;     /* 0x2C: Control (FIFO clear) */
} S300_MAILBOX_TypeDef;

#define MAILBOX0        ((S300_MAILBOX_TypeDef *)MAILBOX_BASE)
#define DSP_MAILBOX0    ((S300_MAILBOX_TypeDef *)DSP_MAILBOX_BASE)

/* Bitfields helpers (kept simple as the HW uses single-bit flags) */
#define MAILBOX_STATUS_EMPTY_Pos   0u
#define MAILBOX_STATUS_EMPTY_Msk   (1u << MAILBOX_STATUS_EMPTY_Pos)
#define MAILBOX_STATUS_FULL_Pos    1u
#define MAILBOX_STATUS_FULL_Msk    (1u << MAILBOX_STATUS_FULL_Pos)
#define MAILBOX_STATUS_TXTA_Pos    2u
#define MAILBOX_STATUS_TXTA_Msk    (1u << MAILBOX_STATUS_TXTA_Pos)
#define MAILBOX_STATUS_RXTA_Pos    3u
#define MAILBOX_STATUS_RXTA_Msk    (1u << MAILBOX_STATUS_RXTA_Pos)

#define MAILBOX_ERR_EMPTY_Pos      0u
#define MAILBOX_ERR_EMPTY_Msk      (1u << MAILBOX_ERR_EMPTY_Pos)
#define MAILBOX_ERR_FULL_Pos       1u
#define MAILBOX_ERR_FULL_Msk       (1u << MAILBOX_ERR_FULL_Pos)

#define MAILBOX_IE_SIT_Pos         0u
#define MAILBOX_IE_RIT_Pos         1u
#define MAILBOX_IE_ERR_Pos         2u
#define MAILBOX_IE_SIT_Msk         (1u << MAILBOX_IE_SIT_Pos)
#define MAILBOX_IE_RIT_Msk         (1u << MAILBOX_IE_RIT_Pos)
#define MAILBOX_IE_ERR_Msk         (1u << MAILBOX_IE_ERR_Pos)

#define MAILBOX_CTRL_CLR_TX_Pos    0u
#define MAILBOX_CTRL_CLR_RX_Pos    1u
#define MAILBOX_CTRL_CLR_TX_Msk    (1u << MAILBOX_CTRL_CLR_TX_Pos)
#define MAILBOX_CTRL_CLR_RX_Msk    (1u << MAILBOX_CTRL_CLR_RX_Pos)

#ifdef __cplusplus
}
#endif

#endif /* S300_MAILBOX_S300_H */
