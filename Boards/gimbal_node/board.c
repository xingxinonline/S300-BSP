/**
 * @file    board.c
 * @brief   Board Support Package Implementation for NE005 Card (mPCIE Sub-Board)
 * @details 芯天下智能云台项目子板 BSP 实现
 */

#include "board.h"
#include "s300_gpio.h"
#include "s300_uart.h"
#include "s300_i2c.h"
#include "s300_rcc.h"

/*===========================================================================
 * Private Variables
 *===========================================================================*/

static bool g_board_initialized = false;

/*===========================================================================
 * Clock Initialization
 *===========================================================================*/

int board_clock_init(void)
{
    /* 配置系统时钟为 192MHz */
    /* 使用外部 24MHz 晶振 (OSC1) 作为输入 */
    
    /* TODO: 实现具体的时钟配置代码 */
    /* rcc_set_sysclk_freq(BOARD_CLK_SYSCORE_FREQ); */
    
    return 0;
}

/*===========================================================================
 * Debug UART Initialization (UART3)
 *===========================================================================*/

int board_debug_uart_init(void)
{
#if BOARD_DEBUG_UART_ENABLE
    /* 配置 GPIO26 为 UART3_RX */
    gpio_set_pinmux(BOARD_DEBUG_UART_RX_PIN, BOARD_DEBUG_UART_RX_PINMUX);
    
    /* 配置 GPIO27 为 UART3_TX */
    gpio_set_pinmux(BOARD_DEBUG_UART_TX_PIN, BOARD_DEBUG_UART_TX_PINMUX);
    
    /* 初始化 UART3 */
    uart_config_t cfg = {
        .baudrate = BOARD_DEBUG_UART_BAUDRATE,
        .data_bits = UART_DATA_8BIT,
        .stop_bits = UART_STOP_1BIT,
        .parity = UART_PARITY_NONE,
    };
    
    return uart_init(BOARD_DEBUG_UART_IDX, &cfg);
#else
    return 0;
#endif
}

/*===========================================================================
 * Host Communication UART Initialization (UART2)
 *===========================================================================*/

int board_uart2_init(void)
{
#if BOARD_UART2_ENABLE
    /* 配置 GPIO23 为 UART2_RX */
    gpio_set_pinmux(BOARD_UART2_RX_PIN, BOARD_UART2_RX_PINMUX);
    
    /* 配置 GPIO24 为 UART2_TX */
    gpio_set_pinmux(BOARD_UART2_TX_PIN, BOARD_UART2_TX_PINMUX);
    
    /* 初始化 UART2 - 1Mbps 高速通信 */
    uart_config_t cfg = {
        .baudrate = BOARD_UART2_BAUDRATE,
        .data_bits = UART_DATA_8BIT,
        .stop_bits = UART_STOP_1BIT,
        .parity = UART_PARITY_NONE,
    };
    
    return uart_init(BOARD_UART2_IDX, &cfg);
#else
    return 0;
#endif
}

/*===========================================================================
 * I2C3 Bus Initialization
 *===========================================================================*/

int board_i2c3_init(void)
{
#if BOARD_I2C3_ENABLE
    /* 配置 GPIO10 为 I2C3_SCL */
    gpio_set_pinmux(BOARD_I2C3_SCL_PIN, BOARD_I2C3_SCL_PINMUX);
    gpio_set_pull(BOARD_I2C3_SCL_PIN, GPIO_PULL_UP);
    
    /* 配置 GPIO11 为 I2C3_SDA */
    gpio_set_pinmux(BOARD_I2C3_SDA_PIN, BOARD_I2C3_SDA_PINMUX);
    gpio_set_pull(BOARD_I2C3_SDA_PIN, GPIO_PULL_UP);
    
    /* 初始化 I2C3 控制器 - 400kHz */
    i2c_config_t cfg = {
        .speed = BOARD_I2C3_SPEED,
        .addr_mode = I2C_ADDR_7BIT,
    };
    
    return i2c_init(3, &cfg);
#else
    return 0;
#endif
}

/*===========================================================================
 * Camera Interface Initialization
 *===========================================================================*/

int board_camera_init(void)
{
#if BOARD_CAMERA_ENABLE
    /* 配置 DVP 数据引脚 (8-bit) */
    gpio_set_pinmux(BOARD_DVP_DATA0_PIN, BOARD_DVP_DATA_PINMUX);
    gpio_set_pinmux(BOARD_DVP_DATA1_PIN, BOARD_DVP_DATA_PINMUX);
    gpio_set_pinmux(BOARD_DVP_DATA2_PIN, BOARD_DVP_DATA_PINMUX);
    gpio_set_pinmux(BOARD_DVP_DATA3_PIN, BOARD_DVP_DATA_PINMUX);
    gpio_set_pinmux(BOARD_DVP_DATA4_PIN, BOARD_DVP_DATA_PINMUX);
    gpio_set_pinmux(BOARD_DVP_DATA5_PIN, BOARD_DVP_DATA_PINMUX);
    gpio_set_pinmux(BOARD_DVP_DATA6_PIN, BOARD_DVP_DATA_PINMUX);
    gpio_set_pinmux(BOARD_DVP_DATA7_PIN, BOARD_DVP_DATA_PINMUX);
    
    /* 配置 DVP 控制引脚 */
    gpio_set_pinmux(BOARD_DVP_HSYNC_PIN, BOARD_DVP_CTRL_PINMUX);
    gpio_set_pinmux(BOARD_DVP_VSYNC_PIN, BOARD_DVP_CTRL_PINMUX);
    gpio_set_pinmux(BOARD_DVP_PCLK_PIN, BOARD_DVP_CTRL_PINMUX);
    gpio_set_pinmux(BOARD_DVP_XCLK_PIN, BOARD_DVP_CTRL_PINMUX);
    
    /* 配置摄像头复位引脚 */
    gpio_set_mode(BOARD_DVP_RSTN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_output(BOARD_DVP_RSTN_PIN, 1);  /* 复位释放 */
    
    /* 配置摄像头电源控制引脚 */
    gpio_set_mode(BOARD_DVP_PWDOWN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_output(BOARD_DVP_PWDOWN_PIN, 0);  /* 正常工作模式 */
    
    return 0;
#else
    return 0;
#endif
}

/*===========================================================================
 * Camera Power and Reset Control
 *===========================================================================*/

void board_camera_power(bool on)
{
#if BOARD_CAMERA_ENABLE
    /* PWDOWN: 低电平正常工作, 高电平低功耗 */
    gpio_set_output(BOARD_DVP_PWDOWN_PIN, on ? 0 : 1);
#else
    (void)on;
#endif
}

void board_camera_reset(void)
{
#if BOARD_CAMERA_ENABLE
    /* 拉低复位信号 */
    gpio_set_output(BOARD_DVP_RSTN_PIN, 0);
    
    /* 延时 10ms */
    for (volatile int i = 0; i < 100000; i++);
    
    /* 释放复位 */
    gpio_set_output(BOARD_DVP_RSTN_PIN, 1);
    
    /* 等待摄像头稳定 */
    for (volatile int i = 0; i < 100000; i++);
#endif
}

/*===========================================================================
 * LED Control
 *===========================================================================*/

void board_led_init(void)
{
#if BOARD_LED_ENABLE
    /* 配置 LED1 控制引脚 */
    gpio_set_mode(BOARD_LED1_PIN, GPIO_MODE_OUTPUT);
    gpio_set_output(BOARD_LED1_PIN, 0);  /* 默认熄灭 */
    
    /* 配置 LED2 控制引脚 */
    gpio_set_mode(BOARD_LED2_PIN, GPIO_MODE_OUTPUT);
    gpio_set_output(BOARD_LED2_PIN, 0);  /* 默认熄灭 */
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
    
    gpio_set_output(pin, on ? 1 : 0);
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
    
    gpio_toggle_output(pin);
#else
    (void)led_id;
#endif
}

/*===========================================================================
 * PHY Initialization (RTL8211F)
 *===========================================================================*/

#if BOARD_RGMII_ENABLE

int board_phy_init(void)
{
    /* 配置 MDIO 管理接口引脚 */
    gpio_set_pinmux(BOARD_PHY_MDC_PIN, 5);   /* AF5 = GMII_MDC */
    gpio_set_pinmux(BOARD_PHY_MDIO_PIN, 5);  /* AF5 = GMII_MDIO */
    gpio_set_pull(BOARD_PHY_MDIO_PIN, GPIO_PULL_UP);
    
    /* 复位 PHY */
    board_phy_reset();
    
    /* TODO: 配置 PHY 寄存器 */
    
    return 0;
}

void board_phy_reset(void)
{
    /* 使用 DVP_RSTN 引脚复位 PHY */
    gpio_set_mode(BOARD_PHY_RSTB_PIN, GPIO_MODE_OUTPUT);
    
    /* 拉低复位 */
    gpio_set_output(BOARD_PHY_RSTB_PIN, 0);
    
    /* 延时 10ms */
    for (volatile int i = 0; i < 100000; i++);
    
    /* 释放复位 */
    gpio_set_output(BOARD_PHY_RSTB_PIN, 1);
    
    /* 等待 PHY 稳定 */
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
    
    g_board_initialized = true;
    
    return 0;
}
