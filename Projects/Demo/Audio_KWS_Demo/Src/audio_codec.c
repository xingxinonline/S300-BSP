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

int audio_codec_init(audio_codec_t *codec, uint32_t sample_rate)
{
    int ret;

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
    i2c_soft_init(&codec->i2c, &i2c_cfg, 200000000);
    printf("[Codec] ES7210+ES8311 I2C: GPIO%d(SCL), GPIO%d(SDA)\r\n",
           BOARD_AUDIO_I2C_SCL_PIN, BOARD_AUDIO_I2C_SDA_PIN);
#else
    i2c_soft_init_default_idx(&codec->i2c, 1, 100000);
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

    /* 选择麦克风通道 (4路全开) */
    es7210_mic_select(&g_es7210, ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2 |
                                  ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4);

    /* 设置增益 (所有麦克风) */
    es7210_set_gain(&g_es7210, ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2 |
                               ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4, ES7210_GAIN_30DB);

    /* 初始化 ES8311 (Mono DAC+ADC) */
    ret = es8311_init(&g_es8311, &codec->i2c, BOARD_AUDIO_ADDR_ES8311,
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
    es8311_set_volume(&g_es8311, 200);  /* 0-255 */

    codec->initialized = true;
    printf("[Codec] ES7210+ES8311 initialized (sample_rate=%lu)\r\n", sample_rate);

    return 0;
}

void audio_codec_deinit(audio_codec_t *codec)
{
    if (codec && codec->initialized) {
        es8311_stop(&g_es8311);
        codec->initialized = false;
    }
}

int audio_codec_start(audio_codec_t *codec)
{
    if (!codec || !codec->initialized) return -1;
    /* ES7210 在初始化后自动开始工作 */
    es8311_start(&g_es8311, ES8311_CODEC_DAC);
    
    /* 使能功放 */
    audio_codec_pa_enable(true);
    
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

#endif /* USE_ES7210_ES8311 */

/*---------------------------------------------------------------------------
 * 通用功放控制
 *---------------------------------------------------------------------------*/

void audio_codec_pa_enable(bool enable)
{
#if defined(BOARD_PA_EN_AVAILABLE) && BOARD_PA_EN_AVAILABLE
    gpio_set_function(GPIOA, BOARD_PA_EN_PIN, FUNCTION_0);
    gpio_set_direction(GPIOA, BOARD_PA_EN_PIN, 1);  /* 1 = output */
    gpio_set_data(GPIOA, BOARD_PA_EN_PIN, enable ? 1 : 0);
    printf("[Codec] PA_EN GPIO%d = %d\r\n", BOARD_PA_EN_PIN, enable ? 1 : 0);
#else
    (void)enable;
    /* 没有 PA_EN 引脚，不做任何操作 */
#endif
}
