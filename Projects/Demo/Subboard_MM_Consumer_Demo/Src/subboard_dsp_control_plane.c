#include "subboard_dsp_control_plane.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "control_proto.h"
#include "subboard_log.h"
#include "subboard_runtime_proto.h"

static void clear_effect(SubboardDspControlEffect_t *effect)
{
    memset(effect, 0, sizeof(*effect));
}

const char *subboard_dsp_control_plane_state_name(SubboardDspState_t state)
{
    switch (state) {
    case SUB_DSP_STATE_IDLE: return "IDLE";
    case SUB_DSP_STATE_HANDSHAKING: return "HANDSHAKING";
    case SUB_DSP_STATE_CM4_RESOURCE_READY: return "CM4_RESOURCE_READY";
    case SUB_DSP_STATE_DSP_READY: return "DSP_READY";
    case SUB_DSP_STATE_CONFIGURED: return "CONFIGURED";
    case SUB_DSP_STATE_RUNNING: return "RUNNING";
    case SUB_DSP_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

bool subboard_dsp_control_plane_handle_runtime_message(uint32_t msg,
                                                       SubboardDspState_t state,
                                                       SubboardDspControlEffect_t *out_effect)
{
    uint32_t enable_req;

    if (out_effect == NULL) {
        return false;
    }

    clear_effect(out_effect);

    if (!subboard_runtime_msg_is_mm_enable_req(msg)) {
        return false;
    }

    out_effect->consumed = true;

    if ((state != SUB_DSP_STATE_CONFIGURED) && (state != SUB_DSP_STATE_RUNNING)) {
        SUB_LOG_WARN("[SUB-DSP] ignore MM enable req before stream start, state=%s msg=0x%08lX\r\n",
                 subboard_dsp_control_plane_state_name(state),
                 (unsigned long)msg);
        return true;
    }

    enable_req = subboard_runtime_msg_get_mm_enable_req(msg);
    if ((enable_req != SUBBOARD_RT_MM_ENABLE_REQ) &&
        (enable_req != SUBBOARD_RT_SYNC_REQ_SPI_REG_UPDATE)) {
        SUB_LOG_WARN("[SUB-DSP] unknown MM enable payload=0x%08lX\r\n",
                 (unsigned long)enable_req);
        return true;
    }

    out_effect->request_master_mm_runtime = true;
    SUB_LOG_INFO("[SUB-DSP] DSP requested MM runtime enable\r\n");
    return true;
}

void subboard_dsp_control_plane_handle_control_message(uint32_t msg,
                                                       uint8_t session_id,
                                                       bool mm_ready,
                                                       SubboardDspState_t state,
                                                       SubboardDspPending_t pending,
                                                       SubboardDspControlEffect_t *out_effect)
{
    uint32_t type;

    if (out_effect == NULL) {
        return;
    }

    clear_effect(out_effect);
    type = CONTROL_GET_TYPE(msg);

    if ((type != CONTROL_MSG_TYPE_SYS) &&
        (type != CONTROL_MSG_TYPE_ACK) &&
        (type != CONTROL_MSG_TYPE_NACK) &&
        (type != CONTROL_MSG_TYPE_STATUS)) {
        return;
    }

    out_effect->consumed = true;

    if (!control_msg_session_matches(msg, session_id)) {
        SUB_LOG_WARN("[SUB-DSP] drop stale control msg: 0x%08lX\r\n", (unsigned long)msg);
        return;
    }

    if (type == CONTROL_MSG_TYPE_SYS) {
        uint8_t subtype = (uint8_t)CONTROL_GET_SUBTYPE(msg);
        uint16_t arg = CONTROL_GET_ARG(msg);

        if (subtype == CONTROL_SYS_SUBTYPE_HELLO_ACK) {
                 SUB_LOG_INFO("[SUB-DSP] RX HELLO_ACK session=0x%02X boot=%u feature=%u state=%u\r\n",
                        (unsigned)CONTROL_GET_SESSION(msg),
                        (unsigned)CONTROL_HELLO_ACK_GET_BOOT_REASON(arg),
                        (unsigned)CONTROL_HELLO_ACK_GET_FEATURE_LEVEL(arg),
                        (unsigned)CONTROL_HELLO_ACK_GET_RUN_STATE(arg));

            if ((state == SUB_DSP_STATE_HANDSHAKING) && mm_ready) {
                out_effect->request_resource_ready = true;
                out_effect->has_next_state = true;
                out_effect->next_state = SUB_DSP_STATE_CM4_RESOURCE_READY;
            }
            return;
        }

        if (subtype == CONTROL_SYS_SUBTYPE_DSP_MODEL_READY) {
                 SUB_LOG_INFO("[SUB-DSP] RX DSP_MODEL_READY algo=%u flags=%u run_state=%u\r\n",
                        (unsigned)CONTROL_MODEL_READY_GET_ALGO_ID(arg),
                        (unsigned)CONTROL_MODEL_READY_GET_FLAGS(arg),
                        (unsigned)CONTROL_MODEL_READY_GET_RUN_STATE(arg));

            if ((state == SUB_DSP_STATE_CM4_RESOURCE_READY) &&
                (pending == SUB_DSP_PENDING_NONE)) {
                out_effect->has_next_state = true;
                out_effect->next_state = SUB_DSP_STATE_DSP_READY;
                out_effect->request_config_apply = true;
            }
            return;
        }

        if (subtype == CONTROL_SYS_SUBTYPE_HEARTBEAT) {
            uint8_t status = (uint8_t)CONTROL_HEARTBEAT_GET_STATUS(arg);

            if (status != CONTROL_RUN_STATE_RUNNING) {
                  SUB_LOG_DEBUG("[SUB-DSP] RX DSP HEARTBEAT seq=%u status=%u\r\n",
                          (unsigned)CONTROL_HEARTBEAT_GET_SEQ(arg),
                          (unsigned)status);
            }
            return;
        }

        SUB_LOG_DEBUG("[SUB-DSP] RX SYS subtype=%u arg=0x%04X\r\n",
                  (unsigned)subtype, (unsigned)arg);
        return;
    }

    if (type == CONTROL_MSG_TYPE_ACK) {
        uint16_t arg = CONTROL_GET_ARG(msg);
        uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
        uint8_t status = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
        uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

        SUB_LOG_DEBUG("[SUB-DSP] RX ACK kind=%u code=0x%02X status=%u\r\n",
                  (unsigned)kind, (unsigned)code, (unsigned)status);

        if ((kind == CONTROL_RSP_KIND_CMD) &&
            (pending == SUB_DSP_PENDING_CONFIG_ACK) &&
            (code == CONTROL_CMD_CONFIG_APPLY) &&
            (status == CONTROL_ACK_OK)) {
            out_effect->request_buffer_bind = true;
            return;
        }

        if ((kind == CONTROL_RSP_KIND_CMD) &&
            (pending == SUB_DSP_PENDING_BUFFER_ACK) &&
            (code == CONTROL_CMD_BUFFER_BIND) &&
            (status == CONTROL_ACK_OK)) {
            out_effect->has_next_pending = true;
            out_effect->next_pending = SUB_DSP_PENDING_NONE;
            out_effect->has_next_state = true;
            out_effect->next_state = SUB_DSP_STATE_CONFIGURED;
            out_effect->request_start_stream = true;
            return;
        }

        if ((kind == CONTROL_RSP_KIND_SYS) &&
            (pending == SUB_DSP_PENDING_START_ACK) &&
            (code == CONTROL_SYS_SUBTYPE_START_STREAM) &&
            (status == CONTROL_ACK_OK)) {
            out_effect->has_next_pending = true;
            out_effect->next_pending = SUB_DSP_PENDING_NONE;
            out_effect->has_next_state = true;
            out_effect->next_state = SUB_DSP_STATE_RUNNING;
        }
        return;
    }

    if (type == CONTROL_MSG_TYPE_NACK) {
        uint16_t arg = CONTROL_GET_ARG(msg);
        uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
        uint8_t error = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
        uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

        SUB_LOG_WARN("[SUB-DSP] RX NACK kind=%u code=0x%02X error=%u\r\n",
                 (unsigned)kind, (unsigned)code, (unsigned)error);
        out_effect->has_next_pending = true;
        out_effect->next_pending = SUB_DSP_PENDING_NONE;
        out_effect->has_next_state = true;
        out_effect->next_state = SUB_DSP_STATE_ERROR;
        return;
    }

    if (type == CONTROL_MSG_TYPE_STATUS) {
        uint16_t arg = CONTROL_GET_ARG(msg);
        uint8_t run_state = (uint8_t)CONTROL_STATUS_GET_RUN_STATE(arg);
        uint8_t brief = (uint8_t)CONTROL_STATUS_GET_BRIEF(arg);

        if ((run_state != CONTROL_RUN_STATE_RUNNING) || (brief == 0u)) {
            SUB_LOG_DEBUG("[SUB-DSP] RX STATUS run_state=%u brief=%u\r\n",
                          (unsigned)run_state,
                          (unsigned)brief);
        }
    }
}