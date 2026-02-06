/**
 * @file    board.h
 * @brief   Board Support Package for Gimbal Master (智能云台主控板)
 * @details 芯天下智能云台项目主控板配置头文件 (类似大疆OM7P智能跟随模块)
 *          定义硬件引脚、外设参数和初始化接口。
 *          
 *          系统架构:
 *          - 主控 S300 (U38) 负责云台控制和主逻辑
 *          - 4片 S300 子板通过 I2C/UART 互联:
 *            - Card1: 人脸检测
 *            - Card2: 手势检测
 *            - Card3: 人形检测
 *            - 主板: 人脸识别 + 云台跟踪控制
 *          - CPLD (XC2C128) 用于摄像头信号切换
 *          - RGMII 以太网 (RTL8211F)
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

/** @brief 启用 I2C3 总线 (子板通信 + 外设) */
#ifndef BOARD_I2C3_ENABLE
#define BOARD_I2C3_ENABLE           1
#endif

/** @brief 启用 UART2 (子板通信) */
#ifndef BOARD_UART2_ENABLE
#define BOARD_UART2_ENABLE          1
#endif

/** @brief 启用音频子系统 */
#ifndef BOARD_AUDIO_ENABLE
#define BOARD_AUDIO_ENABLE          0
#endif

/** @brief 启用 IMU 传感器 (QMI8658A) */
#ifndef BOARD_IMU_ENABLE
#define BOARD_IMU_ENABLE            1
#endif

/** @brief 启用 PWM 蜂鸣器 */
#ifndef BOARD_BUZZER_ENABLE
#define BOARD_BUZZER_ENABLE         1
#endif

/** @brief 启用舵机控制 */
#ifndef BOARD_SERVO_ENABLE
#define BOARD_SERVO_ENABLE          1
#endif

/** @brief 启用 RGMII 以太网 */
#ifndef BOARD_RGMII_ENABLE
#define BOARD_RGMII_ENABLE          0
#endif

/*===========================================================================
 * Section 2: Debug UART Configuration (UART2)
 * 调试串口配置 - PA23(TX), PA24(RX)
 * 注意: UART3 保留给舵机使用 (GPIO26/27)
 *===========================================================================*/

#ifndef BOARD_DEBUG_UART_IDX
#define BOARD_DEBUG_UART_IDX        2
#endif

/* 兼容旧代码的别名 */
#define BOARD_UART_DEBUG_IDX        BOARD_DEBUG_UART_IDX

#ifndef BOARD_DEBUG_UART_BAUDRATE
#define BOARD_DEBUG_UART_BAUDRATE   921600
#endif

#ifndef BOARD_DEBUG_UART_PORT
#define BOARD_DEBUG_UART_PORT       GPIOA
#endif

/** @brief UART2_TX: GPIO23 (Function3) */
#ifndef BOARD_DEBUG_UART_TX_PIN
#define BOARD_DEBUG_UART_TX_PIN     23
#endif

/** @brief UART2_RX: GPIO24 (Function3) */
#ifndef BOARD_DEBUG_UART_RX_PIN
#define BOARD_DEBUG_UART_RX_PIN     24
#endif

#ifndef BOARD_DEBUG_UART_FUNCTION
#define BOARD_DEBUG_UART_FUNCTION   FUNCTION_3
#endif

/* 调试串口外设指针和 IRQ 映射 */
#if BOARD_DEBUG_UART_IDX == 0
#define BOARD_DEBUG_UART            UART0
#define BOARD_DEBUG_UART_IRQn       UART0_IRQn
#define BOARD_DEBUG_UART_IRQHandler UART0_IRQHandler
#elif BOARD_DEBUG_UART_IDX == 1
#define BOARD_DEBUG_UART            UART1
#define BOARD_DEBUG_UART_IRQn       UART1_IRQn
#define BOARD_DEBUG_UART_IRQHandler UART1_IRQHandler
#elif BOARD_DEBUG_UART_IDX == 2
#define BOARD_DEBUG_UART            UART2
#define BOARD_DEBUG_UART_IRQn       UART2_IRQn
#define BOARD_DEBUG_UART_IRQHandler UART2_IRQHandler
#elif BOARD_DEBUG_UART_IDX == 3
#define BOARD_DEBUG_UART            UART3
#define BOARD_DEBUG_UART_IRQn       UART3_IRQn
#define BOARD_DEBUG_UART_IRQHandler UART3_IRQHandler
#else
#error "Invalid BOARD_DEBUG_UART_IDX"
#endif

/*===========================================================================
 * Section 2.1: UART1 Configuration (BLE/外部通信)
 * UART1 配置 - GPIO16(RX), GPIO17(TX) 用于蓝牙通信
 *===========================================================================*/

#ifndef BOARD_UART1_ENABLE
#define BOARD_UART1_ENABLE          1
#endif

#ifndef BOARD_UART1_BAUDRATE
#define BOARD_UART1_BAUDRATE        115200
#endif

#ifndef BOARD_UART1_PORT
#define BOARD_UART1_PORT            GPIOA
#endif

/** @brief UART1_RX: GPIO16 (Function3) */
#ifndef BOARD_UART1_RX_PIN
#define BOARD_UART1_RX_PIN          16
#endif

/** @brief UART1_TX: GPIO17 (Function3) */
#ifndef BOARD_UART1_TX_PIN
#define BOARD_UART1_TX_PIN          17
#endif

#ifndef BOARD_UART1_FUNCTION
#define BOARD_UART1_FUNCTION        FUNCTION_3
#endif

/*===========================================================================
 * Section 2.2: UART2 Configuration (子板通信)
 * UART2 配置 - GPIO23(RX), GPIO24(TX)
 *===========================================================================*/

#if BOARD_UART2_ENABLE

#ifndef BOARD_UART2_BAUDRATE
#define BOARD_UART2_BAUDRATE        921600
#endif

#ifndef BOARD_UART2_PORT
#define BOARD_UART2_PORT            GPIOA
#endif

/** @brief UART2_RX: GPIO23 (Function3) */
#ifndef BOARD_UART2_RX_PIN
#define BOARD_UART2_RX_PIN          23
#endif

/** @brief UART2_TX: GPIO24 (Function3) */
#ifndef BOARD_UART2_TX_PIN
#define BOARD_UART2_TX_PIN          24
#endif

#ifndef BOARD_UART2_FUNCTION
#define BOARD_UART2_FUNCTION        FUNCTION_3
#endif

#endif /* BOARD_UART2_ENABLE */

/*===========================================================================
 * Section 3: LCD Configuration
 * LCD 显示屏配置 (通过 TXS0108E 电平转换)
 *===========================================================================*/

/** @brief LCD 控制器类型: 0=ST7735S, 1=ST7789 */
#ifndef BOARD_LCD_TYPE
#define BOARD_LCD_TYPE              0   /* ST7735S for 160x128 */
#endif

#ifndef BOARD_LCD_WIDTH
#define BOARD_LCD_WIDTH             160
#endif

#ifndef BOARD_LCD_HEIGHT
#define BOARD_LCD_HEIGHT            128
#endif

/* LCD Backlight: GPIO25, FUNCTION_2 */
#ifndef BOARD_LCD_BL_PORT
#define BOARD_LCD_BL_PORT           GPIOA
#endif

#ifndef BOARD_LCD_BL_PIN
#define BOARD_LCD_BL_PIN            25
#endif

/** @brief 背光有效电平: 1=高电平点亮, 0=低电平点亮 */
#ifndef BOARD_LCD_BL_ACTIVE_LEVEL
#define BOARD_LCD_BL_ACTIVE_LEVEL   1
#endif

/* 显示区域配置 */
#ifndef BOARD_DISPLAY_WIDTH
#define BOARD_DISPLAY_WIDTH         BOARD_LCD_WIDTH
#endif

#ifndef BOARD_DISPLAY_HEIGHT
#define BOARD_DISPLAY_HEIGHT        BOARD_LCD_HEIGHT
#endif

/*===========================================================================
 * Section 4: Camera Configuration (OV5640)
 * 摄像头配置 - DVP 接口
 *===========================================================================*/

/** @brief 摄像头数据格式: 0=RGB565, 1=YUV422 */
#ifndef BOARD_CAMERA_FORMAT
#define BOARD_CAMERA_FORMAT         0
#endif

/* 控制引脚 (RST/PWDN) */
#ifndef BOARD_CAM_PORT
#define BOARD_CAM_PORT              GPIOA
#endif

/* CAM_RSTN: 主板复位引脚 GPIO2 */
#ifndef BOARD_CAM_RST_PIN
#define BOARD_CAM_RST_PIN           2   /* GPIO2 对应 CAM_RSTN */
#endif

/* CAM_PWDN: 主板 PWDN 引脚 (未使用则设为 0xFF) */
#ifndef BOARD_CAM_PWDN_PIN
#define BOARD_CAM_PWDN_PIN          0xFF  /* 主板无 PWDN 引脚 */
#endif

#ifndef BOARD_CAM_CTRL_FUNCTION
#define BOARD_CAM_CTRL_FUNCTION     FUNCTION_2
#endif

/* I2C 引脚 (I2C3 用于摄像头控制) */
#ifndef BOARD_CAMERA_I2C_IDX
#define BOARD_CAMERA_I2C_IDX        3
#endif

#ifndef BOARD_CAMERA_I2C_FREQ
#define BOARD_CAMERA_I2C_FREQ       50000
#endif

#ifndef BOARD_CAMERA_I2C_PORT
#define BOARD_CAMERA_I2C_PORT       GPIOA
#endif

/** @brief I2C3_SCK: GPIO4 (Function3) */
#ifndef BOARD_CAMERA_I2C_SCL_PIN
#define BOARD_CAMERA_I2C_SCL_PIN    4
#endif

/** @brief I2C3_SDA: GPIO5 (Function3) */
#ifndef BOARD_CAMERA_I2C_SDA_PIN
#define BOARD_CAMERA_I2C_SDA_PIN    5
#endif

#ifndef BOARD_CAMERA_I2C_FUNCTION
#define BOARD_CAMERA_I2C_FUNCTION   FUNCTION_3
#endif

/* 兼容旧代码的别名 */
#ifndef CAM_RST_PIN
#define CAM_RST_PIN                 BOARD_CAM_RST_PIN
#endif

#ifndef CAM_PWDN_PIN
#define CAM_PWDN_PIN                BOARD_CAM_PWDN_PIN
#endif

/*===========================================================================
 * Section 5: I2C3 Configuration (共享总线)
 * I2C3 配置 - GPIO4(SCL), GPIO5(SDA)
 * 用于: 摄像头、音频Codec(ES8311/ES7210)、IMU(QMI8658A)、子板通信
 *===========================================================================*/

#if BOARD_I2C3_ENABLE

#ifndef BOARD_I2C3_FREQ
#define BOARD_I2C3_FREQ             50000
#endif

#ifndef BOARD_I2C3_PORT
#define BOARD_I2C3_PORT             GPIOA
#endif

/** @brief I2C3_SCK: GPIO4 (Function3) */
#ifndef BOARD_I2C3_SCL_PIN
#define BOARD_I2C3_SCL_PIN          4
#endif

/** @brief I2C3_SDA: GPIO5 (Function3) */
#ifndef BOARD_I2C3_SDA_PIN
#define BOARD_I2C3_SDA_PIN          5
#endif

#ifndef BOARD_I2C3_FUNCTION
#define BOARD_I2C3_FUNCTION         FUNCTION_3
#endif

/* I2C3 设备地址定义 */
/** @brief ES8311 音频Codec I2C 地址 */
#define BOARD_I2C3_ADDR_ES8311      0x18

/** @brief ES7210 音频ADC I2C 地址 */
#define BOARD_I2C3_ADDR_ES7210      0x41

/** @brief QMI8658A IMU I2C 地址 */
#define BOARD_I2C3_ADDR_QMI8658A    0x6A

/** @brief OV5640 摄像头 I2C 地址 */
#define BOARD_I2C3_ADDR_OV5640      0x3C

#endif /* BOARD_I2C3_ENABLE */

/*===========================================================================
 * Section 6: Audio Configuration (ES8311 + ES7210)
 * 音频配置 - I2S1 接口
 *===========================================================================*/

#if BOARD_AUDIO_ENABLE

/** @brief 使用 ES7210 + ES8311 编解码器组合 */
#define BOARD_AUDIO_CODEC_ES7210    1
#define BOARD_AUDIO_CODEC_ES8311    1

/* I2S1 引脚配置 */
#ifndef BOARD_I2S_MCLK_PORT
#define BOARD_I2S_MCLK_PORT         GPIOA
#endif

/** @brief I2S_MCLK: GPIO3 (Function1 - CLKOUT2) */
#ifndef BOARD_I2S_MCLK_PIN
#define BOARD_I2S_MCLK_PIN          3
#endif

#ifndef BOARD_I2S_MCLK_FUNCTION
#define BOARD_I2S_MCLK_FUNCTION     FUNCTION_1
#endif

#ifndef BOARD_I2S1_PORT
#define BOARD_I2S1_PORT             GPIOA
#endif

/** @brief I2S1_CK: GPIO6 (Function3) */
#ifndef BOARD_I2S1_CK_PIN
#define BOARD_I2S1_CK_PIN           6
#endif

/** @brief I2S1_WS: GPIO7 (Function3) */
#ifndef BOARD_I2S1_WS_PIN
#define BOARD_I2S1_WS_PIN           7
#endif

/** @brief I2S1_DATA_DOUT: GPIO8 (Function3) */
#ifndef BOARD_I2S1_DATA_DOUT_PIN
#define BOARD_I2S1_DATA_DOUT_PIN    8
#endif

/** @brief I2S1_DATA_DIN: GPIO9 (Function3) */
#ifndef BOARD_I2S1_DATA_DIN_PIN
#define BOARD_I2S1_DATA_DIN_PIN     9
#endif

#ifndef BOARD_I2S1_FUNCTION
#define BOARD_I2S1_FUNCTION         FUNCTION_3
#endif

/* 音频功放控制引脚 (NS4150) */
#ifndef BOARD_PA_EN_PORT
#define BOARD_PA_EN_PORT            GPIOA
#endif

/** @brief PA_EN: GPIO13 (Function2) - 高电平使能 */
#ifndef BOARD_PA_EN_PIN
#define BOARD_PA_EN_PIN             13
#endif

#ifndef BOARD_PA_EN_FUNCTION
#define BOARD_PA_EN_FUNCTION        FUNCTION_2
#endif

/** @brief PA_EN 可用标志 */
#define BOARD_PA_EN_AVAILABLE       1

/* 音频 I2C 配置 (与摄像头共用 I2C3) */
#ifndef BOARD_AUDIO_I2C_IDX
#define BOARD_AUDIO_I2C_IDX         3
#endif

#ifndef BOARD_AUDIO_I2C_FREQ
#define BOARD_AUDIO_I2C_FREQ        100000
#endif

/** @brief 音频 I2C 使用与 I2C3 相同的引脚 */
#define BOARD_AUDIO_I2C_PORT        BOARD_I2C3_PORT
#define BOARD_AUDIO_I2C_SCL_PIN     BOARD_I2C3_SCL_PIN   /* GPIO4 */
#define BOARD_AUDIO_I2C_SDA_PIN     BOARD_I2C3_SDA_PIN   /* GPIO5 */
#define BOARD_AUDIO_I2C_FUNCTION    BOARD_I2C3_FUNCTION

/* 音频设备地址别名 */
#define BOARD_AUDIO_ADDR_ES8311     BOARD_I2C3_ADDR_ES8311   /* 0x18 */
#define BOARD_AUDIO_ADDR_ES7210     BOARD_I2C3_ADDR_ES7210   /* 0x41 */

#endif /* BOARD_AUDIO_ENABLE */

/*===========================================================================
 * Section 7: IMU Configuration (QMI8658A)
 * 六轴姿态传感器配置
 *===========================================================================*/

#if BOARD_IMU_ENABLE

/** @brief QMI8658A I2C 地址 */
#ifndef BOARD_IMU_I2C_ADDR
#define BOARD_IMU_I2C_ADDR          0x6A
#endif

/** @brief IMU 使用 I2C3 */
#ifndef BOARD_IMU_I2C_IDX
#define BOARD_IMU_I2C_IDX           3
#endif

#endif /* BOARD_IMU_ENABLE */

/*===========================================================================
 * Section 8: Servo/Motor Configuration
 * 总线舵机控制配置
 * 使用 UART3 进行半双工通信，通过 SN74LVC1G3157 模拟开关切换 TX/RX
 * MOTO_BUSEN 控制开关选择: HIGH=TX模式, LOW=RX模式
 *===========================================================================*/

#if BOARD_SERVO_ENABLE

#ifndef BOARD_SERVO_PORT
#define BOARD_SERVO_PORT            GPIOA
#endif

/** @brief MOTO_BUSEN: 总线方向控制引脚 (GPIO22)
 *  控制 SN74LVC1G3157 模拟开关选择 TX/RX
 *  - HIGH: 选择 B1 (U3TXD) -> 发送模式
 *  - LOW:  选择 B0 (U3RXD) -> 接收模式
 */
#ifndef BOARD_MOTO_BUSEN_PIN
#define BOARD_MOTO_BUSEN_PIN        22
#endif

#ifndef BOARD_MOTO_BUSEN_FUNCTION
#define BOARD_MOTO_BUSEN_FUNCTION   FUNCTION_2
#endif

/** @brief 总线舵机 UART 索引 (UART3)
 *  @note 舵机必须使用 UART3，因为硬件电路将舵机信号连接到 U3TXD/U3RXD
 *        如果调试串口也使用 UART3，则存在冲突，请使用其他 UART 作为调试串口
 */
#ifndef BOARD_SERVO_UART_IDX
#define BOARD_SERVO_UART_IDX        3
#endif

/** @brief 总线舵机 UART 波特率 (固定 115200) */
#ifndef BOARD_SERVO_UART_BAUDRATE
#define BOARD_SERVO_UART_BAUDRATE   115200
#endif

/** @brief UART3_TX: GPIO27 (Function3) - U3TXD 用于舵机通信 */
#ifndef BOARD_SERVO_UART_TX_PIN
#define BOARD_SERVO_UART_TX_PIN     27
#endif

/** @brief UART3_RX: GPIO26 (Function3) - U3RXD 用于舵机通信 */
#ifndef BOARD_SERVO_UART_RX_PIN
#define BOARD_SERVO_UART_RX_PIN     26
#endif

#ifndef BOARD_SERVO_UART_FUNCTION
#define BOARD_SERVO_UART_FUNCTION   FUNCTION_3
#endif

#endif /* BOARD_SERVO_ENABLE */

/*===========================================================================
 * Section 9: Buzzer Configuration
 * 蜂鸣器配置 - PWM 驱动
 *===========================================================================*/

#if BOARD_BUZZER_ENABLE

#ifndef BOARD_BUZZER_PORT
#define BOARD_BUZZER_PORT           GPIOA
#endif

/** @brief PWM0: GPIO20 (Function1) */
#ifndef BOARD_BUZZER_PIN
#define BOARD_BUZZER_PIN            20
#endif

#ifndef BOARD_BUZZER_FUNCTION
#define BOARD_BUZZER_FUNCTION       FUNCTION_1
#endif

/** @brief 蜂鸣器频率 (Hz) */
#ifndef BOARD_BUZZER_FREQ
#define BOARD_BUZZER_FREQ           2700
#endif

#endif /* BOARD_BUZZER_ENABLE */

/*===========================================================================
 * Section 10: RGB LED Configuration (WS2812)
 * RGB LED 配置 - 两条独立数据线
 *===========================================================================*/

#ifndef BOARD_RGB_LED_ENABLE
#define BOARD_RGB_LED_ENABLE        0
#endif

#if BOARD_RGB_LED_ENABLE

#ifndef BOARD_RGB_LED_PORT
#define BOARD_RGB_LED_PORT          GPIOA
#endif

/** @brief RGB_DIN1: GPIO30 */
#ifndef BOARD_RGB_DIN1_PIN
#define BOARD_RGB_DIN1_PIN          30
#endif

/** @brief RGB_DIN2: GPIO21 */
#ifndef BOARD_RGB_DIN2_PIN
#define BOARD_RGB_DIN2_PIN          21
#endif

#ifndef BOARD_RGB_LED_FUNCTION
#define BOARD_RGB_LED_FUNCTION      FUNCTION_2
#endif

/** @brief RGB LED1 链上的 LED 数量 */
#ifndef BOARD_RGB_LED1_COUNT
#define BOARD_RGB_LED1_COUNT        8
#endif

/** @brief RGB LED2 链上的 LED 数量 */
#ifndef BOARD_RGB_LED2_COUNT
#define BOARD_RGB_LED2_COUNT        5
#endif

#endif /* BOARD_RGB_LED_ENABLE */

/*===========================================================================
 * Section 11: Reset Pin Configuration
 * 复位引脚配置 (用于控制子板)
 *===========================================================================*/

/** @brief 主板复位信号 (PORRESETn) */
#ifndef BOARD_PORRESETN_PORT
#define BOARD_PORRESETN_PORT        GPIOA
#endif

#ifndef BOARD_PORRESETN_PIN
#define BOARD_PORRESETN_PIN         15  /* GPIO15 */
#endif

#ifndef BOARD_PORRESETN_FUNCTION
#define BOARD_PORRESETN_FUNCTION    FUNCTION_2
#endif

/*===========================================================================
 * Section 12: JTAG Configuration
 * JTAG 调试接口配置
 *===========================================================================*/

/** @brief 主板 JTAG 启用 */
#ifndef BOARD_JTAG_ENABLE
#define BOARD_JTAG_ENABLE           1
#endif

/*===========================================================================
 * Section 13: Configuration Validation
 * 配置有效性检查
 *===========================================================================*/

#if (BOARD_DISPLAY_WIDTH > BOARD_LCD_WIDTH) || (BOARD_DISPLAY_HEIGHT > BOARD_LCD_HEIGHT)
#error "BOARD_DISPLAY size cannot exceed BOARD_LCD size"
#endif

/*===========================================================================
 * Section 14: Core Board Functions
 * 核心板级初始化函数
 *===========================================================================*/

/**
 * @brief  板级统一初始化入口
 * @note   初始化顺序: 系统时钟 → GPIO 时钟 → 调试 UART
 *         推荐在 main() 开头首先调用
 */
void board_init(void);

/**
 * @brief  初始化系统时钟 (CM4 PLL 192MHz)
 */
void board_clock_init(void);

/**
 * @brief  初始化调试 UART (UART3)
 * @note   仅当 BOARD_DEBUG_UART_ENABLE=1 时有效
 */
void board_debug_uart_init(void);

/**
 * @brief  初始化 UART1 (蓝牙/外部通信)
 * @note   仅当 BOARD_UART1_ENABLE=1 时有效
 */
void board_uart1_init(void);

/**
 * @brief  初始化 UART2 (子板通信)
 * @note   仅当 BOARD_UART2_ENABLE=1 时有效
 */
void board_uart2_init(void);

/*===========================================================================
 * Section 15: Optional Peripheral Functions
 * 可选外设初始化函数 (按需调用)
 *===========================================================================*/

#if BOARD_I2C3_ENABLE
/**
 * @brief  初始化 I2C3 引脚
 */
void board_i2c3_pins_init(void);

/**
 * @brief  初始化 I2C3 通信
 * @param  i2c  指向 I2C 句柄的指针
 * @return 0 成功，其他失败
 */
int board_i2c3_init(void *i2c);
#endif /* BOARD_I2C3_ENABLE */

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

#if BOARD_BUZZER_ENABLE
/**
 * @brief  初始化蜂鸣器引脚
 */
void board_buzzer_pin_init(void);

/**
 * @brief  蜂鸣器发声
 * @param  freq_hz  频率 (Hz)，0 表示关闭
 */
void board_buzzer_beep(uint32_t freq_hz);
#endif /* BOARD_BUZZER_ENABLE */

#if BOARD_AUDIO_ENABLE
/**
 * @brief  初始化 I2S1 音频引脚
 */
void board_audio_i2s_pins_init(void);

/**
 * @brief  音频功放使能
 * @param  enable  true=使能, false=禁用
 */
void board_audio_pa_enable(bool enable);
#endif /* BOARD_AUDIO_ENABLE */

#if BOARD_SERVO_ENABLE
/**
 * @brief  初始化总线舵机引脚
 * @note   配置 UART3 TX/RX 引脚和方向控制引脚 (MOTO_BUSEN)
 */
void board_servo_pins_init(void);

/**
 * @brief  初始化总线舵机控制器
 * @param  servo     舵机控制器句柄指针
 * @return 0 成功，其他失败
 */
int board_servo_init(void *servo);
#endif /* BOARD_SERVO_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_BOARD_H */
