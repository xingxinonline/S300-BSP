/**
 * @file main.c
 * @brief Gimbal Master 主入口
 *
 * 职责: 仅负责系统初始化和任务启动，业务逻辑在各任务模块中实现
 */

#include "board.h"
#include "rcc.h"
#include "app_config.h"
#include "app_log.h"
#include "task_state.h"
#include "task_command.h"
#include "task_heartbeat.h"
#include "task_kws.h"

#include "FreeRTOS.h"
#include "task.h"

static void on_photo_request(TrackState_t current_state)
{
    (void)current_state;
    task_heartbeat_notify_photo_event();
}

static void on_record_request(TrackState_t current_state, bool start)
{
    (void)current_state;
    task_heartbeat_set_recording(start);
}

/* ========== FreeRTOS Hooks ========== */

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

/* ========== Main ========== */

int main(void)
{
    /* 硬件初始化 */
    board_init();
    app_log_init(BOARD_UART_DEBUG_IDX);
    task_heartbeat_init();
    SystemCoreClockUpdate();

    /* 状态机服务初始化 */
    if (task_state_init() < 0) {
        app_log_puts("task_state_init failed\r\n");
        for (;;) {}
    }

    task_state_set_photo_callback(on_photo_request);
    task_state_set_record_callback(on_record_request);

    if (task_kws_init() < 0) {
        app_log_puts("task_kws_init failed\r\n");
        for (;;) {}
    }

    /* 启动信息 */
    app_log_puts("\r\nS300 Gimbal Master MVP0 start\r\n");
    app_log_printf("Clock: SYS=%lu Hz, APB0=%lu Hz, APB1=%lu Hz\r\n",
                   (unsigned long)rcc_get_clock(RCC_CLOCK_SYSTEM),
                   (unsigned long)rcc_get_clock(RCC_CLOCK_APB0),
                   (unsigned long)rcc_get_clock(RCC_CLOCK_APB1));

    /* 启动任务 */
    if (task_heartbeat_start() < 0) {
        app_log_puts("task_heartbeat_start failed\r\n");
        for (;;) {}
    }

    if (task_command_start() < 0) {
        app_log_puts("task_command_start failed\r\n");
        for (;;) {}
    }

    if (task_state_start() < 0) {
        app_log_puts("task_state_start failed\r\n");
        for (;;) {}
    }

    if (task_kws_start() < 0) {
        app_log_puts("task_kws_start failed\r\n");
        for (;;) {}
    }

    /* 启动调度器 */
    app_log_puts("FreeRTOS scheduler starting...\r\n");
    vTaskStartScheduler();

    app_log_puts("vTaskStartScheduler returned unexpectedly\r\n");
    for (;;) {}
}
