/**
 * @file    gpio_pinmux.h
 * @brief   S300 GPIO Pinmux 语义化宏定义
 * @details 提供易读的引脚功能映射宏，简化 Pinmux 配置。
 *          使用方法: #include "gpio_pinmux.h"
 *
 * @note    完整的引脚功能映射需参考芯片数据手册。
 *          硬件原理图参考: Boards/generic_evb/ 或 Boards/app_board/ 目录。
 *          以下定义基于当前 BSP 已验证的配置。
 *
 * @example
 *          // 配置 UART3 引脚
 *          gpio_set_function(GPIOA, PINMUX_UART3_TX_PIN, PINMUX_UART3_TX_FUNC);
 *          gpio_set_function(GPIOA, PINMUX_UART3_RX_PIN, PINMUX_UART3_RX_FUNC);
 */

#ifndef S300_GPIO_PINMUX_H
#define S300_GPIO_PINMUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gpio.h"

/*===========================================================================
 * Section 1: Function Semantic Aliases
 * 功能语义别名 - 使代码更具可读性
 *===========================================================================*/

/** @brief GPIO 功能 (通用输入输出) */
#define PINMUX_FUNC_GPIO        FUNCTION_2

/** @brief UART 功能 */
#define PINMUX_FUNC_UART        FUNCTION_3

/** @brief I2C 功能 (软件 I2C GPIO 模式) */
#define PINMUX_FUNC_I2C_GPIO    FUNCTION_2

/** @brief DVP 摄像头接口功能 */
#define PINMUX_FUNC_DVP         FUNCTION_1

/** @brief SPI 功能 */
#define PINMUX_FUNC_SPI         FUNCTION_1

/*===========================================================================
 * Section 2: UART Pinmux Definitions
 * UART 引脚复用定义 (基于芯片 IO Pinmux 表)
 *===========================================================================*/

/* UART0: PAD1(TX), PAD3(RX) */
#define PINMUX_UART0_TX_PIN     1
#define PINMUX_UART0_RX_PIN     3
#define PINMUX_UART0_TX_FUNC    FUNCTION_0
#define PINMUX_UART0_RX_FUNC    FUNCTION_3

/* UART1: PAD17(TX), PAD16(RX) - App Board 调试串口 */
#define PINMUX_UART1_TX_PIN     17
#define PINMUX_UART1_RX_PIN     16
#define PINMUX_UART1_TX_FUNC    FUNCTION_2
#define PINMUX_UART1_RX_FUNC    FUNCTION_3

/* UART2: PAD24(TX), PAD23(RX) */
#define PINMUX_UART2_TX_PIN     24
#define PINMUX_UART2_RX_PIN     23
#define PINMUX_UART2_TX_FUNC    FUNCTION_2
#define PINMUX_UART2_RX_FUNC    FUNCTION_3

/* UART3: PAD27(TX), PAD26(RX) - Generic EVB 调试串口 */
#define PINMUX_UART3_TX_PIN     27
#define PINMUX_UART3_RX_PIN     26
#define PINMUX_UART3_TX_FUNC    FUNCTION_2
#define PINMUX_UART3_RX_FUNC    FUNCTION_3

/*===========================================================================
 * Section 3: I2C Pinmux Definitions
 * I2C 引脚复用定义
 *===========================================================================*/

/* I2C0: PAD14(SCL), PAD15(SDA) */
#define PINMUX_I2C0_SCL_PIN     14
#define PINMUX_I2C0_SDA_PIN     15
#define PINMUX_I2C0_FUNC        FUNCTION_0  /* 或 FUNCTION_3 */

/* I2C1: PAD0(SCL), PAD1(SDA) */
#define PINMUX_I2C1_SCL_PIN     0
#define PINMUX_I2C1_SDA_PIN     1
#define PINMUX_I2C1_FUNC        FUNCTION_3

/* I2C2: PAD2(SCL), PAD3(SDA) */
#define PINMUX_I2C2_SCL_PIN     2
#define PINMUX_I2C2_SDA_PIN     3
#define PINMUX_I2C2_FUNC        FUNCTION_3

/* I2C3: PAD4(SCL), PAD5(SDA) */
#define PINMUX_I2C3_SCL_PIN     4
#define PINMUX_I2C3_SDA_PIN     5
#define PINMUX_I2C3_FUNC        FUNCTION_3

/* 兼容旧定义: I2C for Camera */
#define PINMUX_I2C_CAM_SCL_PIN  PINMUX_I2C1_SCL_PIN
#define PINMUX_I2C_CAM_SDA_PIN  PINMUX_I2C1_SDA_PIN
#define PINMUX_I2C_CAM_FUNC     PINMUX_I2C1_FUNC

/*===========================================================================
 * Section 4: SPI Pinmux Definitions
 * SPI 引脚复用定义
 *===========================================================================*/

/* SPI2: PAD4(CLK), PAD5(CS0), PAD2(WP) - 支持 QSPI */
#define PINMUX_SPI2_CLK_PIN     4
#define PINMUX_SPI2_CS0_PIN     5
#define PINMUX_SPI2_WP_PIN      2
#define PINMUX_SPI2_D0_PIN      13  /* SPI2D0 */
#define PINMUX_SPI2_D1_PIN      7   /* SPI2D1 */
#define PINMUX_SPI2_D2_PIN      8   /* SPI2D2 */
#define PINMUX_SPI2_D3_PIN      9   /* SPI2D3 */
#define PINMUX_SPI2_D4_PIN      10  /* SPI2D4 */
#define PINMUX_SPI2_D5_PIN      11  /* SPI2D5 */
#define PINMUX_SPI2_D6_PIN      12  /* SPI2D6 */
#define PINMUX_SPI2_D7_PIN      6   /* SPI2D7 */
#define PINMUX_SPI2_FUNC        FUNCTION_1

/* SPI3: PAD6(CLK), PAD11(CS0) */
#define PINMUX_SPI3_CLK_PIN     6
#define PINMUX_SPI3_CS0_PIN     11
#define PINMUX_SPI3_WP_PIN      10
#define PINMUX_SPI3_D0_PIN      12
#define PINMUX_SPI3_D1_PIN      23
#define PINMUX_SPI3_D2_PIN      24
#define PINMUX_SPI3_D3_PIN      25
#define PINMUX_SPI3_D4_PIN      26
#define PINMUX_SPI3_D5_PIN      27
#define PINMUX_SPI3_D6_PIN      28
#define PINMUX_SPI3_D7_PIN      29
#define PINMUX_SPI3_FUNC        FUNCTION_1

/*===========================================================================
 * Section 5: I2S Pinmux Definitions
 * I2S 引脚复用定义
 *===========================================================================*/

/* I2S0: PAD20(CK), PAD21(WS), PAD22(DATA) */
#define PINMUX_I2S0_CK_PIN      20
#define PINMUX_I2S0_WS_PIN      21
#define PINMUX_I2S0_DATA_PIN    22
#define PINMUX_I2S0_FUNC        FUNCTION_3

/* I2S1: PAD6(CK), PAD7(WS), PAD8(DATA) */
#define PINMUX_I2S1_CK_PIN      6
#define PINMUX_I2S1_WS_PIN      7
#define PINMUX_I2S1_DATA_PIN    8
#define PINMUX_I2S1_FUNC        FUNCTION_3

/*===========================================================================
 * Section 6: PWM Pinmux Definitions
 * PWM 引脚复用定义
 *===========================================================================*/

#define PINMUX_PWM_D0_PIN       20
#define PINMUX_PWM_D1_PIN       21
#define PINMUX_PWM_D2_PIN       22
#define PINMUX_PWM_FUNC         FUNCTION_1

/*===========================================================================
 * Section 7: Camera Control Pinmux Definitions
 * 摄像头控制引脚复用定义
 *===========================================================================*/

/* Camera Control Pins (GPIO 模式) */
#define PINMUX_CAM_RST_PIN      15
#define PINMUX_CAM_PWDN_PIN     6
#define PINMUX_CAM_CTRL_FUNC    FUNCTION_2

/*===========================================================================
 * Section 8: DSP JTAG Pinmux Definitions
 * DSP 调试接口引脚复用定义
 *===========================================================================*/

#define PINMUX_DSP_TCK_PIN      26
#define PINMUX_DSP_TDI_PIN      27
#define PINMUX_DSP_TMS_PIN      28
#define PINMUX_DSP_TDO_PIN      29
#define PINMUX_DSP_JTAG_FUNC    FUNCTION_3  /* TCK/TDI/TMS 在 F3, TDO 在 F0 */

/*===========================================================================
 * Section 9: PDM Pinmux Definitions
 * PDM 数字麦克风引脚复用定义
 *===========================================================================*/

#define PINMUX_PDM1_L_PIN       30
#define PINMUX_PDM1_R_PIN       31
#define PINMUX_PDM_FUNC         FUNCTION_3

/*===========================================================================
 * Section 10: Helper Macros
 * 辅助宏定义
 *===========================================================================*/

/**
 * @brief 快速配置 UART 引脚宏
 * @param uart_idx UART 索引 (0, 1, 2, 3)
 *
 * @example PINMUX_INIT_UART(3); // 初始化 UART3 TX/RX 引脚
 */
#define PINMUX_INIT_UART(uart_idx) do { \
    gpio_set_function(GPIOA, PINMUX_UART##uart_idx##_TX_PIN, PINMUX_UART##uart_idx##_TX_FUNC); \
    gpio_set_function(GPIOA, PINMUX_UART##uart_idx##_RX_PIN, PINMUX_UART##uart_idx##_RX_FUNC); \
} while(0)

/**
 * @brief 快速配置摄像头 I2C 引脚宏
 */
#define PINMUX_INIT_CAM_I2C() do { \
    gpio_set_function(GPIOA, PINMUX_I2C_CAM_SCL_PIN, PINMUX_I2C_CAM_FUNC); \
    gpio_set_function(GPIOA, PINMUX_I2C_CAM_SDA_PIN, PINMUX_I2C_CAM_FUNC); \
} while(0)

/**
 * @brief 快速配置摄像头控制引脚宏 (RST, PWDN)
 */
#define PINMUX_INIT_CAM_CTRL() do { \
    gpio_set_function(GPIOA, PINMUX_CAM_RST_PIN, PINMUX_CAM_CTRL_FUNC); \
    gpio_set_function(GPIOA, PINMUX_CAM_PWDN_PIN, PINMUX_CAM_CTRL_FUNC); \
    gpio_set_direction(GPIOA, PINMUX_CAM_RST_PIN, 1); \
    gpio_set_direction(GPIOA, PINMUX_CAM_PWDN_PIN, 1); \
} while(0)

/**
 * @brief 配置引脚为 GPIO 输出模式
 * @param pin 引脚号
 * @param init_val 初始输出值 (0 或 1)
 */
#define PINMUX_CONFIG_GPIO_OUT(pin, init_val) do { \
    gpio_set_function(GPIOA, (pin), PINMUX_FUNC_GPIO); \
    gpio_set_direction(GPIOA, (pin), 1); \
    gpio_set_data(GPIOA, (pin), (init_val)); \
} while(0)

/**
 * @brief 配置引脚为 GPIO 输入模式 (带上拉)
 * @param pin 引脚号
 */
#define PINMUX_CONFIG_GPIO_IN_PU(pin) do { \
    gpio_set_function(GPIOA, (pin), PINMUX_FUNC_GPIO); \
    gpio_set_mode(GPIOA, (pin), GPIO_UP); \
    gpio_set_direction(GPIOA, (pin), 0); \
} while(0)

/**
 * @brief 配置引脚为 GPIO 输入模式 (带下拉)
 * @param pin 引脚号
 */
#define PINMUX_CONFIG_GPIO_IN_PD(pin) do { \
    gpio_set_function(GPIOA, (pin), PINMUX_FUNC_GPIO); \
    gpio_set_mode(GPIOA, (pin), GPIO_DOWN); \
    gpio_set_direction(GPIOA, (pin), 0); \
} while(0)

/*===========================================================================
 * Section 8: Pin Function Lookup Table (Future Extension)
 * 引脚功能查找表 (未来扩展预留)
 *===========================================================================*/

/**
 * @brief 引脚功能描述结构体 (用于运行时查询)
 * @note  待芯片手册对接后填充完整数据
 */
typedef struct {
    uint8_t  pin;           /**< 引脚号 (0-31) */
    uint8_t  func;          /**< 功能号 (0-3) */
    const char *name;       /**< 功能名称 (如 "UART3_TX") */
    const char *desc;       /**< 功能描述 */
} pinmux_func_entry_t;

/* 预留: 完整引脚功能表 (待手册数据填充)
 * extern const pinmux_func_entry_t g_pinmux_table[];
 * extern const uint32_t g_pinmux_table_size;
 */

#ifdef __cplusplus
}
#endif

#endif /* S300_GPIO_PINMUX_H */
