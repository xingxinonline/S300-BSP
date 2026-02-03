#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "mailbox.h"
#include "mailbox_proto.h"
#include "detection_proto.h"
#include "video.h"
#include "face_tracker.h"

#ifndef FACE_COORD_SPACE_W
#define FACE_COORD_SPACE_W DISP_IMAGE_WIDTH
#endif
#ifndef FACE_COORD_SPACE_H
#define FACE_COORD_SPACE_H DISP_IMAGE_HEIGHT
#endif

/** 调试打印级别: 0=关闭, 1=关键事件, 2=每帧 */
#ifndef FACE_TRACKER_DEBUG_LEVEL
#define FACE_TRACKER_DEBUG_LEVEL  1
#endif

#define FT_LOG(level, fmt, ...) \
    do { if (FACE_TRACKER_DEBUG_LEVEL >= (level)) printf(fmt, ##__VA_ARGS__); } while(0)

static uint32_t (*s_get_millis)(void) = 0;

void face_tracker_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;
}

static inline uint32_t millis(void)
{
    return s_get_millis ? s_get_millis() : 0u;
}

/* Helper to set alpha for a single pixel (PSRAM-safe: uses 16-bit aligned read-modify-write) */
static void set_pixel_alpha(uint32_t x, uint32_t y, uint8_t alpha)
{
    /* PSRAM only supports 16-bit aligned access, so we need read-modify-write */
    volatile uint16_t *ab16 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    uint32_t pixel_idx = y * DISP_IMAGE_WIDTH + x;
    uint32_t word_idx = pixel_idx / 2;
    uint32_t byte_pos = pixel_idx & 1;  /* 0=low byte, 1=high byte */
    
    uint16_t val = ab16[word_idx];
    if (byte_pos == 0) {
        val = (val & 0xFF00) | alpha;
    } else {
        val = (val & 0x00FF) | ((uint16_t)alpha << 8);
    }
    ab16[word_idx] = val;
}

/* Helper to set alpha for a rectangle border */
static void set_rect_alpha(int x1, int y1, int x2, int y2, uint8_t alpha)
{
    /* Clip coordinates */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= DISP_IMAGE_WIDTH) x2 = DISP_IMAGE_WIDTH - 1;
    if (y2 >= DISP_IMAGE_HEIGHT) y2 = DISP_IMAGE_HEIGHT - 1;

    if (x1 > x2 || y1 > y2) return;

    /* Draw top and bottom lines using PSRAM-safe 16-bit access */
    for (int x = x1; x <= x2; x++) {
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, alpha);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, alpha);
    }

    /* Draw left and right lines */
    for (int y = y1; y <= y2; y++) {
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, alpha);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, alpha);
    }
}

/* State to track the last drawn box for clearing */
static int32_t s_last_x1 = 0;
static int32_t s_last_y1 = 0;
static int32_t s_last_x2 = 0;
static int32_t s_last_y2 = 0;
static bool    s_has_last = false;
static uint32_t s_last_valid_ts = 0;

#define FACE_TIMEOUT_MS 200

/**
 * @brief 处理单个检测框
 */
static void process_detection_box(const DetectionBox_t *box)
{
    int32_t x1 = box->x1, y1 = box->y1, x2 = box->x2, y2 = box->y2;
    
    /* 坐标排序 */
    if (x2 < x1) { int32_t t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { int32_t t = y1; y1 = y2; y2 = t; }

    /* 有效性检查 */
    bool valid = true;
    if (x1 < 0 || y1 < 0 || x2 > FACE_COORD_SPACE_W || y2 > FACE_COORD_SPACE_H) valid = false;
    if ((x2 - x1) <= 2 || (y2 - y1) <= 2) valid = false;

    if (valid)
    {
        s_last_valid_ts = millis();

        /* Clear previous box if it exists */
        if (s_has_last) {
            set_rect_alpha(s_last_x1, s_last_y1, s_last_x2, s_last_y2, 0x00);
        }

        /* Draw new box (Opaque) */
        set_rect_alpha(x1, y1, x2, y2, 0xFF);

        /* Update state */
        s_last_x1 = x1;
        s_last_y1 = y1;
        s_last_x2 = x2;
        s_last_y2 = y2;
        s_has_last = true;
        
        FT_LOG(2, "[FT] box: (%ld,%ld)-(%ld,%ld) score=%.2f\r\n", 
               x1, y1, x2, y2, box->score);
    }
}

void face_tracker_poll(void)
{
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

        switch (msg_type) {
        case MAILBOX_MSG_TYPE_MULTI: {
            /* 新协议：多目标检测结果 */
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionResult_t *result = (const DetectionResult_t*)addr;
            
            /* 校验 magic */
            if (!DETECTION_RESULT_IS_VALID(result)) {
                FT_LOG(1, "[FT] Invalid result @0x%08lX (magic=0x%08lX)\r\n", 
                       (unsigned long)addr, (unsigned long)result->magic);
                break;
            }
            
            FT_LOG(2, "[FT] frame=%lu cnt=%lu sel=%ld\r\n", 
                   result->frame_id, result->count, result->selected_idx);
            
            /* 优先使用 DSP 选中的目标，否则使用第一个 */
            if (result->count > 0) {
                int idx = (result->selected_idx >= 0 && 
                          result->selected_idx < (int)result->count) 
                         ? result->selected_idx : 0;
                process_detection_box(&result->boxes[idx]);
            } else {
                /* 无检测目标，清除旧框 */
                if (s_has_last) {
                    set_rect_alpha(s_last_x1, s_last_y1, s_last_x2, s_last_y2, 0x00);
                    s_has_last = false;
                }
            }
            break;
        }
        
        case MAILBOX_MSG_TYPE_SINGLE: {
            /* 旧协议兼容：单目标 FaceRect */
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionBox_t *box = (const DetectionBox_t*)addr;
            process_detection_box(box);
            break;
        }
        
        case MAILBOX_MSG_TYPE_NO_RESULT:
            /* 本帧无检测结果 */
            FT_LOG(2, "[FT] No detection\r\n");
            break;
            
        default:
            FT_LOG(1, "[FT] Unknown msg type: 0x%08lX\r\n", (unsigned long)msg);
            break;
        }
    }

    /* Check for timeout */
    if (s_has_last && (millis() - s_last_valid_ts > FACE_TIMEOUT_MS))
    {
        set_rect_alpha(s_last_x1, s_last_y1, s_last_x2, s_last_y2, 0x00);
        s_has_last = false;
    }
}
