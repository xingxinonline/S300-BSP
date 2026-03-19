/**
 * @file detection_proto.h
 * @brief 检测/追踪算法的邮箱消息 payload 定义
 * 
 * 本文件定义了 M4 和 DSP 之间传递检测结果的具体数据结构。
 * 该文件应同时被 M4 侧和 DSP 侧代码引用，确保双方数据结构一致。
 * 
 * 适用算法：
 *   - Face_Detection (人脸检测)
 *   - Human_Detection (人体检测)
 *   - Hand_Gesture (手势检测)
 * 
 * 协议流程：
 *   1. DSP 完成一帧检测后，将 DetectionResult 写入共享内存
 *   2. DSP 通过 Mailbox 发送 MAILBOX_MSG_TYPE_MULTI | offset
 *   3. M4 读取 Mailbox，提取 offset，从共享内存解析 DetectionResult
 *   4. M4 根据 result.count 遍历所有检测目标
 * 
 * 版本: 3.2 (新增手势细分类 PALM/PEACE)
 * 
 * @see mailbox_proto.h 通用邮箱消息格式
 */
#ifndef DETECTION_PROTO_H
#define DETECTION_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include "mailbox_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 协议版本与配置
 *===========================================================================*/

/** 协议版本号，M4/DSP 握手时可用于版本校验 */
#define DETECTION_PROTOCOL_VERSION_V300    0x0300u
#define DETECTION_PROTOCOL_VERSION_V301    0x0301u
#define DETECTION_PROTOCOL_VERSION_V302    0x0302u
#define DETECTION_PROTOCOL_VERSION         DETECTION_PROTOCOL_VERSION_V302  /* v3.2 — gesture type扩展 (PALM/PEACE) */

/** M4 访问 DSP PTCM 的地址基偏移（与 DSP 协议头保持一致） */
#define DSP_PTCM_M4_BASE_OFFSET            0x44800000u

/** 最大支持的检测目标数量 */
#define MAX_DETECTION_COUNT           10

/** 魔数，用于校验数据有效性 */
#define DETECTION_RESULT_MAGIC        0x44455446u  /* "DETF" in ASCII */

/*===========================================================================
 * 速度箭头渲染配置
 *===========================================================================*/

#define ARROW_SHOW_THRESHOLD  2     /**< 箭头显示阈值（speed > 此值时显示）*/
#define ARROW_HIDE_THRESHOLD  1     /**< 箭头隐藏阈值（speed < 此值时隐藏）*/
#define ARROW_SCALE           8     /**< 速度到箭头长度的比例因子 */
#define ARROW_HEAD_RATIO      0.35f /**< 箭头头部占总长度的比例 */
#define ARROW_WIDTH_BASE      2     /**< 基础线宽 */
#define ARROW_WIDTH_SCALE     0.1f  /**< 线宽随速度的缩放系数 */

/*===========================================================================
 * 检测目标类型定义
 *===========================================================================*/

/** 检测目标类型枚举 */
typedef enum {
    DETECTION_TYPE_UNKNOWN  = 0,    /**< 未知类型 */
    DETECTION_TYPE_FACE     = 1,    /**< 人脸检测 */
    DETECTION_TYPE_HUMAN    = 2,    /**< 人体检测（兼容旧命名） */
    DETECTION_TYPE_PERSON   = 2,    /**< 人体检测（新命名） */
    DETECTION_TYPE_HAND     = 3,    /**< 手势检测（兼容旧命名） */
    DETECTION_TYPE_GESTURE  = 3,    /**< 手势检测（新命名） */
    DETECTION_TYPE_OBJECT   = 4,    /**< 通用物体检测 */
    DETECTION_TYPE_PALM     = 5,    /**< 手势：掌心 */
    DETECTION_TYPE_PEACE    = 6,    /**< 手势：比耶 */
} DetectionType_t;

/** DSP 跟踪器状态（v3.x） */
typedef enum {
    TRACKER_STATE_DISABLED  = 0,  /**< 跟踪器禁用 */
    TRACKER_STATE_IDLE      = 1,  /**< 空闲 */
    TRACKER_STATE_TENTATIVE = 2,  /**< 试探确认中 */
    TRACKER_STATE_TRACKING  = 3,  /**< 跟踪中 */
    TRACKER_STATE_LOST      = 4,  /**< 丢失搜索中 */
} TrackerState_t;

/** 跟踪器标志位定义（v3.x） */
#define TRACKER_FLAG_ENABLED      0x01u
#define TRACKER_FLAG_REID_ACTIVE  0x02u
#define TRACKER_FLAG_COASTING     0x04u
#define TRACKER_FLAG_APPEAR_VALID 0x08u

/** DetectionBox.reserved 中编码的原始 tracker 状态（低 4 bit） */
#define DETECTION_TRACKER_RAW_DISABLED   0u
#define DETECTION_TRACKER_RAW_TRACKED    1u
#define DETECTION_TRACKER_RAW_PREDICTED  2u
#define DETECTION_TRACKER_RAW_LOST       3u

/** DetectionBox.reserved 中编码的原始 tracker 标志（高 4 bit） */
#define DETECTION_TRACKER_FLAG_FOLLOW_ACTIVE  (1u << 0)
#define DETECTION_TRACKER_FLAG_LOCKED         (1u << 1)
#define DETECTION_TRACKER_FLAG_LOW_SCORE      (1u << 2)
#define DETECTION_TRACKER_FLAG_EDGE_NEAR      (1u << 3)

#define DETECTION_TRACKER_META_STATE(meta)  ((uint8_t)((meta) & 0x0Fu))
#define DETECTION_TRACKER_META_FLAGS(meta)  ((uint8_t)(((meta) >> 4) & 0x0Fu))

/** 协议版本支持检查（兼容 v3.x：0x0300 ~ 0x03FF） */
#define DETECTION_PROTOCOL_IS_SUPPORTED(ver) \
    ((((uint32_t)(ver)) >> 8) == 0x03u)

/** 主版本号兼容检查（用于日志/灰度兼容） */
#define DETECTION_PROTOCOL_IS_SAME_MAJOR(ver) \
    ((((uint32_t)(ver)) >> 8) == (DETECTION_PROTOCOL_VERSION >> 8))

/** 将原始 type 值映射到标准枚举，未知值兜底为 UNKNOWN */
static inline DetectionType_t detection_type_from_raw(uint8_t raw_type)
{
    switch (raw_type) {
    case 1: return DETECTION_TYPE_FACE;
    case 2: return DETECTION_TYPE_PERSON;
    case 3: return DETECTION_TYPE_GESTURE;
    case 5: return DETECTION_TYPE_PALM;
    case 6: return DETECTION_TYPE_PEACE;
    default: return DETECTION_TYPE_UNKNOWN;
    }
}

/** 判断目标类型是否属于手势类 */
static inline bool detection_type_is_gesture(DetectionType_t type)
{
    return (type == DETECTION_TYPE_GESTURE ||
            type == DETECTION_TYPE_PALM ||
            type == DETECTION_TYPE_PEACE);
}

/** 类型名称字符串（用于日志/UI显示） */
static inline const char *detection_type_name(DetectionType_t type)
{
    switch (type) {
    case DETECTION_TYPE_FACE:    return "FACE";
    case DETECTION_TYPE_PERSON:  return "PERSON";
    case DETECTION_TYPE_GESTURE: return "GESTURE";
    case DETECTION_TYPE_PALM:    return "PALM";
    case DETECTION_TYPE_PEACE:   return "PEACE";
    default:                     return "UNKNOWN";
    }
}

/*===========================================================================
 * 单个检测目标结构
 *===========================================================================*/

/**
 * @brief 单个检测目标的边界框和属性
 * 
 * 内存布局（68 bytes，协议 v2.2）：
 *   Offset  0: float   score         (4 bytes)
 *   Offset  4: int32_t x1            (4 bytes)
 *   Offset  8: int32_t y1            (4 bytes)
 *   Offset 12: int32_t x2            (4 bytes)
 *   Offset 16: int32_t y2            (4 bytes)
 *   Offset 20: float   lm[10]        (40 bytes) - 5个关键点 (x,y)
 *   Offset 60: uint8_t type          (1 byte)   - DetectionType_t
 *   Offset 61: uint8_t track_id      (1 byte)   - 跟踪ID
 *   Offset 62: int8_t  vx            (1 byte)   - X方向速度 (像素/帧)
 *   Offset 63: int8_t  vy            (1 byte)   - Y方向速度 (像素/帧)
 *   Offset 64: uint8_t speed         (1 byte)   - 速度大小 (0-255)
 *   Offset 65: uint8_t kf_confidence (1 byte)   - 卡尔曼滤波置信度 (0-100)
 *   Offset 66: uint8_t miss_count    (1 byte)   - DSP侧连续漏检帧数 (0=真实检测, >0=Kalman coast)
 *   Offset 67: uint8_t reserved      (1 byte)   - 预留字段
 *   Total: 68 bytes
 */
typedef struct __attribute__((packed)) {
    float    score;           /**< 检测置信度 [0.0, 1.0] */
    int32_t  x1;              /**< 边界框左上角 X */
    int32_t  y1;              /**< 边界框左上角 Y */
    int32_t  x2;              /**< 边界框右下角 X */
    int32_t  y2;              /**< 边界框右下角 Y */
    float    lm[10];          /**< 5个关键点坐标 (x0,y0,x1,y1,...) */
    uint8_t  type;            /**< 检测类型 (DetectionType_t) */
    uint8_t  track_id;        /**< 跟踪ID（用于多帧关联，0表示未分配）*/
    int8_t   vx;              /**< X方向速度 (像素/帧, 卡尔曼输出) */
    int8_t   vy;              /**< Y方向速度 (像素/帧, 卡尔曼输出) */
    uint8_t  speed;           /**< 速度大小 [0-255]，用于箭头渲染 */
    uint8_t  kf_confidence;   /**< 卡尔曼滤波置信度 (0-100) */
    uint8_t  miss_count;      /**< DSP侧连续漏检帧数 (0=真实CNN检测, >0=Kalman coast预测) */
    uint8_t  reserved;        /**< 预留对齐 */
} DetectionBox_t;

/** 兼容旧版本的类型别名 */
typedef DetectionBox_t FaceRect;

/*===========================================================================
 * 多目标检测结果结构
 *===========================================================================*/

/**
 * @brief 一帧的完整检测结果
 * 
 * 内存布局（704 bytes）：
 *   Offset   0: uint32_t magic        (4 bytes)
 *   Offset   4: uint32_t version      (4 bytes)
 *   Offset   8: uint32_t frame_id     (4 bytes)
 *   Offset  12: uint16_t timestamp_ms (2 bytes)
 *   Offset  14: uint8_t  tracker_state(1 byte)
 *   Offset  15: uint8_t  tracker_flags(1 byte)
 *   Offset  16: uint32_t count        (4 bytes)
 *   Offset  20: int32_t  selected_idx (4 bytes)
 *   Offset  24: DetectionBox_t boxes[10] (68 * 10 = 680 bytes)
 *   Total: 704 bytes
 */
typedef struct __attribute__((packed)) {
    uint32_t       magic;         /**< 魔数 (DETECTION_RESULT_MAGIC) */
    uint32_t       version;       /**< 协议版本 (DETECTION_PROTOCOL_VERSION) */
    uint32_t       frame_id;      /**< 帧序号（DSP 递增）*/
    uint16_t       timestamp_ms;  /**< 时间戳低16位（毫秒）*/
    uint8_t        tracker_state; /**< 跟踪器状态 (TrackerState_t) */
    uint8_t        tracker_flags; /**< 跟踪器标志位 */
    uint32_t       count;         /**< 本帧检测到的目标数量 [0, MAX_DETECTION_COUNT] */
    int32_t        selected_idx;  /**< DSP选中的目标索引 [0,count-1]，-1表示无选中 */
    DetectionBox_t boxes[MAX_DETECTION_COUNT];  /**< 检测目标数组 */
} DetectionResult_t;

/*===========================================================================
 * M4 → DSP 控制命令（检测场景特定）
 *===========================================================================*/

/** 启动追踪 - DSP 开始输出检测结果 */
#define CMD_TYPE_START_TRACKING       MAILBOX_CMD_START

/** 停止追踪 - DSP 停止输出检测结果 */
#define CMD_TYPE_STOP_TRACKING        MAILBOX_CMD_STOP

/** 重置选中目标 - DSP 重新选择目标 */
#define CMD_TYPE_RESET_SELECTION      MAILBOX_CMD_RESET_SELECTION

/** 命令类型/参数提取宏 */
#define CMD_TYPE_MASK                 MAILBOX_MSG_TYPE_MASK
#define CMD_PARAM_MASK                MAILBOX_MSG_PAYLOAD_MASK
#define CMD_GET_TYPE(cmd)             MAILBOX_GET_MSG_TYPE(cmd)
#define CMD_GET_PARAM(cmd)            MAILBOX_GET_PAYLOAD(cmd)
#define CMD_MAKE(type, param)         MAILBOX_MAKE_MSG(type, param)

/*===========================================================================
 * 辅助宏
 *===========================================================================*/

/** 检查 DetectionResult 是否有效 */
#define DETECTION_RESULT_IS_VALID(pResult) \
    ((pResult)->magic == DETECTION_RESULT_MAGIC && \
    DETECTION_PROTOCOL_IS_SUPPORTED((pResult)->version) && \
     (pResult)->count <= MAX_DETECTION_COUNT)

/** 获取结构体大小 */
#define DETECTION_RESULT_SIZE         sizeof(DetectionResult_t)
#define DETECTION_BOX_SIZE            sizeof(DetectionBox_t)

/*===========================================================================
 * 兼容旧协议的宏定义
 *===========================================================================*/

/* 保持向后兼容 */
#define MAILBOX_MSG_TYPE_NO_DETECT    MAILBOX_MSG_TYPE_NO_RESULT

/* 与 DSP 协议头命名保持一致 */
#define GET_MSG_TYPE(msg)             MAILBOX_GET_MSG_TYPE(msg)
#define GET_MSG_PAYLOAD(msg)          MAILBOX_GET_PAYLOAD(msg)

/*===========================================================================
 * 编译时断言（确保与 DSP 端结构体大小一致）
 *===========================================================================*/

_Static_assert(sizeof(DetectionBox_t) == 68, "DetectionBox_t size mismatch");
_Static_assert(sizeof(DetectionResult_t) == 704, "DetectionResult_t size mismatch");

#ifdef __cplusplus
}
#endif

#endif /* DETECTION_PROTO_H */
