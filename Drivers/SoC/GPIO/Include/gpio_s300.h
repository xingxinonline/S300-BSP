#ifndef S300_GPIO_S300_H
#define S300_GPIO_S300_H

#ifdef __cplusplus
extern "C" {
#endif

#include "s300.h"

/* GPIO register map (DW_apb_gpio-like) */
typedef struct
{
    volatile uint32_t SWPORTA_DR;     /* 0x00 */
    volatile uint32_t SWPORTA_DDR;    /* 0x04 */
    volatile uint32_t SWPORTA_CTL;    /* 0x08 */
    volatile uint32_t SWPORTB_DR;     /* 0x0C */
    volatile uint32_t SWPORTB_DDR;    /* 0x10 */
    volatile uint32_t SWPORTB_CTL;    /* 0x14 */
    volatile uint32_t SWPORTC_DR;     /* 0x18 */
    volatile uint32_t SWPORTC_DDR;    /* 0x1C */
    volatile uint32_t SWPORTC_CTL;    /* 0x20 */
    volatile uint32_t SWPORTD_DR;     /* 0x24 */
    volatile uint32_t SWPORTD_DDR;    /* 0x28 */
    volatile uint32_t SWPORTD_CTL;    /* 0x2C */
    volatile uint32_t INTEN;          /* 0x30 */
    volatile uint32_t INTMASK;        /* 0x34 */
    volatile uint32_t INTTYPE_LEVEL;  /* 0x38 */
    volatile uint32_t INT_POLARITY;   /* 0x3C */
    volatile uint32_t INTSTATUS;      /* 0x40 */
    volatile uint32_t RAW_INTSTATUS;  /* 0x44 */
    volatile uint32_t DEBOUNCE;       /* 0x48 */
    volatile uint32_t PORTA_EOI;      /* 0x4C */
    volatile uint32_t EXT_PORTA;      /* 0x50 */
    volatile uint32_t EXT_PORTB;      /* 0x54 */
    volatile uint32_t EXT_PORTC;      /* 0x58 */
    volatile uint32_t EXT_PORTD;      /* 0x5C */
    volatile uint32_t LS_SYNC;        /* 0x60 */
    volatile uint32_t ID_CODE;        /* 0x64 */
    volatile uint32_t RESERVED_68;    /* 0x68 */
    volatile uint32_t VER_ID_CODE;    /* 0x6C */
    volatile uint32_t CONFIG_REG2;    /* 0x70 */
    volatile uint32_t CONFIG_REG1;    /* 0x74 */
} S300_GPIO_TypeDef;

#define GPIO        ((S300_GPIO_TypeDef *)GPIO_BASE)

/* IO MATRIX / MUX simple views */
typedef struct
{
    volatile uint32_t CFG[0x100 / 4]; /* Enough for IO_MATRIX and IO_MUX ranges we use */
} S300_IO_REG_ARRAY;

#define IO_MATRIX   ((S300_IO_REG_ARRAY *)IO_MATRIX_BASE)
#define IO_MUX      ((S300_IO_REG_ARRAY *)IO_MUX_BASE)

#ifdef __cplusplus
}
#endif

#endif /* S300_GPIO_S300_H */
