#include "app_mailbox.h"
#include "mailbox.h"
#include "mailbox_proto.h"
#include "detection_proto.h"
#include <stdio.h>

static uint32_t (*s_get_ms)(void) = NULL;
static volatile uint32_t s_last_face_ms = 0;
/* 500ms timeout for face detection signal */
#define FACE_TIMEOUT_MS 300 

void app_mailbox_init(void) {
}

void app_mailbox_set_time_callback(uint32_t (*get_ms)(void)) {
    s_get_ms = get_ms;
}

void app_mailbox_poll(void) {
    uint32_t now = (s_get_ms != NULL) ? s_get_ms() : 0;
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
        uint32_t msg = 0;
        int ret = mailbox_read_u32(MAILBOX_BASE, &msg, 100);
        if (ret != 0) break;
        
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);
        
        bool has_valid_face = false;
        
        switch (msg_type) {
        case MAILBOX_MSG_TYPE_MULTI: {
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionResult_t *result = (const DetectionResult_t*)addr;
            if (DETECTION_RESULT_IS_VALID(result) && result->count > 0) {
                has_valid_face = true;
            }
            break;
        }
        case MAILBOX_MSG_TYPE_SINGLE: {
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionBox_t *box = (const DetectionBox_t*)addr;
            if (!(box->x1 < 0 && box->y1 < 0 && box->x2 < 0 && box->y2 < 0)) {
                has_valid_face = true;
            }
            break;
        }
        default:
            break;
        }
        
        if (has_valid_face) {
            s_last_face_ms = now;
        }
    }
}

bool app_mailbox_is_face_present(void) {
    if (s_get_ms == NULL) return false;
    uint32_t now = s_get_ms();
    if (now < s_last_face_ms) return true; // Time wrapped around
    return (now - s_last_face_ms) <= FACE_TIMEOUT_MS;
}
