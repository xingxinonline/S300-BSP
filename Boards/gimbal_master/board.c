/**
 * @file    board.c
 * @brief   Board Support Package Implementation for NE005 Smart Gimbal Main Board
 * @details 芯天下智能云台项目主板初始化实现
 */

#include "board.h"
#include "rcc.h"
#include "gpio.h"
#include "uart.h"
#include <stdio.h>

#if __has_include("i2c_soft.h") && (BOARD_I2C3_ENABLE || BOARD_CAMERA_ENABLE)
#include "i2c_soft.h"
#define HAS_I2C_SOFT 1
#else
#define HAS_I2C_SOFT 0
#endif

/*===========================================================================
 * Internal Functions
 *===========================================================================*/

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
 *
 * 实际使用的 UART 及引脚由 BOARD_DEBUG_UART_* 宏决定。
 * 对 gimbal_master 默认配置，CM4 printf 走 UART2: PA23/PA24。
 */
static void board_debug_uart_pins_init(void)
{
    set_gpio_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_TX_PIN, BOARD_DEBUG_UART_FUNCTION);
    set_gpio_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_RX_PIN, BOARD_DEBUG_UART_FUNCTION);
}

#if BOARD_UART1_ENABLE
/**
 * @brief 配置 UART1 引脚复用 (PA16/PA17)
 */
static void board_uart1_pins_init(void)
{
    set_gpio_function(BOARD_UART1_PORT, BOARD_UART1_TX_PIN, BOARD_UART1_FUNCTION);
    set_gpio_function(BOARD_UART1_PORT, BOARD_UART1_RX_PIN, BOARD_UART1_FUNCTION);
}
#endif

#if BOARD_UART2_ENABLE
/**
 * @brief 配置 UART2 引脚复用 (PA23/PA24)
 */
static void board_uart2_pins_init(void)
{
    set_gpio_function(BOARD_UART2_PORT, BOARD_UART2_TX_PIN, BOARD_UART2_FUNCTION);
    set_gpio_function(BOARD_UART2_PORT, BOARD_UART2_RX_PIN, BOARD_UART2_FUNCTION);
}
#endif

/*===========================================================================
 * Core Board Functions
 *===========================================================================*/

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
            set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
            break;
    }

    board_debug_uart_pins_init();
    init_uart(BOARD_DEBUG_UART_IDX, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), BOARD_DEBUG_UART_BAUDRATE);
    
    /* 关闭缓冲 */
    setvbuf(stdout, NULL, _IONBF, 0);
}

#if BOARD_UART1_ENABLE
void board_uart1_init(void)
{
    /* 开启 UART1 时钟 */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART1, true);
    
    board_uart1_pins_init();
    init_uart(1, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), BOARD_UART1_BAUDRATE);
}
#endif

#if BOARD_UART2_ENABLE
void board_uart2_init(void)
{
    /* 开启 UART2 时钟 */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART2, true);
    
    board_uart2_pins_init();
    init_uart(2, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), BOARD_UART2_BAUDRATE);
}
#endif

void board_init(void)
{
    board_clock_init();
    board_gpio_clock_init();  /* GPIO 时钟需在引脚配置之前使能 */
    board_debug_uart_init();
    
#if BOARD_UART1_ENABLE
    board_uart1_init();
#endif

#if BOARD_UART2_ENABLE
    board_uart2_init();
#endif
    
    __enable_irq();
}

/*===========================================================================
 * Optional Peripheral Pin Initialization Functions
 *===========================================================================*/

#if BOARD_I2C3_ENABLE
void board_i2c3_pins_init(void)
{
    set_gpio_function(BOARD_I2C3_PORT, BOARD_I2C3_SCL_PIN, BOARD_I2C3_FUNCTION);
    set_gpio_function(BOARD_I2C3_PORT, BOARD_I2C3_SDA_PIN, BOARD_I2C3_FUNCTION);
}

int board_i2c3_init(void *i2c)
{
    board_i2c3_pins_init();
    
#if HAS_I2C_SOFT
    if (i2c != NULL) {
        /* I2C3: GPIO4(SCL), GPIO5(SDA) -> idx=3 */
        return i2c_soft_init_default_idx((i2c_soft_t *)i2c, 3, BOARD_I2C3_FREQ);
    }
#else
    (void)i2c;
#endif
    return 0;
}
#endif /* BOARD_I2C3_ENABLE */

#if BOARD_CAMERA_ENABLE
#define BOARD_CAM_PWDN_VALID() ((BOARD_CAM_PWDN_PIN) != 0xFFu && (BOARD_CAM_PWDN_PIN) < 32u)

void board_camera_i2c_pins_init(void)
{
    set_gpio_function(BOARD_CAMERA_I2C_PORT, BOARD_CAMERA_I2C_SCL_PIN, BOARD_CAMERA_I2C_FUNCTION);
    set_gpio_function(BOARD_CAMERA_I2C_PORT, BOARD_CAMERA_I2C_SDA_PIN, BOARD_CAMERA_I2C_FUNCTION);
}

void board_camera_ctrl_pins_init(void)
{
    /* PWDN Pin (部分板型无该引脚，0xFF 表示未连接) */
    if (BOARD_CAM_PWDN_VALID()) {
        set_gpio_function(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, BOARD_CAM_CTRL_FUNCTION);
        set_gpio_mode(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, GPIO_UP);
        set_gpio_direction(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 1);  /* Output */
    }

    /* RST Pin */
    set_gpio_function(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, BOARD_CAM_CTRL_FUNCTION);
    set_gpio_mode(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, GPIO_UP);
    set_gpio_direction(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 1);   /* Output */
}

void board_camera_power_on_sequence(void)
{
    /* 1. PWDN 拉高 (省电模式) */
    if (BOARD_CAM_PWDN_VALID()) {
        set_gpio_data(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 1);
    }
    
    /* 2. RST 拉低 (复位) */
    set_gpio_data(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 0);
    
    /* 3. 等待电源稳定 */
    for (volatile int i = 0; i < 100000; i++);
    
    /* 4. PWDN 拉低 (正常工作) */
    if (BOARD_CAM_PWDN_VALID()) {
        set_gpio_data(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 0);
    }
    
    /* 5. 等待 PWDN 生效 */
    for (volatile int i = 0; i < 50000; i++);
    
    /* 6. RST 拉高 (释放复位) */
    set_gpio_data(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 1);
    
    /* 7. 等待摄像头就绪 */
    for (volatile int i = 0; i < 100000; i++);
}

int board_camera_i2c_init(void *i2c)
{
    board_camera_i2c_pins_init();
    
#if HAS_I2C_SOFT
    if (i2c != NULL) {
        /* Camera I2C: GPIO4(SCL), GPIO5(SDA) -> idx=3 (与 I2C3 共用) */
        return i2c_soft_init_default_idx((i2c_soft_t *)i2c, 3, BOARD_CAMERA_I2C_FREQ);
    }
#else
    (void)i2c;
#endif
    return 0;
}
#endif /* BOARD_CAMERA_ENABLE */

#if BOARD_LCD_BL_ENABLE && (BOARD_LCD_BL_PIN != 0xFF)
void board_lcd_backlight_pin_init(void)
{
    set_gpio_function(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, FUNCTION_2);
    set_gpio_mode(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, GPIO_DOWN);
    set_gpio_direction(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, 1);  /* Output */
}

void board_lcd_backlight_on(void)
{
    set_gpio_data(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, BOARD_LCD_BL_ACTIVE_LEVEL);
}

void board_lcd_backlight_off(void)
{
    set_gpio_data(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, !BOARD_LCD_BL_ACTIVE_LEVEL);
}
#endif /* BOARD_LCD_BL_ENABLE */

#if BOARD_BUZZER_ENABLE
void board_buzzer_pin_init(void)
{
    set_gpio_function(BOARD_BUZZER_PORT, BOARD_BUZZER_PIN, BOARD_BUZZER_FUNCTION);
    set_gpio_mode(BOARD_BUZZER_PORT, BOARD_BUZZER_PIN, GPIO_DOWN);
    set_gpio_direction(BOARD_BUZZER_PORT, BOARD_BUZZER_PIN, 1);  /* Output */
}

void board_buzzer_beep(uint32_t freq_hz)
{
    if (freq_hz == 0) {
        /* 关闭蜂鸣器 */
        set_gpio_data(BOARD_BUZZER_PORT, BOARD_BUZZER_PIN, 0);
    } else {
        /* TODO: 配置 PWM 输出指定频率 */
        /* 临时实现：简单开关 */
        set_gpio_data(BOARD_BUZZER_PORT, BOARD_BUZZER_PIN, 1);
    }
}
#endif /* BOARD_BUZZER_ENABLE */

#if BOARD_AUDIO_ENABLE
void board_audio_i2s_pins_init(void)
{
    /* I2S_MCLK */
    set_gpio_function(BOARD_I2S_MCLK_PORT, BOARD_I2S_MCLK_PIN, BOARD_I2S_MCLK_FUNCTION);
    
    /* I2S1 引脚 */
    set_gpio_function(BOARD_I2S1_PORT, BOARD_I2S1_CK_PIN, BOARD_I2S1_FUNCTION);
    set_gpio_function(BOARD_I2S1_PORT, BOARD_I2S1_WS_PIN, BOARD_I2S1_FUNCTION);
    set_gpio_function(BOARD_I2S1_PORT, BOARD_I2S1_DATA_DOUT_PIN, BOARD_I2S1_FUNCTION);
    set_gpio_function(BOARD_I2S1_PORT, BOARD_I2S1_DATA_DIN_PIN, BOARD_I2S1_FUNCTION);
}

void board_audio_pa_enable(bool enable)
{
    set_gpio_function(BOARD_PA_EN_PORT, BOARD_PA_EN_PIN, BOARD_PA_EN_FUNCTION);
    set_gpio_mode(BOARD_PA_EN_PORT, BOARD_PA_EN_PIN, GPIO_DOWN);
    set_gpio_direction(BOARD_PA_EN_PORT, BOARD_PA_EN_PIN, 1);  /* Output */
    set_gpio_data(BOARD_PA_EN_PORT, BOARD_PA_EN_PIN, enable ? 1 : 0);
}
#endif /* BOARD_AUDIO_ENABLE */

#if BOARD_SERVO_ENABLE
#if __has_include("bus_servo.h")
#include "bus_servo.h"
#define HAS_BUS_SERVO 1
#else
#define HAS_BUS_SERVO 0
#endif

void board_servo_pins_init(void)
{
    /* 配置 UART3 TX/RX 引脚 (舵机通信) */
    set_gpio_function(BOARD_SERVO_PORT, BOARD_SERVO_UART_TX_PIN, BOARD_SERVO_UART_FUNCTION);
    set_gpio_function(BOARD_SERVO_PORT, BOARD_SERVO_UART_RX_PIN, BOARD_SERVO_UART_FUNCTION);
    
    /* 配置 MOTO_BUSEN 方向控制引脚 (与原始demo保持一致，不设置mode) */
    set_gpio_function(BOARD_SERVO_PORT, BOARD_MOTO_BUSEN_PIN, BOARD_MOTO_BUSEN_FUNCTION);
    set_gpio_direction(BOARD_SERVO_PORT, BOARD_MOTO_BUSEN_PIN, 1);  /* Output */
    set_gpio_data(BOARD_SERVO_PORT, BOARD_MOTO_BUSEN_PIN, 1);  /* 默认发送模式 */
}

int board_servo_init(void *servo)
{
    /* 开启 UART3 时钟 */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    
    /* 初始化引脚 */
    board_servo_pins_init();
    
#if HAS_BUS_SERVO
    if (servo != NULL) {
        return bus_servo_init((bus_servo_t *)servo, 
                              BOARD_SERVO_UART_IDX, 
                              BOARD_MOTO_BUSEN_PIN,
                              rcc_get_clock(RCC_CLOCK_APB1));
    }
#else
    (void)servo;
#endif
    return 0;
}
#endif /* BOARD_SERVO_ENABLE */
