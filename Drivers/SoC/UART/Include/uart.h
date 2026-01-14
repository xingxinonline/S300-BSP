#ifndef S300_BSP_UART_H
#define S300_BSP_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "uart_s300.h"

/* Public enums compatible with legacy */
typedef enum
{
    UART_IDX0 = 0,
    UART_IDX1 = 1,
    UART_IDX2 = 2,
    UART_IDX3 = 3,
} uart_idx_t;

typedef enum
{
    UARTTYPE_STD_SERIAL = 0x00000000,
    UARTTYPE_BIT9_SERIAL = 0x00000002,
    UARTTYPE_RS485_SERIAL = 0x00000004,
    UARTTYPE_IRDA_SERIAL = 0x00000008,
} uart_type_t;

typedef enum
{
    UART_FIFO_RX_1B = 0x00,
    UART_FIFO_RX_1_4 = 0x40,
    UART_FIFO_RX_1_2 = 0x80,
    UART_FIFO_RX_2 = 0xC0,
    UART_FIFO_TX_NULL = 0x00,
    UART_FIFO_TX_2 = 0x10,
    UART_FIFO_TX_1_4 = 0x20,
    UART_FIFO_TX_1_2 = 0x30,
    UART_FIFO_DMAMODE = 0x08,
    UART_FIFO_TX_RESET = 0x04,
    UART_FIFO_RX_RESET = 0x02,
    UART_FIFO_EN = 0x01,
} uart_fifo_t;

/* API */
int  uart_init(uart_idx_t idx, uart_type_t type, uint32_t sysclock_hz, uint32_t baud);
void uart_set_baud(uart_idx_t idx, uint32_t sysclock_hz, uint32_t baud);
void uart_set_interrupt(uart_idx_t idx, bool tx, bool rx);
void uart_set_property(uart_idx_t idx, uint8_t datalen, uint8_t stop, uint8_t parity);
void uart_set_sir_mode(uart_idx_t idx, bool en);
void uart_set_fifo(uart_idx_t idx, uart_fifo_t cfg);
uint16_t uart_read(uart_idx_t idx, uart_type_t type);
int  uart_write(uart_idx_t idx, uart_type_t type, uint16_t data);

/* Legacy inline aliases to ease porting (optional) */
static inline int init_uart(int u, int t, uint32_t s, uint32_t b)
{
    return uart_init((uart_idx_t)u, (uart_type_t)t, s, b);
}
static inline void set_uart_baud(int u, uint32_t s, uint32_t b)
{
    uart_set_baud((uart_idx_t)u, s, b);
}
static inline void set_uart_interrupt(int u, uint8_t tx, uint8_t rx)
{
    uart_set_interrupt((uart_idx_t)u, tx, rx);
}
static inline void set_uart_property(int u, uint8_t d, uint8_t st, uint8_t p)
{
    uart_set_property((uart_idx_t)u, d, st, p);
}
static inline void set_uart_sir_mode(int u, uint8_t en)
{
    uart_set_sir_mode((uart_idx_t)u, en);
}
static inline void set_uart_fifo(int u, uint32_t pro)
{
    uart_set_fifo((uart_idx_t)u, (uart_fifo_t)pro);
}
static inline uint16_t read_uart(int u, int t)
{
    return uart_read((uart_idx_t)u, (uart_type_t)t);
}
static inline int write_uart(int u, int t, uint16_t d)
{
    return uart_write((uart_idx_t)u, (uart_type_t)t, d);
}

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_UART_H */
