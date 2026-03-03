/**
 * @file    audio_codec.c
 * @brief   音频编解码器抽象层实现
 * @details 根据板级配置自动选择 WM8978 或 ES7210+ES8311
 */

#include "audio_codec.h"
#include "board.h"
#include "gpio.h"
#include <stdio.h>

/*===========================================================================
 * 板级编解码器类型检测
 *===========================================================================*/

/* 根据板级宏判断使用哪种编解码器 */
#if defined(BOARD_AUDIO_CODEC_ES7210) && defined(BOARD_AUDIO_CODEC_ES8311)
    #define USE_ES7210_ES8311   1
    #include "es7210.h"
    #include "es8311.h"
#elif defined(BOARD_AUDIO_CODEC_WM8978) || !defined(BOARD_AUDIO_CODEC_ES7210)
    #define USE_WM8978          1
    #include "wm8978.h"
#endif

/*===========================================================================
 * 私有变量
 *===========================================================================*/

#if USE_ES7210_ES8311
/* ES7210 + ES8311 句柄 */
static es7210_t g_es7210;
static es8311_t g_es8311;
static bool g_es7210_ready;
#endif

/*===========================================================================
 * 公开 API 实现
 *===========================================================================*/

audio_codec_type_t audio_codec_get_board_type(void)
{
#if USE_ES7210_ES8311
    return AUDIO_CODEC_ES7210_ES8311;
#elif USE_WM8978
    return AUDIO_CODEC_WM8978;
#else
    return AUDIO_CODEC_NONE;
#endif
}

const char *audio_codec_get_name(audio_codec_type_t type)
{
    switch (type) {
        case AUDIO_CODEC_WM8978:        return "WM8978";
        case AUDIO_CODEC_ES7210_ES8311: return "ES7210+ES8311";
        default:                        return "Unknown";
    }
}

/*---------------------------------------------------------------------------
 * WM8978 实现
 *---------------------------------------------------------------------------*/
#if USE_WM8978

int audio_codec_init(audio_codec_t *codec, uint32_t sample_rate)
{
    int ret;

    if (!codec) return -1;

    codec->type = AUDIO_CODEC_WM8978;
    codec->sample_rate = sample_rate;

    /* 初始化 I2C (使用板级配置或默认 idx=1: PA0/PA1) */
#ifdef BOARD_AUDIO_I2C_SCL_PIN
    i2c_soft_cfg_t i2c_cfg = {
        .port     = GPIOA,
        .scl_pin  = BOARD_AUDIO_I2C_SCL_PIN,
        .sda_pin  = BOARD_AUDIO_I2C_SDA_PIN,
        .speed_hz = 100000,
    };
    i2c_soft_init(&codec->i2c, &i2c_cfg);
    printf("[Codec] WM8978 I2C: GPIO%d(SCL), GPIO%d(SDA)\r\n",
           BOARD_AUDIO_I2C_SCL_PIN, BOARD_AUDIO_I2C_SDA_PIN);
#else
    i2c_soft_init_default_idx(&codec->i2c, 1, 100000);
    printf("[Codec] WM8978 I2C: PA0(SCL), PA1(SDA)\r\n");
#endif

    /* 初始化 WM8978 */
    ret = wm8978_init(&codec->i2c);
    if (ret != 0) {
        printf("[Codec] WM8978 init failed: %d\r\n", ret);
        return ret;
    }

    /* 配置音量 */
    wm8978_set_hp_vol(&codec->i2c, 40, 40);
    wm8978_set_spk_vol(&codec->i2c, 50);

    /* 配置 ADC/DAC */
    wm8978_set_adda(&codec->i2c, true, true);

    /* 配置输入: MIC + LINE IN */
    wm8978_set_input(&codec->i2c, true, true, false);

    /* 配置输出: DAC 到输出 */
    wm8978_set_output(&codec->i2c, true, false);

    /* 配置 MIC 增益 */
    wm8978_set_mic_gain(&codec->i2c, 46);

    /* 配置 I2S 格式: fmt=2 (I2S 标准), len=0 (16-bit) */
    wm8978_i2s_cfg(&codec->i2c, 2, 0);

    codec->initialized = true;
    printf("[Codec] WM8978 initialized (sample_rate=%lu)\r\n", sample_rate);

    return 0;
}

void audio_codec_deinit(audio_codec_t *codec)
{
    if (codec && codec->initialized) {
        codec->initialized = false;
    }
}

int audio_codec_start(audio_codec_t *codec)
{
    (void)codec;
    /* WM8978 初始化后自动开始工作 */
    return 0;
}

int audio_codec_stop(audio_codec_t *codec)
{
    (void)codec;
    /* WM8978 没有显式停止命令，可以设置静音 */
    return 0;
}

int audio_codec_set_mic_gain(audio_codec_t *codec, uint8_t gain)
{
    if (!codec || !codec->initialized) return -1;
    /* WM8978 MIC 增益范围 0-63，映射 0-100 */
    uint8_t hw_gain = (gain * 63) / 100;
    wm8978_set_mic_gain(&codec->i2c, hw_gain);
    codec->mic_gain = gain;
    return 0;
}

int audio_codec_set_spk_volume(audio_codec_t *codec, uint8_t volume)
{
    if (!codec || !codec->initialized) return -1;
    /* WM8978 SPK 音量范围 0-63，映射 0-100 */
    uint8_t hw_vol = (volume * 63) / 100;
    wm8978_set_spk_vol(&codec->i2c, hw_vol);
    codec->spk_volume = volume;
    return 0;
}

int audio_codec_set_hp_volume(audio_codec_t *codec, uint8_t volume)
{
    if (!codec || !codec->initialized) return -1;
    /* WM8978 HP 音量范围 0-63，映射 0-100 */
    uint8_t hw_vol = (volume * 63) / 100;
    wm8978_set_hp_vol(&codec->i2c, hw_vol, hw_vol);
    codec->hp_volume = volume;
    return 0;
}

int audio_codec_set_mute(audio_codec_t *codec, bool mute)
{
    if (!codec || !codec->initialized) return -1;
    /* WM8978 通过设置音量为0实现静音 */
    if (mute) {
        wm8978_set_hp_vol(&codec->i2c, 0, 0);
        wm8978_set_spk_vol(&codec->i2c, 0);
    } else {
        uint8_t hp_vol = (codec->hp_volume * 63) / 100;
        uint8_t spk_vol = (codec->spk_volume * 63) / 100;
        wm8978_set_hp_vol(&codec->i2c, hp_vol, hp_vol);
        wm8978_set_spk_vol(&codec->i2c, spk_vol);
    }
    return 0;
}

#endif /* USE_WM8978 */

/*---------------------------------------------------------------------------
 * ES7210 + ES8311 实现
 *---------------------------------------------------------------------------*/
#if USE_ES7210_ES8311

/* ES7210/ES8311 I2C 地址 */
#ifndef BOARD_AUDIO_ADDR_ES7210
#define BOARD_AUDIO_ADDR_ES7210     0x41
#endif
#ifndef BOARD_AUDIO_ADDR_ES8311
#define BOARD_AUDIO_ADDR_ES8311     0x18
#endif

static int select_es8311_addr(i2c_soft_t *i2c, uint8_t *addr)
{
    if (!i2c || !addr) return -1;

    if (i2c_soft_probe(i2c, BOARD_AUDIO_ADDR_ES8311) == 0) {
        *addr = BOARD_AUDIO_ADDR_ES8311;
        return 0;
    }

    if (i2c_soft_probe(i2c, ES8311_I2C_ADDR_CE1) == 0) {
        *addr = ES8311_I2C_ADDR_CE1;
        return 0;
    }

    return -1;
}

static void es7210_update_bits_compat(uint8_t reg, uint8_t mask, uint8_t val)
{
    int old = es7210_read_reg(&g_es7210, reg);
    if (old < 0) {
        return;
    }

    es7210_write_reg(&g_es7210,
                     reg,
                     (uint8_t)(((uint8_t)old & (uint8_t)(~mask)) | (val & mask)));
}

/* SDK全局变量: 当前MIC选择状态 */
static uint8_t g_es7210_mic_reg12 = 0x20;  /* REG12: 0x20=MIC1+2, 0x10=MIC4 */
static es7210_input_mics_t g_es7210_mic_select = (es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2);

/**
 * @brief SDK风格的es7210_mic_select函数
 * @note  完全复刻SDK: externdevice/audio_es7210.c:es7210_mic_select()
 */
static void es7210_sdk_mic_select(void)
{
    const uint8_t gain = (uint8_t)ES7210_GAIN_3DB;  /* SDK使用3dB */

    /* 先关闭所有MIC的PGA使能 */
    es7210_update_bits_compat(ES7210_MIC1_GAIN_REG43, 0x10, 0x00);
    es7210_update_bits_compat(ES7210_MIC2_GAIN_REG44, 0x10, 0x00);
    es7210_update_bits_compat(ES7210_MIC3_GAIN_REG45, 0x10, 0x00);
    es7210_update_bits_compat(ES7210_MIC4_GAIN_REG46, 0x10, 0x00);

    /* 先关闭电源 */
    es7210_write_reg(&g_es7210, ES7210_MIC12_POWER_REG4B, 0xFF);
    es7210_write_reg(&g_es7210, ES7210_MIC34_POWER_REG4C, 0xFF);

    /* 根据选择的MIC开启对应通道 */
    if (g_es7210_mic_select & ES7210_INPUT_MIC1) {
        es7210_update_bits_compat(ES7210_CLOCK_OFF_REG01, 0x0B, 0x00);
        es7210_write_reg(&g_es7210, ES7210_MIC12_POWER_REG4B, 0x00);
        es7210_update_bits_compat(ES7210_MIC1_GAIN_REG43, 0x10, 0x10);
        es7210_update_bits_compat(ES7210_MIC1_GAIN_REG43, 0x0F, gain);
    }
    if (g_es7210_mic_select & ES7210_INPUT_MIC2) {
        es7210_update_bits_compat(ES7210_CLOCK_OFF_REG01, 0x0B, 0x00);
        es7210_write_reg(&g_es7210, ES7210_MIC12_POWER_REG4B, 0x00);
        es7210_update_bits_compat(ES7210_MIC2_GAIN_REG44, 0x10, 0x10);
        es7210_update_bits_compat(ES7210_MIC2_GAIN_REG44, 0x0F, gain);
    }
    if (g_es7210_mic_select & ES7210_INPUT_MIC3) {
        es7210_update_bits_compat(ES7210_CLOCK_OFF_REG01, 0x15, 0x00);
        es7210_write_reg(&g_es7210, ES7210_MIC34_POWER_REG4C, 0x00);
        es7210_update_bits_compat(ES7210_MIC3_GAIN_REG45, 0x10, 0x10);
        es7210_update_bits_compat(ES7210_MIC3_GAIN_REG45, 0x0F, gain);
    }
    if (g_es7210_mic_select & ES7210_INPUT_MIC4) {
        es7210_update_bits_compat(ES7210_CLOCK_OFF_REG01, 0x15, 0x00);
        es7210_write_reg(&g_es7210, ES7210_MIC34_POWER_REG4C, 0x00);
        es7210_update_bits_compat(ES7210_MIC4_GAIN_REG46, 0x10, 0x10);
        es7210_update_bits_compat(ES7210_MIC4_GAIN_REG46, 0x0F, gain);
    }

    /* 写REG12路由 */
    es7210_write_reg(&g_es7210, ES7210_SDP_INTERFACE2_REG12, g_es7210_mic_reg12);

    /* 取消静音 */
    es7210_update_bits_compat(ES7210_ADC34_MUTERANGE_REG14, 0x03, 0x00);
    es7210_update_bits_compat(ES7210_ADC12_MUTERANGE_REG15, 0x03, 0x00);
}

/**
 * @brief SDK风格的es7210_start函数
 * @note  复刻SDK: externdevice/audio_es7210.c:es7210_start()
 */
static void es7210_sdk_start(void)
{
    /* 读CLOCK_OFF寄存器当前值 */
    int clock_reg = es7210_read_reg(&g_es7210, ES7210_CLOCK_OFF_REG01);
    if (clock_reg < 0 || clock_reg == 0x7F || clock_reg == 0xFF) {
        clock_reg = 0x00;
    }

    es7210_write_reg(&g_es7210, ES7210_CLOCK_OFF_REG01, (uint8_t)clock_reg);
    es7210_write_reg(&g_es7210, ES7210_POWER_DOWN_REG06, 0x00);
    es7210_write_reg(&g_es7210, ES7210_ANALOG_REG40, 0x43);
    es7210_write_reg(&g_es7210, ES7210_MIC1_POWER_REG47, 0x08);
    es7210_write_reg(&g_es7210, ES7210_MIC2_POWER_REG48, 0x08);
    es7210_write_reg(&g_es7210, ES7210_MIC3_POWER_REG49, 0x08);
    es7210_write_reg(&g_es7210, ES7210_MIC4_POWER_REG4A, 0x08);

    /* SDK的es7210_start最后会再次调用mic_select */
    es7210_sdk_mic_select();
}

/**
 * @brief SDK风格的完整重初始化函数
 * @note  复刻SDK: app/kws_dsp.c:apply_es7210_config()
 */
static void es7210_apply_sdk_config(void)
{
    /* 1. 重新初始化ES7210 (对应SDK的es7210_init) */
    es7210_write_reg(&g_es7210, ES7210_RESET_REG00, 0xFF);
    es7210_write_reg(&g_es7210, ES7210_RESET_REG00, 0x41);
    es7210_write_reg(&g_es7210, ES7210_CLOCK_OFF_REG01, 0x3F);
    es7210_write_reg(&g_es7210, ES7210_TIME_CONTROL0_REG09, 0x30);
    es7210_write_reg(&g_es7210, ES7210_TIME_CONTROL1_REG0A, 0x30);
    es7210_write_reg(&g_es7210, ES7210_ADC12_HPF2_REG23, 0x2A);
    es7210_write_reg(&g_es7210, ES7210_ADC12_HPF1_REG22, 0x0A);
    es7210_write_reg(&g_es7210, ES7210_ADC34_HPF2_REG20, 0x0A);
    es7210_write_reg(&g_es7210, ES7210_ADC34_HPF1_REG21, 0x2A);
    /* Slave mode */
    es7210_update_bits_compat(ES7210_MODE_REG08, 0x01, 0x00);
    /* 模拟配置 */
    es7210_write_reg(&g_es7210, ES7210_ANALOG_REG40, 0x43);
    es7210_write_reg(&g_es7210, ES7210_MIC12_BIAS_REG41, 0x70);
    es7210_write_reg(&g_es7210, ES7210_MIC34_BIAS_REG42, 0x70);
    es7210_write_reg(&g_es7210, ES7210_OSR_REG07, 0x20);
    es7210_write_reg(&g_es7210, ES7210_MAINCLK_REG02, 0xC1);
    /* 16kHz@12.288MHz: adc_div=0x03, doubler=1, dll=1
     * REG02 = adc_div | (doubler<<6) | (dll<<7) = 0x03 | 0x40 | 0x80 = 0xC3
     */
    es7210_write_reg(&g_es7210, ES7210_MAINCLK_REG02, 0xC3);
    es7210_write_reg(&g_es7210, ES7210_LRCK_DIVH_REG04, 0x03);
    es7210_write_reg(&g_es7210, ES7210_LRCK_DIVL_REG05, 0x00);
    /* es7210_mic_select在init中调用 */
    es7210_sdk_mic_select();

    /* 2. 配置I2S (对应SDK的es7210_adc_config_i2s) */
    /* I2S Normal, 16-bit */
    es7210_update_bits_compat(ES7210_SDP_INTERFACE1_REG11, 0x03, 0x00);
    es7210_update_bits_compat(ES7210_SDP_INTERFACE1_REG11, 0xE0, 0x60);

    /* 3. 设置增益3dB (对应SDK的es7210_adc_set_volume) */
    /* 已在es7210_sdk_mic_select中设置 */

    /* 4. 取消静音 (对应SDK的es7210_set_mute(FALSE)) */
    es7210_update_bits_compat(ES7210_ADC34_MUTERANGE_REG14, 0x03, 0x00);
    es7210_update_bits_compat(ES7210_ADC12_MUTERANGE_REG15, 0x03, 0x00);

    /* 5. 启动ADC (对应SDK的es7210_adc_ctrl_state(START)) */
    es7210_sdk_start();
}

/**
 * @brief 兼容旧API的包装函数
 */
static void es7210_apply_sdk_profile(bool use_mic4)
{
    /* 更新全局MIC选择状态 */
    if (use_mic4) {
        g_es7210_mic_reg12 = 0x10;
        g_es7210_mic_select = ES7210_INPUT_MIC4;
    } else {
        g_es7210_mic_reg12 = 0x20;
        g_es7210_mic_select = (es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2);
    }

    /* SDK风格: 完整重新初始化ES7210 */
    es7210_apply_sdk_config();
}

int audio_codec_init(audio_codec_t *codec, uint32_t sample_rate)
{
    int ret;
    uint8_t es8311_addr;

    if (!codec) return -1;

    codec->type = AUDIO_CODEC_ES7210_ES8311;
    codec->sample_rate = sample_rate;
    codec->spk_volume = 80;
    codec->mic_gain = 70;

    /* 初始化 I2C */
#ifdef BOARD_AUDIO_I2C_SCL_PIN
    i2c_soft_cfg_t i2c_cfg = {
        .port     = GPIOA,
        .pin_scl  = BOARD_AUDIO_I2C_SCL_PIN,
        .pin_sda  = BOARD_AUDIO_I2C_SDA_PIN,
        .func_scl = FUNCTION_2,
        .func_sda = FUNCTION_2,
        .pull_mode = GPIO_UP,
        .bus_hz   = 100000,
    };
    ret = i2c_soft_init(&codec->i2c, &i2c_cfg, SystemCoreClock);
    if (ret != 0) {
        printf("[Codec] I2C soft init failed: %d\r\n", ret);
        return ret;
    }
    i2c_soft_bus_recover(&codec->i2c);
    printf("[Codec] ES7210+ES8311 I2C: GPIO%d(SCL), GPIO%d(SDA)\r\n",
           BOARD_AUDIO_I2C_SCL_PIN, BOARD_AUDIO_I2C_SDA_PIN);
#else
    ret = i2c_soft_init_default_idx(&codec->i2c, 1, 100000);
    if (ret != 0) {
        printf("[Codec] I2C default init failed: %d\r\n", ret);
        return ret;
    }
    i2c_soft_bus_recover(&codec->i2c);
    printf("[Codec] ES7210+ES8311 I2C: PA0(SCL), PA1(SDA)\r\n");
#endif

    /* 确定采样率 */
    es7210_sample_t es7210_sr;
    es8311_sample_t es8311_sr;
    switch (sample_rate) {
        case 8000:  es7210_sr = ES7210_SAMPLE_8K;  es8311_sr = ES8311_SAMPLE_8K;  break;
        case 16000: es7210_sr = ES7210_SAMPLE_16K; es8311_sr = ES8311_SAMPLE_16K; break;
        case 32000: es7210_sr = ES7210_SAMPLE_32K; es8311_sr = ES8311_SAMPLE_32K; break;
        case 44100: es7210_sr = ES7210_SAMPLE_44K; es8311_sr = ES8311_SAMPLE_44K; break;
        case 48000: es7210_sr = ES7210_SAMPLE_48K; es8311_sr = ES8311_SAMPLE_48K; break;
        default:    es7210_sr = ES7210_SAMPLE_16K; es8311_sr = ES8311_SAMPLE_16K; break;
    }

    /* 初始化 ES7210 (4通道 ADC) */
    ret = es7210_init(&g_es7210, &codec->i2c, BOARD_AUDIO_ADDR_ES7210,
                      es7210_sr, ES7210_MODE_SLAVE);
    if (ret != 0) {
        printf("[Codec] ES7210 init failed: %d\r\n", ret);
        return ret;
    }
    printf("[Codec] ES7210 initialized\r\n");

    /* 配置 ES7210 I2S: I2S标准格式, 16位 */
    ret = es7210_config_i2s(&g_es7210, ES7210_I2S_NORMAL, es7210_sr, ES7210_BITS_16);
    if (ret != 0) {
        printf("[Codec] ES7210 I2S config failed: %d\r\n", ret);
        return ret;
    }

    /* 对齐 SDK：使用兼容寄存器序列设置默认 MIC1+MIC2 */
    es7210_apply_sdk_profile(false);

    ret = select_es8311_addr(&codec->i2c, &es8311_addr);
    if (ret != 0) {
        printf("[Codec] ES8311 probe failed on 0x%02X/0x%02X\r\n",
               BOARD_AUDIO_ADDR_ES8311,
               ES8311_I2C_ADDR_CE1);
        return ret;
    }
    printf("[Codec] ES8311 addr: 0x%02X\r\n", es8311_addr);

    /* 初始化 ES8311 (Mono DAC+ADC) */
    ret = es8311_init(&g_es8311, &codec->i2c, es8311_addr,
                      ES8311_MODE_SLAVE, es8311_sr);
    if (ret != 0) {
        printf("[Codec] ES8311 init failed: %d\r\n", ret);
        return ret;
    }
    printf("[Codec] ES8311 initialized\r\n");

    /* 配置 ES8311 I2S: 16位, I2S标准格式 */
    ret = es8311_config_i2s(&g_es8311, ES8311_BITS_16, ES8311_I2S_NORMAL);
    if (ret != 0) {
        printf("[Codec] ES8311 I2S config failed: %d\r\n", ret);
        return ret;
    }

    /* 设置音量 */
    es8311_set_volume(&g_es8311, 255);  /* 0-255 */

    g_es7210_ready = true;

    codec->initialized = true;
    printf("[Codec] ES7210+ES8311 initialized (sample_rate=%lu)\r\n", sample_rate);

    return 0;
}

void audio_codec_deinit(audio_codec_t *codec)
{
    if (codec && codec->initialized) {
        es8311_stop(&g_es8311);
        g_es7210_ready = false;
        codec->initialized = false;
    }
}

int audio_codec_start(audio_codec_t *codec)
{
    if (!codec || !codec->initialized) return -1;
    int reg31;
    int reg32;
    int reg0d;
    int reg0e;

    /* ES7210已在audio_codec_init中通过es7210_apply_sdk_profile完整启动
     * 不需要再调用es7210_start()，那会破坏SDK配置
     */

    es8311_set_mute(&g_es8311, false);
    es8311_set_mic_gain(&g_es8311, ES8311_MIC_GAIN_MAX);
    es8311_start(&g_es8311, ES8311_CODEC_BOTH);
    es8311_set_volume(&g_es8311, 255);
    
    /* 使能功放 */
    audio_codec_pa_enable(true);

    reg31 = es8311_read_reg(&g_es8311, ES8311_DAC_REG31);
    reg32 = es8311_read_reg(&g_es8311, ES8311_DAC_REG32);
    reg0d = es8311_read_reg(&g_es8311, ES8311_SYSTEM_REG0D);
    reg0e = es8311_read_reg(&g_es8311, ES8311_SYSTEM_REG0E);

    if (reg31 < 0 || reg32 < 0 || reg0d < 0 || reg0e < 0) {
        printf("[CodecDbg] ES8311 reg read failed: REG31=%d REG32=%d REG0D=%d REG0E=%d\r\n",
               reg31, reg32, reg0d, reg0e);
    } else {
        printf("[CodecDbg] ES8311 REG31=0x%02X REG32=0x%02X REG0D=0x%02X REG0E=0x%02X\r\n",
               (unsigned int)reg31,
               (unsigned int)reg32,
               (unsigned int)reg0d,
               (unsigned int)reg0e);
    }
    
    return 0;
}

int audio_codec_stop(audio_codec_t *codec)
{
    if (!codec || !codec->initialized) return -1;
    
    /* 禁用功放 */
    audio_codec_pa_enable(false);
    
    es8311_stop(&g_es8311);
    return 0;
}

int audio_codec_set_mic_gain(audio_codec_t *codec, uint8_t gain)
{
    if (!codec || !codec->initialized) return -1;
    
    /* 将 0-100 映射到 ES7210 增益等级 */
    es7210_gain_t hw_gain;
    if (gain < 15)      hw_gain = ES7210_GAIN_0DB;
    else if (gain < 30) hw_gain = ES7210_GAIN_3DB;
    else if (gain < 45) hw_gain = ES7210_GAIN_6DB;
    else if (gain < 60) hw_gain = ES7210_GAIN_12DB;
    else if (gain < 75) hw_gain = ES7210_GAIN_24DB;
    else if (gain < 90) hw_gain = ES7210_GAIN_30DB;
    else                hw_gain = ES7210_GAIN_37_5DB;
    
    /* 设置所有麦克风增益 */
    es7210_set_gain(&g_es7210, ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2 |
                               ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4, hw_gain);
    codec->mic_gain = gain;
    return 0;
}

int audio_codec_set_spk_volume(audio_codec_t *codec, uint8_t volume)
{
    if (!codec || !codec->initialized) return -1;
    
    /* ES8311 音量范围 0-255，映射 0-100 -> 0-255 */
    uint8_t hw_vol = (volume * 255) / 100;
    es8311_set_volume(&g_es8311, hw_vol);
    codec->spk_volume = volume;
    return 0;
}

int audio_codec_set_hp_volume(audio_codec_t *codec, uint8_t volume)
{
    /* ES8311 只有一个 DAC 输出，与 SPK 共享 */
    return audio_codec_set_spk_volume(codec, volume);
}

int audio_codec_set_mute(audio_codec_t *codec, bool mute)
{
    if (!codec || !codec->initialized) return -1;
    es8311_set_mute(&g_es8311, mute);
    return 0;
}

int audio_codec_apply_kws_mic_profile(bool use_mic4)
{
    if (!g_es7210_ready) return -1;

    es7210_apply_sdk_profile(use_mic4);

    return 0;
}

#endif /* USE_ES7210_ES8311 */

#if !USE_ES7210_ES8311
int audio_codec_apply_kws_mic_profile(bool use_mic4)
{
    (void)use_mic4;
    return -1;
}
#endif

/*---------------------------------------------------------------------------
 * 通用功放控制
 *---------------------------------------------------------------------------*/

void audio_codec_pa_enable(bool enable)
{
#if defined(BOARD_PA_EN_AVAILABLE) && BOARD_PA_EN_AVAILABLE
    gpio_set_function(BOARD_PA_EN_PORT, BOARD_PA_EN_PIN, BOARD_PA_EN_FUNCTION);
    gpio_set_direction(BOARD_PA_EN_PORT, BOARD_PA_EN_PIN, 1);  /* 1 = output */
    gpio_set_mode(BOARD_PA_EN_PORT, BOARD_PA_EN_PIN, GPIO_UP);
    gpio_set_data(BOARD_PA_EN_PORT, BOARD_PA_EN_PIN, enable ? 1 : 0);
    printf("[Codec] PA_EN GPIO%d = %d\r\n", BOARD_PA_EN_PIN, enable ? 1 : 0);
#else
    (void)enable;
    /* 没有 PA_EN 引脚，不做任何操作 */
#endif
}
