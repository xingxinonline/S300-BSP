#ifndef TASK_KWS_H
#define TASK_KWS_H

/**
 * @file task_kws.h
 * @brief KWS 任务（DSP Mailbox 轮询）
 *
 * 职责:
 * - 初始化 DSP PLL 与 Mailbox
 * - 轮询 Mailbox 的 KWS 结果
 * - 将关键词映射为跟踪事件并投递到事件队列
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 KWS 子系统
 * @return 0 成功, <0 失败
 */
int task_kws_init(void);

/**
 * @brief 启动 KWS 任务
 * @return 0 成功, <0 失败
 */
int task_kws_start(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_KWS_H */
