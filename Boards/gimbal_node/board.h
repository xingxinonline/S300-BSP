/**
 * @file    board.h
 * @brief   Board Support Package for Gimbal Node (智能云台AI节点)
 * @details 芯天下智能云台项目 AI 检测节点配置头文件
 *          Gimbal Node 是一款基于 mPCIE 接口的 S300 AI处理子板，
 *          通过 mPCIE 连接器与主板互联。
 *
 *          子板功能分配:
 *          - Card1: 人脸检测 (Face Detection)
 *          - Card2: 手势检测 (Gesture Detection)
 *          - Card3: 人形检测 (Human Detection)
 *
 *          硬件特性:
 *          - S300 SoC (Cortex-M4 @ 192MHz)
 *          - W956D8MBYA 8MB PSRAM
 *          - W25Q128 128Mbit SPI Flash
 *          - RTL8211F RGMII 以太网 PHY (可选)
 *          - DVP 摄像头接口
 *          - I2C1 与主板通信（当前 bring-up / 板间控制链路）
 *          - UART2 作为主板高速串口通信（可选）
 *          - I2C3 预留给板载/本地外设，不作为当前板间 bring-up 通道
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
 * Section 1: Board Identification
 * 板卡标识
 *===========================================================================*/

/** @brief 板卡名称 */
#define BOARD_NAME                  "Gimbal_Node"

/** @brief 板卡版本 */
#define BOARD_VERSION               "1.0"

/** @brief 板卡类型 - 子板 */
#define BOARD_TYPE_SUBBOARD         1

/*===========================================================================
 * Section 2: Feature Enable Switches
 * 功能开关宏 - 控制哪些外设/子系统被启用
 *===========================================================================*/

/** @brief 启用多媒体子系统 (摄像头) */
#ifndef BOARD_MM_ENABLE
#define BOARD_MM_ENABLE             0
#endif

/** @brief 启用摄像头支持 (MM 启用时自动启用) */
#ifndef BOARD_CAMERA_ENABLE
#define BOARD_CAMERA_ENABLE         BOARD_MM_ENABLE
#endif

/** @brief 启用调试 UART (UART3) */
#ifndef BOARD_DEBUG_UART_ENABLE
#define BOARD_DEBUG_UART_ENABLE     1
#endif

/** @brief 启用主板通信 UART (UART2) */
#ifndef BOARD_UART2_ENABLE
#define BOARD_UART2_ENABLE          1
#endif

/** @brief 启用 I2C3 总线 (板载/本地外设) */
#ifndef BOARD_I2C3_ENABLE
#define BOARD_I2C3_ENABLE           1
#endif

/** @brief 启用 RGMII 以太网 (RTL8211F) */
#ifndef BOARD_RGMII_ENABLE
#define BOARD_RGMII_ENABLE          0
#endif

/** @brief 启用状态 LED */
#ifndef BOARD_LED_ENABLE
#define BOARD_LED_ENABLE            1
#endif

/*===========================================================================
 * Section 3: Clock Configuration
 * 时钟配置
 *===========================================================================*/

/** @brief 主时钟频率 (OSC1 24MHz) */
#define BOARD_CLK_MAIN_FREQ         24000000

/** @brief RTC 时钟频率 (X2 32.768kHz) */
#define BOARD_CLK_RTC_FREQ          32768

/** @brief 系统核心频率 */
#define BOARD_CLK_SYSCORE_FREQ      192000000

/** @brief PHY 时钟频率 (X1 25MHz) */
#define BOARD_CLK_PHY_FREQ          25000000

/*===========================================================================
 * Section 4: Debug UART Configuration (UART3)
 * 调试串口配置 - GPIO26(RX), GPIO27(TX)
 *===========================================================================*/

#ifndef BOARD_DEBUG_UART_IDX
#define BOARD_DEBUG_UART_IDX        3
#endif

/* 兼容旧代码的别名 */
#define BOARD_UART_DEBUG_IDX        BOARD_DEBUG_UART_IDX

#ifndef BOARD_DEBUG_UART_TX_PIN
#define BOARD_DEBUG_UART_TX_PIN     27      /**< GPIO27 = J1 pin 2 (M_J_TMS) */
#endif

#ifndef BOARD_DEBUG_UART_RX_PIN
#define BOARD_DEBUG_UART_RX_PIN     26      /**< GPIO26 = J1 pin 1 */
#endif

/** @brief 调试串口引脚功能复用值 */
#define BOARD_DEBUG_UART_TX_PINMUX  3       /**< GPIO27 AF3 = UART3_TX */
#define BOARD_DEBUG_UART_RX_PINMUX  3       /**< GPIO26 AF3 = UART3_RX */

/** @brief 默认调试串口波特率 */
#ifndef BOARD_DEBUG_UART_BAUDRATE
#define BOARD_DEBUG_UART_BAUDRATE   115200
#endif

#ifndef BOARD_DEBUG_UART_PORT
#define BOARD_DEBUG_UART_PORT       GPIOA
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
#else
#define BOARD_DEBUG_UART            UART3
#define BOARD_DEBUG_UART_IRQn       UART3_IRQn
#define BOARD_DEBUG_UART_IRQHandler UART3_IRQHandler
#endif

/*===========================================================================
 * Section 5: Host Communication UART (UART2)
 * 主板通信串口配置 - 通过 mPCIE 连接器
 *===========================================================================*/

#ifndef BOARD_UART2_IDX
#define BOARD_UART2_IDX             2
#endif

/** @brief UART2 引脚 (通过 mPCIE 连接器 I/O43, I/O44) */
#ifndef BOARD_UART2_TX_PIN
#define BOARD_UART2_TX_PIN          24      /**< U2TXD -> mPCIE I/O43 */
#endif

#ifndef BOARD_UART2_RX_PIN
#define BOARD_UART2_RX_PIN          23      /**< U2RXD -> mPCIE I/O44 */
#endif

#define BOARD_UART2_TX_PINMUX       3       /**< GPIO24 AF3 = UART2_TX */
#define BOARD_UART2_RX_PINMUX       3       /**< GPIO23 AF3 = UART2_RX */

/** @brief 主板通信波特率 */
#ifndef BOARD_UART2_BAUDRATE
#define BOARD_UART2_BAUDRATE        1000000  /**< 1Mbps 高速通信 */
#endif

#ifndef BOARD_UART2_PORT
#define BOARD_UART2_PORT            GPIOA
#endif

#ifndef BOARD_UART2_FUNCTION
#define BOARD_UART2_FUNCTION        FUNCTION_3
#endif

/*===========================================================================
 * Section 6: UART1 Configuration (Optional Secondary)
 * 备用串口配置
 *===========================================================================*/

#ifndef BOARD_UART1_TX_PIN
#define BOARD_UART1_TX_PIN          15      /**< GPIO15 (U1TXD) */
#endif

#ifndef BOARD_UART1_RX_PIN
#define BOARD_UART1_RX_PIN          14      /**< GPIO14 (U1RXD) */
#endif

#define BOARD_UART1_TX_PINMUX       2       /**< GPIO15 AF2 = UART1_TX */
#define BOARD_UART1_RX_PINMUX       2       /**< GPIO14 AF2 = UART1_RX */

/*===========================================================================
 * Section 7: I2C3 Bus Configuration
 * I2C3 总线配置 - 板载/本地外设
 *===========================================================================*/

#ifndef BOARD_I2C3_SCL_PIN
#define BOARD_I2C3_SCL_PIN          10      /**< I2C3_SCK -> mPCIE I/O41 */
#endif

#ifndef BOARD_I2C3_SDA_PIN
#define BOARD_I2C3_SDA_PIN          11      /**< I2C3_SDA -> mPCIE I/O42 */
#endif

#define BOARD_I2C3_SCL_PINMUX       2       /**< GPIO10 AF2 = I2C3_SCL */
#define BOARD_I2C3_SDA_PINMUX       2       /**< GPIO11 AF2 = I2C3_SDA */

/** @brief I2C3 总线速率 */
#ifndef BOARD_I2C3_SPEED
#define BOARD_I2C3_SPEED            400000  /**< 400kHz Fast Mode */
#endif

#ifndef BOARD_I2C3_PORT
#define BOARD_I2C3_PORT             GPIOA
#endif

#ifndef BOARD_I2C3_FUNCTION
#define BOARD_I2C3_FUNCTION         FUNCTION_2
#endif

/** @brief I2C 设备地址 */
#define BOARD_I2C_ADDR_CAMERA       0x3C    /**< OV5640 摄像头 (0x78 >> 1) */

/*===========================================================================
 * Section 8: Camera DVP Interface Configuration
 * DVP 摄像头接口配置 - 通过 mPCIE 连接器
 *===========================================================================*/

/** @brief DVP 数据引脚 (8-bit) */
#define BOARD_DVP_DATA0_PIN         8       /**< DVP_DATA[0] (N8) -> mPCIE I/O29 */
#define BOARD_DVP_DATA1_PIN         6       /**< DVP_DATA[1] (M6) -> mPCIE I/O30 */
#define BOARD_DVP_DATA2_PIN         9       /**< DVP_DATA[2] (P6) -> mPCIE I/O31 */
#define BOARD_DVP_DATA3_PIN         5       /**< DVP_DATA[3] (L6) -> mPCIE I/O32 */
#define BOARD_DVP_DATA4_PIN         4       /**< DVP_DATA[4] (P8) -> mPCIE I/O33 */
#define BOARD_DVP_DATA5_PIN         7       /**< DVP_DATA[5] (M7) -> mPCIE I/O34 */
#define BOARD_DVP_DATA6_PIN         3       /**< DVP_DATA[6] (L4) -> mPCIE I/O35 */
#define BOARD_DVP_DATA7_PIN         2       /**< DVP_DATA[7] (N9) -> mPCIE I/O36 */

/** @brief DVP 控制引脚 */
#define BOARD_DVP_HSYNC_PIN         12      /**< DVP_HSYNC (M8) -> mPCIE I/O39 */
#define BOARD_DVP_VSYNC_PIN         13      /**< DVP_VSYNC (P5) -> mPCIE I/O40 */
#define BOARD_DVP_PCLK_PIN          1       /**< DVP_PCLK (P10) -> mPCIE I/O38 */
#define BOARD_DVP_XCLK_PIN          0       /**< DVP_XCLK (N6) -> mPCIE I/O37 */
#define BOARD_DVP_RSTN_PIN          28      /**< DVP_RSTN (N5) - 摄像头复位 */

/** @brief DVP 引脚功能复用值 */
#define BOARD_DVP_DATA_PINMUX       4       /**< AF4 = DVP_DATA */
#define BOARD_DVP_CTRL_PINMUX       4       /**< AF4 = DVP Control */

/** @brief 摄像头电源控制引脚 */
#define BOARD_DVP_PWDOWN_PIN        19      /**< DVP_PWDOWN - 摄像头低功耗模式 */

/*===========================================================================
 * Section 9: SPI Display Interface (Optional)
 * SPI 显示接口配置 (用于调试)
 *===========================================================================*/

#define BOARD_DVP_O_CS_PIN          21      /**< DVP_O_CS_N (K3) - LCD片选 */
#define BOARD_DVP_O_DC_PIN          29      /**< DVP_O_DC (L2) - 数据/命令选择 */
#define BOARD_DVP_O_RSTN_PIN        30      /**< DVP_O_RSTN (L1) - LCD复位 */
#define BOARD_DVP_O_SCK_PIN         31      /**< DVP_O_SCK (P1) - SPI时钟 */
#define BOARD_DVP_O_SDO_PIN         25      /**< DVP_O_SDO (K4) - SPI数据 */
#define BOARD_DVP_O_BL_PIN          18      /**< DVP_O_BL - LCD背光控制 */

/*===========================================================================
 * Section 10: AON GPIO Configuration
 * 常开 GPIO 配置
 *===========================================================================*/

/** @brief AON GPIO 引脚 (M4, N4, L7, N7, P9, N10, M13, P2) */
#define BOARD_AON_GPIO0_PIN         0       /**< AON_GPIO_IN[0] (M4) */
#define BOARD_AON_GPIO1_PIN         1       /**< AON_GPIO_IN[1] (N4) */
#define BOARD_AON_GPIO2_PIN         2       /**< AON_GPIO_IN[2] (L7) */
#define BOARD_AON_GPIO3_PIN         3       /**< AON_GPIO_IN[3] (N7) */
#define BOARD_AON_GPIO4_PIN         4       /**< AON_GPIO_IN[4] (P9) */
#define BOARD_AON_GPIO5_PIN         5       /**< AON_GPIO_IN[5] (N10) */
#define BOARD_AON_GPIO6_PIN         6       /**< AON_GPIO_IN[6] (M13) */
#define BOARD_AON_GPIO7_PIN         7       /**< AON_GPIO_IN[7] (P2) */

/*===========================================================================
 * Section 11: Status LED Configuration
 * 状态 LED 配置
 *===========================================================================*/

/** @brief LED1 (绿色) - GPIO驱动三极管控制 */
#define BOARD_LED1_PIN              16      /**< 通过 Q1 控制 LED1 */
#define BOARD_LED1_ACTIVE_HIGH      1       /**< 高电平点亮 */

/** @brief LED2 (绿色) - GPIO驱动三极管控制 */
#define BOARD_LED2_PIN              17      /**< 通过 Q2 控制 LED2 */
#define BOARD_LED2_ACTIVE_HIGH      1       /**< 高电平点亮 */

/** @brief LED 电阻值 */
#define BOARD_LED_RESISTOR          20      /**< 20Ω 限流电阻 (R4, R10) */
#define BOARD_LED_CTRL_RESISTOR     2000    /**< 2kΩ 基极电阻 (R1, R2) */

/*===========================================================================
 * Section 12: Reset and Power Control
 * 复位与电源控制
 *===========================================================================*/

/** @brief 系统复位按键 (SW1) */
#define BOARD_RESET_BTN_PIN         7       /**< PORESETN_I (P7) */
#define BOARD_RESET_BTN_ACTIVE_LOW  1       /**< 低电平触发复位 */

/** @brief 电源域电压 */
#define BOARD_VDD_CORE              0.9f    /**< D0V9 - 核心电压 */
#define BOARD_VDD_IO_1V8            1.8f    /**< D1V8 - IO电压 */
#define BOARD_VDD_IO_3V3            3.3f    /**< D3V3 - IO电压 */

/*===========================================================================
 * Section 13: RGMII Ethernet Configuration (RTL8211F)
 * RGMII 以太网配置
 *===========================================================================*/

#if BOARD_RGMII_ENABLE

/** @brief PHY 地址 (由 PHYAD0/1/2 引脚配置) */
#define BOARD_PHY_ADDR              0x01    /**< RTL8211F 默认地址 */

/** @brief MDIO 管理接口引脚 */
#define BOARD_PHY_MDC_PIN           20      /**< GMII_MDC (P3) */
#define BOARD_PHY_MDIO_PIN          22      /**< GMII_MDIO (M1) */

/** @brief PHY 复位引脚 */
#define BOARD_PHY_RSTB_PIN          28      /**< 共用 DVP_RSTN */

/** @brief PHY LED 引脚 (通过 mPCIE I/O9, I/O10) */
#define BOARD_PHY_LED0_PIN          0       /**< PHY_LED0 -> mPCIE I/O9 */
#define BOARD_PHY_LED1_PIN          1       /**< PHY_LED1 -> mPCIE I/O10 */

/** @brief RGMII 时钟延迟配置 */
#define BOARD_PHY_TXDLY_ENABLE      1       /**< TX 2ns延迟 (R23 上拉) */
#define BOARD_PHY_RXDLY_ENABLE      1       /**< RX 2ns延迟 (R24 上拉) */

#endif /* BOARD_RGMII_ENABLE */

/*===========================================================================
 * Section 14: I2S Audio Interface (Optional)
 * I2S 音频接口配置 (可选)
 *===========================================================================*/

#define BOARD_I2S_MCLK_PIN          18      /**< I2S_MCLK */
#define BOARD_I2S1_CK_PIN           12      /**< I2S1_CK */
#define BOARD_I2S1_WS_PIN           13      /**< I2S1_WS */
#define BOARD_I2S1_DOUT_PIN         15      /**< I2S1_DATA_DOUT */
#define BOARD_I2S1_DIN_PIN          14      /**< I2S1_DATA_DIN */

/*===========================================================================
 * Section 15: PDM Microphone Interface
 * PDM 麦克风接口配置
 *===========================================================================*/

#define BOARD_PDM_CLK_PIN           5       /**< PDM_CLK (L5) */
#define BOARD_PDM0_L_PIN            6       /**< PDM0_L (M5) */
#define BOARD_PDM0_R_PIN            4       /**< PDM0_R (P4) */

/*===========================================================================
 * Section 16: Flash and PSRAM Configuration
 * Flash 和 PSRAM 配置
 *===========================================================================*/

/** @brief SPI Flash (W25Q128JVSIQ - U11) */
#define BOARD_FLASH_SIZE            (16 * 1024 * 1024)  /**< 16MB (128Mbit) */
#define BOARD_FLASH_PAGE_SIZE       256
#define BOARD_FLASH_SECTOR_SIZE     4096

/** @brief PSRAM (W956D8MBYA - U8) */
#define BOARD_PSRAM_SIZE            (8 * 1024 * 1024)   /**< 8MB */
#define BOARD_PSRAM_BASE_ADDR       0x30000000          /**< PSRAM 基地址 */

/*===========================================================================
 * Section 17: Motor/Servo Control (Via mPCIE)
 * 电机/舵机控制 (通过 mPCIE 传输)
 *===========================================================================*/

/** @brief 舵机总线使能引脚 */
#define BOARD_MOTO_BUSEN_PIN        0       /**< MOTO_BUSEN - 通过 mPCIE 到主板 */

/** @brief 功放使能引脚 */
#define BOARD_PA_EN_PIN             16      /**< PA_EN */

/*===========================================================================
 * Section 18: JTAG Debug Interface
 * JTAG 调试接口配置 (J1 连接器 - 7pin)
 *===========================================================================*/

#define BOARD_JTAG_TCK_PIN          2       /**< M_J_TCK (N2) - J1.4 */
#define BOARD_JTAG_TDI_PIN          3       /**< M_J_TDI (M3) - J1.5 */
#define BOARD_JTAG_TDO_PIN          1       /**< M_J_TDO (N1) - J1.6 */
#define BOARD_JTAG_TMS_PIN          2       /**< M_J_TMS (M2) - J1.7 */
#define BOARD_JTAG_NTRST_PIN        3       /**< nTRST (L3) - 通过 R131 */

/** @brief JTAG 连接器引脚定义 (J1 - PZ254V-11-07P) */
#define BOARD_JTAG_CONN_PIN1        26      /**< U3RXD */
#define BOARD_JTAG_CONN_PIN2        27      /**< U3TXD */
#define BOARD_JTAG_CONN_PIN3        0       /**< GND */
#define BOARD_JTAG_CONN_PIN4        0       /**< TCK */
#define BOARD_JTAG_CONN_PIN5        0       /**< TDI */
#define BOARD_JTAG_CONN_PIN6        0       /**< TDO */
#define BOARD_JTAG_CONN_PIN7        0       /**< TMS */

/*===========================================================================
 * Section 19: PWM Configuration
 * PWM 配置
 *===========================================================================*/

#define BOARD_PWM0_PIN              19      /**< PWM0 */

/*===========================================================================
 * Section 20: mPCIE Connector Pin Mapping
 * mPCIE 连接器引脚映射 (U3 - 52pin)
 *===========================================================================*/

/**
 * @brief mPCIE 连接器信号定义
 * 子板通过此连接器与主板互联
 */
typedef struct {
    /* 电源引脚 */
    uint8_t vcc_a;      /**< I/O1: VCC_A (D3V3) */
    uint8_t gnd_a;      /**< I/O2: GND_A */
    uint8_t vcc_b;      /**< I/O3: VCC_B (D1V8) */
    uint8_t gnd_b;      /**< I/O4: GND_B */
    uint8_t vcc_c;      /**< I/O5: VCC_C (D0V9) */
    uint8_t gnd_c;      /**< I/O6: GND_C */
    uint8_t vcc_d;      /**< I/O7: VCC_D */
    uint8_t gnd_d;      /**< I/O8: GND_D */
    
    /* PHY LED 状态 */
    uint8_t phy_led0;   /**< I/O9: PHY_LED0 */
    uint8_t phy_led1;   /**< I/O10: PHY_LED1 */
    
    /* 以太网 MDI 差分信号 */
    /* I/O11-22: MDI0..MDI3 P/N pairs */
    
    /* 摄像头数据 */
    uint8_t dvp_data[8];/**< I/O29-36: DVP_DATA0-7 */
    uint8_t dvp_xclk;   /**< I/O37: DVP_XCLK */
    uint8_t dvp_pclk;   /**< I/O38: DVP_PCLK */
    uint8_t dvp_hsync;  /**< I/O39: DVP_HSYNC */
    uint8_t dvp_vsync;  /**< I/O40: DVP_VSYNC */
    
    /* I2C 总线 */
    uint8_t i2c3_sck;   /**< I/O41: I2C3_SCK */
    uint8_t i2c3_sda;   /**< I/O42: I2C3_SDA */
    
    /* UART2 */
    uint8_t u2txd;      /**< I/O43: U2TXD */
    uint8_t u2rxd;      /**< I/O44: U2RXD */
    
    /* 控制信号 */
    uint8_t por_reset;  /**< I/O45: PORRESETn */
} board_mpcie_connector_t;

/*===========================================================================
 * Section 21: Board Initialization API
 * 板卡初始化接口
 *===========================================================================*/

/*===========================================================================
 * Compatibility Macros For Shared BSP Modules
 * 与通用驱动/示例工程保持兼容的别名定义
 *===========================================================================*/

#ifndef BOARD_LCD_TYPE
#define BOARD_LCD_TYPE              0
#endif

#ifndef BOARD_LCD_WIDTH
#define BOARD_LCD_WIDTH             160
#endif

#ifndef BOARD_LCD_HEIGHT
#define BOARD_LCD_HEIGHT            128
#endif

#ifndef BOARD_DISPLAY_WIDTH
#define BOARD_DISPLAY_WIDTH         BOARD_LCD_WIDTH
#endif

#ifndef BOARD_DISPLAY_HEIGHT
#define BOARD_DISPLAY_HEIGHT        BOARD_LCD_HEIGHT
#endif

#ifndef BOARD_CAMERA_I2C_PORT
#define BOARD_CAMERA_I2C_PORT       GPIOA
#endif

#ifndef BOARD_CAMERA_I2C_SCL_PIN
#define BOARD_CAMERA_I2C_SCL_PIN    BOARD_I2C3_SCL_PIN
#endif

#ifndef BOARD_CAMERA_I2C_SDA_PIN
#define BOARD_CAMERA_I2C_SDA_PIN    BOARD_I2C3_SDA_PIN
#endif

#ifndef BOARD_CAMERA_I2C_FUNCTION
#define BOARD_CAMERA_I2C_FUNCTION   FUNCTION_2
#endif

#ifndef BOARD_CAMERA_I2C_FREQ
#define BOARD_CAMERA_I2C_FREQ       50000u
#endif

#ifndef BOARD_CAM_PORT
#define BOARD_CAM_PORT              GPIOA
#endif

#ifndef BOARD_CAM_RST_PIN
#define BOARD_CAM_RST_PIN           BOARD_DVP_RSTN_PIN
#endif

#ifndef BOARD_CAM_PWDN_PIN
#define BOARD_CAM_PWDN_PIN          BOARD_DVP_PWDOWN_PIN
#endif

#ifndef BOARD_CAM_CTRL_FUNCTION
#define BOARD_CAM_CTRL_FUNCTION     FUNCTION_0
#endif

#ifndef BOARD_AUDIO_CODEC_ES7210
#define BOARD_AUDIO_CODEC_ES7210    1
#endif

#ifndef BOARD_AUDIO_CODEC_ES8311
#define BOARD_AUDIO_CODEC_ES8311    1
#endif

#ifndef BOARD_RGB_DIN1_PIN
#define BOARD_RGB_DIN1_PIN          BOARD_LED1_PIN
#endif

#ifndef BOARD_RGB_DIN2_PIN
#define BOARD_RGB_DIN2_PIN          BOARD_LED2_PIN
#endif

/**
 * @brief 板卡初始化
 * @details 初始化系统时钟、调试串口和基本外设
 * @return 0 成功, 负值 失败
 */
int board_init(void);

/**
 * @brief 时钟系统初始化
 * @return 0 成功, 负值 失败
 */
int board_clock_init(void);

/**
 * @brief 调试串口初始化 (UART3)
 * @return 0 成功, 负值 失败
 */
int board_debug_uart_init(void);

/**
 * @brief 主板通信串口初始化 (UART2)
 * @return 0 成功, 负值 失败
 */
int board_uart2_init(void);

/**
 * @brief I2C3 总线初始化
 * @return 0 成功, 负值 失败
 */
int board_i2c3_init(void);

/** @brief 初始化摄像头 I2C 引脚 (兼容通用 Demo) */
void board_camera_i2c_pins_init(void);

/** @brief 初始化摄像头控制引脚 (兼容通用 Demo) */
void board_camera_ctrl_pins_init(void);

/** @brief 摄像头上电时序 (兼容通用 Demo) */
void board_camera_power_on_sequence(void);

/** @brief 初始化摄像头 I2C (兼容通用 Demo) */
int board_camera_i2c_init(void *i2c);

/**
 * @brief 摄像头接口初始化
 * @return 0 成功, 负值 失败
 */
int board_camera_init(void);

/**
 * @brief LED 初始化
 */
void board_led_init(void);

/**
 * @brief LED 控制
 * @param led_id LED编号 (1 或 2)
 * @param on true=点亮, false=熄灭
 */
void board_led_set(uint8_t led_id, bool on);

/**
 * @brief LED 翻转
 * @param led_id LED编号 (1 或 2)
 */
void board_led_toggle(uint8_t led_id);

/** @brief 初始化舵机相关引脚 (兼容 Demo) */
void board_servo_pins_init(void);

/** @brief 初始化舵机总线 (兼容 Demo) */
int board_servo_init(void *servo);

/**
 * @brief 摄像头电源控制
 * @param on true=上电, false=断电
 */
void board_camera_power(bool on);

/**
 * @brief 摄像头复位
 */
void board_camera_reset(void);

#if BOARD_RGMII_ENABLE
/**
 * @brief PHY 初始化 (RTL8211F)
 * @return 0 成功, 负值 失败
 */
int board_phy_init(void);

/**
 * @brief PHY 复位
 */
void board_phy_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_BOARD_H */
