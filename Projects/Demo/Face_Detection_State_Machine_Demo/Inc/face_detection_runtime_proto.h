#ifndef FACE_DETECTION_RUNTIME_PROTO_H
#define FACE_DETECTION_RUNTIME_PROTO_H

#include <stdint.h>

#include "mailbox_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Demo 私有运行时通知：DSP 只声明“请使能 MM 运行态”，
 * 具体需要写哪些寄存器由 CM4 根据板型和资源状态自行决定。
 *
 * 兼容说明：历史 DSP 可能仍发送 CORE/SPI 两种 payload；
 * CM4 侧现在会把它们统一折算为同一条 MM_ENABLE 语义。
 */
#define FD_RT_MSG_TYPE_MM_ENABLE_REQ      0x40000000u

#define FD_RT_MM_ENABLE_REQ               0x00000001u

#define FD_RT_MAKE_MM_ENABLE_REQ() \
    MAILBOX_MAKE_MSG(FD_RT_MSG_TYPE_MM_ENABLE_REQ, FD_RT_MM_ENABLE_REQ)

static inline int fd_runtime_msg_is_mm_enable_req(uint32_t msg)
{
    return MAILBOX_GET_MSG_TYPE(msg) == FD_RT_MSG_TYPE_MM_ENABLE_REQ;
}

static inline uint32_t fd_runtime_msg_get_mm_enable_req(uint32_t msg)
{
    return MAILBOX_GET_PAYLOAD(msg);
}

#define FD_RT_MSG_TYPE_MM_SYNC_REQ        FD_RT_MSG_TYPE_MM_ENABLE_REQ
#define FD_RT_SYNC_REQ_CORE_REG_UPDATE    FD_RT_MM_ENABLE_REQ
#define FD_RT_SYNC_REQ_SPI_REG_UPDATE     0x00000002u

#define FD_RT_MAKE_MM_SYNC_REQ(req) \
    MAILBOX_MAKE_MSG(FD_RT_MSG_TYPE_MM_ENABLE_REQ, (req))

static inline int fd_runtime_msg_is_mm_sync_req(uint32_t msg)
{
    return fd_runtime_msg_is_mm_enable_req(msg);
}

static inline uint32_t fd_runtime_msg_get_mm_sync_req(uint32_t msg)
{
    return fd_runtime_msg_get_mm_enable_req(msg);
}

#ifdef __cplusplus
}
#endif

#endif