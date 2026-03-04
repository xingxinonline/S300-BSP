/**
 * @file task_state.c
 * @brief 状态机服务任务实现
 */

#include "task_state.h"
#include "track_state.h"
#include "track_events.h"
#include "app_log.h"
#include "app_config.h"

#include "FreeRTOS.h"
#include "task.h"

/* 任务配置 */
#ifndef TASK_STATE_STACK_WORDS
#define TASK_STATE_STACK_WORDS  256U
#endif

#ifndef TASK_STATE_PRIORITY
#define TASK_STATE_PRIORITY     (tskIDLE_PRIORITY + 3)
#endif

/* 私有变量 */
static TaskHandle_t g_state_task_handle = NULL;
static StateChangeCallback_t g_change_callback = NULL;
static PhotoRequestCallback_t g_photo_callback = NULL;
static RecordRequestCallback_t g_record_callback = NULL;

/**
 * @brief 处理状态转换结果
 */
static void handle_transition(const TrackTransition_t *trans)
{
    if (trans->is_photo) {
        app_log_printf("[STATE] PHOTO event in %s\r\n",
                       track_state_to_string(trans->prev_state));
        if (g_photo_callback != NULL) {
            g_photo_callback(trans->prev_state);
        }
        return;
    }

    if (trans->is_record_start || trans->is_record_stop) {
        app_log_printf("[STATE] %s event in %s\r\n",
                       trans->is_record_start ? "RECORD_START" : "RECORD_STOP",
                       track_state_to_string(trans->prev_state));
        if (g_record_callback != NULL) {
            g_record_callback(trans->prev_state, trans->is_record_start);
        }
        return;
    }

    if (trans->changed) {
        app_log_printf("[STATE] %s -> %s (evt=%s)\r\n",
                       track_state_to_string(trans->prev_state),
                       track_state_to_string(trans->next_state),
                       track_event_to_string(trans->event));
        if (g_change_callback != NULL) {
            g_change_callback(trans);
        }
    } else {
        app_log_printf("[STATE] %s (evt=%s ignored)\r\n",
                       track_state_to_string(trans->prev_state),
                       track_event_to_string(trans->event));
    }
}

/**
 * @brief 状态机服务任务入口
 */
static void state_task_entry(void *arg)
{
    TrackEventMsg_t msg;
    TrackTransition_t trans;

    (void)arg;

    app_log_printf("[STATE] service started, state=%s\r\n",
                   track_state_to_string(track_state_get()));

    for (;;) {
        /* 阻塞等待事件 */
        if (track_events_wait(&msg, 0)) {
            /* 临界区内处理状态机 (保证线程安全) */
            taskENTER_CRITICAL();
            trans = track_state_process(msg.event);
            taskEXIT_CRITICAL();

            /* 临界区外处理日志和回调 */
            handle_transition(&trans);
        }
    }
}

int task_state_init(void)
{
    int ret;

    /* 初始化事件队列 */
    ret = track_events_init();
    if (ret < 0) {
        app_log_puts("[STATE] event queue init failed\r\n");
        return ret;
    }

    /* 重置状态机 */
    track_state_reset();

    app_log_printf("[STATE] init -> %s\r\n", track_state_to_string(track_state_get()));
    return 0;
}

int task_state_start(void)
{
    BaseType_t ret;

    if (g_state_task_handle != NULL) {
        return 0; /* 已启动 */
    }

    ret = xTaskCreate(state_task_entry,
                      "state",
                      TASK_STATE_STACK_WORDS,
                      NULL,
                      TASK_STATE_PRIORITY,
                      &g_state_task_handle);

    if (ret != pdPASS) {
        app_log_puts("[STATE] task create failed\r\n");
        return -1;
    }

    return 0;
}

void task_state_set_change_callback(StateChangeCallback_t cb)
{
    g_change_callback = cb;
}

void task_state_set_photo_callback(PhotoRequestCallback_t cb)
{
    g_photo_callback = cb;
}

void task_state_set_record_callback(RecordRequestCallback_t cb)
{
    g_record_callback = cb;
}
