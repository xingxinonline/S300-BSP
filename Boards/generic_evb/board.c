#include "board.h"
#include "rcc.h"
#include "gpio.h"
#include "uart.h"
#include <stdio.h>

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
 * @brief 配置调试 UART 引脚复用
 */
static void board_uart_pins_init(void)
{
    set_gpio_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_TX_PIN, BOARD_DEBUG_UART_FUNCTION);
    set_gpio_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_RX_PIN, BOARD_DEBUG_UART_FUNCTION);
}

void board_clock_init(void)
{
    /* 切到 CM4 PLL：与参考配置一致（192MHz） */
    (void)init_cortex_m4_pll(6, 768, 0, 4, 2);
    /* 可选：保持 APB0/APB1 分频为 0（不分频），确保 APB=SYS */
    // set_apb_clock_div(0, 0);
    // set_apb_clock_div(1, 0);
    SystemCoreClockUpdate();
}

void board_debug_uart_init(void)
{
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
        default:
            set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
            break;
    }

    board_uart_pins_init();
    init_uart(BOARD_DEBUG_UART_IDX, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), BOARD_DEBUG_UART_BAUDRATE);
    
    // 关闭缓冲
    setvbuf(stdout, NULL, _IONBF, 0);
}

void board_init(void)
{
    board_clock_init();
    board_gpio_clock_init();  /* GPIO 时钟需在引脚配置之前使能 */
    board_debug_uart_init();
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
#endif /* BOARD_CAMERA_ENABLE */

#if BOARD_LCD_BL_ENABLE && (BOARD_LCD_BL_PIN != 0xFF)
void board_lcd_backlight_pin_init(void)
{
    set_gpio_function(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, FUNCTION_2);
    set_gpio_mode(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, GPIO_DOWN);
    set_gpio_direction(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, 1);  /* Output */
}
#endif /* BOARD_LCD_BL_ENABLE */
