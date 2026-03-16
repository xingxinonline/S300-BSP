#pragma once
/*
 * 模块：face_tracker（多目标检测画框 → v3 SORT 验证）
 * 作用：
 *  - 从 MAILBOX 读取 DSP 侧写入的 DetectionResult (v2.3 协议)；
 *  - 为每个 confirmed track 绘制彩色边界框 (track_id 映射不同颜色)；
 *  - miss_count=0 的真实检测全亮, >0 的 coast 预测半透明显示；
 *  - 输出详细 SORT 跟踪日志，验证 DSP 端多目标追踪实现。
 * 依赖：mailbox.h、video.h（获取屏幕宽高）、detection_proto.h。
 */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：传入毫秒节拍提供者（1ms），用于超时判断。 */
void face_tracker_init(uint32_t (*get_millis_fn)(void));

/* 轮询：读取邮箱、为所有 track 绘制彩色边框。 */
void face_tracker_poll(void);

/* 查询是否已经收到过至少一个有效检测结果。 */
bool face_tracker_has_seen_result(void);

#ifdef __cplusplus
}
#endif
