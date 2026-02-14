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
 * M4 → DSP 命令协议 (v3.1)
 * 
 * 命令格式:
 *   ┌────────┬────────┬────────────────────┐
 *   │ 31..28 │ 27..24 │       23..0        │
 *   │  0x8   │ CmdGrp │      Payload       │
 *   └────────┴────────┴────────────────────┘
 *===========================================================================*/

/** 命令消息类型 (Type = 0x8) */
#define MAILBOX_CMD_TYPE              0x80000000u

/* 命令组定义 (CmdGrp) */
#define MAILBOX_CMD_GRP_BASIC         0x00  /**< 基础控制 */
#define MAILBOX_CMD_GRP_SELECT        0x01  /**< 目标选择 */
#define MAILBOX_CMD_GRP_MODE          0x02  /**< 模式设置 */
#define MAILBOX_CMD_GRP_GIMBAL        0x03  /**< 云台控制 */
#define MAILBOX_CMD_GRP_CONFIG        0x04  /**< 配置参数 */
#define MAILBOX_CMD_GRP_SYSTEM        0x0F  /**< 系统命令 */

/*---------------------------------------------------------------------------
 * 基础控制命令 (CmdGrp = 0x0)
 *---------------------------------------------------------------------------*/
/** 启动追踪 */
#define MAILBOX_CMD_START_TRACK       0x80000001u

/** 停止追踪（保持检测） */
#define MAILBOX_CMD_STOP_TRACK        0x80000002u

/** 重置当前目标 */
#define MAILBOX_CMD_RESET_TRACK       0x80000003u

/* 兼容旧代码 */
#define MAILBOX_CMD_START             MAILBOX_CMD_START_TRACK
#define MAILBOX_CMD_STOP              MAILBOX_CMD_STOP_TRACK

/*---------------------------------------------------------------------------
 * 目标选择命令 (CmdGrp = 0x1)
 *---------------------------------------------------------------------------*/
/** 选择目标: 0x81000000 | det_idx */
#define MAILBOX_CMD_SELECT_TARGET     0x81000000u

/*---------------------------------------------------------------------------
 * 模式设置命令 (CmdGrp = 0x2)
 *---------------------------------------------------------------------------*/
/** 设置选择模式: 0x82000000 | mode (0=中心,1=最大,2=手动) */
#define MAILBOX_CMD_SET_SELECT_MODE   0x82000000u

/*---------------------------------------------------------------------------
 * 云台控制命令 (CmdGrp = 0x3, v3.1)
 * 
 * 格式: 0x83000000 | (pan_q7 << 8) | tilt_q7
 * 
 *   ┌────────────┬──────────┬──────────┬──────────┐
 *   │   31..24   │  23..16  │  15..8   │   7..0   │
 *   │    0x83    │ reserved │  pan_q7  │ tilt_q7  │
 *   └────────────┴──────────┴──────────┴──────────┘
 * 
 * pan_q7/tilt_q7: 有符号 Q7 定点数 (°/frame × 128), int8_t
 * 范围: ±1.0°/frame, 精度: ~0.008°
 *---------------------------------------------------------------------------*/
/** 云台角速度命令前缀 */
#define MAILBOX_CMD_SET_GIMBAL_VEL    0x83000000u

/** @brief 将浮点角速度编码为 Q7 定点 (int8_t) */
#define MAILBOX_VEL_TO_Q7(deg)  \
    ((int8_t)((deg) * 128.0f))

/** @brief 从 Q7 定点解码为浮点角速度 */
#define MAILBOX_Q7_TO_VEL(q7)  \
    ((float)((int8_t)(q7)) / 128.0f)

/** @brief 构造云台角速度消息 (v3.1 格式) */
#define MAILBOX_MAKE_GIMBAL_VEL(pan_deg, tilt_deg)  \
    (MAILBOX_CMD_SET_GIMBAL_VEL |                   \
     (((uint8_t)MAILBOX_VEL_TO_Q7(pan_deg)) << 8) | \
     ((uint8_t)MAILBOX_VEL_TO_Q7(tilt_deg)))

/** @brief 从消息中提取 pan 角速度 (°/帧) */
#define MAILBOX_GET_GIMBAL_PAN_VEL(msg)  \
    MAILBOX_Q7_TO_VEL(((msg) >> 8) & 0xFF)

/** @brief 从消息中提取 tilt 角速度 (°/帧) */
#define MAILBOX_GET_GIMBAL_TILT_VEL(msg)  \
    MAILBOX_Q7_TO_VEL((msg) & 0xFF)

/*---------------------------------------------------------------------------
 * 配置命令 (CmdGrp = 0x4, v3.1 预留)
 * 
 * 格式: 0x84000000 | (param_id << 8) | value
 *---------------------------------------------------------------------------*/
/** 配置命令前缀 */
#define MAILBOX_CMD_SET_CONFIG        0x84000000u

/* 配置参数 ID */
#define MAILBOX_CFG_CONF_THRESHOLD    0x01  /**< 检测置信度阈值 (0-100) */
#define MAILBOX_CFG_IOU_THRESH        0x02  /**< IOU 匹配阈值 (0-100) */
#define MAILBOX_CFG_APPEAR_THRESH     0x03  /**< 外观相似度阈值 (0-100) */
#define MAILBOX_CFG_MAX_LOST_FRAMES   0x04  /**< 最大丢失搜索帧数 (0-255) */
#define MAILBOX_CFG_SELECT_MODE       0x05  /**< 目标选择模式 (0-2) */

/*---------------------------------------------------------------------------
 * 系统命令 (CmdGrp = 0xF)
 *---------------------------------------------------------------------------*/
/** 心跳测试 */
#define MAILBOX_CMD_PING              0x8F000000u

/*---------------------------------------------------------------------------
 * 旧命令兼容 (已废弃，保留用于过渡)
 *---------------------------------------------------------------------------*/
#define MAILBOX_CMD_RESET_SELECTION   0xA0000000u  /**< @deprecated 使用 RESET_TRACK */
#define MAILBOX_CMD_SWITCH_MODEL      0xB0000000u  /**< @deprecated */
#define MAILBOX_CMD_AUDIO_DATA        0xC0000000u  /**< 音频数据通知 */
#define MAILBOX_CMD_IMU_DATA          0xD0000000u  /**< IMU 数据通知 */

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
