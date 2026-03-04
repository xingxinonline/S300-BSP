#ifndef TRACK_EVENTS_H
#define TRACK_EVENTS_H

/**
 * @file track_events.h
 * @brief 跟踪事件队列 - 任务间通信模块
 *
 * 设计原则:
 * - 低耦合: 生产者/消费者通过队列解耦
 * - 高内聚: 队列管理逻辑集中于此模块
 * - 线程安全: 内部使用 FreeRTOS Queue
 */

#include <stdbool.h>
#include <stdint.h>
#include "track_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 事件消息结构体 */
typedef struct {
    TrackEvent_t event;     /**< 事件类型 */
    uint32_t timestamp;     /**< 时间戳 (tick) */
} TrackEventMsg_t;

/**
 * @brief 初始化事件队列
 * @return 0 成功, <0 失败
 */
int track_events_init(void);

/**
 * @brief 发送事件到队列 (任意任务/ISR 可调用)
 * @param evt 事件类型
 * @return true 成功, false 队列满
 */
bool track_events_post(TrackEvent_t evt);

/**
 * @brief 从 ISR 发送事件
 * @param evt 事件类型
 * @return true 成功, false 队列满
 */
bool track_events_post_from_isr(TrackEvent_t evt);

/**
 * @brief 等待并获取事件 (阻塞)
 * @param msg 输出事件消息
 * @param timeout_ms 超时时间 (0=永久等待)
 * @return true 获取成功, false 超时
 */
bool track_events_wait(TrackEventMsg_t *msg, uint32_t timeout_ms);

/**
 * @brief 查询队列中待处理事件数量
 * @return 待处理事件数
 */
uint32_t track_events_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_EVENTS_H */
