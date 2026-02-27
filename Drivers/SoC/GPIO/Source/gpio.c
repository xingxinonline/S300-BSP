#include <stdint.h>
#include <stdbool.h>
#include "../Include/gpio.h"
#include "../Include/gpio_s300.h"

#define PIN_MASK(n)   (1u << (n))

int gpio_set_function(gpio_port_t port, uint8_t pin, gpio_func_t func)
{
    (void)port; /* Only GPIOA exposed */
    /* S300 IO_MATRIX: 2 bits per GPIO, 16 GPIOs per 32-bit register */
    uint32_t lin = (uint32_t)port * 32u + (uint32_t)pin;
    uint32_t idx = (lin << 1) / 32u;   /* Register index: lin*2 / 32 = lin / 16 */
    uint32_t sh  = (lin << 1) % 32u;   /* Bit shift: (lin*2) % 32 */
    uint32_t v = IO_MATRIX->CFG[idx];
    v &= ~(0x3u << sh);                /* Clear 2 bits */
    v |= ((uint32_t)func & 0x3u) << sh; /* Set 2 bits */
    IO_MATRIX->CFG[idx] = v;
    return 0;
}

int gpio_set_mode(gpio_port_t port, uint8_t pin, uint32_t mode_raw)
{
    (void)port;
    if (mode_raw <= GPIO_UP)
    {
        uint32_t lin = (uint32_t)port * 32u + (uint32_t)pin;
        uint32_t idx = (lin << 1) / 32u;
        uint32_t sh  = (lin << 1) % 32u;
        uint32_t v = IO_MUX->CFG[idx];
        v &= ~(0x3u << sh);
        v |= (mode_raw & 0x3u) << sh;
        IO_MUX->CFG[idx] = v;
        return 0;
    }
    /* For special pad groups (POR/PSRAM/FLASH/SDIO/GMII), keep raw encoding compatible with legacy */
    if (mode_raw >= PORESETN_DOWN && mode_raw <= GMII_MSC_UP)
    {
        /* Legacy encodings used fixed indices: 3,10,11,12,13,14, apply same mapping as old code */
        uint32_t idx = 0u, sh = 0u, index_bit = mode_raw & 1u, base = 0u;
        if (mode_raw >= PORESETN_DOWN && mode_raw <= DVP_O_SDO_UP)
        {
            base = 3u;
            sh = (((mode_raw - 2u) & ~1u) >> 1);
            idx = base;
        }
        else if (mode_raw >= PSRAM_PD_DOWN && mode_raw <= PSRAM_MSC_UP)
        {
            base = 10u;
            sh = (((mode_raw - 0x40u) & ~1u) >> 1);
            idx = base;
        }
        else if (mode_raw >= FLASH_PD_DOWN && mode_raw <= FLASH_MSC_UP)
        {
            base = 11u;
            sh = (((mode_raw - 0x60u) & ~1u) >> 1);
            idx = base;
        }
        else if (mode_raw >= SDIO0_PD_DOWN && mode_raw <= SDIO0_MSC_UP)
        {
            base = 12u;
            sh = (((mode_raw - 0x80u) & ~1u) >> 1);
            idx = base;
        }
        else if (mode_raw >= SDIO1_PD_DOWN && mode_raw <= SDIO1_MSC_UP)
        {
            base = 13u;
            sh = (((mode_raw - 0xA0u) & ~1u) >> 1);
            idx = base;
        }
        else if (mode_raw >= GMII_PD_DOWN && mode_raw <= GMII_MSC_UP)
        {
            base = 14u;
            sh = (((mode_raw - 0xC0u) & ~1u) >> 1);
            idx = base;
        }
        else
        {
            return -1; /* unknown encoding */
        }
        uint32_t v = IO_MUX->CFG[idx];
        v &= ~(0x3u << sh);
        v |= (index_bit << sh);
        IO_MUX->CFG[idx] = v;
        return 0;
    }
    return -1;
}

uint32_t gpio_get_port_numbers(gpio_port_t port)
{
    (void)port;
    return ((GPIO->CONFIG_REG2 >> ((uint32_t)port * 5u)) & 0x1Fu) + 1u;
}

int gpio_set_data(gpio_port_t port, uint8_t pin, uint32_t value)
{
    (void)port;
    uint32_t mask = PIN_MASK(pin);
    uint32_t v;
    if (pin == 0xFFu)
    {
        v = value;
    }
    else
    {
        v = GPIO->SWPORTA_DR;
        v = (v & ~mask) | (value ? mask : 0u);
    }
    GPIO->SWPORTA_DR = v;
    return 0;
}

uint32_t gpio_get_data(gpio_port_t port)
{
    (void)port;
    return GPIO->EXT_PORTA;
}

bool gpio_get_value(gpio_port_t port, uint32_t pin)
{
    (void)port;
    return (GPIO->EXT_PORTA & PIN_MASK(pin)) != 0u;
}

int gpio_set_direction(gpio_port_t port, uint8_t pin, uint32_t is_output)
{
    (void)port;
    uint32_t mask = PIN_MASK(pin);
    uint32_t v = (pin == 0xFFu) ? is_output : ((GPIO->SWPORTA_DDR & ~mask) | (is_output ? mask : 0u));
    GPIO->SWPORTA_DDR = v;
    return 0;
}

int gpio_set_soft_mode(gpio_port_t port, bool issoft)
{
    (void)port;
    GPIO->SWPORTA_CTL = issoft ? 0u : 1u;
    return 0;
}

void gpio_interrupt_clear(gpio_port_t port, uint8_t pin)
{
    (void)port;
    GPIO->PORTA_EOI = PIN_MASK(pin);
}

void gpio_set_interrupt(gpio_port_t port, uint8_t pin, gpio_int_type_t type, bool en)
{
    (void)port;
    uint32_t mask = PIN_MASK(pin);
    GPIO->INTTYPE_LEVEL = (GPIO->INTTYPE_LEVEL & ~mask) | ((type & 0x10u) ? mask : 0u);
    GPIO->INT_POLARITY  = (GPIO->INT_POLARITY  & ~mask) | ((type & 0x01u) ? mask : 0u);
    GPIO->INTMASK      |= mask;
    GPIO->INTEN        &= ~mask;
    if (en)
    {
        GPIO->INTEN |= mask;
        GPIO->INTMASK &= ~mask;
    }
}
