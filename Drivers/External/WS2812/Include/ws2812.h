/**
 * @file    ws2812.h
 * @brief   WS2812 RGB LED 灯带驱动
 * @details 支持 WS2812/WS2812B/SK6812 等兼容型号
 *          通过 GPIO 位翻转实现单线通信协议
 * 
 * @note    时序要求 (WS2812B):
 *          - T0H: 0.4µs ± 150ns (0 码高电平)
 *          - T0L: 0.85µs ± 150ns (0 码低电平)
 *          - T1H: 0.85µs ± 150ns (1 码高电平)
 *          - T1L: 0.4µs ± 150ns (1 码低电平)
 *          - Reset: > 50µs (低电平)
 */

#ifndef S300_BSP_WS2812_H
#define S300_BSP_WS2812_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * 配置宏
 * =============================================================================*/

/** @brief 最大支持的 LED 数量 (单条链) */
#ifndef WS2812_MAX_LEDS
#define WS2812_MAX_LEDS         32
#endif

/** @brief LED 链数量 */
#ifndef WS2812_CHAIN_COUNT
#define WS2812_CHAIN_COUNT      2
#endif

/* =============================================================================
 * 数据结构
 * =============================================================================*/

/**
 * @brief RGB 颜色结构 (GRB 顺序，WS2812 协议要求)
 */
typedef struct {
    uint8_t g;      /**< 绿色分量 [0-255] */
    uint8_t r;      /**< 红色分量 [0-255] */
    uint8_t b;      /**< 蓝色分量 [0-255] */
} ws2812_color_t;

/**
 * @brief WS2812 LED 链配置
 */
typedef struct {
    void *gpio_port;            /**< GPIO 端口 (如 GPIOA) */
    uint8_t gpio_pin;           /**< GPIO 引脚号 */
    uint8_t led_count;          /**< LED 数量 */
    ws2812_color_t *buffer;     /**< 颜色缓冲区 (由用户分配) */
} ws2812_chain_t;

/**
 * @brief WS2812 驱动句柄
 */
typedef struct {
    ws2812_chain_t chains[WS2812_CHAIN_COUNT];  /**< LED 链数组 */
    uint8_t chain_count;                         /**< 实际使用的链数 */
    bool initialized;                            /**< 初始化标志 */
} ws2812_handle_t;

/* =============================================================================
 * 预定义颜色
 * =============================================================================*/

#define WS2812_COLOR_OFF        ((ws2812_color_t){0, 0, 0})
#define WS2812_COLOR_RED        ((ws2812_color_t){0, 255, 0})
#define WS2812_COLOR_GREEN      ((ws2812_color_t){255, 0, 0})
#define WS2812_COLOR_BLUE       ((ws2812_color_t){0, 0, 255})
#define WS2812_COLOR_WHITE      ((ws2812_color_t){255, 255, 255})
#define WS2812_COLOR_YELLOW     ((ws2812_color_t){255, 255, 0})
#define WS2812_COLOR_CYAN       ((ws2812_color_t){255, 0, 255})
#define WS2812_COLOR_MAGENTA    ((ws2812_color_t){0, 255, 255})
#define WS2812_COLOR_ORANGE     ((ws2812_color_t){165, 255, 0})
#define WS2812_COLOR_PURPLE     ((ws2812_color_t){0, 128, 128})

/* =============================================================================
 * API 函数
 * =============================================================================*/

/**
 * @brief  创建 RGB 颜色
 * @param  r  红色分量 [0-255]
 * @param  g  绿色分量 [0-255]
 * @param  b  蓝色分量 [0-255]
 * @return ws2812_color_t 颜色结构
 */
static inline ws2812_color_t ws2812_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_color_t c = {g, r, b};
    return c;
}

/**
 * @brief  HSV 转 RGB
 * @param  h  色相 [0-359]
 * @param  s  饱和度 [0-255]
 * @param  v  明度 [0-255]
 * @return ws2812_color_t RGB 颜色
 */
ws2812_color_t ws2812_hsv(uint16_t h, uint8_t s, uint8_t v);

/**
 * @brief  初始化 WS2812 驱动
 * @param  handle  驱动句柄指针
 * @return 0=成功, <0=失败
 */
int ws2812_init(ws2812_handle_t *handle);

/**
 * @brief  添加 LED 链
 * @param  handle     驱动句柄
 * @param  gpio_port  GPIO 端口
 * @param  gpio_pin   GPIO 引脚号
 * @param  led_count  LED 数量
 * @param  buffer     颜色缓冲区 (大小 = led_count * sizeof(ws2812_color_t))
 * @return 链索引 (0-based), <0=失败
 */
int ws2812_add_chain(ws2812_handle_t *handle, void *gpio_port, uint8_t gpio_pin,
                     uint8_t led_count, ws2812_color_t *buffer);

/**
 * @brief  设置单个 LED 颜色 (不立即发送)
 * @param  handle      驱动句柄
 * @param  chain_idx   链索引
 * @param  led_idx     LED 索引 (0-based)
 * @param  color       颜色值
 * @return 0=成功, <0=失败
 */
int ws2812_set_pixel(ws2812_handle_t *handle, uint8_t chain_idx,
                     uint8_t led_idx, ws2812_color_t color);

/**
 * @brief  设置所有 LED 为同一颜色 (不立即发送)
 * @param  handle      驱动句柄
 * @param  chain_idx   链索引
 * @param  color       颜色值
 * @return 0=成功, <0=失败
 */
int ws2812_fill(ws2812_handle_t *handle, uint8_t chain_idx, ws2812_color_t color);

/**
 * @brief  清空所有 LED (设为黑色，不立即发送)
 * @param  handle      驱动句柄
 * @param  chain_idx   链索引
 * @return 0=成功, <0=失败
 */
int ws2812_clear(ws2812_handle_t *handle, uint8_t chain_idx);

/**
 * @brief  发送数据到 LED 链 (刷新显示)
 * @param  handle      驱动句柄
 * @param  chain_idx   链索引
 * @return 0=成功, <0=失败
 * @note   该函数会临时禁用中断以保证时序精度
 */
int ws2812_show(ws2812_handle_t *handle, uint8_t chain_idx);

/**
 * @brief  刷新所有 LED 链
 * @param  handle  驱动句柄
 * @return 0=成功, <0=失败
 */
int ws2812_show_all(ws2812_handle_t *handle);

/**
 * @brief  设置全局亮度
 * @param  handle      驱动句柄
 * @param  brightness  亮度 [0-255]，255=全亮度
 * @note   该设置会影响后续的 show 操作
 */
void ws2812_set_brightness(ws2812_handle_t *handle, uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_WS2812_H */
