#include "app_mailbox.h"
#include "mailbox.h"
#include <stdio.h>

#ifndef DSP_FACE_BASE_ADDR
#define DSP_FACE_BASE_ADDR 0x44800000u
#endif

typedef struct FaceRect_ {
    float score;
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
    float lm[10];
} FaceRect;

static uint32_t (*s_get_ms)(void) = NULL;
static volatile uint32_t s_last_face_ms = 0;
/* 500ms timeout for face detection signal */
#define FACE_TIMEOUT_MS 500 

void app_mailbox_init(void) {
}

void app_mailbox_set_time_callback(uint32_t (*get_ms)(void)) {
    s_get_ms = get_ms;
}

void app_mailbox_poll(void) {
    uint32_t now = (s_get_ms != NULL) ? s_get_ms() : 0;
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
        uint32_t offset = 0;
        int ret = mailbox_read_u32(MAILBOX_BASE, &offset, 100);
        if (ret != 0) break;
        
        uintptr_t addr = (uintptr_t)DSP_FACE_BASE_ADDR + (uintptr_t)offset;
        const FaceRect *fr = (const FaceRect*)addr;
        
        if (fr->x1 < 0 && fr->y1 < 0 && fr->x2 < 0 && fr->y2 < 0) continue;
        s_last_face_ms = now;
    }
}

bool app_mailbox_is_face_present(void) {
    if (s_get_ms == NULL) return false;
    uint32_t now = s_get_ms();
    if (now < s_last_face_ms) return true; // Time wrapped around
    return (now - s_last_face_ms) <= FACE_TIMEOUT_MS;
}
