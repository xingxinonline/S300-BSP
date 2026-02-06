#ifndef S300_BSP_ES7210_H
#define S300_BSP_ES7210_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "i2c_soft.h"

/* =============================================================================
 * ES7210 - 4-Channel ADC (Audio Codec for Microphone Input)
 * 支持应用板和云台主控板的麦克风输入
 * =============================================================================
 */

/* ES7210 I2C 7-bit 地址 (根据 AD1/AD0 引脚配置) */
#define ES7210_I2C_ADDR_00  0x40  /* AD1=0, AD0=0 */
#define ES7210_I2C_ADDR_01  0x41  /* AD1=0, AD0=1 (默认) */
#define ES7210_I2C_ADDR_10  0x42  /* AD1=1, AD0=0 */
#define ES7210_I2C_ADDR_11  0x43  /* AD1=1, AD0=1 */

#define ES7210_DEFAULT_ADDR ES7210_I2C_ADDR_01

/* =============================================================================
 * ES7210 寄存器定义
 * =============================================================================
 */
#define ES7210_RESET_REG00           0x00  /* Reset control */
#define ES7210_CLOCK_OFF_REG01       0x01  /* Used to turn off the ADC clock */
#define ES7210_MAINCLK_REG02         0x02  /* Set ADC clock frequency division */
#define ES7210_MASTER_CLK_REG03      0x03  /* MCLK source & SCLK division */
#define ES7210_LRCK_DIVH_REG04       0x04  /* lrck_divh */
#define ES7210_LRCK_DIVL_REG05       0x05  /* lrck_divl */
#define ES7210_POWER_DOWN_REG06      0x06  /* power down */
#define ES7210_OSR_REG07             0x07  /* OSR */
#define ES7210_MODE_REG08            0x08  /* Set master/slave & channels */
#define ES7210_TIME_CONTROL0_REG09   0x09  /* Set Chip initial state period */
#define ES7210_TIME_CONTROL1_REG0A   0x0A  /* Set Power up state period */
#define ES7210_SDP_INTERFACE1_REG11  0x11  /* Set sample & fmt */
#define ES7210_SDP_INTERFACE2_REG12  0x12  /* Pins state */
#define ES7210_ADC_AUTOMUTE_REG13    0x13  /* Set mute */
#define ES7210_ADC34_MUTERANGE_REG14 0x14  /* Set mute range */
#define ES7210_ADC12_MUTERANGE_REG15 0x15  /* Set mute range */
#define ES7210_ADC34_HPF2_REG20      0x20  /* HPF */
#define ES7210_ADC34_HPF1_REG21      0x21  /* HPF */
#define ES7210_ADC12_HPF1_REG22      0x22  /* HPF */
#define ES7210_ADC12_HPF2_REG23      0x23  /* HPF */
#define ES7210_ANALOG_REG40          0x40  /* ANALOG Power */
#define ES7210_MIC12_BIAS_REG41      0x41  /* MIC12 Bias */
#define ES7210_MIC34_BIAS_REG42      0x42  /* MIC34 Bias */
#define ES7210_MIC1_GAIN_REG43       0x43  /* MIC1 Gain */
#define ES7210_MIC2_GAIN_REG44       0x44  /* MIC2 Gain */
#define ES7210_MIC3_GAIN_REG45       0x45  /* MIC3 Gain */
#define ES7210_MIC4_GAIN_REG46       0x46  /* MIC4 Gain */
#define ES7210_MIC1_POWER_REG47      0x47  /* MIC1 Power */
#define ES7210_MIC2_POWER_REG48      0x48  /* MIC2 Power */
#define ES7210_MIC3_POWER_REG49      0x49  /* MIC3 Power */
#define ES7210_MIC4_POWER_REG4A      0x4A  /* MIC4 Power */
#define ES7210_MIC12_POWER_REG4B     0x4B  /* MICBias & ADC & PGA Power */
#define ES7210_MIC34_POWER_REG4C     0x4C  /* MICBias & ADC & PGA Power */

/* =============================================================================
 * 枚举类型定义
 * =============================================================================
 */

/* 麦克风输入选择 (可组合使用) */
typedef enum {
    ES7210_INPUT_MIC1 = 0x01,
    ES7210_INPUT_MIC2 = 0x02,
    ES7210_INPUT_MIC3 = 0x04,
    ES7210_INPUT_MIC4 = 0x08
} es7210_input_mics_t;

/* 增益值 (0dB ~ 37.5dB) */
typedef enum {
    ES7210_GAIN_0DB = 0,
    ES7210_GAIN_3DB,
    ES7210_GAIN_6DB,
    ES7210_GAIN_9DB,
    ES7210_GAIN_12DB,
    ES7210_GAIN_15DB,
    ES7210_GAIN_18DB,
    ES7210_GAIN_21DB,
    ES7210_GAIN_24DB,
    ES7210_GAIN_27DB,
    ES7210_GAIN_30DB,
    ES7210_GAIN_33DB,
    ES7210_GAIN_34_5DB,
    ES7210_GAIN_36DB,
    ES7210_GAIN_37_5DB,
    ES7210_GAIN_MAX
} es7210_gain_t;

/* 采样率 */
typedef enum {
    ES7210_SAMPLE_8K   = 8000,
    ES7210_SAMPLE_11K  = 11025,
    ES7210_SAMPLE_12K  = 12000,
    ES7210_SAMPLE_16K  = 16000,
    ES7210_SAMPLE_22K  = 22050,
    ES7210_SAMPLE_24K  = 24000,
    ES7210_SAMPLE_32K  = 32000,
    ES7210_SAMPLE_44K  = 44100,
    ES7210_SAMPLE_48K  = 48000,
    ES7210_SAMPLE_64K  = 64000,
    ES7210_SAMPLE_88K  = 88200,
    ES7210_SAMPLE_96K  = 96000
} es7210_sample_t;

/* I2S 数据格式 */
typedef enum {
    ES7210_I2S_NORMAL = 0,   /* I2S Philips 标准 */
    ES7210_I2S_LEFT   = 1,   /* Left Justified */
    ES7210_I2S_RIGHT  = 2,   /* Right Justified */
    ES7210_I2S_DSP    = 3    /* DSP/PCM 模式 */
} es7210_i2s_fmt_t;

/* 字长 */
typedef enum {
    ES7210_BITS_16 = 16,
    ES7210_BITS_18 = 18,
    ES7210_BITS_20 = 20,
    ES7210_BITS_24 = 24,
    ES7210_BITS_32 = 32
} es7210_bits_t;

/* 工作模式 */
typedef enum {
    ES7210_MODE_SLAVE = 0,
    ES7210_MODE_MASTER = 1
} es7210_mode_t;

/* ES7210 设备句柄 */
typedef struct {
    i2c_soft_t *i2c;
    uint8_t addr;
    es7210_input_mics_t mic_select;
    es7210_gain_t gain;
} es7210_t;

/* =============================================================================
 * API 函数声明
 * =============================================================================
 */

/**
 * @brief 初始化 ES7210
 * @param dev 设备句柄
 * @param i2c I2C 软模拟句柄
 * @param addr I2C 7-bit 地址
 * @param sample_rate 采样率
 * @param mode 主/从模式
 * @return 0 成功, 非0 失败
 */
int es7210_init(es7210_t *dev, i2c_soft_t *i2c, uint8_t addr,
                es7210_sample_t sample_rate, es7210_mode_t mode);

/**
 * @brief 配置 I2S 接口
 * @param dev 设备句柄
 * @param fmt I2S 格式
 * @param sample_rate 采样率
 * @param bits 字长
 * @return 0 成功, 非0 失败
 */
int es7210_config_i2s(es7210_t *dev, es7210_i2s_fmt_t fmt,
                      es7210_sample_t sample_rate, es7210_bits_t bits);

/**
 * @brief 选择麦克风输入
 * @param dev 设备句柄
 * @param mic_select 麦克风选择 (可用 | 组合多个)
 * @return 0 成功, 非0 失败
 */
int es7210_mic_select(es7210_t *dev, es7210_input_mics_t mic_select);

/**
 * @brief 设置增益
 * @param dev 设备句柄
 * @param mic_select 目标麦克风
 * @param gain 增益值
 * @return 0 成功, 非0 失败
 */
int es7210_set_gain(es7210_t *dev, es7210_input_mics_t mic_select, es7210_gain_t gain);

/**
 * @brief 设置音量 (所有通道统一)
 * @param dev 设备句柄
 * @param volume 音量值 (0-255)
 * @return 0 成功, 非0 失败
 */
int es7210_set_volume(es7210_t *dev, uint8_t volume);

/**
 * @brief 设置静音
 * @param dev 设备句柄
 * @param mute true=静音, false=取消静音
 * @return 0 成功, 非0 失败
 */
int es7210_set_mute(es7210_t *dev, bool mute);

/**
 * @brief 启动 ADC
 * @param dev 设备句柄
 * @return 0 成功, 非0 失败
 */
int es7210_start(es7210_t *dev);

/**
 * @brief 停止 ADC
 * @param dev 设备句柄
 * @return 0 成功, 非0 失败
 */
int es7210_stop(es7210_t *dev);

/**
 * @brief 读取寄存器 (调试用)
 * @param dev 设备句柄
 * @param reg 寄存器地址
 * @return 寄存器值, 失败返回 -1
 */
int es7210_read_reg(es7210_t *dev, uint8_t reg);

/**
 * @brief 写入寄存器 (调试用)
 * @param dev 设备句柄
 * @param reg 寄存器地址
 * @param val 寄存器值
 * @return 0 成功, 非0 失败
 */
int es7210_write_reg(es7210_t *dev, uint8_t reg, uint8_t val);

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_ES7210_H */
