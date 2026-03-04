# =============================================================================
# WS2812_LED_Demo - WS2812 RGB LED 灯带演示
# =============================================================================
# 演示 WS2812 灯带的各种灯效:
#   - 逐个点亮
#   - 彩虹流水
#   - 呼吸灯
#   - 颜色渐变
#   - 交替闪烁
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(WS2812_LED_Demo)

# 添加 WS2812 驱动
add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/WS2812 ${CMAKE_CURRENT_BINARY_DIR}/WS2812)

# 使用统一的项目创建函数
s300_add_executable(
    TARGET s300_ws2812_led_demo
    SOURCES Src/main.c
    DRIVERS rcc gpio uart
    LIBRARIES ws2812
)
