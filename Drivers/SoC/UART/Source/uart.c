#include <stdint.h>
#include <stdbool.h>
#include "../Include/uart.h"
#include "../Include/uart_s300.h"

static inline S300_UART_TypeDef *uart_dev(uart_idx_t idx)
{
    switch (idx)
    {
    case UART_IDX0:
        return UART0;
    case UART_IDX1:
        return UART1;
    case UART_IDX2:
        return UART2;
    default:
        return UART3;
    }
}

static inline void uart_wait_idle(S300_UART_TypeDef *U)
{
    while (U->USR & 1u) {}
}

int uart_init(uart_idx_t idx, uart_type_t type, uint32_t sysclock_hz, uint32_t baud)
{
    S300_UART_TypeDef *U = uart_dev(idx);
    volatile uint32_t tmp;
    /* Soft reset & clear */
    U->LCR = 0x0u;
    tmp = U->RBR_THR_DLL;
    (void)tmp;
    tmp = U->LSR;
    (void)tmp;
    tmp = U->MSR;
    (void)tmp;
    uart_wait_idle(U);
    U->IIR_FCR = 0x0u;
    U->LCR_EXT = 0x0u;
    U->TCR = 0x0u;
    U->IER_DLH = 0x0u;
    U->HTX = 0x0u;
    U->DMASA = 0x0u;
    uart_set_property(idx, 3u, 0u, 0u); /* 8N1 */
    if (type & UARTTYPE_RS485_SERIAL)
    {
        U->TCR = (0u << 3) | (1u << 2) | (1u << 1) | 1u;
        U->DET = (2u << 16) | 2u;
        U->TAT = (0u << 16) | 0u;
    }
    if (type & UARTTYPE_IRDA_SERIAL)
    {
        U->MCR |= 0x40u;
    }
    uart_set_fifo(idx, (uart_fifo_t)(0x06u | 0x01u));
    uart_set_baud(idx, sysclock_hz, baud);
    uart_set_interrupt(idx, false, false);
    (void)sysclock_hz;
    return 0;
}

void uart_set_baud(uart_idx_t idx, uint32_t sysclock_hz, uint32_t baud)
{
    S300_UART_TypeDef *U = uart_dev(idx);
    uart_wait_idle(U);
    U->LCR |= 0x80u; /* DLAB */
    uint32_t div = sysclock_hz / (16u * baud);
    U->RBR_THR_DLL = div & 0xFFu;          /* DLL */
    U->IER_DLH = (U->IER_DLH & ~0xFFu) | ((div >> 8) & 0xFFu); /* DLH */
    U->LPDLL = div & 0xFFu;                /* Low power divisor low */
    U->LPDLH = (div >> 8) & 0xFFu;         /* Low power divisor high */
    uint32_t rem = sysclock_hz - div * (16u * baud);
    uint32_t dlf = rem / baud;             /* same as legacy: (sys - div*16*baud)/baud */
    U->DLF = dlf & 0xFFu;                  /* Only low 8 bits used in legacy macros */
    U->LCR &= ~0x80u; /* clear DLAB */
    (void)U->LCR;
    uart_wait_idle(U);
}

void uart_set_interrupt(uart_idx_t idx, bool tx, bool rx)
{
    S300_UART_TypeDef *U = uart_dev(idx);
    volatile uint32_t tmp;
    tmp = U->RBR_THR_DLL;
    (void)tmp;
    tmp = U->LSR;
    (void)tmp;
    tmp = U->MSR;
    (void)tmp;
    uart_wait_idle(U);
    uint32_t ier = 0u;
    if (tx) ier |= 0x02u;
    if (rx) ier |= 0x15u;
    U->IER_DLH = (U->IER_DLH & ~0xFFu) | ier;
    tmp = U->IIR_FCR;
    (void)tmp;
}

void uart_set_property(uart_idx_t idx, uint8_t datalen, uint8_t stop, uint8_t parity)
{
    S300_UART_TypeDef *U = uart_dev(idx);
    U->LCR = (datalen & 0x3u) | stop | parity;
}

void uart_set_sir_mode(uart_idx_t idx, bool en)
{
    S300_UART_TypeDef *U = uart_dev(idx);
    uint32_t v = U->MCR;
    if (en) v |= 0x40u;
    else v &= ~0x40u;
    U->MCR = v;
}

void uart_set_fifo(uart_idx_t idx, uart_fifo_t cfg)
{
    S300_UART_TypeDef *U = uart_dev(idx);
    U->IIR_FCR = (uint32_t)cfg;
}

uint16_t uart_read(uart_idx_t idx, uart_type_t type)
{
    S300_UART_TypeDef *U = uart_dev(idx);
    if (type == UARTTYPE_RS485_SERIAL)
    {
        U->DE_EN = 0u;
        U->RE_EN = 1u;
    }
    /* Wait until RX FIFO not empty, then read from RBR (works for both FIFO/non-FIFO) */
    while (!(U->USR & 0x8u)) {}
    return (uint16_t)U->RBR_THR_DLL;
}

int uart_write(uart_idx_t idx, uart_type_t type, uint16_t data)
{
    S300_UART_TypeDef *U = uart_dev(idx);
    if (type == UARTTYPE_RS485_SERIAL)
    {
        U->RE_EN = 0u;
        U->DE_EN = 1u;
    }
    /* Wait until TX FIFO empty (or choose TFNF for higher throughput), then write THR */
    while (!(U->USR & 0x4u)) {}
    U->RBR_THR_DLL = data;
    (void)type;
    return 0;
}
