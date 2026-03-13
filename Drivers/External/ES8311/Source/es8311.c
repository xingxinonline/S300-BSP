/**
 * @file es8311.c
 * @brief ES8311 Low Power Mono Audio Codec Driver for S300 BSP
 *
 * 基于旧 SDK audio_es8311.c 移植，适配新的 i2c_soft 接口
 * 支持应用板和云台主控板的音频播放和录音
 */

#include "es8311.h"
#include <string.h>

/* =============================================================================
 * 内部常量定义
 * =============================================================================
 */

/* MCLK 来源 */
#define ES8311_MCLK_FROM_MCLK_PIN   0
#define ES8311_MCLK_FROM_SCLK_PIN   1

/* 是否反转时钟 */
#define ES8311_INVERT_MCLK    0
#define ES8311_INVERT_SCLK    0

/* 是否使用数字麦克风 */
#define ES8311_IS_DMIC        0

/* MCLK 分频系数 */
#define ES8311_MCLK_DIV_FRE   768

/* 时钟系数表 */
typedef struct {
    uint32_t mclk;        /* mclk frequency */
    uint32_t rate;        /* sample rate */
    uint8_t  pre_div;     /* the pre divider with range from 1 to 8 */
    uint8_t  pre_multi;   /* the pre multiplier with x1, x2, x4 and x8 selection */
    uint8_t  adc_div;     /* adcclk divider */
    uint8_t  dac_div;     /* dacclk divider */
    uint8_t  fs_mode;     /* double speed or single speed, =0, ss, =1, ds */
    uint8_t  lrck_h;      /* adclrck divider and daclrck divider */
    uint8_t  lrck_l;
    uint8_t  bclk_div;    /* sclk divider */
    uint8_t  adc_osr;     /* adc osr */
    uint8_t  dac_osr;     /* dac osr */
} es8311_coeff_t;

/* Codec HIFI MCLK clock divider coefficients */
static const es8311_coeff_t coeff_div[] = {
    /* mclk      rate    pre_div mult  adc_div dac_div fs_mode lrch lrcl bckdiv osr */
    /* 8k */
    {12288000,   8000,  0x06, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {18432000,   8000,  0x03, 0x02, 0x03, 0x03, 0x00, 0x05, 0xff, 0x18, 0x10, 0x20},
    {16384000,   8000,  0x08, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {8192000,    8000,  0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000,    8000,  0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {4096000,    8000,  0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000,    8000,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2048000,    8000,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000,    8000,  0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1024000,    8000,  0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    /* 11.025k */
    {11289600,  11025,  0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {5644800,   11025,  0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2822400,   11025,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1411200,   11025,  0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    /* 12k */
    {12288000,  12000,  0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000,   12000,  0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000,   12000,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    /* 16k */
    {12288000,  16000,  0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {18432000,  16000,  0x03, 0x02, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x20},
    {16384000,  16000,  0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {8192000,   16000,  0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000,   16000,  0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {4096000,   16000,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000,   16000,  0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2048000,   16000,  0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000,   16000,  0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1024000,   16000,  0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    /* 22.05k */
    {11289600,  22050,  0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {5644800,   22050,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2822400,   22050,  0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1411200,   22050,  0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    /* 24k */
    {12288000,  24000,  0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {18432000,  24000,  0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000,   24000,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000,   24000,  0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    /* 32k */
    {12288000,  32000,  0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {18432000,  32000,  0x03, 0x04, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x20},
    {16384000,  32000,  0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {8192000,   32000,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000,   32000,  0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {4096000,   32000,  0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000,   32000,  0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2048000,   32000,  0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000,   32000,  0x03, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x20},
    {1024000,   32000,  0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    /* 44.1k */
    {11289600,  44100,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {5644800,   44100,  0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2822400,   44100,  0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1411200,   44100,  0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    /* 48k */
    {12288000,  48000,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {18432000,  48000,  0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000,   48000,  0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000,   48000,  0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000,   48000,  0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
};

#define COEFF_COUNT (sizeof(coeff_div) / sizeof(coeff_div[0]))

/* =============================================================================
 * 内部函数
 * =============================================================================
 */

int es8311_read_reg(es8311_t *dev, uint8_t reg)
{
    uint8_t val;
    if (i2c_soft_mem_read(dev->i2c, dev->addr, reg, false, &val, 1) != 0) {
        return -1;
    }
    return (int)val;
}

int es8311_write_reg(es8311_t *dev, uint8_t reg, uint8_t val)
{
    return i2c_soft_mem_write(dev->i2c, dev->addr, reg, false, &val, 1);
}

static int es8311_update_bits(es8311_t *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
    int old = es8311_read_reg(dev, reg);
    if (old < 0) return -1;
    uint8_t newv = (uint8_t)((old & ~mask) | (val & mask));
    return es8311_write_reg(dev, reg, newv);
}

static int get_coeff(uint32_t mclk, uint32_t rate)
{
    for (size_t i = 0; i < COEFF_COUNT; i++) {
        if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk) {
            return (int)i;
        }
    }
    return -1;
}

/* =============================================================================
 * 公共 API 实现
 * =============================================================================
 */

int es8311_init(es8311_t *dev, i2c_soft_t *i2c, uint8_t addr,
                es8311_mode_t mode, es8311_sample_t sample_rate)
{
    if (!dev || !i2c) return -1;

    dev->i2c = i2c;
    dev->addr = addr;
    dev->mode = mode;
    dev->sample_rate = sample_rate;

    uint8_t datmp, regv;
    int reg_read;
    uint32_t mclk = 12288000;  /* 默认使用 12.288MHz MCLK */

    int coeff_idx = get_coeff(mclk, (uint32_t)sample_rate);
    if (coeff_idx < 0) {
        /* 找不到匹配的系数，使用默认值 */
        coeff_idx = 0;
    }
    const es8311_coeff_t *coeff = &coeff_div[coeff_idx];

    /* 软复位 */
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG01, 0x30);
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG02, 0x00);
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG03, 0x10);
    es8311_write_reg(dev, ES8311_ADC_REG16, 0x24);
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG04, 0x10);
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG05, 0x00);
    es8311_write_reg(dev, ES8311_SYSTEM_REG0B, 0x00);
    es8311_write_reg(dev, ES8311_SYSTEM_REG0C, 0x00);
    es8311_write_reg(dev, ES8311_SYSTEM_REG10, 0x1F);
    es8311_write_reg(dev, ES8311_SYSTEM_REG11, 0x7F);
    es8311_write_reg(dev, ES8311_RESET_REG00, 0x80);

    /* 设置 ADC/DAC 时钟 */
    datmp = 0;
    if (ES8311_MCLK_FROM_SCLK_PIN) {
        datmp |= 0x01;
    }
    if (ES8311_INVERT_MCLK) {
        datmp |= 0x02;
    }
    if (ES8311_INVERT_SCLK) {
        datmp |= 0x04;
    }
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG01, datmp);

    /* 设置 pre_div, pre_multi */
    reg_read = es8311_read_reg(dev, ES8311_CLK_MANAGER_REG02);
    regv = (reg_read < 0) ? 0u : (uint8_t)reg_read;
    regv &= 0x07;
    regv |= (coeff->pre_div - 1) << 5;
    datmp = 0;
    switch (coeff->pre_multi) {
        case 1:  datmp = 0; break;
        case 2:  datmp = 1; break;
        case 4:  datmp = 2; break;
        case 8:  datmp = 3; break;
        default: datmp = 0; break;
    }
    regv |= (datmp << 3);
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG02, regv);

    /* 设置 ADC 和 DAC 时钟分频 */
    reg_read = es8311_read_reg(dev, ES8311_CLK_MANAGER_REG05);
    regv = (reg_read < 0) ? 0u : (uint8_t)reg_read;
    regv &= 0x00;
    regv |= (coeff->adc_div - 1) << 4;
    regv |= (coeff->dac_div - 1) << 0;
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG05, regv);

    /* 设置 fs mode 和 osr */
    regv = coeff->fs_mode << 6;
    regv |= coeff->adc_osr;
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG03, regv);
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG04, coeff->dac_osr);

    /* 设置 BCLK 分频和 LRCK 分频 */
    reg_read = es8311_read_reg(dev, ES8311_CLK_MANAGER_REG06);
    regv = (reg_read < 0) ? 0u : (uint8_t)reg_read;
    regv &= 0xE0;
    if (ES8311_INVERT_SCLK) {
        regv |= 0x20;
    }
    regv |= coeff->bclk_div;
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG06, regv);

    /* LRCK */
    reg_read = es8311_read_reg(dev, ES8311_CLK_MANAGER_REG07);
    regv = (reg_read < 0) ? 0u : (uint8_t)reg_read;
    regv &= 0xC0;
    regv |= coeff->lrck_h;
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG07, regv);
    es8311_write_reg(dev, ES8311_CLK_MANAGER_REG08, coeff->lrck_l);

    /* 主/从模式 */
    reg_read = es8311_read_reg(dev, ES8311_RESET_REG00);
    regv = (reg_read < 0) ? 0u : (uint8_t)reg_read;
    if (mode == ES8311_MODE_SLAVE) {
        regv &= 0xBF;  /* Slave mode */
    } else {
        regv |= 0x40;  /* Master mode */
    }
    es8311_write_reg(dev, ES8311_RESET_REG00, regv);

    /* 上电系统 */
    es8311_write_reg(dev, ES8311_SYSTEM_REG0D, 0x01);
    es8311_write_reg(dev, ES8311_ADC_REG15, 0x40);
    es8311_write_reg(dev, ES8311_SYSTEM_REG14, 0x1A);

    /* 设置 PGA 增益 */
    es8311_write_reg(dev, ES8311_SYSTEM_REG14, 0x1A);

    /* GPIO 设置 */
    es8311_write_reg(dev, ES8311_GPIO_REG44, 0x00);
    es8311_write_reg(dev, ES8311_GP_REG45, 0x00);

    /* 上电 */
    es8311_write_reg(dev, ES8311_SYSTEM_REG0E, 0x02);
    es8311_write_reg(dev, ES8311_SYSTEM_REG12, 0x00);
    es8311_write_reg(dev, ES8311_SYSTEM_REG13, 0x10);

    /* 使能 HP */
    es8311_write_reg(dev, ES8311_RESET_REG00, 0x80);

    /* ADC/DAC 设置 */
    es8311_write_reg(dev, ES8311_ADC_REG17, 0xBF);  /* ADC volume */
    es8311_write_reg(dev, ES8311_DAC_REG32, 0xBF);  /* DAC volume */

    return 0;
}

int es8311_config_i2s(es8311_t *dev, es8311_bits_t bits, es8311_i2s_fmt_t fmt)
{
    if (!dev) return -1;

    uint8_t datmp;

    /* SDP 输入格式设置 (DAC) */
    datmp = 0;
    switch (fmt) {
        case ES8311_I2S_NORMAL:
            datmp = 0x00;
            break;
        case ES8311_I2S_LEFT:
            datmp = 0x01;
            break;
        case ES8311_I2S_RIGHT:
            datmp = 0x02;
            break;
        case ES8311_I2S_DSP:
            datmp = 0x03;
            break;
        default:
            datmp = 0x00;
            break;
    }
    es8311_update_bits(dev, ES8311_SDPIN_REG09, 0x03, datmp);

    /* SDP 输出格式设置 (ADC) */
    es8311_update_bits(dev, ES8311_SDPOUT_REG0A, 0x03, datmp);

    /* 字长设置 */
    uint8_t bits_val = 0;
    switch (bits) {
        case ES8311_BITS_16:
            bits_val = 0x03;
            break;
        case ES8311_BITS_18:
            bits_val = 0x02;
            break;
        case ES8311_BITS_20:
            bits_val = 0x01;
            break;
        case ES8311_BITS_24:
            bits_val = 0x00;
            break;
        case ES8311_BITS_32:
            bits_val = 0x04;
            break;
        default:
            bits_val = 0x03;
            break;
    }
    es8311_update_bits(dev, ES8311_SDPIN_REG09, 0x1C, bits_val << 2);
    es8311_update_bits(dev, ES8311_SDPOUT_REG0A, 0x1C, bits_val << 2);

    return 0;
}

int es8311_ctrl_state(es8311_t *dev, es8311_codec_mode_t codec_mode, bool start)
{
    if (!dev) return -1;

    if (start) {
        return es8311_start(dev, codec_mode);
    } else {
        return es8311_stop(dev);
    }
}

int es8311_start(es8311_t *dev, es8311_codec_mode_t codec_mode)
{
    if (!dev) return -1;

    uint8_t adc_iface = 0, dac_iface = 0;

    adc_iface = es8311_read_reg(dev, ES8311_SDPOUT_REG0A);
    dac_iface = es8311_read_reg(dev, ES8311_SDPIN_REG09);

    /* ADC/DAC 静音解除 */
    adc_iface &= ~(1 << 6);  /* Unmute ADC */
    dac_iface &= ~(1 << 6);  /* Unmute DAC */

    if (codec_mode == ES8311_CODEC_ADC || codec_mode == ES8311_CODEC_BOTH) {
        es8311_write_reg(dev, ES8311_SDPOUT_REG0A, adc_iface);
        es8311_write_reg(dev, ES8311_SYSTEM_REG14, 0x1A);  /* 设置 ADC 增益 */
    }

    if (codec_mode == ES8311_CODEC_DAC || codec_mode == ES8311_CODEC_BOTH) {
        es8311_write_reg(dev, ES8311_SDPIN_REG09, dac_iface);
        es8311_write_reg(dev, ES8311_DAC_REG37, 0x08);  /* DAC ramp rate */
    }

    /* 上电 */
    es8311_write_reg(dev, ES8311_SYSTEM_REG0D, 0x01);
    es8311_write_reg(dev, ES8311_SYSTEM_REG0E, 0x02);
    es8311_write_reg(dev, ES8311_SYSTEM_REG12, 0x00);

    if (codec_mode == ES8311_CODEC_DAC || codec_mode == ES8311_CODEC_BOTH) {
        /* 使能 DAC */
        es8311_write_reg(dev, ES8311_DAC_REG31, 0x00);  /* Unmute DAC */
    }

    return 0;
}

int es8311_stop(es8311_t *dev)
{
    if (!dev) return -1;

    /* 静音 */
    es8311_update_bits(dev, ES8311_DAC_REG31, 0x40, 0x40);  /* Mute DAC */
    es8311_update_bits(dev, ES8311_SDPOUT_REG0A, 0x40, 0x40);  /* Mute ADC */

    /* 掉电 */
    es8311_write_reg(dev, ES8311_SYSTEM_REG0E, 0xFF);
    es8311_write_reg(dev, ES8311_SYSTEM_REG12, 0x02);
    es8311_write_reg(dev, ES8311_SYSTEM_REG0D, 0xFA);
    es8311_write_reg(dev, ES8311_ADC_REG15, 0x00);
    es8311_write_reg(dev, ES8311_DAC_REG37, 0x08);

    es8311_write_reg(dev, ES8311_RESET_REG00, 0x00);
    es8311_write_reg(dev, ES8311_RESET_REG00, 0x1F);

    return 0;
}

int es8311_set_volume(es8311_t *dev, uint8_t volume)
{
    if (!dev) return -1;

    /* DAC 音量: 0x00 = 0dB, 0xBF = -95.5dB */
    /* 反转：用户期望 0=静音，255=最大 */
    uint8_t dac_vol = (volume >= 192) ? 0 : (uint8_t)(191 - (volume * 191 / 255));
    es8311_write_reg(dev, ES8311_DAC_REG32, dac_vol);

    return 0;
}

int es8311_get_volume(es8311_t *dev, uint8_t *volume)
{
    if (!dev || !volume) return -1;

    int val = es8311_read_reg(dev, ES8311_DAC_REG32);
    if (val < 0) return -1;

    /* 反转映射 */
    uint8_t dac_vol = (uint8_t)val;
    *volume = (dac_vol >= 191) ? 0 : (uint8_t)((191 - dac_vol) * 255 / 191);

    return 0;
}

int es8311_set_mute(es8311_t *dev, bool mute)
{
    if (!dev) return -1;

    if (mute) {
        es8311_update_bits(dev, ES8311_DAC_REG31, 0x60, 0x60);  /* Mute DAC */
    } else {
        es8311_update_bits(dev, ES8311_DAC_REG31, 0x60, 0x00);  /* Unmute DAC */
    }

    return 0;
}

int es8311_set_mic_gain(es8311_t *dev, es8311_mic_gain_t gain)
{
    if (!dev) return -1;

    if (gain < ES8311_MIC_GAIN_0DB || gain >= ES8311_MIC_GAIN_MAX) {
        return -1;
    }

    /* PGA 增益设置在 REG14[4:0] */
    uint8_t gain_val = (uint8_t)gain;
    es8311_update_bits(dev, ES8311_SYSTEM_REG14, 0x1F, gain_val);

    return 0;
}

int es8311_pa_power(bool enable)
{
    /* PA 控制通常通过 GPIO 实现，这里留空让用户在应用层实现 */
    (void)enable;
    return 0;
}
