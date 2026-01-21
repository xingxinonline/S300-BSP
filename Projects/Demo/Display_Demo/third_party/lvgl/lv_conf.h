#ifndef LV_CONF_H
#define LV_CONF_H

/* Minimal LVGL v9 config for bare-metal RGB565 framebuffer without OS */

#define LV_USE_OS             LV_OS_NONE
#define LV_TICK_CUSTOM        1
#define LV_TICK_CUSTOM_INCLUDE "s300.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (0) /* we call lv_tick_inc in SysTick */

/* Display */
#define LV_COLOR_DEPTH        16
#define LV_COLOR_16_SWAP      0
#define LV_COLOR_SCREEN_TRANSP 0

/* No GPU */
#define LV_USE_DRAW_SW        1
#define LV_DRAW_SW_COMPLEX    1

/* Features */
#define LV_USE_LOG            0
/* When logging is off, set level to NONE to avoid redefinition warnings */
#define LV_LOG_LEVEL          LV_LOG_LEVEL_NONE

/* Enable object transform (rotation/scale) for overlay vertical text, etc. */
#ifndef LV_USE_TRANSFORM
#define LV_USE_TRANSFORM      1
#endif

/* Enable GIF decoder and widget */
#ifndef LV_USE_GIF
#define LV_USE_GIF            1
#endif

/* Default display resolution (can be overridden at runtime) */
#define LV_HOR_RES_MAX        128
#define LV_VER_RES_MAX        160

/* Fonts: minimize text footprint. Keep only very small UNSCII 8 as default. */
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_28 1  /* Required for LVGL_Gesture_Progress_Bar */
#define LV_FONT_UNSCII_8      1
#define LV_FONT_DEFAULT       &lv_font_unscii_8

/* Memory: use static internal heap placed into MM SRAM window.
 * Reserve a 116KB block at [0x44040000, 0x4405D000) and give 100KB to LVGL.
 * This avoids overlap with display frame buffers starting at 0x4405D000
 * (see video.h: DISP_WFRAME*_ADDR = 0x44080000 - 128*160*7 = 0x4405D000).
 */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_MEM_SIZE           (128U * 1024U)
#define LVGL_MEM_ADDR         (0x44040000U)
/* LVGL expects LV_MEM_ADR for fixed heap placement when using built-in malloc */
#define LV_MEM_ADR            (LVGL_MEM_ADDR)

/* Input devices off for now */
#define LV_USE_INDEV          0

/* Widgets on (defaults OK). Explicitly disable Label to remove text rendering. */
#define LV_USE_WIDGETS        1
/* Keep label enabled because image/gif widgets require it even if we don't use text at runtime */
#define LV_USE_LABEL          1

#endif /* LV_CONF_H */
