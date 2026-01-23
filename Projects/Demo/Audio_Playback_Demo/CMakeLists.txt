# =============================================================================
# Audio_Playback_Demo - 音频播放/录音演示
# =============================================================================
# 功能: I2S + WM8978 Codec 音频直通、正弦波播放、录音回放
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(Audio_Playback_Demo)

s300_add_executable(
    TARGET s300_audio_playback_demo
    SOURCES
    Src/main.c
    DRIVERS
    rcc
    gpio
    uart
    dma
    i2s
    EXTERNALS
    wm8978
    i2c_soft
)
