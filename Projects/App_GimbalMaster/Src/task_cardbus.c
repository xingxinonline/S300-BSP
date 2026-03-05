#include "task_cardbus.h"

#include "app_config.h"
#include "app_log.h"
#include "display_overlay.h"
#include "track_events.h"
#include "track_state.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <string.h>

#ifndef APP_CARDBUS_TASK_STACK_WORDS
#define APP_CARDBUS_TASK_STACK_WORDS    256U
#endif

#ifndef APP_CARDBUS_TASK_PRIORITY
#define APP_CARDBUS_TASK_PRIORITY       (tskIDLE_PRIORITY + 4)
#endif

#ifndef APP_CARDBUS_POLL_PERIOD_MS
#define APP_CARDBUS_POLL_PERIOD_MS      20U
#endif

#ifndef APP_CARDBUS_LOST_TIMEOUT_MS
#define APP_CARDBUS_LOST_TIMEOUT_MS     300U
#endif

#ifndef APP_CARDBUS_GESTURE_COOLDOWN_MS
#define APP_CARDBUS_GESTURE_COOLDOWN_MS 600U
#endif

#define GESTURE_PALM_OLD    1u
#define GESTURE_PEACE_OLD   2u
#define GESTURE_PALM_NEW    5u
#define GESTURE_PEACE_NEW   6u

static TaskHandle_t g_cardbus_task_handle;
static SemaphoreHandle_t g_snapshot_lock;
static cardbus_snapshot_t g_snapshot;

static uint8_t g_target_present;
static TickType_t g_last_target_tick;
static TickType_t g_last_gesture_tick;
static uint8_t g_card1_start_sent;
static uint8_t g_card1_wait_display_logged;

static bool post_event_safe(TrackEvent_t evt)
{
    if (!track_events_post(evt)) {
        app_log_printf("[CARDBUS] event queue full, evt=%s\r\n", track_event_to_string(evt));
        return false;
    }
    return true;
}

static void handle_target_presence(uint8_t present)
{
    TickType_t now = xTaskGetTickCount();

    if (present != 0u) {
        g_last_target_tick = now;
        if (g_target_present == 0u) {
            g_target_present = 1u;
            (void)post_event_safe(TRACK_EVT_TARGET_FOUND);
        }
        return;
    }

    if (g_target_present != 0u) {
        if ((now - g_last_target_tick) >= pdMS_TO_TICKS(APP_CARDBUS_LOST_TIMEOUT_MS)) {
            g_target_present = 0u;
            (void)post_event_safe(TRACK_EVT_TARGET_LOST);
        }
    }
}

static void handle_gesture(uint8_t gesture_type)
{
    TickType_t now;

    if (gesture_type == 0u) {
        return;
    }

    now = xTaskGetTickCount();
    if ((now - g_last_gesture_tick) < pdMS_TO_TICKS(APP_CARDBUS_GESTURE_COOLDOWN_MS)) {
        return;
    }

    if (gesture_type == GESTURE_PALM_OLD || gesture_type == GESTURE_PALM_NEW) {
        if (track_state_get() == TRACK_STATE_IDLE) {
            (void)post_event_safe(TRACK_EVT_START);
            app_log_puts("[CARDBUS] gesture=Palm -> START\r\n");
        } else {
            (void)post_event_safe(TRACK_EVT_STOP);
            app_log_puts("[CARDBUS] gesture=Palm -> STOP\r\n");
        }
        g_last_gesture_tick = now;
        return;
    }

    if (gesture_type == GESTURE_PEACE_OLD || gesture_type == GESTURE_PEACE_NEW) {
        (void)post_event_safe(TRACK_EVT_PHOTO);
        app_log_puts("[CARDBUS] gesture=Peace -> PHOTO\r\n");
        g_last_gesture_tick = now;
    }
}

static void publish_snapshot(const cardbus_snapshot_t *snap)
{
    if (xSemaphoreTake(g_snapshot_lock, pdMS_TO_TICKS(2)) == pdTRUE) {
        g_snapshot = *snap;
        xSemaphoreGive(g_snapshot_lock);
    }
}

static uint8_t update_one_card(uint8_t addr, card_detection_t *dst)
{
    if (i2c_cardbus_read_detection(addr, dst) == 0) {
        return 1u;
    }

    memset(dst, 0, sizeof(*dst));
    return 0u;
}

static void task_cardbus_entry(void *arg)
{
    cardbus_snapshot_t snap;
    TickType_t last_wake;
    uint8_t online_mask;
    uint8_t target_present;
    TickType_t t0;
    TickType_t t1;

    (void)arg;

    memset(&snap, 0, sizeof(snap));

    app_log_puts("[CARDBUS] task started\r\n");
    last_wake = xTaskGetTickCount();

    for (;;) {
        t0 = xTaskGetTickCount();

        online_mask = 0u;
        online_mask |= (uint8_t)(update_one_card(CARDBUS_CARD1_ADDR, &snap.card1) << 0);
        online_mask |= (uint8_t)(update_one_card(CARDBUS_CARD2_ADDR, &snap.card2) << 1);
        online_mask |= (uint8_t)(update_one_card(CARDBUS_CARD3_ADDR, &snap.card3) << 2);

        if ((g_card1_start_sent == 0u) && ((online_mask & 0x01u) != 0u)) {
#if APP_DISPLAY_INIT_ENABLE
            if (display_overlay_is_ready() == 0) {
                if (g_card1_wait_display_logged == 0u) {
                    app_log_puts("[CARDBUS] wait display/video ready before card1 start\r\n");
                    g_card1_wait_display_logged = 1u;
                }
            } else
#endif
            {
                if (i2c_cardbus_write_reg8(CARDBUS_CARD1_ADDR,
                                           CARDBUS_REG_CMD,
                                           CARDBUS_CMD_START_MM_DSP) == 0) {
                    app_log_puts("[CARDBUS] card1 start command sent (after display ready)\r\n");
                    g_card1_start_sent = 1u;
                } else {
                    app_log_puts("[CARDBUS] card1 start command failed\r\n");
                }
            }
        }

        target_present = (uint8_t)((snap.card1.valid != 0u) || (snap.card2.valid != 0u));

        handle_target_presence(target_present);
        handle_gesture(snap.card3.gesture_type);

        t1 = xTaskGetTickCount();
        snap.online_mask = online_mask;
        snap.target_present = g_target_present;
        snap.timestamp_ms = (uint32_t)(t1 * portTICK_PERIOD_MS);
        snap.cycle_time_us = (uint32_t)((t1 - t0) * portTICK_PERIOD_MS * 1000u);

        publish_snapshot(&snap);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_CARDBUS_POLL_PERIOD_MS));
    }
}

int task_cardbus_init(void)
{
    int ret;
    int p1;
    int p2;
    int p3;

    if (g_snapshot_lock == NULL) {
        g_snapshot_lock = xSemaphoreCreateMutex();
        if (g_snapshot_lock == NULL) {
            app_log_puts("[CARDBUS] snapshot lock create failed\r\n");
            return -1;
        }
    }

    ret = i2c_cardbus_init();
    if (ret != 0) {
        app_log_puts("[CARDBUS] i2c init failed\r\n");
        return -1;
    }

    p1 = i2c_cardbus_probe(CARDBUS_CARD1_ADDR);
    p2 = i2c_cardbus_probe(CARDBUS_CARD2_ADDR);
    p3 = i2c_cardbus_probe(CARDBUS_CARD3_ADDR);

    app_log_printf("[CARDBUS] probe: 0x10=%d 0x11=%d 0x12=%d\r\n", p1, p2, p3);

    g_target_present = 0u;
    g_last_target_tick = xTaskGetTickCount();
    g_last_gesture_tick = 0u;
    g_card1_start_sent = 0u;
    g_card1_wait_display_logged = 0u;
    memset(&g_snapshot, 0, sizeof(g_snapshot));

    return 0;
}

int task_cardbus_start(void)
{
    BaseType_t ret;

    if (g_cardbus_task_handle != NULL) {
        return 0;
    }

    ret = xTaskCreate(task_cardbus_entry,
                      "cardbus",
                      APP_CARDBUS_TASK_STACK_WORDS,
                      NULL,
                      APP_CARDBUS_TASK_PRIORITY,
                      &g_cardbus_task_handle);

    if (ret != pdPASS) {
        app_log_puts("[CARDBUS] task create failed\r\n");
        return -1;
    }

    return 0;
}

bool task_cardbus_get_snapshot(cardbus_snapshot_t *out)
{
    if (out == NULL || g_snapshot_lock == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_snapshot_lock, pdMS_TO_TICKS(2)) != pdTRUE) {
        return false;
    }

    *out = g_snapshot;
    xSemaphoreGive(g_snapshot_lock);
    return true;
}
