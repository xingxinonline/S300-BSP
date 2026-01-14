#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

/* 32x32 RGB565 checkerboard demo image (no alpha) */
static const LV_ATTRIBUTE_MEM_ALIGN uint16_t img_demo_pixels[32 * 32] = {
    /* Generate a simple 4x4 checker pattern using two colors */
#define C0 0xF800 /* Red */
#define C1 0x07E0 /* Green */
#define ROW(px) px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px, px
    /* We will alternate 4x4 blocks of C0 and C1 */
#define BLOCK(a,b) a, a, a, a, b, b, b, b, a, a, a, a, b, b, b, b, a, a, a, a, b, b, b, b, a, a, a, a, b, b, b, b
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C0,C1),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
    BLOCK(C1,C0),
#undef BLOCK
#undef ROW
#undef C0
#undef C1
};

const lv_image_dsc_t img_demo = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 32,
        .h = 32,
        .stride = 32 * 2,
    },
    .data_size = sizeof(img_demo_pixels),
    .data = (const uint8_t*)img_demo_pixels,
};
