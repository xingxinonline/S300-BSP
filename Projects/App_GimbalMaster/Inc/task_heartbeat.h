#ifndef TASK_HEARTBEAT_H
#define TASK_HEARTBEAT_H

/**
 * @file task_heartbeat.h
 * @brief 心跳任务 (LED 指示 + 周期日志)
 *
 * 职责: WS2812 LED 动画 + 心跳日志输出
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化心跳模块 (LED 硬件)
 */
void task_heartbeat_init(void);

/**
 * @brief 启动心跳任务
 * @return 0 成功, <0 失败
 */
int task_heartbeat_start(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_HEARTBEAT_H */
