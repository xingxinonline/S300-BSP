/**
 * @file task_heartbeat.c
 * @brief 心跳任务实现
 */

#include "task_heartbeat.h"
#include "app_config.h"
#include "app_log.h"
#include "board.h"
#include "ws2812.h"
#include "track_state.h"
#include "display_overlay.h"

#include "FreeRTOS.h"
#include "task.h"

/* ========== 私有变量 ========== */

#if APP_HEARTBEAT_WS2812_ENABLE
static ws2812_handle_t g_ws2812;
static ws2812_color_t g_chain0_buffer[APP_HEARTBEAT_WS2812_CHAIN0_LEDS];
#if APP_HEARTBEAT_WS2812_CHAIN1_ENABLE
static ws2812_color_t g_chain1_buffer[APP_HEARTBEAT_WS2812_CHAIN1_LEDS];
#endif
static volatile uint8_t g_photo_flash_ticks;
static volatile uint8_t g_recording_active;
#endif

/* ========== 私有函数 ========== */

#if APP_HEARTBEAT_WS2812_ENABLE
static void led_status_update(void)
{
    static uint8_t breathe = APP_HEARTBEAT_WS2812_BREATHE_MIN;
    static int8_t breathe_dir = 1;
    static uint8_t search_phase = 0;
    static uint8_t recording_phase = 0;
    static uint8_t tracking_phase = 0;
    ws2812_color_t color = WS2812_COLOR_BLUE;
    uint8_t brightness = breathe;
    TrackState_t state = track_state_get();
    uint8_t photo_flash_ticks = g_photo_flash_ticks;
    uint8_t use_tracking_marquee = 0u;

    if (photo_flash_ticks > 0u) {
        g_photo_flash_ticks = (uint8_t)(photo_flash_ticks - 1u);
        color = WS2812_COLOR_WHITE;
        brightness = APP_HEARTBEAT_WS2812_PHOTO_FLASH_BRIGHTNESS;
    } else if (g_recording_active != 0u) {
        color = WS2812_COLOR_RED;
        recording_phase = (uint8_t)((recording_phase + 1u) & 0x0Fu);
        if ((recording_phase <= 1u) || (recording_phase >= 4u && recording_phase <= 5u)) {
            brightness = APP_HEARTBEAT_WS2812_RECORDING_BRIGHTNESS_HIGH;
        } else {
            brightness = APP_HEARTBEAT_WS2812_RECORDING_BRIGHTNESS_LOW;
        }
    } else {
        switch (state) {
        case TRACK_STATE_IDLE:
            color = WS2812_COLOR_BLUE;
            brightness = breathe;
            break;

        case TRACK_STATE_TRACKING:
            color = WS2812_COLOR_GREEN;
            brightness = APP_HEARTBEAT_WS2812_BRIGHTNESS;
            use_tracking_marquee = 1u;
            break;

        case TRACK_STATE_LOCK:
            color = WS2812_COLOR_YELLOW;
            brightness = APP_HEARTBEAT_WS2812_BRIGHTNESS;
            break;

        case TRACK_STATE_SEARCH:
            color = WS2812_COLOR_RED;
            search_phase = (uint8_t)((search_phase + 1U) & 0x01U);
            brightness = search_phase ? APP_HEARTBEAT_WS2812_BRIGHTNESS : APP_HEARTBEAT_WS2812_BREATHE_MIN;
            break;

        default:
            color = WS2812_COLOR_BLUE;
            brightness = APP_HEARTBEAT_WS2812_BRIGHTNESS;
            break;
        }
    }

    ws2812_set_brightness(&g_ws2812, brightness);
    ws2812_clear(&g_ws2812, 0);

    if (use_tracking_marquee) {
        uint8_t head0 = (uint8_t)(tracking_phase % APP_HEARTBEAT_WS2812_CHAIN0_LEDS);
        uint8_t tail0 = (uint8_t)((head0 + APP_HEARTBEAT_WS2812_CHAIN0_LEDS - 1u) % APP_HEARTBEAT_WS2812_CHAIN0_LEDS);
        uint8_t tail20 = (uint8_t)((head0 + APP_HEARTBEAT_WS2812_CHAIN0_LEDS - 2u) % APP_HEARTBEAT_WS2812_CHAIN0_LEDS);
        ws2812_set_pixel(&g_ws2812, 0, head0, WS2812_COLOR_GREEN);
        ws2812_set_pixel(&g_ws2812, 0, tail0, ws2812_rgb(0, 96, 0));
        ws2812_set_pixel(&g_ws2812, 0, tail20, ws2812_rgb(0, 36, 0));
    } else {
        for (uint8_t i = 0; i < APP_HEARTBEAT_WS2812_CHAIN0_LEDS; i++) {
            ws2812_set_pixel(&g_ws2812, 0, i, color);
        }
    }

#if APP_HEARTBEAT_WS2812_CHAIN1_ENABLE
    ws2812_clear(&g_ws2812, 1);
    if (use_tracking_marquee) {
        uint8_t phase1 = (uint8_t)(tracking_phase % APP_HEARTBEAT_WS2812_CHAIN1_LEDS);
        uint8_t head1 = (uint8_t)((APP_HEARTBEAT_WS2812_CHAIN1_LEDS - 1u - phase1) % APP_HEARTBEAT_WS2812_CHAIN1_LEDS);
        uint8_t tail1 = (uint8_t)((head1 + 1u) % APP_HEARTBEAT_WS2812_CHAIN1_LEDS);
        uint8_t tail21 = (uint8_t)((head1 + 2u) % APP_HEARTBEAT_WS2812_CHAIN1_LEDS);
        ws2812_set_pixel(&g_ws2812, 1, head1, WS2812_COLOR_GREEN);
        ws2812_set_pixel(&g_ws2812, 1, tail1, ws2812_rgb(0, 96, 0));
        ws2812_set_pixel(&g_ws2812, 1, tail21, ws2812_rgb(0, 36, 0));
    } else {
        for (uint8_t i = 0; i < APP_HEARTBEAT_WS2812_CHAIN1_LEDS; i++) {
            ws2812_set_pixel(&g_ws2812, 1, i, color);
        }
    }
#endif

    (void)ws2812_show_all(&g_ws2812);

    if (use_tracking_marquee) {
        tracking_phase++;
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
    TickType_t last_overlay_tick = 0;

    for (;;) {
#if APP_HEARTBEAT_WS2812_ENABLE
        led_status_update();
#endif

        if ((xTaskGetTickCount() - last_overlay_tick) >= pdMS_TO_TICKS(500)) {
            display_overlay_render_debug();
            last_overlay_tick = xTaskGetTickCount();
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

void task_heartbeat_notify_photo_event(void)
{
#if APP_HEARTBEAT_WS2812_ENABLE
    g_photo_flash_ticks = APP_HEARTBEAT_WS2812_PHOTO_FLASH_TICKS;
#endif
}

void task_heartbeat_set_recording(bool enable)
{
#if APP_HEARTBEAT_WS2812_ENABLE
    g_recording_active = enable ? 1u : 0u;
#else
    (void)enable;
#endif
}
