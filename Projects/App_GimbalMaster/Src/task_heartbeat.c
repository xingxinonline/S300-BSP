/**
 * @file task_heartbeat.c
 * @brief 心跳任务实现
 */

#include "task_heartbeat.h"
#include "app_config.h"
#include "app_log.h"
#include "board.h"
#include "ws2812.h"

#include "FreeRTOS.h"
#include "task.h"

/* ========== 私有变量 ========== */

#if APP_HEARTBEAT_WS2812_ENABLE
static ws2812_handle_t g_ws2812;
static ws2812_color_t g_chain0_buffer[APP_HEARTBEAT_WS2812_CHAIN0_LEDS];
#if APP_HEARTBEAT_WS2812_CHAIN1_ENABLE
static ws2812_color_t g_chain1_buffer[APP_HEARTBEAT_WS2812_CHAIN1_LEDS];
#endif
#endif

/* ========== 私有函数 ========== */

#if APP_HEARTBEAT_WS2812_ENABLE
static void led_chase_update(void)
{
    static uint8_t pos = 0;
    static uint8_t color_idx = 0;
    static uint8_t breathe = APP_HEARTBEAT_WS2812_BREATHE_MIN;
    static int8_t breathe_dir = 1;
    static const ws2812_color_t colors[3] = {
        WS2812_COLOR_RED,
        WS2812_COLOR_GREEN,
        WS2812_COLOR_BLUE
    };

    ws2812_set_brightness(&g_ws2812, breathe);
    ws2812_clear(&g_ws2812, 0);
    ws2812_set_pixel(&g_ws2812, 0, pos, colors[color_idx]);

#if APP_HEARTBEAT_WS2812_CHAIN1_ENABLE
    ws2812_clear(&g_ws2812, 1);
    ws2812_set_pixel(&g_ws2812,
                     1,
                     (uint8_t)(APP_HEARTBEAT_WS2812_CHAIN1_LEDS - 1U -
                               (pos % APP_HEARTBEAT_WS2812_CHAIN1_LEDS)),
                     colors[color_idx]);
#endif

    (void)ws2812_show_all(&g_ws2812);

    pos++;
    if (pos >= APP_HEARTBEAT_WS2812_CHAIN0_LEDS) {
        pos = 0;
        color_idx = (uint8_t)((color_idx + 1U) % 3U);
    }

    if (breathe_dir > 0) {
        if (breathe + APP_HEARTBEAT_WS2812_BREATHE_STEP < APP_HEARTBEAT_WS2812_BREATHE_MAX) {
            breathe = (uint8_t)(breathe + APP_HEARTBEAT_WS2812_BREATHE_STEP);
        } else {
            breathe = APP_HEARTBEAT_WS2812_BREATHE_MAX;
            breathe_dir = -1;
        }
    } else {
        if (breathe > APP_HEARTBEAT_WS2812_BREATHE_MIN + APP_HEARTBEAT_WS2812_BREATHE_STEP) {
            breathe = (uint8_t)(breathe - APP_HEARTBEAT_WS2812_BREATHE_STEP);
        } else {
            breathe = APP_HEARTBEAT_WS2812_BREATHE_MIN;
            breathe_dir = 1;
        }
    }
}
#endif

static void heartbeat_task_entry(void *arg)
{
    (void)arg;
    TickType_t next_log_tick = 0;

    for (;;) {
        TickType_t tick = xTaskGetTickCount();

#if APP_HEARTBEAT_WS2812_ENABLE
        led_chase_update();
#endif

        if (tick >= next_log_tick) {
            app_log_printf("[Heartbeat] tick=%lu\r\n", (unsigned long)tick);
            next_log_tick = tick + pdMS_TO_TICKS(APP_HEARTBEAT_PERIOD_MS);
        }

        vTaskDelay(pdMS_TO_TICKS(APP_HEARTBEAT_WS2812_STEP_MS));
    }
}

/* ========== 公开接口 ========== */

void task_heartbeat_init(void)
{
#if APP_HEARTBEAT_WS2812_ENABLE
    int ret;

    ret = ws2812_init(&g_ws2812);
    if (ret < 0) {
        app_log_printf("[Heartbeat][WS2812] init failed=%d\r\n", ret);
        return;
    }

    ret = ws2812_add_chain(&g_ws2812,
                           (void *)GPIO_BASE,
                           APP_HEARTBEAT_WS2812_CHAIN0_PIN,
                           APP_HEARTBEAT_WS2812_CHAIN0_LEDS,
                           g_chain0_buffer);
    if (ret < 0) {
        app_log_printf("[Heartbeat][WS2812] add chain0 failed=%d\r\n", ret);
        return;
    }

#if APP_HEARTBEAT_WS2812_CHAIN1_ENABLE
    ret = ws2812_add_chain(&g_ws2812,
                           (void *)GPIO_BASE,
                           APP_HEARTBEAT_WS2812_CHAIN1_PIN,
                           APP_HEARTBEAT_WS2812_CHAIN1_LEDS,
                           g_chain1_buffer);
    if (ret < 0) {
        app_log_printf("[Heartbeat][WS2812] add chain1 failed=%d\r\n", ret);
        return;
    }
#endif

    ws2812_set_brightness(&g_ws2812, APP_HEARTBEAT_WS2812_BRIGHTNESS);
    ws2812_clear(&g_ws2812, 0);
#if APP_HEARTBEAT_WS2812_CHAIN1_ENABLE
    ws2812_clear(&g_ws2812, 1);
#endif
    (void)ws2812_show_all(&g_ws2812);
    app_log_puts("[Heartbeat][WS2812] ready\r\n");
#endif
}

int task_heartbeat_start(void)
{
    BaseType_t ret;

    ret = xTaskCreate(heartbeat_task_entry,
                      "heartbeat",
                      APP_HEARTBEAT_TASK_STACK_WORDS,
                      NULL,
                      APP_HEARTBEAT_TASK_PRIORITY,
                      NULL);

    if (ret != pdPASS) {
        app_log_puts("[Heartbeat] task create failed\r\n");
        return -1;
    }

    return 0;
}
