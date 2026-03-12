#ifndef SUBBOARD_RUNTIME_PROTO_H
#define SUBBOARD_RUNTIME_PROTO_H

#include <stdint.h>

#include "mailbox_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 子板 DSP 运行时通知：DSP 只声明“请使能 MM 运行态”，
 * 子板 CM4 再通过 I2C 请求主板执行主板侧所需寄存器写入。
 *
 * 兼容说明：历史 DSP 可能仍发送 CORE/SPI 两种 payload；
 * 子板 CM4 现在会把它们统一折算为同一条主板 MM_ENABLE 运行时请求。
 */
#define SUBBOARD_RT_MSG_TYPE_MM_ENABLE_REQ      0x40000000u

#define SUBBOARD_RT_MM_ENABLE_REQ               0x00000001u

#define SUBBOARD_RT_MAKE_MM_ENABLE_REQ() \
    MAILBOX_MAKE_MSG(SUBBOARD_RT_MSG_TYPE_MM_ENABLE_REQ, SUBBOARD_RT_MM_ENABLE_REQ)

static inline int subboard_runtime_msg_is_mm_enable_req(uint32_t msg)
{
    return MAILBOX_GET_MSG_TYPE(msg) == SUBBOARD_RT_MSG_TYPE_MM_ENABLE_REQ;
}

static inline uint32_t subboard_runtime_msg_get_mm_enable_req(uint32_t msg)
{
    return MAILBOX_GET_PAYLOAD(msg);
}

#define SUBBOARD_RT_MSG_TYPE_MM_SYNC_REQ        SUBBOARD_RT_MSG_TYPE_MM_ENABLE_REQ
#define SUBBOARD_RT_SYNC_REQ_CORE_REG_UPDATE    SUBBOARD_RT_MM_ENABLE_REQ
#define SUBBOARD_RT_SYNC_REQ_SPI_REG_UPDATE     0x00000002u

#define SUBBOARD_RT_MAKE_MM_SYNC_REQ(req) \
    MAILBOX_MAKE_MSG(SUBBOARD_RT_MSG_TYPE_MM_ENABLE_REQ, (req))

static inline int subboard_runtime_msg_is_mm_sync_req(uint32_t msg)
{
    return subboard_runtime_msg_is_mm_enable_req(msg);
}

static inline uint32_t subboard_runtime_msg_get_mm_sync_req(uint32_t msg)
{
    return subboard_runtime_msg_get_mm_enable_req(msg);
}

#ifdef __cplusplus
}
#endif

#endif