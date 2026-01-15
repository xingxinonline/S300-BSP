/**
 * @file    board.h
 * @brief   Board Support Package for S300 Application Board
 * @details 应用板配置头文件，定义硬件引脚、外设参数和初始化接口。
 *          所有宏均可在 CMakeLists.txt 或编译命令中通过 -D 预定义覆盖。
 *
 * @note    配置优先级: 编译参数 > 用户预定义 > 本文件默认值
 */

#ifndef S300_BSP_BOARD_H
#define S300_BSP_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * Section 1: Feature Enable Switches
 * 功能开关宏 - 控制哪些外设/子系统被启用
 * 这些开关影响后续配置和函数声明的条件编译
 *===========================================================================*/

/** @brief 启用多媒体子系统 (摄像头 + 显示) */
#ifndef BOARD_MM_ENABLE
#define BOARD_MM_ENABLE             0
#endif

/** @brief 启用摄像头支持 (MM 启用时自动启用) */
#ifndef BOARD_CAMERA_ENABLE
#define BOARD_CAMERA_ENABLE         BOARD_MM_ENABLE
#endif

/** @brief 启用 LCD 背光控制 (MM 启用时自动启用) */
#ifndef BOARD_LCD_BL_ENABLE
#define BOARD_LCD_BL_ENABLE         BOARD_MM_ENABLE
#endif

/** @brief 启用调试 UART */
#ifndef BOARD_DEBUG_UART_ENABLE
#define BOARD_DEBUG_UART_ENABLE     1
#endif

/*===========================================================================
 * Section 2: Debug UART Configuration
 * 调试串口配置
 *===========================================================================*/

/* 允许用户覆盖调试 UART 索引 */
#ifndef BOARD_UART_DEBUG_IDX
#define BOARD_UART_DEBUG_IDX        1u
#endif

/* 与 generic_evb 兼容的别名 */
#define BOARD_DEBUG_UART_IDX        BOARD_UART_DEBUG_IDX

#ifndef BOARD_DEBUG_UART_BAUDRATE
#define BOARD_DEBUG_UART_BAUDRATE   115200
#endif

#ifndef BOARD_DEBUG_UART_PORT
#define BOARD_DEBUG_UART_PORT       GPIOA
#endif

/* UART1: PA16(TX), PA17(RX), FUNCTION_3 */
#ifndef BOARD_DEBUG_UART_TX_PIN
#define BOARD_DEBUG_UART_TX_PIN     16
#endif

#ifndef BOARD_DEBUG_UART_RX_PIN
#define BOARD_DEBUG_UART_RX_PIN     17
#endif

#ifndef BOARD_DEBUG_UART_FUNCTION
#define BOARD_DEBUG_UART_FUNCTION   FUNCTION_3
#endif

/*===========================================================================
 * Section 2.1: UART3 Configuration (Secondary UART)
 * UART3 辅助串口配置
 *===========================================================================*/

#ifndef BOARD_UART3_ENABLE
#define BOARD_UART3_ENABLE          1
#endif

#ifndef BOARD_UART3_BAUDRATE
#define BOARD_UART3_BAUDRATE        921600
#endif

#ifndef BOARD_UART3_PORT
#define BOARD_UART3_PORT            GPIOA
#endif

#ifndef BOARD_UART3_TX_PIN
#define BOARD_UART3_TX_PIN          26
#endif

#ifndef BOARD_UART3_RX_PIN
#define BOARD_UART3_RX_PIN          27
#endif

#ifndef BOARD_UART3_FUNCTION
#define BOARD_UART3_FUNCTION        FUNCTION_3
#endif

/*===========================================================================
 * Section 3: LCD Configuration
 * LCD 显示屏配置 (240x320)
 *===========================================================================*/

/** @brief LCD 控制器类型: 0=ST7735S, 1=ST7789 */
#ifndef BOARD_LCD_TYPE
#define BOARD_LCD_TYPE              1   /* ST7789 for 240x320 */
#endif

#ifndef BOARD_LCD_WIDTH
#define BOARD_LCD_WIDTH             240
#endif

#ifndef BOARD_LCD_HEIGHT
#define BOARD_LCD_HEIGHT            320
#endif

/* LCD Backlight: PA24, FUNCTION_2, 低电平点亮 */
#ifndef BOARD_LCD_BL_PORT
#define BOARD_LCD_BL_PORT           GPIOA
#endif

#ifndef BOARD_LCD_BL_PIN
#define BOARD_LCD_BL_PIN            24
#endif

/** @brief 背光有效电平: 1=高电平点亮, 0=低电平点亮 */
#ifndef BOARD_LCD_BL_ACTIVE_LEVEL
#define BOARD_LCD_BL_ACTIVE_LEVEL   0   /* 低电平点亮 */
#endif

/* 显示区域配置 (用于视频子系统，必须 <= LCD 尺寸) */
#ifndef BOARD_DISPLAY_WIDTH
#define BOARD_DISPLAY_WIDTH         BOARD_LCD_WIDTH
#endif

#ifndef BOARD_DISPLAY_HEIGHT
#define BOARD_DISPLAY_HEIGHT        BOARD_LCD_HEIGHT
#endif

/*===========================================================================
 * Section 4: Camera Configuration (OV5640)
 * 摄像头配置
 *===========================================================================*/

/** @brief 摄像头数据格式: 0=RGB565, 1=YUV422 */
#ifndef BOARD_CAMERA_FORMAT
#define BOARD_CAMERA_FORMAT         0
#endif

/* 控制引脚 (RST/PWDN) */
#ifndef BOARD_CAM_PORT
#define BOARD_CAM_PORT              GPIOA
#endif

/* CAM_RST_PIN: GPIOA10 */
#ifndef BOARD_CAM_RST_PIN
#define BOARD_CAM_RST_PIN           10u
#endif

/* CAM_PWDN_PIN: GPIOA19 */
#ifndef BOARD_CAM_PWDN_PIN
#define BOARD_CAM_PWDN_PIN          19u
#endif

#ifndef BOARD_CAM_CTRL_FUNCTION
#define BOARD_CAM_CTRL_FUNCTION     FUNCTION_2
#endif

/* I2C 引脚 (软件 I2C, Index 1) */
#ifndef BOARD_CAMERA_I2C_IDX
#define BOARD_CAMERA_I2C_IDX        1
#endif

#ifndef BOARD_CAMERA_I2C_FREQ
#define BOARD_CAMERA_I2C_FREQ       50000
#endif

#ifndef BOARD_CAMERA_I2C_PORT
#define BOARD_CAMERA_I2C_PORT       GPIOA
#endif

#ifndef BOARD_CAMERA_I2C_SCL_PIN
#define BOARD_CAMERA_I2C_SCL_PIN    0
#endif

#ifndef BOARD_CAMERA_I2C_SDA_PIN
#define BOARD_CAMERA_I2C_SDA_PIN    1
#endif

#ifndef BOARD_CAMERA_I2C_FUNCTION
#define BOARD_CAMERA_I2C_FUNCTION   FUNCTION_2
#endif

/* 兼容旧代码的别名 */
#ifndef CAM_RST_PIN
#define CAM_RST_PIN                 BOARD_CAM_RST_PIN
#endif

#ifndef CAM_PWDN_PIN
#define CAM_PWDN_PIN                BOARD_CAM_PWDN_PIN
#endif

/*===========================================================================
 * Section 5: Configuration Validation
 * 配置有效性检查
 *===========================================================================*/

#if (BOARD_DISPLAY_WIDTH > BOARD_LCD_WIDTH) || (BOARD_DISPLAY_HEIGHT > BOARD_LCD_HEIGHT)
#error "BOARD_DISPLAY size cannot exceed BOARD_LCD size"
#endif

/*===========================================================================
 * Section 6: Core Board Functions
 * 核心板级初始化函数
 *===========================================================================*/

/**
 * @brief  板级统一初始化入口
 * @note   初始化顺序: 系统时钟 → GPIO 时钟 → 调试 UART → UART3
 *         推荐在 main() 开头首先调用
 */
void board_init(void);

/**
 * @brief  初始化系统时钟 (CM4 PLL 192MHz)
 */
void board_clock_init(void);

/**
 * @brief  初始化调试 UART (UART1)
 * @note   仅当 BOARD_DEBUG_UART_ENABLE=1 时有效
 */
void board_debug_uart_init(void);

/**
 * @brief  初始化 UART3
 * @note   仅当 BOARD_UART3_ENABLE=1 时有效
 */
void board_uart3_init(void);

/*===========================================================================
 * Section 7: Optional Peripheral Functions
 * 可选外设初始化函数 (按需调用)
 *===========================================================================*/

#if BOARD_CAMERA_ENABLE
/** @brief 初始化摄像头 I2C 引脚 */
void board_camera_i2c_pins_init(void);

/** @brief 初始化摄像头控制引脚 (RST/PWDN) */
void board_camera_ctrl_pins_init(void);

/**
 * @brief  摄像头上电时序
 * @note   执行 RST/PWDN 引脚的上电顺序，等待摄像头就绪
 */
void board_camera_power_on_sequence(void);

/**
 * @brief  初始化摄像头 I2C 通信
 * @param  i2c  指向 I2C 句柄的指针
 * @return 0 成功，其他失败
 */
int board_camera_i2c_init(void *i2c);
#endif /* BOARD_CAMERA_ENABLE */

#if BOARD_LCD_BL_ENABLE
/**
 * @brief  初始化 LCD 背光引脚
 */
void board_lcd_backlight_pin_init(void);

/**
 * @brief  开启 LCD 背光
 */
void board_lcd_backlight_on(void);

/**
 * @brief  关闭 LCD 背光
 */
void board_lcd_backlight_off(void);
#endif /* BOARD_LCD_BL_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_BOARD_H */
