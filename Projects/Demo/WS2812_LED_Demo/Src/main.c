/**
 * @file    main.c
 * @brief   WS2812 RGB LED 灯带演示程序
 * @details 演示两条 WS2812 灯带的各种效果:
 *          - 链1 (RGB_DIN1/GPIO15): 5颗 LED
 *          - 链2 (RGB_DIN2/GPIO30): 5颗 LED
 *
 *          基础效果演示:
 *          1. 逐个点亮 (红/绿/蓝)
 *          2. 彩虹流水
 *          3. 呼吸灯
 *          4. 颜色渐变
 *          5. 交替闪烁
 *
 *          系统状态灯效果演示:
 *          - 启动流水、待机呼吸、语音唤醒、跟踪中
 *          - 目标丢失、UWB辅助、拍照、录像中
 *          - 蓝牙配对、蓝牙已连接、错误、低电量
 * 
 * @note    硬件: gimbal_master 板，两条 WS2812B 灯带
 */

#include "s300.h"
#include "board.h"
#include "ws2812.h"
#include "gpio.h"
#include "gpio_s300.h"
#include "rcc.h"
#include <stdio.h>

/*===========================================================================
 * 配置
 *===========================================================================*/

#define LED_CHAIN1_PIN      BOARD_RGB_DIN1_PIN   /* GPIO15 */
#define LED_CHAIN2_PIN      BOARD_RGB_DIN2_PIN   /* GPIO30 */
#define LED_COUNT_PER_CHAIN 5                     /* 每条链 5 颗 LED */

/* 演示效果时间 */
#define EFFECT_DURATION_MS  5000
#define PIXEL_DELAY_MS      100
#define RAINBOW_DELAY_MS    50
#define BREATHE_DELAY_MS    20

/*===========================================================================
 * 私有变量
 *===========================================================================*/

static ws2812_handle_t g_led_handle;
static ws2812_color_t g_chain1_buffer[LED_COUNT_PER_CHAIN];
static ws2812_color_t g_chain2_buffer[LED_COUNT_PER_CHAIN];

/*===========================================================================
 * 私有函数
 *===========================================================================*/

/**
 * @brief 毫秒延时
 */
static void delay_ms(uint32_t ms)
{
    uint32_t reload = SystemCoreClock / 1000 - 1;
    if (reload > 0xFFFFFF) reload = 0xFFFFFF;
    
    SysTick->LOAD = reload;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    
    for (uint32_t i = 0; i < ms; i++) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    }
    
    SysTick->CTRL = 0;
}

/**
 * @brief 初始化 LED 系统
 */
static int led_init(void)
{
    int ret;

    /* 初始化驱动 */
    ret = ws2812_init(&g_led_handle);
    if (ret < 0) {
        printf("WS2812 init failed: %d\n", ret);
        return ret;
    }

    /* 添加链1 */
    ret = ws2812_add_chain(&g_led_handle, (void*)GPIO_BASE, LED_CHAIN1_PIN,
                           LED_COUNT_PER_CHAIN, g_chain1_buffer);
    if (ret < 0) {
        printf("Add chain1 failed: %d\n", ret);
        return ret;
    }
    printf("Chain1 added: GPIO%d, %d LEDs\n", LED_CHAIN1_PIN, LED_COUNT_PER_CHAIN);

    /* 添加链2 */
    ret = ws2812_add_chain(&g_led_handle, (void*)GPIO_BASE, LED_CHAIN2_PIN,
                           LED_COUNT_PER_CHAIN, g_chain2_buffer);
    if (ret < 0) {
        printf("Add chain2 failed: %d\n", ret);
        return ret;
    }
    printf("Chain2 added: GPIO%d, %d LEDs\n", LED_CHAIN2_PIN, LED_COUNT_PER_CHAIN);

    /* 设置默认亮度 (32 = ~12.5%, 避免刺眼) */
    ws2812_set_brightness(&g_led_handle, 32);
    printf("Brightness set to 32/255\n");

    /* 清空并显示 */
    ws2812_clear(&g_led_handle, 0);
    ws2812_clear(&g_led_handle, 1);
    ws2812_show_all(&g_led_handle);

    return 0;
}

/**
 * @brief 效果1: 逐个点亮
 */
static void effect_pixel_chase(void)
{
    ws2812_color_t colors[] = {
        WS2812_COLOR_RED,
        WS2812_COLOR_GREEN,
        WS2812_COLOR_BLUE
    };
    
    printf("Effect: Pixel Chase\n");
    
    for (int c = 0; c < 3; c++) {
        for (int i = 0; i < LED_COUNT_PER_CHAIN; i++) {
            /* 链1 正向 */
            ws2812_clear(&g_led_handle, 0);
            ws2812_set_pixel(&g_led_handle, 0, i, colors[c]);
            
            /* 链2 反向 */
            ws2812_clear(&g_led_handle, 1);
            ws2812_set_pixel(&g_led_handle, 1, LED_COUNT_PER_CHAIN - 1 - i, colors[c]);
            
            ws2812_show_all(&g_led_handle);
            delay_ms(PIXEL_DELAY_MS);
        }
    }
}

/**
 * @brief 效果2: 彩虹流水
 */
static void effect_rainbow(void)
{
    printf("Effect: Rainbow\n");
    
    uint16_t hue_offset = 0;
    uint32_t duration_count = EFFECT_DURATION_MS / RAINBOW_DELAY_MS;
    
    for (uint32_t t = 0; t < duration_count; t++) {
        for (int i = 0; i < LED_COUNT_PER_CHAIN; i++) {
            uint16_t hue = (hue_offset + i * 360 / LED_COUNT_PER_CHAIN) % 360;
            ws2812_color_t color = ws2812_hsv(hue, 255, 255);
            
            ws2812_set_pixel(&g_led_handle, 0, i, color);
            ws2812_set_pixel(&g_led_handle, 1, i, color);
        }
        
        ws2812_show_all(&g_led_handle);
        hue_offset = (hue_offset + 5) % 360;
        delay_ms(RAINBOW_DELAY_MS);
    }
}

/**
 * @brief 效果3: 呼吸灯
 */
static void effect_breathe(void)
{
    printf("Effect: Breathe\n");
    
    ws2812_color_t base_color = WS2812_COLOR_CYAN;
    
    /* 渐亮 (亮度范围 0-64，避免刺眼) */
    for (int b = 0; b <= 64; b += 2) {
        ws2812_set_brightness(&g_led_handle, b);
        ws2812_fill(&g_led_handle, 0, base_color);
        ws2812_fill(&g_led_handle, 1, base_color);
        ws2812_show_all(&g_led_handle);
        delay_ms(BREATHE_DELAY_MS);
    }
    
    /* 渐暗 */
    for (int b = 64; b >= 0; b -= 2) {
        ws2812_set_brightness(&g_led_handle, b);
        ws2812_fill(&g_led_handle, 0, base_color);
        ws2812_fill(&g_led_handle, 1, base_color);
        ws2812_show_all(&g_led_handle);
        delay_ms(BREATHE_DELAY_MS);
    }
    
    /* 恢复到演示亮度 */
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 效果4: 颜色渐变
 */
static void effect_color_fade(void)
{
    printf("Effect: Color Fade\n");
    
    ws2812_color_t colors[] = {
        WS2812_COLOR_RED,
        WS2812_COLOR_ORANGE,
        WS2812_COLOR_YELLOW,
        WS2812_COLOR_GREEN,
        WS2812_COLOR_CYAN,
        WS2812_COLOR_BLUE,
        WS2812_COLOR_PURPLE,
        WS2812_COLOR_MAGENTA
    };
    int num_colors = sizeof(colors) / sizeof(colors[0]);
    
    for (int c = 0; c < num_colors; c++) {
        ws2812_fill(&g_led_handle, 0, colors[c]);
        ws2812_fill(&g_led_handle, 1, colors[c]);
        ws2812_show_all(&g_led_handle);
        delay_ms(500);
    }
}

/**
 * @brief 效果5: 交替闪烁
 */
static void effect_alternate_blink(void)
{
    printf("Effect: Alternate Blink\n");
    
    for (int t = 0; t < 10; t++) {
        /* 链1 亮, 链2 灭 */
        ws2812_fill(&g_led_handle, 0, WS2812_COLOR_CYAN);
        ws2812_clear(&g_led_handle, 1);
        ws2812_show_all(&g_led_handle);
        delay_ms(300);
        
        /* 链1 灭, 链2 亮 */
        ws2812_clear(&g_led_handle, 0);
        ws2812_fill(&g_led_handle, 1, WS2812_COLOR_CYAN);
        ws2812_show_all(&g_led_handle);
        delay_ms(300);
    }
}

/*===========================================================================
 * 系统状态灯效果 (美化版)
 *===========================================================================*/

/* 预定义颜色 */
#define COLOR_PALE_GOLD     ws2812_rgb(255, 220, 100)
#define COLOR_PALE_CYAN     ws2812_rgb(150, 230, 255)
#define COLOR_GOLD          ws2812_rgb(255, 215, 0)
#define COLOR_AMBER         ws2812_rgb(255, 191, 0)
#define COLOR_CORAL         ws2812_rgb(255, 127, 80)
#define COLOR_MINT          ws2812_rgb(152, 255, 152)
#define COLOR_SKY_BLUE      ws2812_rgb(135, 206, 250)
#define COLOR_VIOLET        ws2812_rgb(238, 130, 238)
#define COLOR_DEEP_PURPLE   ws2812_rgb(148, 0, 211)
#define COLOR_LIME          ws2812_rgb(173, 255, 47)
#define COLOR_PINK          ws2812_rgb(255, 105, 180)

/**
 * @brief 辅助: 颜色插值 (线性混合)
 */
static ws2812_color_t color_blend(ws2812_color_t c1, ws2812_color_t c2, uint8_t ratio)
{
    ws2812_color_t result;
    result.r = (c1.r * (255 - ratio) + c2.r * ratio) / 255;
    result.g = (c1.g * (255 - ratio) + c2.g * ratio) / 255;
    result.b = (c1.b * (255 - ratio) + c2.b * ratio) / 255;
    return result;
}

/**
 * @brief 辅助: 获取衰减颜色 (用于彗星尾迹)
 */
static ws2812_color_t color_dim(ws2812_color_t c, uint8_t factor)
{
    ws2812_color_t result;
    result.r = (c.r * factor) / 255;
    result.g = (c.g * factor) / 255;
    result.b = (c.b * factor) / 255;
    return result;
}

/**
 * @brief 状态: 启动中 - 双彗星尾迹对冲
 */
static void status_boot(void)
{
    printf("Status: BOOT (Dual comet trails)\n");
    
    ws2812_set_brightness(&g_led_handle, 48);
    ws2812_color_t comet_color = COLOR_SKY_BLUE;
    
    /* 尾迹衰减系数 */
    uint8_t trail_fade[] = {255, 180, 100, 50, 20};
    
    for (int cycle = 0; cycle < 2; cycle++) {
        for (int pos = 0; pos < LED_COUNT_PER_CHAIN + 3; pos++) {
            ws2812_clear(&g_led_handle, 0);
            ws2812_clear(&g_led_handle, 1);
            
            /* 链1: 正向彗星 */
            for (int t = 0; t < 5 && pos - t >= 0; t++) {
                int idx = pos - t;
                if (idx < LED_COUNT_PER_CHAIN) {
                    ws2812_set_pixel(&g_led_handle, 0, idx, color_dim(comet_color, trail_fade[t]));
                }
            }
            
            /* 链2: 反向彗星 */
            int rev_pos = LED_COUNT_PER_CHAIN - 1 - pos;
            for (int t = 0; t < 5; t++) {
                int idx = rev_pos + t;
                if (idx >= 0 && idx < LED_COUNT_PER_CHAIN) {
                    ws2812_set_pixel(&g_led_handle, 1, idx, color_dim(comet_color, trail_fade[t]));
                }
            }
            
            ws2812_show_all(&g_led_handle);
            delay_ms(60);
        }
        
        /* 颜色渐变到下一色 */
        comet_color = (cycle == 0) ? COLOR_VIOLET : COLOR_MINT;
    }
    
    /* 汇聚闪光效果 */
    for (int b = 0; b <= 64; b += 8) {
        ws2812_set_brightness(&g_led_handle, b);
        ws2812_fill(&g_led_handle, 0, COLOR_PALE_GOLD);
        ws2812_fill(&g_led_handle, 1, COLOR_PALE_GOLD);
        ws2812_show_all(&g_led_handle);
        delay_ms(20);
    }
    for (int b = 64; b >= 32; b -= 4) {
        ws2812_set_brightness(&g_led_handle, b);
        ws2812_show_all(&g_led_handle);
        delay_ms(20);
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 待机 - 极光呼吸 (柔和渐变)
 */
static void status_idle(void)
{
    printf("Status: IDLE (Aurora breathe)\n");
    
    ws2812_color_t aurora_colors[] = {
        ws2812_rgb(64, 224, 208),   /* 青绿 */
        ws2812_rgb(127, 255, 212),  /* 薄荷 */
        ws2812_rgb(173, 216, 230),  /* 浅蓝 */
    };
    
    for (int cycle = 0; cycle < 2; cycle++) {
        /* 渐亮 + 颜色渐变 */
        for (int step = 0; step < 60; step++) {
            int b = 8 + (step < 30 ? step : 60 - step);
            ws2812_set_brightness(&g_led_handle, b);
            
            /* 每个LED略有色差，形成极光效果 */
            for (int i = 0; i < LED_COUNT_PER_CHAIN; i++) {
                int color_idx = (step / 20 + i) % 3;
                int next_idx = (color_idx + 1) % 3;
                uint8_t blend = (step * 4) % 255;
                ws2812_color_t c = color_blend(aurora_colors[color_idx], aurora_colors[next_idx], blend);
                ws2812_set_pixel(&g_led_handle, 0, i, c);
                ws2812_set_pixel(&g_led_handle, 1, LED_COUNT_PER_CHAIN - 1 - i, c);
            }
            ws2812_show_all(&g_led_handle);
            delay_ms(50);
        }
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 语音唤醒 - 绿色涟漪扩散
 */
static void status_voice_wakeup(void)
{
    printf("Status: VOICE_WAKEUP (Green ripple)\n");
    
    ws2812_set_brightness(&g_led_handle, 48);
    
    for (int wave = 0; wave < 3; wave++) {
        /* 从中心向两端扩散 */
        for (int r = 0; r <= LED_COUNT_PER_CHAIN; r++) {
            ws2812_clear(&g_led_handle, 0);
            ws2812_clear(&g_led_handle, 1);
            
            int center = LED_COUNT_PER_CHAIN / 2;
            
            /* 绘制扩散波纹 (带渐变) */
            for (int d = 0; d <= r && d < 3; d++) {
                uint8_t intensity = 255 - d * 80;
                ws2812_color_t c = color_dim(COLOR_LIME, intensity);
                
                int left = center - (r - d);
                int right = center + (r - d);
                
                if (left >= 0) {
                    ws2812_set_pixel(&g_led_handle, 0, left, c);
                    ws2812_set_pixel(&g_led_handle, 1, left, c);
                }
                if (right < LED_COUNT_PER_CHAIN && right != left) {
                    ws2812_set_pixel(&g_led_handle, 0, right, c);
                    ws2812_set_pixel(&g_led_handle, 1, right, c);
                }
            }
            
            ws2812_show_all(&g_led_handle);
            delay_ms(40);
        }
        delay_ms(80);
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 跟踪中 - 双链追逐彩虹
 */
static void status_tracking(void)
{
    printf("Status: TRACKING (Dual rainbow chase)\n");
    
    ws2812_set_brightness(&g_led_handle, 40);
    
    uint16_t hue_offset = 0;
    for (int t = 0; t < 80; t++) {
        for (int i = 0; i < LED_COUNT_PER_CHAIN; i++) {
            /* 链1: 暖色调彩虹 */
            uint16_t hue1 = (hue_offset + i * 30) % 360;
            if (hue1 > 60) hue1 = hue1 % 60;  /* 限制在暖色 */
            ws2812_set_pixel(&g_led_handle, 0, i, ws2812_hsv(hue1, 255, 255));
            
            /* 链2: 冷色调彩虹 */
            uint16_t hue2 = (180 + hue_offset + (LED_COUNT_PER_CHAIN - 1 - i) * 30) % 360;
            if (hue2 < 180) hue2 = 180 + (hue2 % 60);
            if (hue2 > 300) hue2 = 180 + (hue2 % 120);
            ws2812_set_pixel(&g_led_handle, 1, i, ws2812_hsv(hue2, 200, 255));
        }
        
        ws2812_show_all(&g_led_handle);
        hue_offset = (hue_offset + 8) % 360;
        delay_ms(40);
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 目标丢失 - 琥珀色搜索扫描
 */
static void status_target_lost(void)
{
    printf("Status: TARGET_LOST (Amber scanning)\n");
    
    ws2812_set_brightness(&g_led_handle, 48);
    ws2812_color_t amber = COLOR_AMBER;
    
    for (int cycle = 0; cycle < 3; cycle++) {
        /* 左右扫描效果 */
        for (int dir = 0; dir < 2; dir++) {
            for (int pos = 0; pos < LED_COUNT_PER_CHAIN; pos++) {
                ws2812_clear(&g_led_handle, 0);
                ws2812_clear(&g_led_handle, 1);
                
                int actual_pos = dir ? (LED_COUNT_PER_CHAIN - 1 - pos) : pos;
                
                /* 扫描光点 + 尾迹 */
                for (int t = 0; t < 3; t++) {
                    int idx = dir ? (actual_pos + t) : (actual_pos - t);
                    if (idx >= 0 && idx < LED_COUNT_PER_CHAIN) {
                        ws2812_set_pixel(&g_led_handle, 0, idx, color_dim(amber, 255 - t * 80));
                        ws2812_set_pixel(&g_led_handle, 1, idx, color_dim(amber, 255 - t * 80));
                    }
                }
                
                ws2812_show_all(&g_led_handle);
                delay_ms(50);
            }
        }
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: UWB 辅助 - 紫色雷达扫描
 */
static void status_uwb_assist(void)
{
    printf("Status: UWB_ASSIST (Purple radar scan)\n");
    
    ws2812_set_brightness(&g_led_handle, 48);
    
    for (int scan = 0; scan < 4; scan++) {
        /* 雷达扫描: 从左到右渐变 */
        for (int pos = 0; pos < LED_COUNT_PER_CHAIN * 2; pos++) {
            for (int i = 0; i < LED_COUNT_PER_CHAIN; i++) {
                int dist = pos - i;
                if (dist >= 0 && dist < LED_COUNT_PER_CHAIN) {
                    uint8_t intensity = 255 - dist * 50;
                    if (intensity < 30) intensity = 30;
                    ws2812_color_t c = color_blend(COLOR_DEEP_PURPLE, COLOR_PINK, (i * 50) % 255);
                    ws2812_set_pixel(&g_led_handle, 0, i, color_dim(c, intensity));
                    ws2812_set_pixel(&g_led_handle, 1, LED_COUNT_PER_CHAIN - 1 - i, color_dim(c, intensity));
                } else if (dist >= LED_COUNT_PER_CHAIN) {
                    ws2812_color_t dim = color_dim(COLOR_DEEP_PURPLE, 20);
                    ws2812_set_pixel(&g_led_handle, 0, i, dim);
                    ws2812_set_pixel(&g_led_handle, 1, LED_COUNT_PER_CHAIN - 1 - i, dim);
                }
            }
            ws2812_show_all(&g_led_handle);
            delay_ms(35);
        }
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 拍照 - 闪光灯效果
 */
static void status_capture(void)
{
    printf("Status: CAPTURE (Flash effect)\n");
    
    /* 预闪 (柔和) */
    for (int b = 0; b <= 32; b += 8) {
        ws2812_set_brightness(&g_led_handle, b);
        ws2812_fill(&g_led_handle, 0, COLOR_PALE_GOLD);
        ws2812_fill(&g_led_handle, 1, COLOR_PALE_GOLD);
        ws2812_show_all(&g_led_handle);
        delay_ms(15);
    }
    delay_ms(100);
    
    /* 主闪光 (强烈) */
    ws2812_set_brightness(&g_led_handle, 80);
    ws2812_fill(&g_led_handle, 0, COLOR_PALE_GOLD);
    ws2812_fill(&g_led_handle, 1, COLOR_PALE_GOLD);
    ws2812_show_all(&g_led_handle);
    delay_ms(120);
    
    /* 快速衰减 */
    for (int b = 80; b >= 0; b -= 10) {
        ws2812_set_brightness(&g_led_handle, b);
        ws2812_show_all(&g_led_handle);
        delay_ms(20);
    }
    
    /* 短暂黑屏 */
    ws2812_clear(&g_led_handle, 0);
    ws2812_clear(&g_led_handle, 1);
    ws2812_show_all(&g_led_handle);
    delay_ms(300);
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 录像中 - 红色心跳脉冲
 */
static void status_recording(void)
{
    printf("Status: RECORDING (Red heartbeat)\n");
    
    ws2812_color_t red = WS2812_COLOR_RED;
    ws2812_color_t dark_red = ws2812_rgb(80, 0, 0);
    
    for (int beat = 0; beat < 4; beat++) {
        /* 第一次脉冲 (快) */
        for (int b = 16; b <= 64; b += 12) {
            ws2812_set_brightness(&g_led_handle, b);
            ws2812_fill(&g_led_handle, 0, red);
            ws2812_fill(&g_led_handle, 1, red);
            ws2812_show_all(&g_led_handle);
            delay_ms(15);
        }
        for (int b = 64; b >= 24; b -= 10) {
            ws2812_set_brightness(&g_led_handle, b);
            ws2812_show_all(&g_led_handle);
            delay_ms(15);
        }
        
        delay_ms(80);
        
        /* 第二次脉冲 (稍弱) */
        for (int b = 24; b <= 48; b += 8) {
            ws2812_set_brightness(&g_led_handle, b);
            ws2812_show_all(&g_led_handle);
            delay_ms(15);
        }
        for (int b = 48; b >= 16; b -= 4) {
            ws2812_set_brightness(&g_led_handle, b);
            ws2812_fill(&g_led_handle, 0, red);
            ws2812_fill(&g_led_handle, 1, dark_red);
            ws2812_show_all(&g_led_handle);
            delay_ms(20);
        }
        
        delay_ms(400);
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 蓝牙配对 - 蓝色呼吸 + 白色星点闪烁
 */
static void status_ble_pairing(void)
{
    printf("Status: BLE_PAIRING (Blue breathe + sparkle)\n");
    
    for (int cycle = 0; cycle < 3; cycle++) {
        for (int step = 0; step < 40; step++) {
            int b = 20 + (step < 20 ? step : 40 - step);
            ws2812_set_brightness(&g_led_handle, b);
            
            /* 基底: 蓝色渐变 */
            for (int i = 0; i < LED_COUNT_PER_CHAIN; i++) {
                uint8_t blue_val = 200 + (i * 10);
                ws2812_color_t base = ws2812_rgb(0, blue_val / 4, blue_val);
                ws2812_set_pixel(&g_led_handle, 0, i, base);
                ws2812_set_pixel(&g_led_handle, 1, i, base);
            }
            
            /* 随机星点闪烁 */
            if ((step % 5) == 0) {
                int star = (step / 5) % LED_COUNT_PER_CHAIN;
                ws2812_set_pixel(&g_led_handle, 0, star, COLOR_PALE_CYAN);
                ws2812_set_pixel(&g_led_handle, 1, (star + 2) % LED_COUNT_PER_CHAIN, COLOR_PALE_CYAN);
            }
            
            ws2812_show_all(&g_led_handle);
            delay_ms(50);
        }
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 蓝牙已连接 - 蓝色汇聚闪光
 */
static void status_ble_connected(void)
{
    printf("Status: BLE_CONNECTED (Blue converge flash)\n");
    
    ws2812_set_brightness(&g_led_handle, 48);
    ws2812_color_t blue = COLOR_SKY_BLUE;
    
    /* 两端向中心汇聚 */
    int center = LED_COUNT_PER_CHAIN / 2;
    for (int r = center; r >= 0; r--) {
        ws2812_clear(&g_led_handle, 0);
        ws2812_clear(&g_led_handle, 1);
        
        ws2812_set_pixel(&g_led_handle, 0, r, blue);
        ws2812_set_pixel(&g_led_handle, 0, LED_COUNT_PER_CHAIN - 1 - r, blue);
        ws2812_set_pixel(&g_led_handle, 1, r, blue);
        ws2812_set_pixel(&g_led_handle, 1, LED_COUNT_PER_CHAIN - 1 - r, blue);
        
        ws2812_show_all(&g_led_handle);
        delay_ms(80);
    }
    
    /* 中心闪光扩散 */
    for (int b = 48; b <= 80; b += 8) {
        ws2812_set_brightness(&g_led_handle, b);
        ws2812_fill(&g_led_handle, 0, blue);
        ws2812_fill(&g_led_handle, 1, blue);
        ws2812_show_all(&g_led_handle);
        delay_ms(30);
    }
    
    delay_ms(800);
    
    /* 渐隐 */
    for (int b = 80; b >= 32; b -= 4) {
        ws2812_set_brightness(&g_led_handle, b);
        ws2812_show_all(&g_led_handle);
        delay_ms(30);
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 错误 - 红色闪烁 + 震动感
 */
static void status_error(void)
{
    printf("Status: ERROR (Red strobe)\n");
    
    for (int i = 0; i < 6; i++) {
        /* 快闪两次 */
        for (int flash = 0; flash < 2; flash++) {
            ws2812_set_brightness(&g_led_handle, 80);
            ws2812_fill(&g_led_handle, 0, WS2812_COLOR_RED);
            ws2812_fill(&g_led_handle, 1, WS2812_COLOR_RED);
            ws2812_show_all(&g_led_handle);
            delay_ms(50);
            
            ws2812_set_brightness(&g_led_handle, 8);
            ws2812_fill(&g_led_handle, 0, ws2812_rgb(50, 0, 0));
            ws2812_fill(&g_led_handle, 1, ws2812_rgb(50, 0, 0));
            ws2812_show_all(&g_led_handle);
            delay_ms(50);
        }
        delay_ms(200);
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 低电量 - 橙红渐变警告
 */
static void status_low_battery(void)
{
    printf("Status: LOW_BATTERY (Orange-red warning)\n");
    
    ws2812_color_t orange = COLOR_CORAL;
    ws2812_color_t red = ws2812_rgb(255, 50, 0);
    
    for (int cycle = 0; cycle < 3; cycle++) {
        /* 渐变: 橙 -> 红 -> 橙 */
        for (int step = 0; step < 40; step++) {
            uint8_t ratio = step < 20 ? step * 12 : (40 - step) * 12;
            ws2812_color_t c = color_blend(orange, red, ratio);
            
            int b = 16 + (step < 20 ? step : 40 - step);
            ws2812_set_brightness(&g_led_handle, b);
            
            /* 只点亮链1，链2保持暗淡 */
            ws2812_fill(&g_led_handle, 0, c);
            ws2812_fill(&g_led_handle, 1, color_dim(c, 30));
            ws2812_show_all(&g_led_handle);
            delay_ms(40);
        }
    }
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 板载麦克风 (MIC1/2) - 青色波浪
 */
static void status_mic_onboard(void)
{
    printf("Status: MIC_ONBOARD (Cyan wave)\n");
    
    ws2812_set_brightness(&g_led_handle, 40);
    ws2812_color_t cyan = WS2812_COLOR_CYAN;
    
    /* 显示麦克风图标效果: 中间亮，两边暗 */
    for (int wave = 0; wave < 3; wave++) {
        for (int step = 0; step < 20; step++) {
            for (int i = 0; i < LED_COUNT_PER_CHAIN; i++) {
                int center = LED_COUNT_PER_CHAIN / 2;
                int dist = (i > center) ? (i - center) : (center - i);
                
                /* 波浪效果 */
                int wave_val = (step + dist * 3) % 20;
                uint8_t intensity = wave_val < 10 ? (100 + wave_val * 15) : (100 + (20 - wave_val) * 15);
                
                ws2812_set_pixel(&g_led_handle, 0, i, color_dim(cyan, intensity));
                ws2812_set_pixel(&g_led_handle, 1, i, color_dim(cyan, intensity));
            }
            ws2812_show_all(&g_led_handle);
            delay_ms(50);
        }
    }
    
    /* 确认闪光 */
    ws2812_set_brightness(&g_led_handle, 64);
    ws2812_fill(&g_led_handle, 0, cyan);
    ws2812_fill(&g_led_handle, 1, cyan);
    ws2812_show_all(&g_led_handle);
    delay_ms(300);
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/**
 * @brief 状态: 无线麦克风 (MIC4) - 紫色无线波
 */
static void status_mic_wireless(void)
{
    printf("Status: MIC_WIRELESS (Purple wireless waves)\n");
    
    ws2812_set_brightness(&g_led_handle, 40);
    ws2812_color_t purple = COLOR_VIOLET;
    ws2812_color_t pink = COLOR_PINK;
    
    /* 无线信号波效果: 从中心向外扩散 */
    for (int burst = 0; burst < 4; burst++) {
        for (int r = 0; r <= LED_COUNT_PER_CHAIN; r++) {
            ws2812_clear(&g_led_handle, 0);
            ws2812_clear(&g_led_handle, 1);
            
            int center = LED_COUNT_PER_CHAIN / 2;
            
            /* 绘制多层波纹 */
            for (int wave = 0; wave < 3; wave++) {
                int wave_r = r - wave * 2;
                if (wave_r < 0) continue;
                
                uint8_t intensity = 255 - wave * 70;
                ws2812_color_t c = color_blend(purple, pink, wave * 80);
                c = color_dim(c, intensity);
                
                int left = center - wave_r;
                int right = center + wave_r;
                
                if (left >= 0) {
                    ws2812_set_pixel(&g_led_handle, 0, left, c);
                    ws2812_set_pixel(&g_led_handle, 1, left, c);
                }
                if (right < LED_COUNT_PER_CHAIN && right != left) {
                    ws2812_set_pixel(&g_led_handle, 0, right, c);
                    ws2812_set_pixel(&g_led_handle, 1, right, c);
                }
            }
            
            ws2812_show_all(&g_led_handle);
            delay_ms(45);
        }
        delay_ms(100);
    }
    
    /* 确认效果: 紫色渐变常亮 */
    for (int i = 0; i < LED_COUNT_PER_CHAIN; i++) {
        uint8_t blend = i * 50;
        ws2812_color_t c = color_blend(purple, pink, blend);
        ws2812_set_pixel(&g_led_handle, 0, i, c);
        ws2812_set_pixel(&g_led_handle, 1, LED_COUNT_PER_CHAIN - 1 - i, c);
    }
    ws2812_show_all(&g_led_handle);
    delay_ms(500);
    
    ws2812_set_brightness(&g_led_handle, 32);
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

int main(void)
{
    /* 板级初始化 */
    board_init();
    
    printf("\n");
    printf("===========================================\n");
    printf("  WS2812 RGB LED Demo\n");
    printf("  Board: Gimbal Master\n");
    printf("  Chain1: GPIO%d, %d LEDs\n", LED_CHAIN1_PIN, LED_COUNT_PER_CHAIN);
    printf("  Chain2: GPIO%d, %d LEDs\n", LED_CHAIN2_PIN, LED_COUNT_PER_CHAIN);
    printf("===========================================\n\n");
    
    /* 初始化 LED */
    if (led_init() < 0) {
        printf("LED initialization failed!\n");
        while (1);
    }
    
    printf("LED system initialized. Starting demo...\n\n");
    delay_ms(1000);
    
    /* 主循环: 循环演示各种效果 */
    while (1) {
        /* ===== 第一部分: 基础效果演示 ===== */
        printf("\n========== Basic Effects ==========\n");
        
        effect_pixel_chase();
        delay_ms(500);
        
        effect_rainbow();
        delay_ms(500);
        
        effect_breathe();
        delay_ms(500);
        
        effect_color_fade();
        delay_ms(500);
        
        effect_alternate_blink();
        delay_ms(500);
        
        /* ===== 第二部分: 系统状态灯演示 ===== */
        printf("\n========== System Status Effects ==========\n");
        
        status_boot();          /* 启动中 */
        delay_ms(300);
        
        status_idle();          /* 待机 */
        delay_ms(300);
        
        status_voice_wakeup();  /* 语音唤醒 */
        delay_ms(300);
        
        status_tracking();      /* 跟踪中 */
        delay_ms(300);
        
        status_target_lost();   /* 目标丢失 */
        delay_ms(300);
        
        status_uwb_assist();    /* UWB 辅助 */
        delay_ms(300);
        
        status_capture();       /* 拍照 */
        delay_ms(300);
        
        status_recording();     /* 录像中 */
        delay_ms(300);
        
        status_ble_pairing();   /* 蓝牙配对 */
        delay_ms(300);
        
        status_ble_connected(); /* 蓝牙已连接 */
        delay_ms(300);
        
        status_error();         /* 错误 */
        delay_ms(300);
        
        status_low_battery();   /* 低电量 */
        delay_ms(300);
        
        /* ===== 第三部分: 麦克风切换状态灯演示 ===== */
        printf("\n========== Microphone Status Effects ==========\n");
        
        status_mic_onboard();   /* 板载麦克风 */
        delay_ms(300);
        
        status_mic_wireless();  /* 无线麦克风 */
        delay_ms(300);
        
        /* 清空灯带 */
        ws2812_clear(&g_led_handle, 0);
        ws2812_clear(&g_led_handle, 1);
        ws2812_show_all(&g_led_handle);
        
        printf("\n--- Demo cycle complete, restarting in 2s... ---\n\n");
        delay_ms(2000);
    }
    
    return 0;
}
