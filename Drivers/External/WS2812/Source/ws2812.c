/**
 * @file    ws2812.c
 * @brief   WS2812 RGB LED 灯带驱动实现
 * @details 使用 GPIO 位翻转 + 精确延时实现 WS2812 单线协议
 * 
 * @note    该驱动针对 S300 CM4 @ 200MHz 优化
 *          时序精度依赖于禁用中断
 */

#include "ws2812.h"
#include "gpio.h"
#include "gpio_s300.h"
#include "s300.h"
#include <string.h>

/* =============================================================================
 * 时序配置 (针对 200MHz CM4, -Os 优化)
 * WS2812B 时序:
 *   T0H = 0.4µs  (80 cycles @ 200MHz)
 *   T0L = 0.85µs (170 cycles @ 200MHz)
 *   T1H = 0.8µs  (160 cycles @ 200MHz)
 *   T1L = 0.45µs (90 cycles @ 200MHz)
 *   Reset > 50µs (10000 cycles @ 200MHz)
 *
 * 注: 循环本身有开销，每次迭代约 5-6 个周期 (含 NOP + 分支)
 *     GPIO 写操作也有延迟，需要实测校准
 * =============================================================================*/

/* 
 * 延时循环计数 - 根据实测校准
 * 注意: GPIO 操作本身有延迟，T0H 必须非常短以区分 0/1
 * WS2812 判定阈值: >0.5µs = 1, <0.5µs = 0
 */
#define WS2812_T0H_CYCLES   1       /* 尽可能短，仅靠 GPIO 写入延迟 */
#define WS2812_T0L_CYCLES   20      /* ~1µs 确保足够长 */
#define WS2812_T1H_CYCLES   12      /* ~0.7-0.8µs */
#define WS2812_T1L_CYCLES   8       /* ~0.4-0.5µs */
#define WS2812_RESET_CYCLES 1000    /* >50µs */

/* 全局亮度 */
static uint8_t g_brightness = 255;

/* =============================================================================
 * 内部函数
 * =============================================================================*/

/**
 * @brief 精确延时 (使用多个 NOP 防止被优化)
 * @note  不要修改此函数，通过调整循环数校准
 */
__attribute__((always_inline, optimize("O0")))
static inline void delay_cycles(uint32_t cycles)
{
    while (cycles--) {
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
    }
}

/**
 * @brief 应用亮度调整
 */
static inline uint8_t apply_brightness(uint8_t value)
{
    return (uint8_t)(((uint16_t)value * g_brightness) >> 8);
}

/**
 * @brief 发送单个字节 (8 位)
 * @note  必须在中断禁用状态下调用
 */
__attribute__((always_inline))
static inline void ws2812_send_byte(S300_GPIO_TypeDef *gpio, uint32_t pin_mask, uint8_t byte)
{
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) {
            /* 发送 '1': 高电平 0.8µs, 低电平 0.45µs */
            gpio->SWPORTA_DR |= pin_mask;
            delay_cycles(WS2812_T1H_CYCLES);
            gpio->SWPORTA_DR &= ~pin_mask;
            delay_cycles(WS2812_T1L_CYCLES);
        } else {
            /* 发送 '0': 高电平 0.4µs, 低电平 0.85µs */
            gpio->SWPORTA_DR |= pin_mask;
            delay_cycles(WS2812_T0H_CYCLES);
            gpio->SWPORTA_DR &= ~pin_mask;
            delay_cycles(WS2812_T0L_CYCLES);
        }
    }
}

/**
 * @brief 发送复位信号 (低电平 > 50µs)
 */
static void ws2812_send_reset(S300_GPIO_TypeDef *gpio, uint32_t pin_mask)
{
    gpio->SWPORTA_DR &= ~pin_mask;
    delay_cycles(WS2812_RESET_CYCLES);
}

/* =============================================================================
 * 公共 API 实现
 * =============================================================================*/

ws2812_color_t ws2812_hsv(uint16_t h, uint8_t s, uint8_t v)
{
    ws2812_color_t color;
    uint8_t region, remainder, p, q, t;

    if (s == 0) {
        color.r = color.g = color.b = v;
        return color;
    }

    region = h / 60;
    remainder = (h - (region * 60)) * 255 / 60;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  color.r = v; color.g = t; color.b = p; break;
        case 1:  color.r = q; color.g = v; color.b = p; break;
        case 2:  color.r = p; color.g = v; color.b = t; break;
        case 3:  color.r = p; color.g = q; color.b = v; break;
        case 4:  color.r = t; color.g = p; color.b = v; break;
        default: color.r = v; color.g = p; color.b = q; break;
    }

    return color;
}

int ws2812_init(ws2812_handle_t *handle)
{
    if (!handle) {
        return -1;
    }

    memset(handle, 0, sizeof(ws2812_handle_t));
    handle->initialized = true;
    g_brightness = 255;

    return 0;
}

int ws2812_add_chain(ws2812_handle_t *handle, void *gpio_port, uint8_t gpio_pin,
                     uint8_t led_count, ws2812_color_t *buffer)
{
    if (!handle || !handle->initialized) {
        return -1;
    }
    if (handle->chain_count >= WS2812_CHAIN_COUNT) {
        return -2;  /* 链数量已满 */
    }
    if (!buffer || led_count == 0 || led_count > WS2812_MAX_LEDS) {
        return -3;  /* 参数无效 */
    }

    int idx = handle->chain_count;
    ws2812_chain_t *chain = &handle->chains[idx];

    chain->gpio_port = gpio_port;
    chain->gpio_pin = gpio_pin;
    chain->led_count = led_count;
    chain->buffer = buffer;

    /* 配置 GPIO 为软件控制输出模式 */
    gpio_set_function(GPIOA, gpio_pin, FUNCTION_2);  /* GPIO 功能 (PAD15/30 的 FUNCTION_2 = GPIO) */
    gpio_set_mode(GPIOA, gpio_pin, GPIO_DOWN);       /* 下拉 */
    gpio_set_direction(GPIOA, gpio_pin, 1);          /* 输出 */
    gpio_set_data(GPIOA, gpio_pin, 0);               /* 初始低电平 */

    /* 清空缓冲区 */
    memset(buffer, 0, led_count * sizeof(ws2812_color_t));

    handle->chain_count++;
    return idx;
}

int ws2812_set_pixel(ws2812_handle_t *handle, uint8_t chain_idx,
                     uint8_t led_idx, ws2812_color_t color)
{
    if (!handle || !handle->initialized) {
        return -1;
    }
    if (chain_idx >= handle->chain_count) {
        return -2;
    }

    ws2812_chain_t *chain = &handle->chains[chain_idx];
    if (led_idx >= chain->led_count) {
        return -3;
    }

    chain->buffer[led_idx] = color;
    return 0;
}

int ws2812_fill(ws2812_handle_t *handle, uint8_t chain_idx, ws2812_color_t color)
{
    if (!handle || !handle->initialized) {
        return -1;
    }
    if (chain_idx >= handle->chain_count) {
        return -2;
    }

    ws2812_chain_t *chain = &handle->chains[chain_idx];
    for (uint8_t i = 0; i < chain->led_count; i++) {
        chain->buffer[i] = color;
    }

    return 0;
}

int ws2812_clear(ws2812_handle_t *handle, uint8_t chain_idx)
{
    return ws2812_fill(handle, chain_idx, WS2812_COLOR_OFF);
}

int ws2812_show(ws2812_handle_t *handle, uint8_t chain_idx)
{
    if (!handle || !handle->initialized) {
        return -1;
    }
    if (chain_idx >= handle->chain_count) {
        return -2;
    }

    ws2812_chain_t *chain = &handle->chains[chain_idx];
    S300_GPIO_TypeDef *gpio = GPIO;
    uint32_t pin_mask = (1UL << chain->gpio_pin);

    /* 禁用中断以保证时序精度 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* 发送所有 LED 数据 (GRB 顺序) */
    for (uint8_t i = 0; i < chain->led_count; i++) {
        ws2812_color_t *c = &chain->buffer[i];
        ws2812_send_byte(gpio, pin_mask, apply_brightness(c->g));
        ws2812_send_byte(gpio, pin_mask, apply_brightness(c->r));
        ws2812_send_byte(gpio, pin_mask, apply_brightness(c->b));
    }

    /* 发送复位信号 */
    ws2812_send_reset(gpio, pin_mask);

    /* 恢复中断状态 */
    __set_PRIMASK(primask);

    return 0;
}

int ws2812_show_all(ws2812_handle_t *handle)
{
    if (!handle || !handle->initialized) {
        return -1;
    }

    for (uint8_t i = 0; i < handle->chain_count; i++) {
        int ret = ws2812_show(handle, i);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

void ws2812_set_brightness(ws2812_handle_t *handle, uint8_t brightness)
{
    (void)handle;  /* 全局亮度，不需要句柄 */
    g_brightness = brightness;
}
