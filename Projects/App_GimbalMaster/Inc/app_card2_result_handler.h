#ifndef APP_GIMBAL_MASTER_CARD2_RESULT_HANDLER_H
#define APP_GIMBAL_MASTER_CARD2_RESULT_HANDLER_H

#include <stdbool.h>
#include <stdint.h>

#include "subboard_detection_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*app_card2_result_handler_millis_fn_t)(void);
typedef void (*app_card2_result_handler_overlay_tick_fn_t)(void);
typedef bool (*app_card2_result_handler_overlay_is_active_fn_t)(void);
typedef void (*app_card2_result_handler_overlay_clear_fn_t)(void);
typedef void (*app_card2_result_handler_overlay_draw_fn_t)(const subboard_detection_result_t *result);

typedef struct {
    uint32_t updated_ms;
    bool detection_active;
    uint8_t verify_state;
    uint8_t verify_score;
    uint8_t verify_flags;
    uint8_t face_id;
    uint8_t confidence;
    int32_t cx;
    int32_t cy;
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
} app_card2_verify_snapshot_t;

typedef struct {
    app_card2_result_handler_millis_fn_t millis_fn;
    app_card2_result_handler_overlay_tick_fn_t overlay_tick;
    app_card2_result_handler_overlay_is_active_fn_t overlay_is_active;
    app_card2_result_handler_overlay_clear_fn_t overlay_clear;
    app_card2_result_handler_overlay_draw_fn_t overlay_draw;
} app_card2_result_handler_ops_t;

int app_card2_result_handler_init(const app_card2_result_handler_ops_t *ops);
void app_card2_result_handler_reset(void);
void app_card2_result_handler_tick(void);
void app_card2_result_handler_handle_result(const subboard_detection_result_t *result);
bool app_card2_result_handler_get_snapshot(app_card2_verify_snapshot_t *out_snapshot);
const char *app_card2_result_handler_verify_state_name(uint8_t verify_state);

#ifdef __cplusplus
}
#endif

#endif