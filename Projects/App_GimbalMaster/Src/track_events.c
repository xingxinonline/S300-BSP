/**
 * @file track_events.c
 * @brief 跟踪事件队列实现
 */

#include "track_events.h"
#include "FreeRTOS.h"
#include "queue.h"

/* 队列配置 */
#define EVENT_QUEUE_LENGTH  8

/* 私有队列句柄 */
static QueueHandle_t g_event_queue = NULL;

int track_events_init(void)
{
    if (g_event_queue != NULL) {
        return 0; /* 已初始化 */
    }

    g_event_queue = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(TrackEventMsg_t));
    if (g_event_queue == NULL) {
        return -1;
    }

    return 0;
}

bool track_events_post(TrackEvent_t evt)
{
    TrackEventMsg_t msg;

    if (g_event_queue == NULL) {
        return false;
    }

    msg.event = evt;
    msg.timestamp = xTaskGetTickCount();

    return (xQueueSend(g_event_queue, &msg, 0) == pdTRUE);
}

bool track_events_post_from_isr(TrackEvent_t evt)
{
    TrackEventMsg_t msg;
    BaseType_t higher_priority_woken = pdFALSE;

    if (g_event_queue == NULL) {
        return false;
    }

    msg.event = evt;
    msg.timestamp = xTaskGetTickCountFromISR();

    if (xQueueSendFromISR(g_event_queue, &msg, &higher_priority_woken) != pdTRUE) {
        return false;
    }

    portYIELD_FROM_ISR(higher_priority_woken);
    return true;
}

bool track_events_wait(TrackEventMsg_t *msg, uint32_t timeout_ms)
{
    TickType_t ticks;

    if (g_event_queue == NULL || msg == NULL) {
        return false;
    }

    if (timeout_ms == 0) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS(timeout_ms);
    }

    return (xQueueReceive(g_event_queue, msg, ticks) == pdTRUE);
}

uint32_t track_events_pending(void)
{
    if (g_event_queue == NULL) {
        return 0;
    }

    return (uint32_t)uxQueueMessagesWaiting(g_event_queue);
}
