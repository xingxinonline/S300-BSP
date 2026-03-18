#include "app_card3_subboard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_card3_result_handler.h"
#include "detection_proto.h"
#include "master_log.h"
#include "subboard_detection_result.h"
#include "subboard_startup_proto.h"

#define CARD3_POLL_MS 80u

static app_card3_subboard_ops_t s_ops;
static bool s_ops_ready = false;
static uint32_t s_last_poll_ms = 0u;
static bool s_card3_online = false;
static subboard_detection_result_t s_last_card3_result;

static uint32_t card3_now_ms(void)
{
    return (s_ops.millis_fn != NULL) ? s_ops.millis_fn() : 0u;
}

#if MASTER_LOG_LEVEL >= MASTER_LOG_LEVEL_DEBUG
static const char *detection_type_label(uint8_t raw_type)
{
    return detection_type_name(detection_type_from_raw(raw_type));
}
#endif

int app_card3_subboard_init(const app_card3_subboard_ops_t *ops)
{
    if ((ops == NULL) || (ops->millis_fn == NULL) || (ops->read_regs_at == NULL)) {
        return -1;
    }

    s_ops = *ops;
    s_ops_ready = true;
    app_card3_result_handler_init(s_ops.millis_fn);
    app_card3_subboard_reset();
    return 0;
}

void app_card3_subboard_reset(void)
{
    s_last_poll_ms = 0u;
    s_card3_online = false;
    memset(&s_last_card3_result, 0, sizeof(s_last_card3_result));
    app_card3_result_handler_notify_offline();
}

void app_card3_subboard_tick(void)
{
    subboard_detection_result_t result;
    uint32_t now_ms;

    if (!s_ops_ready) {
        return;
    }

    now_ms = card3_now_ms();
    if ((uint32_t)(now_ms - s_last_poll_ms) < CARD3_POLL_MS) {
        return;
    }
    s_last_poll_ms = now_ms;

    if (s_ops.read_regs_at(SUBBOARD_STARTUP_SLAVE_ADDR_CARD3,
                           SUBBOARD_STARTUP_REG_RESULT,
                           (uint8_t *)&result,
                           sizeof(result)) != 0) {
        if (s_card3_online) {
            MASTER_LOG_INFO("[MASTER][CARD3] offline\r\n");
            s_card3_online = false;
            memset(&s_last_card3_result, 0, sizeof(s_last_card3_result));
        }
        app_card3_result_handler_notify_offline();
        return;
    }

    if (!s_card3_online) {
        MASTER_LOG_INFO("[MASTER][CARD3] online at 0x%02X\r\n", SUBBOARD_STARTUP_SLAVE_ADDR_CARD3);
        s_card3_online = true;
    }

    if (memcmp(&result, &s_last_card3_result, sizeof(result)) != 0) {
        MASTER_LOG_DEBUG("[MASTER][CARD3] result valid=%u count=%u type=%s gesture=%u conf=%u\r\n",
                         (unsigned)result.valid,
                         (unsigned)result.count,
                         detection_type_label(result.type),
                         (unsigned)result.gesture_type,
                         (unsigned)result.confidence);
        s_last_card3_result = result;
    }

    app_card3_result_handler_handle_result(&result);
}