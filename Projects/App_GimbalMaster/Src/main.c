#include <stdio.h>
#include <stdbool.h>

#include "board.h"
#include "rcc.h"
#include "app_config.h"
#include "app_log.h"
#include "ws2812.h"

#include "FreeRTOS.h"
#include "task.h"

static ws2812_handle_t g_ws2812;
static ws2812_color_t g_chain0_buffer[APP_HEARTBEAT_WS2812_CHAIN0_LEDS];
#if APP_HEARTBEAT_WS2812_CHAIN1_ENABLE
static ws2812_color_t g_chain1_buffer[APP_HEARTBEAT_WS2812_CHAIN1_LEDS];
#endif

static void heartbeat_led_init(void)
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

static void heartbeat_led_chase_update(void)
{
#if APP_HEARTBEAT_WS2812_ENABLE
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
                     (uint8_t)(APP_HEARTBEAT_WS2812_CHAIN1_LEDS - 1U - (pos % APP_HEARTBEAT_WS2812_CHAIN1_LEDS)),
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
#else
    return;
#endif
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    TickType_t next_log_tick = 0;

    for (;;) {
        TickType_t tick = xTaskGetTickCount();

        heartbeat_led_chase_update();

        if (tick >= next_log_tick) {
            app_log_printf("[Heartbeat] tick=%lu\r\n", (unsigned long)tick);
            next_log_tick = tick + pdMS_TO_TICKS(APP_HEARTBEAT_PERIOD_MS);
        }

        vTaskDelay(pdMS_TO_TICKS(APP_HEARTBEAT_WS2812_STEP_MS));
    }
}

void vAssertCalled(const char *file, int line)
{
    app_log_printf("assert: %s:%d\r\n", file, line);
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

void vApplicationMallocFailedHook(void)
{
    app_log_puts("Malloc failed!\r\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    app_log_printf("Stack overflow: %s\r\n", task_name);
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

int main(void)
{
    BaseType_t create_ret;

    board_init();
    app_log_init(BOARD_UART_DEBUG_IDX);
    heartbeat_led_init();

    SystemCoreClockUpdate();
    app_log_puts("\r\nS300 Gimbal Master MVP0 start\r\n");
    app_log_printf("Clock: SYS=%lu Hz, APB0=%lu Hz, APB1=%lu Hz\r\n",
                   (unsigned long)rcc_get_clock(RCC_CLOCK_SYSTEM),
                   (unsigned long)rcc_get_clock(RCC_CLOCK_APB0),
                   (unsigned long)rcc_get_clock(RCC_CLOCK_APB1));

    create_ret = xTaskCreate(heartbeat_task,
                             "heartbeat",
                             APP_HEARTBEAT_TASK_STACK_WORDS,
                             NULL,
                             APP_HEARTBEAT_TASK_PRIORITY,
                             NULL);
    if (create_ret != pdPASS) {
        app_log_puts("xTaskCreate heartbeat failed\r\n");
        for (;;) {}
    }

    app_log_puts("FreeRTOS scheduler starting...\r\n");

    vTaskStartScheduler();

    app_log_puts("vTaskStartScheduler returned unexpectedly\r\n");

    for (;;) {}
}
