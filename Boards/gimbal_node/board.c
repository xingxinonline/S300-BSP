/**
 * @file    board.c
 * @brief   Board Support Package Implementation for NE005 Card (mPCIE Sub-Board)
 * @details 芯天下智能云台项目子板 BSP 实现
 */

#include "board.h"
#include "gpio.h"
#include "uart.h"
#include "rcc.h"
#include <stdio.h>

#if __has_include("i2c_soft.h")
#include "i2c_soft.h"
#define HAS_I2C_SOFT 1
#else
#define HAS_I2C_SOFT 0
#endif

#if __has_include("i2c.h") && BOARD_I2C3_ENABLE
#include "i2c.h"
#define HAS_I2C_HW 1
#else
#define HAS_I2C_HW 0
#endif

#if __has_include("bus_servo.h")
#include "bus_servo.h"
#define HAS_BUS_SERVO 1
#else
#define HAS_BUS_SERVO 0
#endif

/*===========================================================================
 * Private Variables
 *===========================================================================*/

static bool g_board_initialized = false;

static void board_gpio_clock_init(void)
{
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_GPIO, true);
}

static void board_enable_uart_clock(uint8_t uart_idx)
{
    switch (uart_idx) {
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
}

/*===========================================================================
 * Clock Initialization
 *===========================================================================*/

int board_clock_init(void)
{
    (void)init_cortex_m4_pll(6, 768, 0, 4, 2);
    SystemCoreClockUpdate();
    return 0;
}

/*===========================================================================
 * Debug UART Initialization (UART3)
 *===========================================================================*/

int board_debug_uart_init(void)
{
#if BOARD_DEBUG_UART_ENABLE
    board_enable_uart_clock(BOARD_DEBUG_UART_IDX);
    set_gpio_function(GPIOA, BOARD_DEBUG_UART_TX_PIN, BOARD_DEBUG_UART_TX_PINMUX);
    set_gpio_function(GPIOA, BOARD_DEBUG_UART_RX_PIN, BOARD_DEBUG_UART_RX_PINMUX);
    init_uart(BOARD_DEBUG_UART_IDX, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), BOARD_DEBUG_UART_BAUDRATE);
    setvbuf(stdout, NULL, _IONBF, 0);
#else
    (void)0;
#endif
    return 0;
}

/*===========================================================================
 * Host Communication UART Initialization (UART2)
 *===========================================================================*/

int board_uart2_init(void)
{
#if BOARD_UART2_ENABLE
    board_enable_uart_clock(BOARD_UART2_IDX);
    set_gpio_function(GPIOA, BOARD_UART2_TX_PIN, BOARD_UART2_TX_PINMUX);
    set_gpio_function(GPIOA, BOARD_UART2_RX_PIN, BOARD_UART2_RX_PINMUX);
    init_uart(BOARD_UART2_IDX, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), BOARD_UART2_BAUDRATE);
#else
    (void)0;
#endif
    return 0;
}

/*===========================================================================
 * I2C3 Bus Initialization
 *===========================================================================*/

int board_i2c3_init(void)
{
#if BOARD_I2C3_ENABLE && HAS_I2C_HW
    set_gpio_function(GPIOA, BOARD_I2C3_SCL_PIN, BOARD_I2C3_SCL_PINMUX);
    set_gpio_mode(GPIOA, BOARD_I2C3_SCL_PIN, GPIO_UP);
    set_gpio_function(GPIOA, BOARD_I2C3_SDA_PIN, BOARD_I2C3_SDA_PINMUX);
    set_gpio_mode(GPIOA, BOARD_I2C3_SDA_PIN, GPIO_UP);

    set_cortex_m4_apb1_clock(RCC_CM4_APB1_I2C3, true);
    
    /* SDK风格初始化 */
    emI2CPRO pro = EM_I2C_MASTER | EM_I2C_RESTART_EN;
    if (BOARD_I2C3_SPEED <= 100000u) {
        pro |= EM_I2C_100K;
    } else if (BOARD_I2C3_SPEED <= 400000u) {
        pro |= EM_I2C_400K;
    } else {
        pro |= EM_I2C_HIGH;
    }
    return init_i2c(EM_I2C3, pro, 0, rcc_get_clock(RCC_CLOCK_APB1), BOARD_I2C3_SPEED);
#else
    return 0;
#endif
}

void board_camera_i2c_pins_init(void)
{
    set_gpio_function(GPIOA, BOARD_CAMERA_I2C_SCL_PIN, BOARD_CAMERA_I2C_FUNCTION);
    set_gpio_mode(GPIOA, BOARD_CAMERA_I2C_SCL_PIN, GPIO_UP);
    set_gpio_function(GPIOA, BOARD_CAMERA_I2C_SDA_PIN, BOARD_CAMERA_I2C_FUNCTION);
    set_gpio_mode(GPIOA, BOARD_CAMERA_I2C_SDA_PIN, GPIO_UP);
}

void board_camera_ctrl_pins_init(void)
{
    set_gpio_function(GPIOA, BOARD_CAM_RST_PIN, BOARD_CAM_CTRL_FUNCTION);
    set_gpio_mode(GPIOA, BOARD_CAM_RST_PIN, GPIO_UP);
    set_gpio_direction(GPIOA, BOARD_CAM_RST_PIN, 1);

    set_gpio_function(GPIOA, BOARD_CAM_PWDN_PIN, BOARD_CAM_CTRL_FUNCTION);
    set_gpio_mode(GPIOA, BOARD_CAM_PWDN_PIN, GPIO_UP);
    set_gpio_direction(GPIOA, BOARD_CAM_PWDN_PIN, 1);
}

void board_camera_power_on_sequence(void)
{
    set_gpio_data(GPIOA, BOARD_CAM_PWDN_PIN, 1);
    set_gpio_data(GPIOA, BOARD_CAM_RST_PIN, 0);
    for (volatile int i = 0; i < 100000; i++);

    set_gpio_data(GPIOA, BOARD_CAM_PWDN_PIN, 0);
    for (volatile int i = 0; i < 50000; i++);

    set_gpio_data(GPIOA, BOARD_CAM_RST_PIN, 1);
    for (volatile int i = 0; i < 100000; i++);
}

int board_camera_i2c_init(void *i2c)
{
    board_camera_i2c_pins_init();

#if HAS_I2C_SOFT
    if (i2c != NULL) {
        return i2c_soft_init_default_idx((i2c_soft_t *)i2c, 3, BOARD_CAMERA_I2C_FREQ);
    }
#else
    (void)i2c;
#endif

    return 0;
}

/*===========================================================================
 * Camera Interface Initialization
 *===========================================================================*/

int board_camera_init(void)
{
#if BOARD_CAMERA_ENABLE
    set_gpio_function(GPIOA, BOARD_DVP_DATA0_PIN, BOARD_DVP_DATA_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_DATA1_PIN, BOARD_DVP_DATA_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_DATA2_PIN, BOARD_DVP_DATA_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_DATA3_PIN, BOARD_DVP_DATA_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_DATA4_PIN, BOARD_DVP_DATA_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_DATA5_PIN, BOARD_DVP_DATA_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_DATA6_PIN, BOARD_DVP_DATA_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_DATA7_PIN, BOARD_DVP_DATA_PINMUX);

    set_gpio_function(GPIOA, BOARD_DVP_HSYNC_PIN, BOARD_DVP_CTRL_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_VSYNC_PIN, BOARD_DVP_CTRL_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_PCLK_PIN, BOARD_DVP_CTRL_PINMUX);
    set_gpio_function(GPIOA, BOARD_DVP_XCLK_PIN, BOARD_DVP_CTRL_PINMUX);

    set_gpio_function(GPIOA, BOARD_DVP_RSTN_PIN, FUNCTION_0);
    set_gpio_mode(GPIOA, BOARD_DVP_RSTN_PIN, GPIO_UP);
    set_gpio_direction(GPIOA, BOARD_DVP_RSTN_PIN, 1);
    set_gpio_data(GPIOA, BOARD_DVP_RSTN_PIN, 1);

    set_gpio_function(GPIOA, BOARD_DVP_PWDOWN_PIN, FUNCTION_0);
    set_gpio_mode(GPIOA, BOARD_DVP_PWDOWN_PIN, GPIO_UP);
    set_gpio_direction(GPIOA, BOARD_DVP_PWDOWN_PIN, 1);
    set_gpio_data(GPIOA, BOARD_DVP_PWDOWN_PIN, 0);
#endif

    return 0;
}

/*===========================================================================
 * Camera Power and Reset Control
 *===========================================================================*/

void board_camera_power(bool on)
{
#if BOARD_CAMERA_ENABLE
    set_gpio_data(GPIOA, BOARD_DVP_PWDOWN_PIN, on ? 0 : 1);
#else
    (void)on;
#endif
}

void board_camera_reset(void)
{
#if BOARD_CAMERA_ENABLE
    set_gpio_data(GPIOA, BOARD_DVP_RSTN_PIN, 0);
    for (volatile int i = 0; i < 100000; i++);
    set_gpio_data(GPIOA, BOARD_DVP_RSTN_PIN, 1);
    for (volatile int i = 0; i < 100000; i++);
#endif
}

/*===========================================================================
 * LED Control
 *===========================================================================*/

void board_led_init(void)
{
#if BOARD_LED_ENABLE
    set_gpio_function(GPIOA, BOARD_LED1_PIN, FUNCTION_0);
    set_gpio_mode(GPIOA, BOARD_LED1_PIN, GPIO_DOWN);
    set_gpio_direction(GPIOA, BOARD_LED1_PIN, 1);
    set_gpio_data(GPIOA, BOARD_LED1_PIN, 0);

    set_gpio_function(GPIOA, BOARD_LED2_PIN, FUNCTION_0);
    set_gpio_mode(GPIOA, BOARD_LED2_PIN, GPIO_DOWN);
    set_gpio_direction(GPIOA, BOARD_LED2_PIN, 1);
    set_gpio_data(GPIOA, BOARD_LED2_PIN, 0);
#endif
}

void board_led_set(uint8_t led_id, bool on)
{
#if BOARD_LED_ENABLE
    uint8_t pin;
    
    switch (led_id) {
        case 1:
            pin = BOARD_LED1_PIN;
            break;
        case 2:
            pin = BOARD_LED2_PIN;
            break;
        default:
            return;
    }
    
    set_gpio_data(GPIOA, pin, on ? 1 : 0);
#else
    (void)led_id;
    (void)on;
#endif
}

void board_led_toggle(uint8_t led_id)
{
#if BOARD_LED_ENABLE
    uint8_t pin;
    
    switch (led_id) {
        case 1:
            pin = BOARD_LED1_PIN;
            break;
        case 2:
            pin = BOARD_LED2_PIN;
            break;
        default:
            return;
    }
    
    set_gpio_data(GPIOA, pin, get_gpio_value(GPIOA, pin) ? 0 : 1);
#else
    (void)led_id;
#endif
}

void board_servo_pins_init(void)
{
    /* 兼容实现：沿用 UART3 + MOTO_BUSEN 作为舵机总线 */
    set_gpio_function(GPIOA, BOARD_DEBUG_UART_TX_PIN, BOARD_DEBUG_UART_FUNCTION);
    set_gpio_function(GPIOA, BOARD_DEBUG_UART_RX_PIN, BOARD_DEBUG_UART_FUNCTION);
    set_gpio_function(GPIOA, BOARD_MOTO_BUSEN_PIN, FUNCTION_0);
    set_gpio_direction(GPIOA, BOARD_MOTO_BUSEN_PIN, 1);
    set_gpio_data(GPIOA, BOARD_MOTO_BUSEN_PIN, 1);
}

int board_servo_init(void *servo)
{
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    board_servo_pins_init();

#if HAS_BUS_SERVO
    if (servo != NULL) {
        return bus_servo_init((bus_servo_t *)servo,
                              3,
                              BOARD_MOTO_BUSEN_PIN,
                              rcc_get_clock(RCC_CLOCK_APB1));
    }
#else
    (void)servo;
#endif

    return 0;
}

/*===========================================================================
 * PHY Initialization (RTL8211F)
 *===========================================================================*/

#if BOARD_RGMII_ENABLE

int board_phy_init(void)
{
    set_gpio_function(GPIOA, BOARD_PHY_MDC_PIN, 3);
    set_gpio_function(GPIOA, BOARD_PHY_MDIO_PIN, 3);
    set_gpio_mode(GPIOA, BOARD_PHY_MDIO_PIN, GPIO_UP);

    board_phy_reset();

    return 0;
}

void board_phy_reset(void)
{
    set_gpio_function(GPIOA, BOARD_PHY_RSTB_PIN, FUNCTION_0);
    set_gpio_mode(GPIOA, BOARD_PHY_RSTB_PIN, GPIO_UP);
    set_gpio_direction(GPIOA, BOARD_PHY_RSTB_PIN, 1);
    set_gpio_data(GPIOA, BOARD_PHY_RSTB_PIN, 0);
    for (volatile int i = 0; i < 100000; i++);
    set_gpio_data(GPIOA, BOARD_PHY_RSTB_PIN, 1);
    for (volatile int i = 0; i < 500000; i++);
}

#endif /* BOARD_RGMII_ENABLE */

/*===========================================================================
 * Board Main Initialization
 *===========================================================================*/

int board_init(void)
{
    int ret;
    
    if (g_board_initialized) {
        return 0;
    }
    
    /* 1. 初始化时钟系统 */
    ret = board_clock_init();
    if (ret != 0) {
        return ret;
    }
    
    board_gpio_clock_init();

    /* 2. 初始化调试串口 */
    ret = board_debug_uart_init();
    if (ret != 0) {
        return ret;
    }
    
    /* 3. 初始化 LED */
    board_led_init();
    
    /* 4. 初始化 I2C3 总线 */
    ret = board_i2c3_init();
    if (ret != 0) {
        return ret;
    }
    
    /* 5. 初始化主板通信串口 */
    ret = board_uart2_init();
    if (ret != 0) {
        return ret;
    }
    
    /* 6. 初始化摄像头接口 */
#if BOARD_CAMERA_ENABLE
    ret = board_camera_init();
    if (ret != 0) {
        return ret;
    }
#endif
    
    /* 7. 初始化以太网 PHY */
#if BOARD_RGMII_ENABLE
    ret = board_phy_init();
    if (ret != 0) {
        return ret;
    }
#endif

    __enable_irq();
    
    g_board_initialized = true;
    
    return 0;
}
