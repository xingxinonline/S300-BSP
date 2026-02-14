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
 * 版本: 2.3 (SORT multi-target tracking, miss_count field)
 * 
 * @see mailbox_proto.h 通用邮箱消息格式
 */
#ifndef DETECTION_PROTO_H
#define DETECTION_PROTO_H

#include <stdint.h>
#include "mailbox_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 协议版本与配置
 *===========================================================================*/

/** 协议版本号，M4/DSP 握手时可用于版本校验 */
#define DETECTION_PROTOCOL_VERSION    0x0300u  /* v3.0 — miss_count + CM4↔DSP双向通信 */

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
    DETECTION_TYPE_HUMAN    = 2,    /**< 人体检测 */
    DETECTION_TYPE_HAND     = 3,    /**< 手势检测 */
    DETECTION_TYPE_OBJECT   = 4,    /**< 通用物体检测 */
} DetectionType_t;

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
 *   Offset  12: uint32_t timestamp    (4 bytes)
 *   Offset  16: uint32_t count        (4 bytes)
 *   Offset  20: int32_t  selected_idx (4 bytes)
 *   Offset  24: DetectionBox_t boxes[10] (68 * 10 = 680 bytes)
 *   Total: 704 bytes
 */
typedef struct __attribute__((packed)) {
    uint32_t       magic;         /**< 魔数 (DETECTION_RESULT_MAGIC) */
    uint32_t       version;       /**< 协议版本 (DETECTION_PROTOCOL_VERSION) */
    uint32_t       frame_id;      /**< 帧序号（DSP 递增）*/
    uint32_t       timestamp;     /**< 时间戳（毫秒，可选）*/
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
     (pResult)->count <= MAX_DETECTION_COUNT)

/** 获取结构体大小 */
#define DETECTION_RESULT_SIZE         sizeof(DetectionResult_t)
#define DETECTION_BOX_SIZE            sizeof(DetectionBox_t)

/*===========================================================================
 * 兼容旧协议的宏定义
 *===========================================================================*/

/* 保持向后兼容 */
#define MAILBOX_MSG_TYPE_NO_DETECT    MAILBOX_MSG_TYPE_NO_RESULT

/*===========================================================================
 * 编译时断言（确保与 DSP 端结构体大小一致）
 *===========================================================================*/

_Static_assert(sizeof(DetectionBox_t) == 68, "DetectionBox_t size mismatch");
_Static_assert(sizeof(DetectionResult_t) == 704, "DetectionResult_t size mismatch");

#ifdef __cplusplus
}
#endif

#endif /* DETECTION_PROTO_H */
