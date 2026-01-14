#ifndef S300_BSP_GPIO_H
#define S300_BSP_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "gpio_s300.h"

/* Public enums aligned with legacy values/subsets */
typedef enum { GPIOA = 0 } gpio_port_t;

typedef enum { FUNCTION_0, FUNCTION_1, FUNCTION_2, FUNCTION_3 } gpio_func_t;

/* Legacy-compatible IO_MUX modes (copied to preserve values/order) */
typedef enum
{
    GPIO_DOWN,
    GPIO_UP = 0x1,

    PORESETN_DOWN,
    PORESETN_UP,
    NTRST_DOWN,
    NTRST_UP,
    NRST_DOWN,
    NRST_UP,
    SWCLKTCK_DOWN,
    SWCLKTCK_UP,
    TDI_DOWN,
    TDI_UP,
    TDO_DOWN,
    TDO_UP,
    SWV_DOWN,
    SWV_UP,
    SWDIOTMS_DOWN,
    SWDIOTMS_UP,
    DSP_TCK_DOWN,
    DSP_TCK_UP,
    DSP_TMS_DOWN,
    DSP_TMS_UP,
    DSP_TDI_DOWN,
    DSP_TDI_UP,
    DSP_TDO_DOWN,
    DSP_TDO_UP,
    DVP_DATA0_DOWN,
    DVP_DATA0_UP,
    DVP_DATA1_DOWN,
    DVP_DATA1_UP,
    DVP_DATA2_DOWN,
    DVP_DATA2_UP,
    DVP_DATA3_DOWN,
    DVP_DATA3_UP,
    DVP_DATA4_DOWN,
    DVP_DATA4_UP,
    DVP_DATA5_DOWN,
    DVP_DATA5_UP,
    DVP_DATA6_DOWN,
    DVP_DATA6_UP,
    DVP_DATA7_DOWN,
    DVP_DATA7_UP,
    DVP_DATA8_DOWN,
    DVP_DATA8_UP,
    DVP_DATA9_DOWN,
    DVP_DATA9_UP,
    DVP_HSYNC_DOWN,
    DVP_HSYNC_UP,
    DVP_PCLK_DOWN,
    DVP_PCLK_UP,
    DVP_VSYNC_DOWN,
    DVP_VSYNC_UP,
    DVP_RSTN_DOWN,
    DVP_RSTN_UP,
    DVP_O_CS_N_DOWN,
    DVP_O_CS_N_UP,
    DVP_O_DC_DOWN,
    DVP_O_DC_UP,
    DVP_O_RSTN_DOWN,
    DVP_O_RSTN_UP,
    DVP_O_SCK_DOWN,
    DVP_O_SCK_UP,
    DVP_O_SDO_DOWN,
    DVP_O_SDO_UP,

    PSRAM_PD_DOWN,
    PSRAM_PD_UP,
    PSRAM_PU_DOWN,
    PSRAM_PU_UP,
    PSRAM_DS0_DOWN,
    PSRAM_DS0_UP,
    PSRAM_DS1_DOWN,
    PSRAM_DS1_UP,
    PSRAM_DS2_DOWN,
    PSRAM_DS2_UP,
    PSRAM_ST_DOWN,
    PSRAM_ST_UP,
    PSRAM_SL_DOWN,
    PSRAM_SL_UP,
    PSRAM_MSC_DOWN,
    PSRAM_MSC_UP,

    FLASH_PD_DOWN,
    FLASH_PD_UP,
    FLASH_PU_DOWN,
    FLASH_PU_UP,
    FLASH_DS0_DOWN,
    FLASH_DS0_UP,
    FLASH_DS1_DOWN,
    FLASH_DS1_UP,
    FLASH_DS2_DOWN,
    FLASH_DS2_UP,
    FLASH_ST_DOWN,
    FLASH_ST_UP,
    FLASH_SL_DOWN,
    FLASH_SL_UP,
    FLASH_MSC_DOWN,
    FLASH_MSC_UP,

    SDIO0_PD_DOWN,
    SDIO0_PD_UP,
    SDIO0_PU_DOWN,
    SDIO0_PU_UP,
    SDIO0_DS0_DOWN,
    SDIO0_DS0_UP,
    SDIO0_DS1_DOWN,
    SDIO0_DS1_UP,
    SDIO0_DS2_DOWN,
    SDIO0_DS2_UP,
    SDIO0_ST_DOWN,
    SDIO0_ST_UP,
    SDIO0_SL_DOWN,
    SDIO0_SL_UP,
    SDIO0_MSC_DOWN,
    SDIO0_MSC_UP,

    SDIO1_PD_DOWN,
    SDIO1_PD_UP,
    SDIO1_PU_DOWN,
    SDIO1_PU_UP,
    SDIO1_DS0_DOWN,
    SDIO1_DS0_UP,
    SDIO1_DS1_DOWN,
    SDIO1_DS1_UP,
    SDIO1_DS2_DOWN,
    SDIO1_DS2_UP,
    SDIO1_ST_DOWN,
    SDIO1_ST_UP,
    SDIO1_SL_DOWN,
    SDIO1_SL_UP,
    SDIO1_MSC_DOWN,
    SDIO1_MSC_UP,

    GMII_PD_DOWN,
    GMII_PD_UP,
    GMII_PU_DOWN,
    GMII_PU_UP,
    GMII_DS0_DOWN,
    GMII_DS0_UP,
    GMII_DS1_DOWN,
    GMII_DS1_UP,
    GMII_DS2_DOWN,
    GMII_DS2_UP,
    GMII_ST_DOWN,
    GMII_ST_UP,
    GMII_SL_DOWN,
    GMII_SL_UP,
    GMII_MSC_DOWN,
    GMII_MSC_UP,
} gpio_mode_t;

typedef enum
{
    INTERRUPT_LEVEL_LOW    = 0x00,
    INTERRUPT_LEVEL_HIGH   = 0x01,
    INTERRUPT_EDGE_FALLING = 0x10,
    INTERRUPT_EDGE_RISING  = 0x11
} gpio_int_type_t;

/* API */
int  gpio_set_function(gpio_port_t port, uint8_t pin, gpio_func_t func);
int  gpio_set_mode(gpio_port_t port, uint8_t pin, uint32_t mode_raw);
uint32_t gpio_get_port_numbers(gpio_port_t port);
int  gpio_set_data(gpio_port_t port, uint8_t pin, uint32_t value);
uint32_t gpio_get_data(gpio_port_t port);
bool gpio_get_value(gpio_port_t port, uint32_t pin);
int  gpio_set_direction(gpio_port_t port, uint8_t pin, uint32_t is_output);
int  gpio_set_soft_mode(gpio_port_t port, bool issoft);
void gpio_interrupt_clear(gpio_port_t port, uint8_t pin);
void gpio_set_interrupt(gpio_port_t port, uint8_t pin, gpio_int_type_t type, bool en);

/* Legacy aliases for painless porting */
static inline int set_gpio_function(int p, uint8_t pin, int f)
{
    return gpio_set_function((gpio_port_t)p, pin, (gpio_func_t)f);
}
static inline int set_gpio_mode(int p, uint8_t pin, uint32_t m)
{
    return gpio_set_mode((gpio_port_t)p, pin, m);
}
static inline uint32_t get_gpio_port_numbers(int p)
{
    return gpio_get_port_numbers((gpio_port_t)p);
}
static inline int set_gpio_data(int p, uint8_t pin, uint32_t v)
{
    return gpio_set_data((gpio_port_t)p, pin, v);
}
static inline uint32_t get_gpio_data(int p)
{
    return gpio_get_data((gpio_port_t)p);
}
static inline bool get_gpio_value(int p, uint32_t pin)
{
    return gpio_get_value((gpio_port_t)p, pin);
}
static inline int set_gpio_direction(int p, uint8_t pin, uint32_t o)
{
    return gpio_set_direction((gpio_port_t)p, pin, o);
}
static inline int set_gpio_soft_mode(int p, bool s)
{
    return gpio_set_soft_mode((gpio_port_t)p, s);
}
static inline void set_gpio_interrupt_clean(int p, uint8_t pin)
{
    gpio_interrupt_clear((gpio_port_t)p, pin);
}
static inline void set_gpio_interrupt(int p, uint8_t pin, int t, bool en)
{
    gpio_set_interrupt((gpio_port_t)p, pin, (gpio_int_type_t)t, en);
}

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_GPIO_H */
