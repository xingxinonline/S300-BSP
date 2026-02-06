/**
 * @file es7210.c
 * @brief ES7210 4-Channel ADC Driver for S300 BSP
 *
 * 基于旧 SDK audio_es7210.c 移植，适配新的 i2c_soft 接口
 * 支持应用板和云台主控板的麦克风输入
 */

#include "es7210.h"
#include <string.h>

/* =============================================================================
 * 内部常量定义
 * =============================================================================
 */

/* MCLK 来源选择 */
#define ES7210_MCLK_FROM_PAD       0
#define ES7210_MCLK_FROM_DOUBLER   1
#define ES7210_MCLK_SOURCE         ES7210_MCLK_FROM_DOUBLER

/* 时钟系数表 */
typedef struct {
    uint32_t mclk;           /* mclk frequency */
    uint32_t lrck;           /* lrck (sample rate) */
    uint8_t  ss_ds;          /* single/double speed */
    uint8_t  adc_div;        /* adcclk divider */
    uint8_t  dll;            /* dll_bypass */
    uint8_t  doubler;        /* doubler enable */
    uint8_t  osr;            /* adc osr */
    uint8_t  mclk_src;       /* select mclk source */
    uint8_t  lrck_h;         /* The high 4 bits of lrck */
    uint8_t  lrck_l;         /* The low 8 bits of lrck */
} es7210_coeff_t;

static const es7210_coeff_t coeff_div[] = {
    /* mclk       lrck    ss_ds adc_div dll  doubler osr  mclk_src lrckh lrckl */
    /* 8k */
    {12288000,   8000,   0x00, 0x03, 0x01, 0x00, 0x20, 0x00, 0x06, 0x00},
    {16384000,   8000,   0x00, 0x04, 0x01, 0x00, 0x20, 0x00, 0x08, 0x00},
    {19200000,   8000,   0x00, 0x1e, 0x00, 0x01, 0x28, 0x00, 0x09, 0x60},
    {4096000,    8000,   0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},
    /* 11.025k */
    {11289600,  11025,   0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x01, 0x00},
    /* 12k */
    {12288000,  12000,   0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x04, 0x00},
    {19200000,  12000,   0x00, 0x14, 0x00, 0x01, 0x28, 0x00, 0x06, 0x40},
    /* 16k */
    {4096000,   16000,   0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},
    {19200000,  16000,   0x00, 0x0a, 0x00, 0x00, 0x1e, 0x00, 0x04, 0x80},
    {16384000,  16000,   0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x04, 0x00},
    {12288000,  16000,   0x00, 0x03, 0x01, 0x01, 0x20, 0x00, 0x03, 0x00},
    /* 22.05k */
    {11289600,  22050,   0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},
    /* 24k */
    {12288000,  24000,   0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},
    {19200000,  24000,   0x00, 0x0a, 0x00, 0x01, 0x28, 0x00, 0x03, 0x20},
    /* 32k */
    {12288000,  32000,   0x00, 0x03, 0x00, 0x00, 0x20, 0x00, 0x01, 0x80},
    {16384000,  32000,   0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},
    {19200000,  32000,   0x00, 0x05, 0x00, 0x00, 0x1e, 0x00, 0x02, 0x58},
    /* 44.1k */
    {11289600,  44100,   0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},
    /* 48k */
    {12288000,  48000,   0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},
    {19200000,  48000,   0x00, 0x05, 0x00, 0x01, 0x28, 0x00, 0x01, 0x90},
    /* 64k */
    {16384000,  64000,   0x01, 0x01, 0x01, 0x00, 0x20, 0x00, 0x01, 0x00},
    {19200000,  64000,   0x00, 0x05, 0x00, 0x01, 0x1e, 0x00, 0x01, 0x2c},
    /* 88.2k */
    {11289600,  88200,   0x01, 0x01, 0x01, 0x01, 0x20, 0x00, 0x00, 0x80},
    /* 96k */
    {12288000,  96000,   0x01, 0x01, 0x01, 0x01, 0x20, 0x00, 0x00, 0x80},
    {19200000,  96000,   0x01, 0x05, 0x00, 0x01, 0x28, 0x00, 0x00, 0xc8},
};

#define COEFF_COUNT (sizeof(coeff_div) / sizeof(coeff_div[0]))

/* =============================================================================
 * 内部函数
 * =============================================================================
 */

int es7210_read_reg(es7210_t *dev, uint8_t reg)
{
    uint8_t val;
    if (i2c_soft_mem_read(dev->i2c, dev->addr, reg, false, &val, 1) != 0) {
        return -1;
    }
    return (int)val;
}

int es7210_write_reg(es7210_t *dev, uint8_t reg, uint8_t val)
{
    return i2c_soft_mem_write(dev->i2c, dev->addr, reg, false, &val, 1);
}

static int es7210_update_bits(es7210_t *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
    int old = es7210_read_reg(dev, reg);
    if (old < 0) return -1;
    uint8_t newv = (uint8_t)((old & ~mask) | (val & mask));
    return es7210_write_reg(dev, reg, newv);
}

static int get_coeff(uint32_t mclk, uint32_t lrck)
{
    for (size_t i = 0; i < COEFF_COUNT; i++) {
        if (coeff_div[i].lrck == lrck && coeff_div[i].mclk == mclk) {
            return (int)i;
        }
    }
    return -1;
}

/* =============================================================================
 * 公共 API 实现
 * =============================================================================
 */

int es7210_init(es7210_t *dev, i2c_soft_t *i2c, uint8_t addr,
                es7210_sample_t sample_rate, es7210_mode_t mode)
{
    if (!dev || !i2c) return -1;

    dev->i2c = i2c;
    dev->addr = addr;
    dev->mic_select = ES7210_INPUT_MIC1;
    dev->gain = ES7210_GAIN_24DB;

    /* 软复位 */
    es7210_write_reg(dev, ES7210_RESET_REG00, 0xFF);
    es7210_write_reg(dev, ES7210_RESET_REG00, 0x32);
    es7210_write_reg(dev, ES7210_CLOCK_OFF_REG01, 0x00);

    /* 时钟配置 */
    es7210_write_reg(dev, ES7210_TIME_CONTROL0_REG09, 0x30);
    es7210_write_reg(dev, ES7210_TIME_CONTROL1_REG0A, 0x30);

    /* 配置主/从模式 */
    if (mode == ES7210_MODE_MASTER) {
        /* Master mode: MCLK from internal, generate SCLK/LRCK */
        es7210_write_reg(dev, ES7210_MODE_REG08, 0x20);  /* Master mode */
    } else {
        /* Slave mode: external SCLK/LRCK */
        es7210_write_reg(dev, ES7210_MODE_REG08, 0x00);  /* Slave mode */
    }

    /* 模拟电源配置 */
    es7210_write_reg(dev, ES7210_ANALOG_REG40, 0x43);  /* VMID, VREF */
    es7210_write_reg(dev, ES7210_MIC12_BIAS_REG41, 0x70);
    es7210_write_reg(dev, ES7210_MIC34_BIAS_REG42, 0x70);
    es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, 0x0F);
    es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, 0x0F);

    /* 默认选择 MIC1 */
    es7210_mic_select(dev, ES7210_INPUT_MIC1);

    /* 配置采样率 (基于 12.288MHz MCLK) */
    uint32_t mclk = 12288000;
    int coeff_idx = get_coeff(mclk, (uint32_t)sample_rate);
    if (coeff_idx >= 0) {
        const es7210_coeff_t *coeff = &coeff_div[coeff_idx];
        es7210_write_reg(dev, ES7210_MAINCLK_REG02, coeff->adc_div);
        es7210_write_reg(dev, ES7210_LRCK_DIVH_REG04, coeff->lrck_h);
        es7210_write_reg(dev, ES7210_LRCK_DIVL_REG05, coeff->lrck_l);
        es7210_write_reg(dev, ES7210_OSR_REG07, coeff->osr);
    }

    /* 使能 HPF (高通滤波器消除直流偏置) */
    es7210_write_reg(dev, ES7210_ADC12_HPF2_REG23, 0x2A);
    es7210_write_reg(dev, ES7210_ADC12_HPF1_REG22, 0x0A);
    es7210_write_reg(dev, ES7210_ADC34_HPF2_REG20, 0x2A);
    es7210_write_reg(dev, ES7210_ADC34_HPF1_REG21, 0x0A);

    return 0;
}

int es7210_config_i2s(es7210_t *dev, es7210_i2s_fmt_t fmt,
                      es7210_sample_t sample_rate, es7210_bits_t bits)
{
    if (!dev) return -1;

    uint8_t val = 0;

    /* 设置 I2S 格式 */
    switch (fmt) {
        case ES7210_I2S_NORMAL:
            val = 0x00;
            break;
        case ES7210_I2S_LEFT:
            val = 0x01;
            break;
        case ES7210_I2S_RIGHT:
            val = 0x02;
            break;
        case ES7210_I2S_DSP:
            val = 0x03;
            break;
        default:
            val = 0x00;
            break;
    }
    es7210_update_bits(dev, ES7210_SDP_INTERFACE1_REG11, 0x03, val);

    /* 设置字长 */
    uint8_t bits_val = 0;
    switch (bits) {
        case ES7210_BITS_16:
            bits_val = 0x03;  /* 16-bit */
            break;
        case ES7210_BITS_18:
            bits_val = 0x02;  /* 18-bit */
            break;
        case ES7210_BITS_20:
            bits_val = 0x01;  /* 20-bit */
            break;
        case ES7210_BITS_24:
            bits_val = 0x00;  /* 24-bit */
            break;
        case ES7210_BITS_32:
            bits_val = 0x04;  /* 32-bit */
            break;
        default:
            bits_val = 0x03;  /* 默认 16-bit */
            break;
    }
    es7210_update_bits(dev, ES7210_SDP_INTERFACE1_REG11, 0x1C, bits_val << 2);

    /* 更新采样率 */
    uint32_t mclk = 12288000;
    int coeff_idx = get_coeff(mclk, (uint32_t)sample_rate);
    if (coeff_idx >= 0) {
        const es7210_coeff_t *coeff = &coeff_div[coeff_idx];
        es7210_write_reg(dev, ES7210_MAINCLK_REG02, coeff->adc_div);
        es7210_write_reg(dev, ES7210_LRCK_DIVH_REG04, coeff->lrck_h);
        es7210_write_reg(dev, ES7210_LRCK_DIVL_REG05, coeff->lrck_l);
        es7210_write_reg(dev, ES7210_OSR_REG07, coeff->osr);
    }

    return 0;
}

int es7210_mic_select(es7210_t *dev, es7210_input_mics_t mic_select)
{
    if (!dev) return -1;

    dev->mic_select = mic_select;

    /* 配置 ADC 通道和 PGA */
    uint8_t adc_en = 0;

    if (mic_select & ES7210_INPUT_MIC1) {
        es7210_write_reg(dev, ES7210_MIC1_POWER_REG47, 0x08);
        es7210_write_reg(dev, ES7210_MIC1_GAIN_REG43, dev->gain);
        adc_en |= 0x01;
    } else {
        es7210_write_reg(dev, ES7210_MIC1_POWER_REG47, 0x00);
    }

    if (mic_select & ES7210_INPUT_MIC2) {
        es7210_write_reg(dev, ES7210_MIC2_POWER_REG48, 0x08);
        es7210_write_reg(dev, ES7210_MIC2_GAIN_REG44, dev->gain);
        adc_en |= 0x02;
    } else {
        es7210_write_reg(dev, ES7210_MIC2_POWER_REG48, 0x00);
    }

    if (mic_select & ES7210_INPUT_MIC3) {
        es7210_write_reg(dev, ES7210_MIC3_POWER_REG49, 0x08);
        es7210_write_reg(dev, ES7210_MIC3_GAIN_REG45, dev->gain);
        adc_en |= 0x04;
    } else {
        es7210_write_reg(dev, ES7210_MIC3_POWER_REG49, 0x00);
    }

    if (mic_select & ES7210_INPUT_MIC4) {
        es7210_write_reg(dev, ES7210_MIC4_POWER_REG4A, 0x08);
        es7210_write_reg(dev, ES7210_MIC4_GAIN_REG46, dev->gain);
        adc_en |= 0x08;
    } else {
        es7210_write_reg(dev, ES7210_MIC4_POWER_REG4A, 0x00);
    }

    /* MIC12 和 MIC34 电源控制 */
    if (mic_select & (ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2)) {
        es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, 0x0F);
    } else {
        es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, 0x00);
    }

    if (mic_select & (ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4)) {
        es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, 0x0F);
    } else {
        es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, 0x00);
    }

    return 0;
}

int es7210_set_gain(es7210_t *dev, es7210_input_mics_t mic_select, es7210_gain_t gain)
{
    if (!dev) return -1;

    dev->gain = gain;

    if (mic_select & ES7210_INPUT_MIC1) {
        es7210_write_reg(dev, ES7210_MIC1_GAIN_REG43, gain);
    }
    if (mic_select & ES7210_INPUT_MIC2) {
        es7210_write_reg(dev, ES7210_MIC2_GAIN_REG44, gain);
    }
    if (mic_select & ES7210_INPUT_MIC3) {
        es7210_write_reg(dev, ES7210_MIC3_GAIN_REG45, gain);
    }
    if (mic_select & ES7210_INPUT_MIC4) {
        es7210_write_reg(dev, ES7210_MIC4_GAIN_REG46, gain);
    }

    return 0;
}

int es7210_set_volume(es7210_t *dev, uint8_t volume)
{
    if (!dev) return -1;

    /* ES7210 没有独立的数字音量控制，使用增益代替 */
    es7210_gain_t gain = (es7210_gain_t)(volume >> 4);  /* 0-255 映射到 0-15 */
    if (gain > ES7210_GAIN_37_5DB) {
        gain = ES7210_GAIN_37_5DB;
    }

    return es7210_set_gain(dev, dev->mic_select, gain);
}

int es7210_set_mute(es7210_t *dev, bool mute)
{
    if (!dev) return -1;

    if (mute) {
        es7210_update_bits(dev, ES7210_ADC_AUTOMUTE_REG13, 0x0F, 0x0F);
    } else {
        es7210_update_bits(dev, ES7210_ADC_AUTOMUTE_REG13, 0x0F, 0x00);
    }

    return 0;
}

int es7210_start(es7210_t *dev)
{
    if (!dev) return -1;

    /* 启动 ADC 时钟 */
    es7210_write_reg(dev, ES7210_CLOCK_OFF_REG01, 0x00);

    /* 上电模拟部分 */
    es7210_write_reg(dev, ES7210_ANALOG_REG40, 0x43);
    es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, 0x0F);
    es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, 0x0F);

    /* 软复位后启动 */
    es7210_write_reg(dev, ES7210_RESET_REG00, 0x71);
    es7210_write_reg(dev, ES7210_RESET_REG00, 0x41);

    return 0;
}

int es7210_stop(es7210_t *dev)
{
    if (!dev) return -1;

    /* 关闭 ADC 时钟 */
    es7210_write_reg(dev, ES7210_CLOCK_OFF_REG01, 0x7F);

    /* 下电模拟部分 */
    es7210_write_reg(dev, ES7210_ANALOG_REG40, 0x00);
    es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, 0x00);
    es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, 0x00);

    /* 进入复位状态 */
    es7210_write_reg(dev, ES7210_RESET_REG00, 0x00);

    return 0;
}
