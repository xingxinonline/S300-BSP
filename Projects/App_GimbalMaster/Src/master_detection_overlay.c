#include "master_detection_overlay.h"

#include <stdbool.h>
#include <stdint.h>

#include "s300.h"
#include "video.h"

#define MASTER_BOX_TIMEOUT_MS 500u
#define MASTER_BOX_COLOR      0xFFE0u
#define MASTER_BOX_ALPHA      0xFFu

typedef struct {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
} DrawnBox_t;

static uint32_t (*s_get_millis)(void) = 0;
static DrawnBox_t s_drawn_box;
static bool s_box_drawn = false;
static uint32_t s_last_box_ms = 0u;

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
}

static void trigger_overlay_refresh(void)
{
    *(volatile uint32_t *)(DSP_VIDEO_SS_BASE + 0x50u) = 1u;
}

static void set_pixel_alpha(uint32_t x, uint32_t y, uint8_t alpha)
{
    volatile uint16_t *alpha_buf = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    uint32_t pixel_idx = y * DISP_IMAGE_WIDTH + x;
    uint32_t word_idx = pixel_idx / 2u;
    uint32_t byte_pos = pixel_idx & 1u;
    uint16_t value = alpha_buf[word_idx];

    if (byte_pos == 0u) {
        value = (uint16_t)((value & 0xFF00u) | alpha);
    } else {
        value = (uint16_t)((value & 0x00FFu) | ((uint16_t)alpha << 8));
    }

    alpha_buf[word_idx] = value;
}

static void set_pixel_color(uint32_t x, uint32_t y, uint16_t color)
{
    volatile uint16_t *framebuffer = (volatile uint16_t *)DISP_RFRAME0_ADDR;
    framebuffer[y * DISP_IMAGE_WIDTH + x] = color;
}

static void draw_rect_border(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                             uint16_t color, uint8_t alpha)
{
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 >= DISP_IMAGE_WIDTH) {
        x2 = DISP_IMAGE_WIDTH - 1;
    }
    if (y2 >= DISP_IMAGE_HEIGHT) {
        y2 = DISP_IMAGE_HEIGHT - 1;
    }
    if ((x1 > x2) || (y1 > y2)) {
        return;
    }

    for (int32_t x = x1; x <= x2; x++) {
        set_pixel_color((uint32_t)x, (uint32_t)y1, color);
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, alpha);
        set_pixel_color((uint32_t)x, (uint32_t)y2, color);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, alpha);
    }

    for (int32_t y = y1 + 1; y < y2; y++) {
        set_pixel_color((uint32_t)x1, (uint32_t)y, color);
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, alpha);
        set_pixel_color((uint32_t)x2, (uint32_t)y, color);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, alpha);
    }
}

static void clear_rect_border(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 >= DISP_IMAGE_WIDTH) {
        x2 = DISP_IMAGE_WIDTH - 1;
    }
    if (y2 >= DISP_IMAGE_HEIGHT) {
        y2 = DISP_IMAGE_HEIGHT - 1;
    }
    if ((x1 > x2) || (y1 > y2)) {
        return;
    }

    for (int32_t x = x1; x <= x2; x++) {
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, 0u);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, 0u);
    }

    for (int32_t y = y1 + 1; y < y2; y++) {
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, 0u);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, 0u);
    }
}

void master_detection_overlay_init(uint32_t (*get_millis_fn)(void))
{
    volatile uint16_t *alpha_buf = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    uint32_t alpha_words = (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT) / 2u;

    s_get_millis = get_millis_fn;
    for (uint32_t index = 0u; index < alpha_words; index++) {
        alpha_buf[index] = 0u;
    }

    s_box_drawn = false;
    s_last_box_ms = 0u;
    trigger_overlay_refresh();
}

void master_detection_overlay_clear(void)
{
    if (!s_box_drawn) {
        return;
    }

    clear_rect_border(s_drawn_box.x1,
                      s_drawn_box.y1,
                      s_drawn_box.x2,
                      s_drawn_box.y2);
    s_box_drawn = false;
    trigger_overlay_refresh();
}

void master_detection_overlay_draw(const subboard_detection_result_t *result)
{
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;

    if ((result == 0) || (result->valid == 0u)) {
        master_detection_overlay_clear();
        return;
    }

    x1 = result->x1;
    y1 = result->y1;
    x2 = result->x2;
    y2 = result->y2;

    if (x2 < x1) {
        int32_t temp = x1;
        x1 = x2;
        x2 = temp;
    }
    if (y2 < y1) {
        int32_t temp = y1;
        y1 = y2;
        y2 = temp;
    }

    master_detection_overlay_clear();
    draw_rect_border(x1, y1, x2, y2, MASTER_BOX_COLOR, MASTER_BOX_ALPHA);

    s_drawn_box.x1 = x1;
    s_drawn_box.y1 = y1;
    s_drawn_box.x2 = x2;
    s_drawn_box.y2 = y2;
    s_box_drawn = true;
    s_last_box_ms = millis();
    trigger_overlay_refresh();
}

void master_detection_overlay_tick(void)
{
    if (s_box_drawn && ((uint32_t)(millis() - s_last_box_ms) >= MASTER_BOX_TIMEOUT_MS)) {
        master_detection_overlay_clear();
    }
}

bool master_detection_overlay_is_active(void)
{
    return s_box_drawn;
}