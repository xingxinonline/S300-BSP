#ifndef SUBBOARD_MM_APP_INTERNAL_H
#define SUBBOARD_MM_APP_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "subboard_detection_result.h"

typedef struct {
    uint32_t (*get_millis)(void);
    uint8_t public_state;
    bool mm_started;
    bool dsp_pll_started;
    bool master_mm_requested;
    bool master_mm_granted;
    bool result_active;
    uint8_t active_request;
    subboard_detection_result_t last_published_result;
    uint32_t last_heartbeat_ms;
    uint8_t last_logged_state;
} SubboardMmAppContext;

uint32_t subboard_mm_app_millis(const SubboardMmAppContext *ctx);
const char *subboard_mm_app_public_state_name(uint8_t state);
void subboard_mm_app_refresh_dsp_resource_flags(SubboardMmAppContext *ctx);
void subboard_mm_app_enter_public_state(SubboardMmAppContext *ctx, uint8_t next_state);
void subboard_mm_app_reset_request_path(SubboardMmAppContext *ctx);
void subboard_mm_app_post_next_master_request(SubboardMmAppContext *ctx);
void subboard_mm_app_consume_request_ack(SubboardMmAppContext *ctx, uint8_t request_ack);
void subboard_mm_app_sync_public_state_from_dsp(SubboardMmAppContext *ctx);
void subboard_mm_app_publish_latest_result(SubboardMmAppContext *ctx);
void subboard_mm_app_bump_heartbeat_if_needed(SubboardMmAppContext *ctx);
void subboard_mm_app_log_public_state_if_needed(SubboardMmAppContext *ctx);
int subboard_mm_app_start_mm_consumer(SubboardMmAppContext *ctx);
int subboard_mm_app_ensure_dsp_pll_started(SubboardMmAppContext *ctx);
void subboard_mm_app_handle_command(SubboardMmAppContext *ctx, uint8_t cmd);

#endif /* SUBBOARD_MM_APP_INTERNAL_H */