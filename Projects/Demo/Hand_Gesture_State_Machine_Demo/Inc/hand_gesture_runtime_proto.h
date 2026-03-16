#ifndef HAND_GESTURE_RUNTIME_PROTO_H
#define HAND_GESTURE_RUNTIME_PROTO_H

#include <stdint.h>

#include "mailbox_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HG_RT_MSG_TYPE_MM_ENABLE_REQ      0x40000000u

#define HG_RT_MM_ENABLE_REQ               0x00000001u

#define HG_RT_MAKE_MM_ENABLE_REQ() \
    MAILBOX_MAKE_MSG(HG_RT_MSG_TYPE_MM_ENABLE_REQ, HG_RT_MM_ENABLE_REQ)

static inline int hg_runtime_msg_is_mm_enable_req(uint32_t msg)
{
    return MAILBOX_GET_MSG_TYPE(msg) == HG_RT_MSG_TYPE_MM_ENABLE_REQ;
}

static inline uint32_t hg_runtime_msg_get_mm_enable_req(uint32_t msg)
{
    return MAILBOX_GET_PAYLOAD(msg);
}

#define HG_RT_MSG_TYPE_MM_SYNC_REQ        HG_RT_MSG_TYPE_MM_ENABLE_REQ
#define HG_RT_SYNC_REQ_CORE_REG_UPDATE    HG_RT_MM_ENABLE_REQ
#define HG_RT_SYNC_REQ_SPI_REG_UPDATE     0x00000002u

#define HG_RT_MAKE_MM_SYNC_REQ(req) \
    MAILBOX_MAKE_MSG(HG_RT_MSG_TYPE_MM_ENABLE_REQ, (req))

static inline int hg_runtime_msg_is_mm_sync_req(uint32_t msg)
{
    return hg_runtime_msg_is_mm_enable_req(msg);
}

static inline uint32_t hg_runtime_msg_get_mm_sync_req(uint32_t msg)
{
    return hg_runtime_msg_get_mm_enable_req(msg);
}

#ifdef __cplusplus
}
#endif

#endif