#ifndef APP_GIMBAL_MASTER_CARD2_SUBBOARD_H
#define APP_GIMBAL_MASTER_CARD2_SUBBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "subboard_detection_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*app_card2_subboard_millis_fn_t)(void);
typedef int (*app_card2_subboard_read_reg8_fn_t)(uint8_t slave_addr, uint8_t reg, uint8_t *value);
typedef int (*app_card2_subboard_read_regs_fn_t)(uint8_t slave_addr, uint8_t reg, uint8_t *buffer, uint32_t length);
typedef int (*app_card2_subboard_write_reg8_fn_t)(uint8_t slave_addr, uint8_t reg, uint8_t value);
typedef int (*app_card2_subboard_prepare_video_fn_t)(void);
typedef void (*app_card2_subboard_runtime_enable_fn_t)(void);
typedef bool (*app_card2_subboard_mm_request_gate_fn_t)(void);
typedef void (*app_card2_subboard_overlay_tick_fn_t)(void);
typedef bool (*app_card2_subboard_overlay_is_active_fn_t)(void);
typedef void (*app_card2_subboard_overlay_clear_fn_t)(void);
typedef void (*app_card2_subboard_overlay_draw_fn_t)(const subboard_detection_result_t *result);

typedef struct {
    app_card2_subboard_millis_fn_t millis_fn;
    app_card2_subboard_read_reg8_fn_t read_reg8_at;
    app_card2_subboard_read_regs_fn_t read_regs_at;
    app_card2_subboard_write_reg8_fn_t write_reg8_at;
    app_card2_subboard_prepare_video_fn_t prepare_video_path;
    app_card2_subboard_runtime_enable_fn_t trigger_mm_runtime_enable;
    app_card2_subboard_mm_request_gate_fn_t is_mm_request_allowed;
    app_card2_subboard_overlay_tick_fn_t overlay_tick;
    app_card2_subboard_overlay_is_active_fn_t overlay_is_active;
    app_card2_subboard_overlay_clear_fn_t overlay_clear;
    app_card2_subboard_overlay_draw_fn_t overlay_draw;
} app_card2_subboard_ops_t;

int app_card2_subboard_init(const app_card2_subboard_ops_t *ops);
void app_card2_subboard_reset(void);
void app_card2_subboard_tick(void);
bool app_card2_subboard_is_running(void);
uint8_t app_card2_subboard_get_public_state(void);

#ifdef __cplusplus
}
#endif

#endif