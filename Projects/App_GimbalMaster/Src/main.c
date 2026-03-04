#include <stdio.h>
#include <stdbool.h>

#include "board.h"
#include "gpio.h"
#include "rcc.h"
#include "app_config.h"
#include "app_log.h"

#include "FreeRTOS.h"
#include "task.h"

static void heartbeat_gpio_init(void)
{
#if APP_HEARTBEAT_GPIO_ENABLE
    set_gpio_function(APP_HEARTBEAT_GPIO_PORT, APP_HEARTBEAT_GPIO_PIN, APP_HEARTBEAT_GPIO_FUNCTION);
    set_gpio_mode(APP_HEARTBEAT_GPIO_PORT, APP_HEARTBEAT_GPIO_PIN, GPIO_DOWN);
    set_gpio_direction(APP_HEARTBEAT_GPIO_PORT, APP_HEARTBEAT_GPIO_PIN, 1);
    set_gpio_data(APP_HEARTBEAT_GPIO_PORT, APP_HEARTBEAT_GPIO_PIN, 0);
#endif
}

static void heartbeat_gpio_toggle(bool level)
{
#if APP_HEARTBEAT_GPIO_ENABLE
    set_gpio_data(APP_HEARTBEAT_GPIO_PORT, APP_HEARTBEAT_GPIO_PIN, level ? 1 : 0);
#else
    (void)level;
#endif
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    bool led_on = false;

    for (;;) {
        TickType_t tick = xTaskGetTickCount();
        led_on = !led_on;
        heartbeat_gpio_toggle(led_on);

        app_log_printf("[Heartbeat] tick=%lu\r\n", (unsigned long)tick);
        vTaskDelay(pdMS_TO_TICKS(APP_HEARTBEAT_PERIOD_MS));
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
    heartbeat_gpio_init();

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
