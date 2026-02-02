#pragma once
/**
 * @file face_tracker.h
 * @brief 单人追踪模块（多目标检测 + 单目标追踪）
 * 
 * 应用场景：
 *  - 画面中可能出现多人，但只追踪其中 1 人
 *  - 自动选择最靠近画面中心的目标
 *  - 基于 track_id 维持追踪，避免框跳变
 * 
 * 显示效果：
 *  - 选中目标（被追踪）：绿框
 *  - 其他目标：蓝框
 * 
 * 协议：
 *  - 使用 detection_protocol.h 定义的数据结构
 *  - 支持 v1.0（单目标）和 v2.0（多目标）两种协议
 * 
 * 依赖：mailbox.h、video.h、detection_protocol.h
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 配置参数
 *===========================================================================*/

/** 检测超时时间（毫秒），超时后清除所有框 */
#define FACE_TRACKER_TIMEOUT_MS       200

/*===========================================================================
 * 统计信息结构
 *===========================================================================*/

/** 跟踪器运行时统计 */
typedef struct {
    uint32_t frame_count;       /**< 已处理的帧数 */
    uint32_t last_frame_id;     /**< 最后一帧的 ID */
    uint32_t current_count;     /**< 当前跟踪的目标数量 */
    uint32_t max_count;         /**< 历史最大目标数量 */
    uint32_t timeout_count;     /**< 超时清除次数 */
} FaceTrackerStats_t;

/*===========================================================================
 * API 函数
 *===========================================================================*/

/**
 * @brief 初始化跟踪器
 * @param get_millis_fn 毫秒节拍获取函数指针（用于超时计算）
 */
void face_tracker_init(uint32_t (*get_millis_fn)(void));

/**
 * @brief 轮询处理（在主循环中调用）
 * 
 * 从 Mailbox 读取检测结果，更新显示 Overlay 上的边界框。
 * 自动兼容单目标和多目标协议。
 * 选中目标（最靠近屏幕中心）画绿框，其他画蓝框。
 */
void face_tracker_poll(void);

/**
 * @brief 获取当前跟踪的目标数量
 * @return 当前显示的目标边界框数量
 */
uint32_t face_tracker_get_count(void);

/**
 * @brief 获取选中目标的索引
 * @return 选中目标的索引（0-based），-1 表示无选中目标
 */
int32_t face_tracker_get_selected_index(void);

/**
 * @brief 获取选中目标的边界框
 * @param x1, y1, x2, y2 输出边界框坐标
 * @return true 如果有选中目标，false 如果无
 */
bool face_tracker_get_selected_box(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2);

/**
 * @brief 获取跟踪器统计信息
 * @param stats 输出统计信息
 */
void face_tracker_get_stats(FaceTrackerStats_t *stats);

/**
 * @brief 清除所有显示的边界框
 */
void face_tracker_clear_all(void);

/*===========================================================================
 * 追踪控制 API（预留接口，后续扩展）
 *===========================================================================*/

/**
 * @brief 启动追踪
 * 
 * 调用后开始处理检测结果并显示边界框。
 * 默认状态为启动。
 * 
 * @note 预留接口，当前默认始终启动
 */
void face_tracker_start(void);

/**
 * @brief 停止追踪
 * 
 * 调用后停止处理检测结果，清除所有边界框。
 * Mailbox 消息仍会被读取但不会处理。
 * 
 * @note 预留接口，当前为空实现
 */
void face_tracker_stop(void);

/**
 * @brief 检查追踪是否正在运行
 * @return true 正在追踪，false 已停止
 * 
 * @note 预留接口，当前始终返回 true
 */
bool face_tracker_is_running(void);

#ifdef __cplusplus
}
#endif
