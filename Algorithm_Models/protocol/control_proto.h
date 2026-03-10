/**
 * @file control_proto.h
 * @brief CM4-DSP 控制面协议定义（session 驱动）
 *
 * 本协议头用于 CM4 与 DSP 之间的控制面联调，不承载实际 AI 数据。
 * 控制面目标：
 *   1. 建立带 session 的可信会话
 *   2. 显式区分 DSP ready、CM4 resource ready、configured、running
 *   3. 为视频/音频流通知提供统一编码框架
 */
#ifndef CONTROL_PROTO_H
#define CONTROL_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 协议版本
 *===========================================================================*/

#define CONTROL_PROTOCOL_VERSION          0x0100u

/*===========================================================================
 * 顶层消息类型
 *===========================================================================*/

#define CONTROL_MSG_TYPE_SYS              0x50000000u
#define CONTROL_MSG_TYPE_CMD              0x80000000u
#define CONTROL_MSG_TYPE_ACK              0x90000000u
#define CONTROL_MSG_TYPE_NACK             0xA0000000u
#define CONTROL_MSG_TYPE_STATUS           0xE0000000u

#define CONTROL_MSG_TYPE_MASK             0xF0000000u
#define CONTROL_MSG_SUBTYPE_MASK          0x0F000000u
#define CONTROL_MSG_SESSION_MASK          0x00FF0000u
#define CONTROL_MSG_ARG_MASK              0x0000FFFFu

#define CONTROL_MSG_SUBTYPE_SHIFT         24u
#define CONTROL_MSG_SESSION_SHIFT         16u

/*===========================================================================
 * SYS 子类型
 *===========================================================================*/

#define CONTROL_SYS_SUBTYPE_HELLO              0x01u
#define CONTROL_SYS_SUBTYPE_HELLO_ACK          0x02u
#define CONTROL_SYS_SUBTYPE_CM4_RESOURCE_READY 0x03u
#define CONTROL_SYS_SUBTYPE_DSP_MODEL_READY    0x04u
#define CONTROL_SYS_SUBTYPE_START_STREAM       0x05u
#define CONTROL_SYS_SUBTYPE_STOP_STREAM        0x06u
#define CONTROL_SYS_SUBTYPE_HEARTBEAT          0x07u
#define CONTROL_SYS_SUBTYPE_RECOVER_REQ        0x08u
#define CONTROL_SYS_SUBTYPE_RECOVER_DONE       0x09u

/*===========================================================================
 * CMD 分组与操作码
 *===========================================================================*/

#define CONTROL_CMD_GRP_BASIC             0x00u
#define CONTROL_CMD_GRP_SELECT            0x01u
#define CONTROL_CMD_GRP_MODE              0x02u
#define CONTROL_CMD_GRP_GIMBAL            0x03u
#define CONTROL_CMD_GRP_CONFIG            0x04u
#define CONTROL_CMD_GRP_BUFFER            0x05u
#define CONTROL_CMD_GRP_STREAM            0x06u
#define CONTROL_CMD_GRP_SYSTEM            0x0Fu

#define CONTROL_CMD_CONFIG_APPLY          0x01u
#define CONTROL_CMD_BUFFER_BIND           0x01u
#define CONTROL_CMD_STREAM_FRAME_READY    0x01u
#define CONTROL_CMD_STREAM_AUDIO_READY    0x02u

/*===========================================================================
 * ACK/NACK 响应类别
 *===========================================================================*/

#define CONTROL_RSP_KIND_SYS              0x00u
#define CONTROL_RSP_KIND_CMD              0x01u

/*===========================================================================
 * 输入类型、资源标志、状态与错误码
 *===========================================================================*/

typedef enum {
    CONTROL_INPUT_NONE = 0,
    CONTROL_INPUT_VIDEO = 1,
    CONTROL_INPUT_AUDIO = 2,
    CONTROL_INPUT_VIDEO_AUDIO = 3,
} ControlInputType_t;

#define CONTROL_RESOURCE_CAMERA_READY     (1u << 0)
#define CONTROL_RESOURCE_MM_READY         (1u << 1)
#define CONTROL_RESOURCE_LCD_READY        (1u << 2)
#define CONTROL_RESOURCE_AUDIO_READY      (1u << 3)
#define CONTROL_RESOURCE_DMA_READY        (1u << 4)

typedef enum {
    CONTROL_BOOT_REASON_UNKNOWN = 0,
    CONTROL_BOOT_REASON_POWER_ON = 1,
    CONTROL_BOOT_REASON_DOMAIN_RELEASE = 2,
    CONTROL_BOOT_REASON_WARM_RESET = 3,
    CONTROL_BOOT_REASON_RECOVER = 4,
} ControlBootReason_t;

typedef enum {
    CONTROL_RUN_STATE_INIT = 0,
    CONTROL_RUN_STATE_READY = 1,
    CONTROL_RUN_STATE_CONFIGURED = 2,
    CONTROL_RUN_STATE_RUNNING = 3,
    CONTROL_RUN_STATE_DEGRADED = 4,
    CONTROL_RUN_STATE_ERROR = 5,
} ControlRunState_t;

typedef enum {
    CONTROL_ACK_OK = 0,
} ControlAckCode_t;

typedef enum {
    CONTROL_ERR_PROTOCOL_MISMATCH = 1,
    CONTROL_ERR_FEATURE_UNSUPPORTED = 2,
    CONTROL_ERR_INVALID_CONFIG = 3,
    CONTROL_ERR_RESOURCE_NOT_READY = 4,
    CONTROL_ERR_BUFFER_BIND_FAILED = 5,
    CONTROL_ERR_BUSY = 6,
    CONTROL_ERR_BAD_FORMAT = 7,
    CONTROL_ERR_MODEL_NOT_READY = 8,
    CONTROL_ERR_SESSION_MISMATCH = 9,
} ControlErrorCode_t;

/*===========================================================================
 * 顶层打包与解析
 *===========================================================================*/

#define CONTROL_MAKE_MSG(type, subtype, session, arg) \
    (((uint32_t)(type) & CONTROL_MSG_TYPE_MASK) | \
     (((uint32_t)(subtype) & 0x0Fu) << CONTROL_MSG_SUBTYPE_SHIFT) | \
     (((uint32_t)(session) & 0xFFu) << CONTROL_MSG_SESSION_SHIFT) | \
     ((uint32_t)(arg) & CONTROL_MSG_ARG_MASK))

#define CONTROL_GET_TYPE(msg)            ((uint32_t)(msg) & CONTROL_MSG_TYPE_MASK)
#define CONTROL_GET_SUBTYPE(msg)         (((uint32_t)(msg) >> CONTROL_MSG_SUBTYPE_SHIFT) & 0x0Fu)
#define CONTROL_GET_SESSION(msg)         (((uint32_t)(msg) >> CONTROL_MSG_SESSION_SHIFT) & 0xFFu)
#define CONTROL_GET_ARG(msg)             ((uint16_t)((uint32_t)(msg) & CONTROL_MSG_ARG_MASK))

/*===========================================================================
 * SYS 消息参数布局
 *===========================================================================*/

#define CONTROL_ARG_HELLO(protocol_version) \
    ((uint16_t)((protocol_version) & 0xFFFFu))

#define CONTROL_ARG_HELLO_ACK(boot_reason, feature_level, run_state) \
    (uint16_t)((((uint16_t)(boot_reason) & 0x0Fu) << 12) | \
               (((uint16_t)(feature_level) & 0x0Fu) << 8) | \
               ((uint16_t)(run_state) & 0xFFu))

#define CONTROL_HELLO_ACK_GET_BOOT_REASON(arg) \
    (((uint16_t)(arg) >> 12) & 0x0Fu)
#define CONTROL_HELLO_ACK_GET_FEATURE_LEVEL(arg) \
    (((uint16_t)(arg) >> 8) & 0x0Fu)
#define CONTROL_HELLO_ACK_GET_RUN_STATE(arg) \
    ((uint16_t)(arg) & 0xFFu)

#define CONTROL_ARG_RESOURCE_READY(input_type, resource_flags, config_slot_id) \
    (uint16_t)((((uint16_t)(input_type) & 0x0Fu) << 12) | \
               (((uint16_t)(resource_flags) & 0x0Fu) << 8) | \
               ((uint16_t)(config_slot_id) & 0xFFu))

#define CONTROL_RESOURCE_READY_GET_INPUT_TYPE(arg) \
    (((uint16_t)(arg) >> 12) & 0x0Fu)
#define CONTROL_RESOURCE_READY_GET_FLAGS(arg) \
    (((uint16_t)(arg) >> 8) & 0x0Fu)
#define CONTROL_RESOURCE_READY_GET_CONFIG_SLOT(arg) \
    ((uint16_t)(arg) & 0xFFu)

#define CONTROL_ARG_MODEL_READY(algo_id, model_flags, run_state) \
    (uint16_t)((((uint16_t)(algo_id) & 0x0Fu) << 12) | \
               (((uint16_t)(model_flags) & 0x0Fu) << 8) | \
               ((uint16_t)(run_state) & 0xFFu))

#define CONTROL_MODEL_READY_GET_ALGO_ID(arg) \
    (((uint16_t)(arg) >> 12) & 0x0Fu)
#define CONTROL_MODEL_READY_GET_FLAGS(arg) \
    (((uint16_t)(arg) >> 8) & 0x0Fu)
#define CONTROL_MODEL_READY_GET_RUN_STATE(arg) \
    ((uint16_t)(arg) & 0xFFu)

#define CONTROL_ARG_STREAM(stream_id, flags) \
    (uint16_t)((((uint16_t)(stream_id) & 0xFFu) << 8) | ((uint16_t)(flags) & 0xFFu))

#define CONTROL_STREAM_GET_ID(arg)       (((uint16_t)(arg) >> 8) & 0xFFu)
#define CONTROL_STREAM_GET_FLAGS(arg)    ((uint16_t)(arg) & 0xFFu)

#define CONTROL_ARG_HEARTBEAT(seq, status) \
    CONTROL_ARG_STREAM((seq), (status))

#define CONTROL_HEARTBEAT_GET_SEQ(arg)   CONTROL_STREAM_GET_ID(arg)
#define CONTROL_HEARTBEAT_GET_STATUS(arg) CONTROL_STREAM_GET_FLAGS(arg)

/*===========================================================================
 * SYS 消息构造宏
 *===========================================================================*/

#define CONTROL_SYS_MAKE(subtype, session, arg) \
    CONTROL_MAKE_MSG(CONTROL_MSG_TYPE_SYS, (subtype), (session), (arg))

#define CONTROL_SYS_HELLO(session, protocol_version) \
    CONTROL_SYS_MAKE(CONTROL_SYS_SUBTYPE_HELLO, (session), CONTROL_ARG_HELLO(protocol_version))

#define CONTROL_SYS_HELLO_ACK(session, boot_reason, feature_level, run_state) \
    CONTROL_SYS_MAKE(CONTROL_SYS_SUBTYPE_HELLO_ACK, (session), \
                     CONTROL_ARG_HELLO_ACK((boot_reason), (feature_level), (run_state)))

#define CONTROL_SYS_CM4_RESOURCE_READY(session, input_type, resource_flags, config_slot_id) \
    CONTROL_SYS_MAKE(CONTROL_SYS_SUBTYPE_CM4_RESOURCE_READY, (session), \
                     CONTROL_ARG_RESOURCE_READY((input_type), (resource_flags), (config_slot_id)))

#define CONTROL_SYS_DSP_MODEL_READY(session, algo_id, model_flags, run_state) \
    CONTROL_SYS_MAKE(CONTROL_SYS_SUBTYPE_DSP_MODEL_READY, (session), \
                     CONTROL_ARG_MODEL_READY((algo_id), (model_flags), (run_state)))

#define CONTROL_SYS_START_STREAM(session, stream_id, start_flags) \
    CONTROL_SYS_MAKE(CONTROL_SYS_SUBTYPE_START_STREAM, (session), \
                     CONTROL_ARG_STREAM((stream_id), (start_flags)))

#define CONTROL_SYS_STOP_STREAM(session, stream_id, stop_reason) \
    CONTROL_SYS_MAKE(CONTROL_SYS_SUBTYPE_STOP_STREAM, (session), \
                     CONTROL_ARG_STREAM((stream_id), (stop_reason)))

#define CONTROL_SYS_HEARTBEAT(session, seq, status) \
    CONTROL_SYS_MAKE(CONTROL_SYS_SUBTYPE_HEARTBEAT, (session), \
                     CONTROL_ARG_HEARTBEAT((seq), (status)))

/*===========================================================================
 * CMD/ACK/NACK/STATUS 编码
 *===========================================================================*/

#define CONTROL_CMD_ARG(opcode, arg8) \
    (uint16_t)((((uint16_t)(opcode) & 0xFFu) << 8) | ((uint16_t)(arg8) & 0xFFu))

#define CONTROL_CMD_GET_OPCODE(arg)      (((uint16_t)(arg) >> 8) & 0xFFu)
#define CONTROL_CMD_GET_ARG8(arg)        ((uint16_t)(arg) & 0xFFu)

#define CONTROL_CMD_MAKE(group, session, opcode, arg8) \
    CONTROL_MAKE_MSG(CONTROL_MSG_TYPE_CMD, (group), (session), CONTROL_CMD_ARG((opcode), (arg8)))

#define CONTROL_ACK_CODE(kind, code)     CONTROL_CMD_ARG((code), (kind))

#define CONTROL_ACK_MAKE(kind, session, code, ack_code) \
    CONTROL_MAKE_MSG(CONTROL_MSG_TYPE_ACK, (kind), (session), CONTROL_CMD_ARG((code), (ack_code)))

#define CONTROL_NACK_MAKE(kind, session, code, error_code) \
    CONTROL_MAKE_MSG(CONTROL_MSG_TYPE_NACK, (kind), (session), CONTROL_CMD_ARG((code), (error_code)))

#define CONTROL_STATUS_MAKE(status_kind, session, run_state, status_brief) \
    CONTROL_MAKE_MSG(CONTROL_MSG_TYPE_STATUS, (status_kind), (session), \
                     CONTROL_CMD_ARG((run_state), (status_brief)))

#define CONTROL_ACK_GET_CODE(arg)        CONTROL_CMD_GET_OPCODE(arg)
#define CONTROL_ACK_GET_STATUS(arg)      CONTROL_CMD_GET_ARG8(arg)
#define CONTROL_STATUS_GET_RUN_STATE(arg) CONTROL_CMD_GET_OPCODE(arg)
#define CONTROL_STATUS_GET_BRIEF(arg)    CONTROL_CMD_GET_ARG8(arg)

/*===========================================================================
 * 便捷判断
 *===========================================================================*/

static inline int control_msg_session_matches(uint32_t msg, uint8_t session_id)
{
    return CONTROL_GET_SESSION(msg) == session_id;
}

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_PROTO_H */