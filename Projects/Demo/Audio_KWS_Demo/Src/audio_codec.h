/**
 * @file    audio_codec.h
 * @brief   音频编解码器抽象层
 * @details 提供统一的音频编解码器接口，支持多种硬件：
 *          - WM8978: generic_evb 开发板
 *          - ES7210 + ES8311: app_board, gimbal_master
 *
 * @note    根据板级定义自动选择编解码器实现
 */

#ifndef AUDIO_CODEC_H
#define AUDIO_CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "i2c_soft.h"

/*===========================================================================
 * 编解码器类型定义
 *===========================================================================*/

/** @brief 音频编解码器类型枚举 */
typedef enum {
    AUDIO_CODEC_NONE = 0,       /**< 未定义 */
    AUDIO_CODEC_WM8978,         /**< Wolfson WM8978 (开发板) */
    AUDIO_CODEC_ES7210_ES8311,  /**< ES7210 (ADC) + ES8311 (DAC) */
} audio_codec_type_t;

/** @brief 音频采样率 */
typedef enum {
    AUDIO_SAMPLE_RATE_8K   = 8000,
    AUDIO_SAMPLE_RATE_16K  = 16000,
    AUDIO_SAMPLE_RATE_32K  = 32000,
    AUDIO_SAMPLE_RATE_44K  = 44100,
    AUDIO_SAMPLE_RATE_48K  = 48000,
} audio_sample_rate_t;

/** @brief 音频编解码器句柄 */
typedef struct {
    audio_codec_type_t type;        /**< 编解码器类型 */
    i2c_soft_t         i2c;         /**< I2C 句柄 (WM8978/ES8311) */
    i2c_soft_t         i2c_adc;     /**< 第二个 I2C 句柄 (ES7210, 可选) */
    uint32_t           sample_rate; /**< 采样率 */
    uint8_t            mic_gain;    /**< 麦克风增益 (0-100) */
    uint8_t            spk_volume;  /**< 扬声器音量 (0-100) */
    uint8_t            hp_volume;   /**< 耳机音量 (0-100) */
    bool               initialized; /**< 初始化标志 */
} audio_codec_t;

/*===========================================================================
 * 公开 API
 *===========================================================================*/

/**
 * @brief 获取当前板子使用的编解码器类型
 * @return 编解码器类型枚举
 */
audio_codec_type_t audio_codec_get_board_type(void);

/**
 * @brief 获取编解码器类型名称（用于调试打印）
 * @param type 编解码器类型
 * @return 类型名称字符串
 */
const char *audio_codec_get_name(audio_codec_type_t type);

/**
 * @brief 初始化音频编解码器
 * @param codec       编解码器句柄
 * @param sample_rate 采样率 (Hz)
 * @return 0=成功, 非0=失败
 *
 * @note 自动根据板级配置选择正确的编解码器实现
 */
int audio_codec_init(audio_codec_t *codec, uint32_t sample_rate);

/**
 * @brief 反初始化音频编解码器
 * @param codec 编解码器句柄
 */
void audio_codec_deinit(audio_codec_t *codec);

/**
 * @brief 启动音频编解码器
 * @param codec 编解码器句柄
 * @return 0=成功, 非0=失败
 */
int audio_codec_start(audio_codec_t *codec);

/**
 * @brief 停止音频编解码器
 * @param codec 编解码器句柄
 * @return 0=成功, 非0=失败
 */
int audio_codec_stop(audio_codec_t *codec);

/**
 * @brief 设置麦克风增益
 * @param codec 编解码器句柄
 * @param gain  增益值 (0-100, 映射到实际硬件范围)
 * @return 0=成功, 非0=失败
 */
int audio_codec_set_mic_gain(audio_codec_t *codec, uint8_t gain);

/**
 * @brief 设置扬声器音量
 * @param codec  编解码器句柄
 * @param volume 音量值 (0-100)
 * @return 0=成功, 非0=失败
 */
int audio_codec_set_spk_volume(audio_codec_t *codec, uint8_t volume);

/**
 * @brief 设置耳机音量
 * @param codec  编解码器句柄
 * @param volume 音量值 (0-100)
 * @return 0=成功, 非0=失败
 */
int audio_codec_set_hp_volume(audio_codec_t *codec, uint8_t volume);

/**
 * @brief 设置静音状态
 * @param codec 编解码器句柄
 * @param mute  true=静音, false=取消静音
 * @return 0=成功, 非0=失败
 */
int audio_codec_set_mute(audio_codec_t *codec, bool mute);

/**
 * @brief 控制功放使能 (PA_EN)
 * @param enable true=使能功放, false=禁用功放
 *
 * @note 如果板子没有 PA_EN 引脚，此函数不做任何操作
 */
void audio_codec_pa_enable(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_CODEC_H */
