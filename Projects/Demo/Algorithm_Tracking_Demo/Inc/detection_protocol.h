/**
 * @file detection_protocol.h
 * @brief M4 与 DSP 共享的检测结果协议定义
 * 
 * 本文件定义了 M4 和 DSP 之间通过共享内存和 Mailbox 传递检测结果的数据结构。
 * 该文件应同时被 M4 侧和 DSP 侧代码引用，确保双方数据结构一致。
 * 
 * 协议流程：
 *   1. DSP 完成一帧检测后，将 DetectionResult 写入共享内存（DSP_DETECTION_BASE_ADDR + offset）
 *   2. DSP 通过 Mailbox 向 M4 发送 offset（相对 DSP_DETECTION_BASE_ADDR 的偏移）
 *   3. M4 读取 Mailbox 获取 offset，从共享内存解析 DetectionResult
 *   4. M4 根据 result.count 遍历所有检测目标
 * 
 * 版本: 2.0 (多目标检测支持)
 */
#ifndef DETECTION_PROTOCOL_H
#define DETECTION_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 协议版本与配置
 *===========================================================================*/

/** 协议版本号，M4/DSP 握手时可用于版本校验 */
#define DETECTION_PROTOCOL_VERSION    0x0202u  /* v2.2 (Kalman filter support) */

/** 最大支持的检测目标数量 */
#define MAX_DETECTION_COUNT           10

/*===========================================================================
 * 速度箭头配置
 *===========================================================================*/

#define ARROW_SHOW_THRESHOLD  25    /**< 箭头显示阈值（speed > 此值时显示）*/
#define ARROW_HIDE_THRESHOLD  8     /**< 箭头隐藏阈值（speed < 此值时隐藏）*/
#define ARROW_SCALE           8     /**< 速度到箭头长度的比例因子 */
#define ARROW_HEAD_RATIO      0.35f /**< 箭头头部占总长度的比例 */
#define ARROW_WIDTH_BASE      2     /**< 基础线宽 */
#define ARROW_WIDTH_SCALE     0.1f  /**< 线宽随速度的缩放系数 */

/** 共享内存基地址（DSP 写入，M4 读取）*/
#ifndef DSP_DETECTION_BASE_ADDR
#define DSP_DETECTION_BASE_ADDR       0x44800000u
#endif

/** 兼容旧版本的宏定义 */
#define DSP_FACE_BASE_ADDR            DSP_DETECTION_BASE_ADDR

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
 * 单个检测目标结构（兼容原 FaceRect）
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
 *   Offset 66: uint8_t reserved[2]   (2 bytes)  - 预留字段
 *   Total: 68 bytes
 */
typedef struct __attribute__((packed)) {
    float    score;           /**< 检测置信度 [0.0, 1.0] */
    int32_t  x1;              /**< 边界框左上角 X */
    int32_t  y1;              /**< 边界框左上角 Y */
    int32_t  x2;              /**< 边界框右下角 X */
    int32_t  y2;              /**< 边界框右下角 Y */
    float    lm[10];          /**< 5个关键点坐标 */
    uint8_t  type;            /**< 检测类型 (DetectionType_t) */
    uint8_t  track_id;        /**< 跟踪ID（用于多帧关联，0表示未分配）*/
    int8_t   vx;              /**< X方向速度 (像素/帧, 卡尔曼输出) */
    int8_t   vy;              /**< Y方向速度 (像素/帧, 卡尔曼输出) */
    uint8_t  speed;           /**< 速度大小 [0-255]，用于箭头渲染 */
    uint8_t  kf_confidence;   /**< 卡尔曼滤波置信度 (0-100) */
    uint8_t  reserved[2];     /**< 预留字段 */
} DetectionBox_t;

/** 兼容旧版本的类型别名 */
typedef DetectionBox_t FaceRect;

/*===========================================================================
 * 多目标检测结果结构
 *===========================================================================*/

/**
 * @brief 一帧的完整检测结果
 * 
 * 内存布局：
 *   Offset   0: uint32_t magic        (4 bytes)
 *   Offset   4: uint32_t version      (4 bytes)
 *   Offset   8: uint32_t frame_id     (4 bytes)
 *   Offset  12: uint32_t timestamp    (4 bytes)
 *   Offset  16: uint32_t count        (4 bytes)
 *   Offset  20: int32_t  selected_idx (4 bytes)  - DSP选中的目标索引，-1表示无
 *   Offset  24: DetectionBox_t boxes[MAX_DETECTION_COUNT] (68 * 10 = 680 bytes)
 *   Total: 704 bytes
 */
typedef struct __attribute__((packed)) {
    uint32_t       magic;         /**< 魔数，用于校验 (DETECTION_RESULT_MAGIC) */
    uint32_t       version;       /**< 协议版本 (DETECTION_PROTOCOL_VERSION) */
    uint32_t       frame_id;      /**< 帧序号（DSP 递增）*/
    uint32_t       timestamp;     /**< 时间戳（毫秒，可选）*/
    uint32_t       count;         /**< 本帧检测到的目标数量 [0, MAX_DETECTION_COUNT] */
    int32_t        selected_idx;  /**< DSP选中的目标索引 [0,count-1]，-1表示无选中 */
    DetectionBox_t boxes[MAX_DETECTION_COUNT];  /**< 检测目标数组 */
} DetectionResult_t;

/** 魔数，用于校验数据有效性 */
#define DETECTION_RESULT_MAGIC        0x44455446u  /* "DETF" in ASCII */

/*===========================================================================
 * Mailbox 消息类型定义
 *===========================================================================*/

/**
 * @brief Mailbox 消息格式
 * 
 * 为支持多种消息类型，Mailbox 传递的 32-bit 值定义如下：
 *   - Bit [31:28]: 消息类型 (MsgType)
 *   - Bit [27:0]:  消息数据 (Payload，通常是 offset)
 * 
 * 消息类型：
 *   0x0: 单目标结果（兼容旧协议，payload = FaceRect offset）
 *   0x1: 多目标结果（新协议，payload = DetectionResult offset）
 *   0xF: 无检测结果（本帧无目标，payload 忽略）
 */
#define MAILBOX_MSG_TYPE_MASK         0xF0000000u
#define MAILBOX_MSG_PAYLOAD_MASK      0x0FFFFFFFu

#define MAILBOX_MSG_TYPE_SINGLE       0x00000000u  /**< 单目标（旧协议兼容）*/
#define MAILBOX_MSG_TYPE_MULTI        0x10000000u  /**< 多目标（新协议）*/
#define MAILBOX_MSG_TYPE_NO_DETECT    0xF0000000u  /**< 本帧无检测结果 */

/** 从 Mailbox 消息中提取消息类型 */
#define MAILBOX_GET_MSG_TYPE(msg)     ((msg) & MAILBOX_MSG_TYPE_MASK)

/** 从 Mailbox 消息中提取 payload（offset）*/
#define MAILBOX_GET_PAYLOAD(msg)      ((msg) & MAILBOX_MSG_PAYLOAD_MASK)

/** 构造 Mailbox 消息 */
#define MAILBOX_MAKE_MSG(type, payload) \
    (((type) & MAILBOX_MSG_TYPE_MASK) | ((payload) & MAILBOX_MSG_PAYLOAD_MASK))

/*===========================================================================
 * CM4 → DSP 控制命令（反向 Mailbox）
 *===========================================================================*/

/**
 * @brief CM4 发送给 DSP 的控制命令格式
 * 
 * 消息格式（32-bit）：
 *   - Bit [31:28]: 命令类型 (CmdType)
 *   - Bit [27:0]:  命令参数 (Param)
 * 
 * 命令类型：
 *   0x8: 启动追踪 - DSP 开始输出检测结果
 *   0x9: 停止追踪 - DSP 停止输出检测结果
 *   0xA: 重置选中 - DSP 重新选择目标（丢弃当前 track_id）
 */
#define CMD_TYPE_MASK                 0xF0000000u
#define CMD_PARAM_MASK                0x0FFFFFFFu

#define CMD_TYPE_START_TRACKING       0x80000000u  /**< 启动追踪 */
#define CMD_TYPE_STOP_TRACKING        0x90000000u  /**< 停止追踪 */
#define CMD_TYPE_RESET_SELECTION      0xA0000000u  /**< 重置选中目标 */

/** 从命令消息中提取命令类型 */
#define CMD_GET_TYPE(cmd)             ((cmd) & CMD_TYPE_MASK)

/** 从命令消息中提取参数 */
#define CMD_GET_PARAM(cmd)            ((cmd) & CMD_PARAM_MASK)

/** 构造命令消息 */
#define CMD_MAKE(type, param) \
    (((type) & CMD_TYPE_MASK) | ((param) & CMD_PARAM_MASK))

/*===========================================================================
 * 辅助宏和函数
 *===========================================================================*/

/** 检查 DetectionResult 是否有效 */
#define DETECTION_RESULT_IS_VALID(pResult) \
    ((pResult)->magic == DETECTION_RESULT_MAGIC && \
     (pResult)->count <= MAX_DETECTION_COUNT)

/** 获取 DetectionResult 结构的总大小 */
#define DETECTION_RESULT_SIZE         sizeof(DetectionResult_t)

/** 获取单个 DetectionBox 的大小 */
#define DETECTION_BOX_SIZE            sizeof(DetectionBox_t)

/*===========================================================================
 * 编译时断言（确保与 DSP 端结构体大小一致）
 *===========================================================================*/

_Static_assert(sizeof(DetectionBox_t) == 68, "DetectionBox_t size mismatch");
_Static_assert(sizeof(DetectionResult_t) == 704, "DetectionResult_t size mismatch");

#ifdef __cplusplus
}
#endif

#endif /* DETECTION_PROTOCOL_H */
