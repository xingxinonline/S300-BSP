#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "mailbox.h"
#include "video.h"
#include "eyes.h"
#include "ui_display.h"
#include "face_tracker.h"

/* FaceRect from DSP shared memory (base + offset) */
typedef struct FaceRect_ {
    float score;
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
    float lm[10];
} FaceRect;

#ifndef DSP_FACE_BASE_ADDR
#define DSP_FACE_BASE_ADDR 0x44800000u
#endif

#ifndef FACE_COORD_SPACE_W
#define FACE_COORD_SPACE_W DISP_IMAGE_WIDTH
#endif
#ifndef FACE_COORD_SPACE_H
#define FACE_COORD_SPACE_H DISP_IMAGE_HEIGHT
#endif

#ifndef FACE_PRESENCE_CONFIRM_MS
#define FACE_PRESENCE_CONFIRM_MS 200u
#endif
#ifndef FACE_DEBUG_CONFIRM
#define FACE_DEBUG_CONFIRM 0
#endif

static uint32_t (*s_get_millis)(void) = 0;

void face_tracker_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;
}

static inline uint32_t millis(void)
{
    return s_get_millis ? s_get_millis() : 0u;
}

void face_tracker_poll(void)
{
    /* 最近一次接收到有效人脸帧的时间戳（无论是否产生移动都更新，用于防止“静止人脸被误判丢失”） */
    static uint32_t s_last_valid_face_ms = 0;
    static uint32_t s_last_idle_log_ms   = 0;
    static int      s_idle_active        = 0;
    static int      s_face_present       = 0;
    static uint32_t s_face_present_since = 0;
    static int      s_face_confirmed     = 0;
    static uint32_t s_last_cmd_ms        = 0;
    static int      s_have_last_cmd      = 0;
    static int32_t  s_last_cmd_mid_x     = 0;
    static int32_t  s_last_cmd_mid_y     = 0;
    static uint32_t s_last_move_log_ms   = 0;

    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t offset = read_mailbox(MAILBOX_BASE);
        uintptr_t addr = (uintptr_t)DSP_FACE_BASE_ADDR + (uintptr_t)offset;
        const FaceRect *fr = (const FaceRect*)addr;

        int32_t x1 = fr->x1, y1 = fr->y1, x2 = fr->x2, y2 = fr->y2;
        if (x2 < x1) { int32_t t = x1; x1 = x2; x2 = t; }
        if (y2 < y1) { int32_t t = y1; y1 = y2; y2 = t; }

        bool valid = true;
        if (x1 < 0 || y1 < 0 || x2 > FACE_COORD_SPACE_W || y2 > FACE_COORD_SPACE_H) valid = false;
        if ((x2 - x1) <= 2 || (y2 - y1) <= 2) valid = false;

        static uint32_t s_last_invalid_log_ms = 0;
        uint32_t now_ms = millis();
        if (!valid)
        {
            if ((uint32_t)(now_ms - s_last_invalid_log_ms) >= INVALID_LOG_INTERVAL_MS)
            {
                printf("RX[M4]: off=0x%08lx addr=%p invalid face=(%ld,%ld)-(%ld,%ld) skip\r\n",
                       (unsigned long)offset, (void*)addr,
                       (long)x1, (long)y1, (long)x2, (long)y2);
                s_last_invalid_log_ms = now_ms;
            }
            s_face_present = 0;
            s_face_confirmed = 0;
#if FACE_DEBUG_CONFIRM
            printf("[S300][FACE] reset by invalid frame at %lums\r\n", (unsigned long)now_ms);
#endif
            continue;
        }

        if (!s_face_present) {
            s_face_present = 1; s_face_present_since = now_ms;
#if FACE_DEBUG_CONFIRM
            printf("[S300][FACE] present start at %lums\r\n", (unsigned long)s_face_present_since);
#endif
        }
    if (!s_face_confirmed) {
            uint32_t held = (uint32_t)(now_ms - s_face_present_since);
            if (held >= FACE_PRESENCE_CONFIRM_MS) {
                s_face_confirmed = 1;
                eyes_blink_hide_and_restore();
        /* 面部确认出现：立即请求一次界面刷新 */
        ui_request_refresh();
#if FACE_DEBUG_CONFIRM
                printf("[S300][FACE] confirmed after %lums (start %lums -> now %lums)\r\n",
                       (unsigned long)held, (unsigned long)s_face_present_since, (unsigned long)now_ms);
#endif
            }
        }

        /* 关键修复：无论是否触发移动，只要收到有效人脸帧，就刷新“最后一次见到人脸”的时间戳。 */
        s_last_valid_face_ms = now_ms;
        /* 一旦有有效人脸帧，退出空闲眨眼态标记（动画关闭动作在确认时由 eyes_blink_hide_and_restore 处理）。 */
        s_idle_active = 0;

        int32_t cx_raw = x1 + (x2 - x1) / 2;
        int32_t cy_raw = y1 + (y2 - y1) / 2;

        int32_t min_x, max_x; eyes_mid_limits_x(&min_x, &max_x);
        int32_t min_y, max_y; eyes_mid_limits_y(&min_y, &max_y);
    if (cx_raw < 0) cx_raw = 0;
    if (cx_raw > FACE_COORD_SPACE_W) cx_raw = FACE_COORD_SPACE_W;
    if (cy_raw < 0) cy_raw = 0;
    if (cy_raw > FACE_COORD_SPACE_H) cy_raw = FACE_COORD_SPACE_H;
        int32_t span_x = (max_x >= min_x) ? (max_x - min_x) : 0;
        int32_t span_y = (max_y >= min_y) ? (max_y - min_y) : 0;
        int32_t mid_x = min_x;
        int32_t mid_y = min_y;
        if (FACE_COORD_SPACE_W > 0 && span_x > 0)
            mid_x = min_x + (int32_t)(((int64_t)cx_raw * (int64_t)span_x) / (int64_t)FACE_COORD_SPACE_W);
        if (FACE_COORD_SPACE_H > 0 && span_y > 0)
            mid_y = min_y + (int32_t)(((int64_t)cy_raw * (int64_t)span_y) / (int64_t)FACE_COORD_SPACE_H);

        static int s_ema_inited = 0;
        static int32_t s_mid_x = 0, s_mid_y = 0;
        if (!s_ema_inited) { s_mid_x = mid_x; s_mid_y = mid_y; s_ema_inited = 1; }
        else {
            s_mid_x = (int32_t)(((int64_t)s_mid_x * (EYE_SMOOTH_DEN - EYE_SMOOTH_NUM) + (int64_t)mid_x * EYE_SMOOTH_NUM) / EYE_SMOOTH_DEN);
            s_mid_y = (int32_t)(((int64_t)s_mid_y * (EYE_SMOOTH_DEN - EYE_SMOOTH_NUM) + (int64_t)mid_y * EYE_SMOOTH_NUM) / EYE_SMOOTH_DEN);
        }

        int32_t lim_x = s_mid_x;
        int32_t lim_y = s_mid_y;
        if (EYE_RATE_LIMIT_PX_PER_S > 0 && s_have_last_cmd)
        {
            uint32_t dt_ms = (uint32_t)(now_ms - s_last_cmd_ms);
            uint32_t max_step = (dt_ms > 0u) ? (uint32_t)((EYE_RATE_LIMIT_PX_PER_S * (uint64_t)dt_ms) / 1000u) : 0u;
            if (max_step == 0u) max_step = 1u;
            int32_t dx_req = s_mid_x - s_last_cmd_mid_x;
            int32_t dy_req = s_mid_y - s_last_cmd_mid_y;
            if (dx_req > (int32_t)max_step) dx_req = (int32_t)max_step;
            if (dx_req < -(int32_t)max_step) dx_req = -(int32_t)max_step;
            if (dy_req > (int32_t)max_step) dy_req = (int32_t)max_step;
            if (dy_req < -(int32_t)max_step) dy_req = -(int32_t)max_step;
            lim_x = s_last_cmd_mid_x + dx_req;
            lim_y = s_last_cmd_mid_y + dy_req;
        }

        int32_t dx = s_have_last_cmd ? (lim_x - s_last_cmd_mid_x) : EYE_MIN_MOVE_PX;
        int32_t dy = s_have_last_cmd ? (lim_y - s_last_cmd_mid_y) : EYE_MIN_MOVE_PX;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx < EYE_MIN_MOVE_PX && dy < EYE_MIN_MOVE_PX) { continue; }
        if (!s_face_confirmed) { continue; }

        bool do_log = ((uint32_t)(now_ms - s_last_move_log_ms) >= MOVE_LOG_MIN_INTERVAL_MS);
        if (do_log)
        {
            printf("RX[M4]: off=0x%08lx addr=%p center=(%ld,%ld) -> mid=(%ld,%ld) smoothed=(%ld,%ld)\r\n",
                   (unsigned long)offset, (void*)addr,
                   (long)cx_raw, (long)cy_raw, (long)mid_x, (long)mid_y, (long)s_mid_x, (long)s_mid_y);
            eyes_move_to_xy(lim_x, lim_y);
            s_last_move_log_ms = now_ms;
        }
        else { eyes_move_to_xy(lim_x, lim_y); }

    /* 保留：发生移动时也顺带刷新，冗余但无害。 */
    s_last_valid_face_ms = now_ms; s_idle_active = 0;
        s_last_cmd_mid_x = lim_x; s_last_cmd_mid_y = lim_y; s_last_cmd_ms = now_ms; s_have_last_cmd = 1;
    }

    uint32_t now2 = millis();
    if (s_last_valid_face_ms != 0u)
    {
        uint32_t dt = (uint32_t)(now2 - s_last_valid_face_ms);
        if (dt >= FACE_LOST_TIMEOUT_MS && !s_idle_active)
        {
            if ((uint32_t)(now2 - s_last_idle_log_ms) >= IDLE_RETURN_LOG_MIN_INTERVAL_MS)
            {
                printf("[S300][IDLE] no face %lums -> blink (show close-eye GIF)\r\n", (unsigned long)dt);
                s_last_idle_log_ms = now2;
            }
            eyes_blink_show();
            /* 面部消失进入 idle：立即请求一次界面刷新 */
            ui_request_refresh();
            s_face_present = 0; s_face_confirmed = 0;
#if FACE_DEBUG_CONFIRM
            printf("[S300][FACE] reset by idle at %lums\r\n", (unsigned long)now2);
#endif
            s_idle_active = 1;
        }
    }
}
