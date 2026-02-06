#ifndef S300_BSP_ES8311_H
#define S300_BSP_ES8311_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "i2c_soft.h"

/* =============================================================================
 * ES8311 - Low Power Mono Audio Codec (DAC + ADC)
 * 支持应用板和云台主控板的音频播放和录音
 * =============================================================================
 */

/* ES8311 I2C 7-bit 地址 (根据 CE 引脚配置)
 * CE=0: 0x18
 * CE=1: 0x19
 */
#define ES8311_I2C_ADDR_CE0  0x18  /* CE=0 (默认) */
#define ES8311_I2C_ADDR_CE1  0x19  /* CE=1 */

#define ES8311_DEFAULT_ADDR  ES8311_I2C_ADDR_CE0

/* =============================================================================
 * ES8311 寄存器定义
 * =============================================================================
 */
/* Reset */
#define ES8311_RESET_REG00              0x00  /* Reset digital, CSM, clock manager etc. */

/* Clock Scheme */
#define ES8311_CLK_MANAGER_REG01        0x01  /* Select clk src for mclk, enable clock for codec */
#define ES8311_CLK_MANAGER_REG02        0x02  /* Clk divider and clk multiplier */
#define ES8311_CLK_MANAGER_REG03        0x03  /* ADC fsmode and osr */
#define ES8311_CLK_MANAGER_REG04        0x04  /* DAC osr */
#define ES8311_CLK_MANAGER_REG05        0x05  /* Clk divider for adc and dac */
#define ES8311_CLK_MANAGER_REG06        0x06  /* BCLK inverter and divider */
#define ES8311_CLK_MANAGER_REG07        0x07  /* Tri-state, lrck divider */
#define ES8311_CLK_MANAGER_REG08        0x08  /* LRCK divider */

/* SDP (Serial Data Port) */
#define ES8311_SDPIN_REG09              0x09  /* DAC serial digital port */
#define ES8311_SDPOUT_REG0A             0x0A  /* ADC serial digital port */

/* System */
#define ES8311_SYSTEM_REG0B             0x0B  /* System */
#define ES8311_SYSTEM_REG0C             0x0C  /* System */
#define ES8311_SYSTEM_REG0D             0x0D  /* System, power up/down */
#define ES8311_SYSTEM_REG0E             0x0E  /* System, power up/down */
#define ES8311_SYSTEM_REG0F             0x0F  /* System, low power */
#define ES8311_SYSTEM_REG10             0x10  /* System */
#define ES8311_SYSTEM_REG11             0x11  /* System */
#define ES8311_SYSTEM_REG12             0x12  /* System, Enable DAC */
#define ES8311_SYSTEM_REG13             0x13  /* System */
#define ES8311_SYSTEM_REG14             0x14  /* System, select DMIC, select analog pga gain */

/* ADC */
#define ES8311_ADC_REG15                0x15  /* ADC, adc ramp rate, dmic sense */
#define ES8311_ADC_REG16                0x16  /* ADC */
#define ES8311_ADC_REG17                0x17  /* ADC, volume */
#define ES8311_ADC_REG18                0x18  /* ADC, alc enable and winsize */
#define ES8311_ADC_REG19                0x19  /* ADC, alc maxlevel */
#define ES8311_ADC_REG1A                0x1A  /* ADC, alc automute */
#define ES8311_ADC_REG1B                0x1B  /* ADC, alc automute, adc hpf s1 */
#define ES8311_ADC_REG1C                0x1C  /* ADC, equalizer, hpf s2 */

/* DAC */
#define ES8311_DAC_REG31                0x31  /* DAC, mute */
#define ES8311_DAC_REG32                0x32  /* DAC, volume */
#define ES8311_DAC_REG33                0x33  /* DAC, offset */
#define ES8311_DAC_REG34                0x34  /* DAC, drc enable, drc winsize */
#define ES8311_DAC_REG35                0x35  /* DAC, drc maxlevel, minilevel */
#define ES8311_DAC_REG37                0x37  /* DAC, ramprate */

/* GPIO */
#define ES8311_GPIO_REG44               0x44  /* GPIO, dac2adc for test */
#define ES8311_GP_REG45                 0x45  /* GP CONTROL */

/* Chip */
#define ES8311_CHD1_REGFD               0xFD  /* CHIP ID1 */
#define ES8311_CHD2_REGFE               0xFE  /* CHIP ID2 */
#define ES8311_CHVER_REGFF              0xFF  /* VERSION */

#define ES8311_MAX_REGISTER             0xFF

/* =============================================================================
 * 枚举类型定义
 * =============================================================================
 */

/* 采样率 */
typedef enum {
    ES8311_SAMPLE_8K   = 8000,
    ES8311_SAMPLE_11K  = 11025,
    ES8311_SAMPLE_12K  = 12000,
    ES8311_SAMPLE_16K  = 16000,
    ES8311_SAMPLE_22K  = 22050,
    ES8311_SAMPLE_24K  = 24000,
    ES8311_SAMPLE_32K  = 32000,
    ES8311_SAMPLE_44K  = 44100,
    ES8311_SAMPLE_48K  = 48000
} es8311_sample_t;

/* I2S 数据格式 */
typedef enum {
    ES8311_I2S_NORMAL = 0,   /* I2S Philips 标准 */
    ES8311_I2S_LEFT   = 1,   /* Left Justified */
    ES8311_I2S_RIGHT  = 2,   /* Right Justified (不推荐) */
    ES8311_I2S_DSP    = 3    /* DSP/PCM 模式 */
} es8311_i2s_fmt_t;

/* 字长 */
typedef enum {
    ES8311_BITS_16 = 16,
    ES8311_BITS_18 = 18,
    ES8311_BITS_20 = 20,
    ES8311_BITS_24 = 24,
    ES8311_BITS_32 = 32
} es8311_bits_t;

/* 工作模式 */
typedef enum {
    ES8311_MODE_SLAVE = 0,
    ES8311_MODE_MASTER = 1
} es8311_mode_t;

/* 编解码模式 */
typedef enum {
    ES8311_CODEC_ADC = 0,    /* 仅 ADC (录音) */
    ES8311_CODEC_DAC = 1,    /* 仅 DAC (播放) */
    ES8311_CODEC_BOTH = 2    /* ADC + DAC */
} es8311_codec_mode_t;

/* 麦克风增益 */
typedef enum {
    ES8311_MIC_GAIN_MIN = -1,
    ES8311_MIC_GAIN_0DB = 0,
    ES8311_MIC_GAIN_6DB,
    ES8311_MIC_GAIN_12DB,
    ES8311_MIC_GAIN_18DB,
    ES8311_MIC_GAIN_24DB,
    ES8311_MIC_GAIN_30DB,
    ES8311_MIC_GAIN_36DB,
    ES8311_MIC_GAIN_42DB,
    ES8311_MIC_GAIN_MAX
} es8311_mic_gain_t;

/* ES8311 设备句柄 */
typedef struct {
    i2c_soft_t *i2c;
    uint8_t addr;
    es8311_sample_t sample_rate;
    es8311_mode_t mode;
} es8311_t;

/* =============================================================================
 * API 函数声明
 * =============================================================================
 */

/**
 * @brief 初始化 ES8311
 * @param dev 设备句柄
 * @param i2c I2C 软模拟句柄
 * @param addr I2C 7-bit 地址
 * @param mode 主/从模式
 * @param sample_rate 采样率
 * @return 0 成功, 非0 失败
 */
int es8311_init(es8311_t *dev, i2c_soft_t *i2c, uint8_t addr,
                es8311_mode_t mode, es8311_sample_t sample_rate);

/**
 * @brief 配置 I2S 接口
 * @param dev 设备句柄
 * @param bits 字长
 * @param fmt I2S 格式
 * @return 0 成功, 非0 失败
 */
int es8311_config_i2s(es8311_t *dev, es8311_bits_t bits, es8311_i2s_fmt_t fmt);

/**
 * @brief 控制编解码状态
 * @param dev 设备句柄
 * @param codec_mode 编解码模式 (ADC/DAC/BOTH)
 * @param start true=启动, false=停止
 * @return 0 成功, 非0 失败
 */
int es8311_ctrl_state(es8311_t *dev, es8311_codec_mode_t codec_mode, bool start);

/**
 * @brief 设置播放音量
 * @param dev 设备句柄
 * @param volume 音量值 (0-255)
 * @return 0 成功, 非0 失败
 */
int es8311_set_volume(es8311_t *dev, uint8_t volume);

/**
 * @brief 获取播放音量
 * @param dev 设备句柄
 * @param volume 返回的音量值
 * @return 0 成功, 非0 失败
 */
int es8311_get_volume(es8311_t *dev, uint8_t *volume);

/**
 * @brief 设置静音
 * @param dev 设备句柄
 * @param mute true=静音, false=取消静音
 * @return 0 成功, 非0 失败
 */
int es8311_set_mute(es8311_t *dev, bool mute);

/**
 * @brief 设置麦克风增益
 * @param dev 设备句柄
 * @param gain 增益值
 * @return 0 成功, 非0 失败
 */
int es8311_set_mic_gain(es8311_t *dev, es8311_mic_gain_t gain);

/**
 * @brief 启动 codec (ADC/DAC/Both)
 * @param dev 设备句柄
 * @param codec_mode 编解码模式
 * @return 0 成功, 非0 失败
 */
int es8311_start(es8311_t *dev, es8311_codec_mode_t codec_mode);

/**
 * @brief 停止 codec
 * @param dev 设备句柄
 * @return 0 成功, 非0 失败
 */
int es8311_stop(es8311_t *dev);

/**
 * @brief PA 电源控制
 * @param enable true=开启, false=关闭
 * @return 0 成功, 非0 失败
 */
int es8311_pa_power(bool enable);

/**
 * @brief 读取寄存器 (调试用)
 * @param dev 设备句柄
 * @param reg 寄存器地址
 * @return 寄存器值, 失败返回 -1
 */
int es8311_read_reg(es8311_t *dev, uint8_t reg);

/**
 * @brief 写入寄存器 (调试用)
 * @param dev 设备句柄
 * @param reg 寄存器地址
 * @param val 寄存器值
 * @return 0 成功, 非0 失败
 */
int es8311_write_reg(es8311_t *dev, uint8_t reg, uint8_t val);

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_ES8311_H */
