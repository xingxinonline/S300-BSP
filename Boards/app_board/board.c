/**
 * @file    board.c
 * @brief   Board Support Package Implementation for S300 Application Board
 * @details 应用板初始化实现
 */

#include "board.h"
#include "rcc.h"
#include "gpio.h"
#include "uart.h"
#include <stdio.h>

#if BOARD_CAMERA_ENABLE
#include "i2c_soft.h"
#endif

/**
 * @brief 初始化 GPIO 时钟
 * 
 * 在所有 GPIO 操作之前调用，确保 GPIO 外设时钟已使能。
 * 这是板级初始化的基础步骤。
 */
static void board_gpio_clock_init(void)
{
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_GPIO, true);
}

/**
 * @brief 配置调试 UART 引脚复用 (UART1: PA16/PA17)
 */
static void board_uart_pins_init(void)
{
    set_gpio_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_TX_PIN, BOARD_DEBUG_UART_FUNCTION);
    set_gpio_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_RX_PIN, BOARD_DEBUG_UART_FUNCTION);
}

/**
 * @brief 配置 UART3 引脚复用 (PA26/PA27)
 */
#if BOARD_UART3_ENABLE
static void board_uart3_pins_init(void)
{
    set_gpio_function(BOARD_UART3_PORT, BOARD_UART3_TX_PIN, BOARD_UART3_FUNCTION);
    set_gpio_function(BOARD_UART3_PORT, BOARD_UART3_RX_PIN, BOARD_UART3_FUNCTION);
}
#endif

void board_clock_init(void)
{
    /* 切到 CM4 PLL：与参考配置一致（192MHz） */
    (void)init_cortex_m4_pll(6, 768, 0, 4, 2);
    SystemCoreClockUpdate();
}

void board_debug_uart_init(void)
{
    /* 开启 UART 时钟 */
    switch (BOARD_DEBUG_UART_IDX) {
        case 0:
            set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART0, true);
            break;
        case 1:
            set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART1, true);
            break;
        case 2:
            set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART2, true);
            break;
        case 3:
            set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
            break;
        default:
            set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART1, true);
            break;
    }

    board_uart_pins_init();
    init_uart(BOARD_DEBUG_UART_IDX, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), BOARD_DEBUG_UART_BAUDRATE);
    
    /* 关闭缓冲 */
    setvbuf(stdout, NULL, _IONBF, 0);
}

#if BOARD_UART3_ENABLE
void board_uart3_init(void)
{
    /* 开启 UART3 时钟 */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    
    board_uart3_pins_init();
    init_uart(3, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), BOARD_UART3_BAUDRATE);
}
#endif

void board_init(void)
{
    board_clock_init();
    board_gpio_clock_init();  /* GPIO 时钟需在引脚配置之前使能 */
    board_debug_uart_init();
    
#if BOARD_UART3_ENABLE
    board_uart3_init();
#endif
    
    __enable_irq();
}

/*===========================================================================
 * Optional Peripheral Pin Initialization Functions
 *===========================================================================*/

#if BOARD_CAMERA_ENABLE
void board_camera_i2c_pins_init(void)
{
    set_gpio_function(BOARD_CAMERA_I2C_PORT, BOARD_CAMERA_I2C_SCL_PIN, BOARD_CAMERA_I2C_FUNCTION);
    set_gpio_function(BOARD_CAMERA_I2C_PORT, BOARD_CAMERA_I2C_SDA_PIN, BOARD_CAMERA_I2C_FUNCTION);
}

void board_camera_ctrl_pins_init(void)
{
    /* PWDN Pin */
    set_gpio_function(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, BOARD_CAM_CTRL_FUNCTION);
    set_gpio_mode(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, GPIO_UP);
    set_gpio_direction(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 1);  /* Output */

    /* RST Pin */
    set_gpio_function(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, BOARD_CAM_CTRL_FUNCTION);
    set_gpio_mode(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, GPIO_UP);
    set_gpio_direction(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 1);   /* Output */
}

void board_camera_power_on_sequence(void)
{
    /* 摄像头上电时序: RST=0, PWDN=1 -> delay -> PWDN=0 -> delay -> RST=1 -> delay */
    gpio_set_data(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 0);
    gpio_set_data(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 1);
    
    /* 延时约 ~8ms @ 100MHz */
    for (volatile uint32_t i = 0; i < 800000u; i++) {
        __asm volatile("nop");
    }
    
    gpio_set_data(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 0);
    
    /* 延时约 ~8ms */
    for (volatile uint32_t i = 0; i < 800000u; i++) {
        __asm volatile("nop");
    }
    
    gpio_set_data(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 1);
    
    /* 延时约 ~24ms，等待摄像头就绪 */
    for (volatile uint32_t i = 0; i < 2400000u; i++) {
        __asm volatile("nop");
    }
}

int board_camera_i2c_init(void *i2c)
{
    return i2c_soft_init_default_idx(i2c, BOARD_CAMERA_I2C_IDX, BOARD_CAMERA_I2C_FREQ);
}
#endif /* BOARD_CAMERA_ENABLE */

#if BOARD_LCD_BL_ENABLE
void board_lcd_backlight_pin_init(void)
{
    set_gpio_function(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, FUNCTION_2);
    set_gpio_mode(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, GPIO_UP);
    set_gpio_direction(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, 1);  /* Output */
}

void board_lcd_backlight_on(void)
{
    /* 根据 BOARD_LCD_BL_ACTIVE_LEVEL 设置背光状态 */
#if BOARD_LCD_BL_ACTIVE_LEVEL
    set_gpio_data(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, 1);
#else
    set_gpio_data(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, 0);
#endif
}

void board_lcd_backlight_off(void)
{
    /* 根据 BOARD_LCD_BL_ACTIVE_LEVEL 设置背光状态 */
#if BOARD_LCD_BL_ACTIVE_LEVEL
    set_gpio_data(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, 0);
#else
    set_gpio_data(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, 1);
#endif
}
#endif /* BOARD_LCD_BL_ENABLE */
