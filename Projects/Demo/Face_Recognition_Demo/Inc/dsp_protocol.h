#ifndef DSP_PROTOCOL_H
#define DSP_PROTOCOL_H

#include <stdint.h>

/* Command IDs (M4 -> DSP) */
#define CMD_PING            0x00000001  /* Check DSP status */
#define CMD_SET_MODEL       0x00000010  /* Set Model Address & Size. Args: [Addr, Size] */
#define CMD_EXTRACT_FEATURE 0x00000011  /* Extract feature from 112x112 RGB888 image. Args: [ImageAddr] */

/* Event/Response IDs (DSP -> M4) */
#define EVT_PONG            0x80000001  /* Response to PING */
#define EVT_READY           0x80000000  /* DSP Init Done / Idle */
#define EVT_ACK             0x80000002  /* Command Accepted */
#define EVT_NACK            0x80000003  /* Command Rejected/Error */
#define EVT_FEATURE_READY   0x80000010  /* Feature vector (128 int8) ready. Payload: [FeatureAddr] */

/*
 * Shared Message Structure
 * The Mailbox register will carry the pointer (address) of this structure.
 */
typedef struct {
    uint32_t type;      /* Command ID or Event ID */
    uint32_t args[3];   /* Arguments (up to 3 words) */
} dsp_msg_t;

#endif /* DSP_PROTOCOL_H */
