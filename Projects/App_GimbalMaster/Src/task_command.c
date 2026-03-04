/**
 * @file task_command.c
 * @brief 串口命令任务实现
 */

#include "task_command.h"
#include "track_state.h"
#include "track_events.h"
#include "app_config.h"
#include "app_log.h"
#include "board.h"
#include "uart.h"

#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

/* ========== 私有函数 ========== */

static inline S300_UART_TypeDef *dbg_uart_dev(void)
{
    switch (BOARD_UART_DEBUG_IDX) {
    case 0:
        return UART0;
    case 1:
        return UART1;
    case 2:
        return UART2;
    default:
        return UART3;
    }
}

static TrackEvent_t cmd_to_event(const char *cmd, bool *matched)
{
    static const struct {
        const char *name;
        TrackEvent_t event;
    } cmd_table[] = {
        { "start", TRACK_EVT_START },
        { "stop",  TRACK_EVT_STOP },
        { "found", TRACK_EVT_TARGET_FOUND },
        { "lost",  TRACK_EVT_TARGET_LOST },
        { "photo", TRACK_EVT_PHOTO },
    };

    for (size_t i = 0; i < sizeof(cmd_table) / sizeof(cmd_table[0]); i++) {
        if (strcmp(cmd, cmd_table[i].name) == 0) {
            *matched = true;
            return cmd_table[i].event;
        }
    }

    *matched = false;
    return TRACK_EVT_STOP;
}

static void process_line(const char *line)
{
    bool matched = false;

    if (line == NULL || line[0] == '\0') {
        return;
    }

    if (strcmp(line, "state") == 0) {
        app_log_printf("[STATE] current=%s\r\n",
                       track_state_to_string(track_state_get()));
        return;
    }

    if (strcmp(line, "help") == 0) {
        app_log_puts("[CMD] start|stop|found|lost|photo|state|help\r\n");
        return;
    }

    TrackEvent_t evt = cmd_to_event(line, &matched);
    if (matched) {
        if (!track_events_post(evt)) {
            app_log_puts("[CMD] event queue full\r\n");
        }
        return;
    }

    app_log_printf("[CMD] unknown: %s\r\n", line);
}

static void command_task_entry(void *arg)
{
    S300_UART_TypeDef *uart = dbg_uart_dev();
    char line_buf[APP_CMD_BUFFER_LEN];
    size_t line_len = 0;

    (void)arg;

    app_log_puts("[CMD] ready, type 'help'\r\n");

    for (;;) {
        while (uart->USR & 0x8u) {
            uint8_t ch = (uint8_t)uart->RBR_THR_DLL;
            app_log_putc((char)ch);

            if (ch == '\r' || ch == '\n') {
                if (line_len > 0) {
                    line_buf[line_len] = '\0';
                    process_line(line_buf);
                    line_len = 0;
                }
                if (ch == '\r') {
                    app_log_putc('\n');
                }
                continue;
            }

            if (ch == 0x08u || ch == 0x7Fu) {
                if (line_len > 0) {
                    line_len--;
                }
                continue;
            }

            if (line_len + 1U < sizeof(line_buf)) {
                line_buf[line_len++] = (char)ch;
            } else {
                line_len = 0;
                app_log_puts("\r\n[CMD] line too long, reset\r\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(APP_CMD_POLL_PERIOD_MS));
    }
}

/* ========== 公开接口 ========== */

int task_command_start(void)
{
    BaseType_t ret;

    ret = xTaskCreate(command_task_entry,
                      "cmd",
                      APP_CMD_TASK_STACK_WORDS,
                      NULL,
                      APP_CMD_TASK_PRIORITY,
                      NULL);

    if (ret != pdPASS) {
        app_log_puts("[CMD] task create failed\r\n");
        return -1;
    }

    return 0;
}
