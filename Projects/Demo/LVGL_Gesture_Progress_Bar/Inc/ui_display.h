#pragma once
/*
 * 模块：ui_display（LVGL 显示绑定）
 */
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_display_t * ui_display_init(void);
void ui_display_set_bg_color(uint32_t rgb24);
void ui_request_refresh(void);

#ifdef __cplusplus
}
#endif
