#include "subboard_dsp_mailbox.h"

#include <stdint.h>
#include <stdio.h>

#include "control_proto.h"
#include "mailbox.h"
#include "subboard_app_identity.h"
#include "subboard_log.h"

#define SUB_DSP_STREAM_ID_MAIN 0x01u

static void send_control_msg(uint32_t msg, const char *label)
{
    int ret = write_mailbox(MAILBOX_BASE, msg);

    if (ret == 0) {
        SUB_LOG_DEBUG(SUBBOARD_APP_DSP_TAG " TX %-20s 0x%08lX\r\n", label, (unsigned long)msg);
    } else {
        SUB_LOG_WARN(SUBBOARD_APP_DSP_TAG " TX %-20s failed (%d)\r\n", label, ret);
    }
}

void subboard_dsp_mailbox_prepare(void)
{
    (void)init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    (void)init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
}

int subboard_dsp_mailbox_read(uint32_t *out_msg)
{
    if (out_msg == 0) {
        return -1;
    }

    if (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
        *out_msg = read_mailbox(MAILBOX_BASE);
        return 0;
    }

    return -1;
}

void subboard_dsp_mailbox_send_hello(uint8_t session_id)
{
    send_control_msg(CONTROL_SYS_HELLO(session_id, CONTROL_PROTOCOL_VERSION), "SYS.HELLO");
}

void subboard_dsp_mailbox_send_resource_ready(uint8_t session_id, uint8_t resource_flags)
{
    send_control_msg(
        CONTROL_SYS_CM4_RESOURCE_READY(
            session_id,
            CONTROL_INPUT_VIDEO,
            resource_flags,
            SUBBOARD_CONFIG_SLOT_ID),
        "SYS.CM4_RESOURCE_READY");

    SUB_LOG_DEBUG(SUBBOARD_APP_DSP_TAG " TX RESOURCE_READY flags=0x%02X (camera=%u mm=%u lcd=%u)\r\n",
                  (unsigned)resource_flags,
                  (unsigned)((resource_flags & CONTROL_RESOURCE_CAMERA_READY) != 0u),
                  (unsigned)((resource_flags & CONTROL_RESOURCE_MM_READY) != 0u),
                  (unsigned)((resource_flags & CONTROL_RESOURCE_LCD_READY) != 0u));
}

void subboard_dsp_mailbox_send_config_apply(uint8_t session_id)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_CONFIG, session_id,
                         CONTROL_CMD_CONFIG_APPLY, SUBBOARD_CONFIG_SLOT_ID),
        "CMD.CONFIG_APPLY");
}

void subboard_dsp_mailbox_send_buffer_bind(uint8_t session_id)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_BUFFER, session_id,
                         CONTROL_CMD_BUFFER_BIND, SUBBOARD_BUFFER_SLOT_ID),
        "CMD.BUFFER_BIND");
}

void subboard_dsp_mailbox_send_start_stream(uint8_t session_id)
{
    send_control_msg(
        CONTROL_SYS_START_STREAM(session_id, SUB_DSP_STREAM_ID_MAIN, 0x01u),
        "SYS.START_STREAM");
}

void subboard_dsp_mailbox_send_heartbeat(uint8_t session_id, uint8_t heartbeat_seq)
{
    uint32_t msg = CONTROL_SYS_HEARTBEAT(session_id, heartbeat_seq, CONTROL_RUN_STATE_RUNNING);
    int ret = write_mailbox(MAILBOX_BASE, msg);

    if (ret != 0) {
        SUB_LOG_WARN(SUBBOARD_APP_DSP_TAG " TX SYS.HEARTBEAT failed (%d)\r\n", ret);
    }
}

void subboard_dsp_mailbox_send_track_command(uint8_t session_id, uint8_t opcode)
{
    char label[24];

    (void)snprintf(label, sizeof(label), "CMD.TRACK.0x%02X", (unsigned)opcode);
    send_control_msg(CONTROL_CMD_TRACK(session_id, opcode, 0u), label);
}