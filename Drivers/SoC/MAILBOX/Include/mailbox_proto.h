/**
 * @file mailbox_proto.h
 * @brief M4 与 DSP 邮箱通信通用协议框架
 * 
 * 本文件定义了 M4 和 DSP 之间通过 Mailbox 通信的通用消息格式。
 * 具体业务场景的 payload 定义请参见各算法模型的协议头文件：
 *   - Algorithm_Models/protocol/detection_proto.h  (检测/追踪)
 *   - Algorithm_Models/protocol/kws_proto.h        (语音识别)
 *   - Algorithm_Models/protocol/recognition_proto.h (人脸识别)
 * 
 * 消息格式 (32-bit):
 *   ┌──────────────┬────────────────────────────────┐
 *   │  Bit [31:28] │  消息类型 (MsgType)             │
 *   ├──────────────┼────────────────────────────────┤
 *   │  Bit [27:0]  │  消息数据 (Payload)             │
 *   └──────────────┴────────────────────────────────┘
 */
#ifndef MAILBOX_PROTO_H
#define MAILBOX_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 通用消息格式定义
 *===========================================================================*/

/** 消息类型掩码（高 4 位）*/
#define MAILBOX_MSG_TYPE_MASK         0xF0000000u

/** 消息数据掩码（低 28 位）*/
#define MAILBOX_MSG_PAYLOAD_MASK      0x0FFFFFFFu

/*===========================================================================
 * DSP → M4 消息类型 (高 4 位 = 0x0 ~ 0x7)
 *===========================================================================*/

/** 单目标检测结果（旧协议兼容，payload = FaceRect 偏移）*/
#define MAILBOX_MSG_TYPE_SINGLE       0x00000000u

/** 多目标检测结果（新协议，payload = DetectionResult 偏移）*/
#define MAILBOX_MSG_TYPE_MULTI        0x10000000u

/** 语音识别结果（payload = KWSResult 偏移）*/
#define MAILBOX_MSG_TYPE_KWS          0x20000000u

/** 人脸识别结果（payload = RecognitionResult 偏移）*/
#define MAILBOX_MSG_TYPE_RECOGNITION  0x30000000u

/** 保留 0x40000000 ~ 0xE0000000 供未来扩展 */

/** 本帧无结果（任何算法场景通用）*/
#define MAILBOX_MSG_TYPE_NO_RESULT    0xF0000000u

/*===========================================================================
 * M4 → DSP 命令类型 (高 4 位 = 0x8 ~ 0xF)
 *===========================================================================*/

/** 启动追踪/检测 */
#define MAILBOX_CMD_START             0x80000000u

/** 停止追踪/检测 */
#define MAILBOX_CMD_STOP              0x90000000u

/** 重置选中目标 */
#define MAILBOX_CMD_RESET_SELECTION   0xA0000000u

/** 切换模型/算法模式 */
#define MAILBOX_CMD_SWITCH_MODEL      0xB0000000u

/** 保留 0xC0000000 ~ 0xE0000000 供未来扩展 */

/*===========================================================================
 * 消息解析宏
 *===========================================================================*/

/** 从消息中提取类型 */
#define MAILBOX_GET_MSG_TYPE(msg)     ((msg) & MAILBOX_MSG_TYPE_MASK)

/** 从消息中提取 payload */
#define MAILBOX_GET_PAYLOAD(msg)      ((msg) & MAILBOX_MSG_PAYLOAD_MASK)

/** 构造消息 */
#define MAILBOX_MAKE_MSG(type, payload) \
    (((type) & MAILBOX_MSG_TYPE_MASK) | ((payload) & MAILBOX_MSG_PAYLOAD_MASK))

/*===========================================================================
 * 共享内存基地址定义
 *===========================================================================*/

/** DSP 检测结果共享内存基地址 */
#ifndef DSP_DETECTION_BASE_ADDR
#define DSP_DETECTION_BASE_ADDR       0x44800000u
#endif

/** DSP 语音识别结果共享内存基地址 */
#ifndef DSP_KWS_BASE_ADDR
#define DSP_KWS_BASE_ADDR             0x44810000u
#endif

/** DSP 人脸识别结果共享内存基地址 */
#ifndef DSP_RECOGNITION_BASE_ADDR
#define DSP_RECOGNITION_BASE_ADDR     0x44820000u
#endif

/** 兼容旧版本的宏定义 */
#define DSP_FACE_BASE_ADDR            DSP_DETECTION_BASE_ADDR

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

/**
 * @brief 判断消息是否为检测类结果
 * @param msg 邮箱消息
 * @return true 如果是检测结果（SINGLE 或 MULTI）
 */
static inline int mailbox_msg_is_detection(uint32_t msg)
{
    uint32_t type = MAILBOX_GET_MSG_TYPE(msg);
    return (type == MAILBOX_MSG_TYPE_SINGLE || type == MAILBOX_MSG_TYPE_MULTI);
}

/**
 * @brief 判断消息是否为命令
 * @param msg 邮箱消息
 * @return true 如果高 4 位 >= 0x8
 */
static inline int mailbox_msg_is_command(uint32_t msg)
{
    return (msg & 0x80000000u) != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* MAILBOX_PROTO_H */
