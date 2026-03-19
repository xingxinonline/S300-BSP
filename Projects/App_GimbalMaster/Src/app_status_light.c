#include "app_status_light.h"

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "master_log.h"
#include "s300.h"
#include "ws2812.h"

#define STATUS_LIGHT_LED_COUNT        5u
#define STATUS_LIGHT_TICK_MS          60u
#define STATUS_LIGHT_PULSE_MS         360u
#define STATUS_LIGHT_BRIGHTNESS_DIM   8u
#define STATUS_LIGHT_BRIGHTNESS_MID   24u
#define STATUS_LIGHT_BRIGHTNESS_HIGH  48u

#define STATUS_LIGHT_IDLE_MIN_BRIGHTNESS  6u
#define STATUS_LIGHT_IDLE_MAX_BRIGHTNESS  16u
#define STATUS_LIGHT_IDLE_STEP_MS         140u
#define STATUS_LIGHT_IDLE_DWELL_STEPS     3u

#define STATUS_LIGHT_FLAG_RECORDING   0x01u
#define STATUS_LIGHT_FLAG_TRACKING    0x02u
#define STATUS_LIGHT_FLAG_FILL_LIGHT  0x04u

static uint32_t (*s_get_millis)(void) = 0;
static ws2812_handle_t s_led_handle;
static ws2812_color_t s_chain1_buffer[STATUS_LIGHT_LED_COUNT];
static ws2812_color_t s_chain2_buffer[STATUS_LIGHT_LED_COUNT];
static bool s_initialized = false;
static app_status_light_mode_t s_mode = APP_STATUS_LIGHT_MODE_DISABLED;
static uint32_t s_last_tick_ms = 0u;
static uint32_t s_pulse_started_ms = 0u;
static uint32_t s_pulse_until_ms = 0u;
static app_status_light_feedback_t s_pulse_feedback = APP_STATUS_LIGHT_FEEDBACK_NONE;
static uint8_t s_runtime_flags = 0u;

static const char *mode_name(app_status_light_mode_t mode)
{
    switch (mode) {
    case APP_STATUS_LIGHT_MODE_DISABLED: return "DISABLED";
    case APP_STATUS_LIGHT_MODE_BOOT: return "BOOT";
    case APP_STATUS_LIGHT_MODE_WAIT_SUBBOARD: return "WAIT_SUBBOARD";
    case APP_STATUS_LIGHT_MODE_SUBBOARD_READY: return "SUBBOARD_READY";
    case APP_STATUS_LIGHT_MODE_KWS_RUNNING: return "KWS_RUNNING";
    case APP_STATUS_LIGHT_MODE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
}

static ws2812_color_t color_dim(ws2812_color_t color, uint8_t factor)
{
    ws2812_color_t result;
    result.r = (uint8_t)(((uint16_t)color.r * factor) / 255u);
    result.g = (uint8_t)(((uint16_t)color.g * factor) / 255u);
    result.b = (uint8_t)(((uint16_t)color.b * factor) / 255u);
    return result;
}

static void clear_all(void)
{
    (void)ws2812_clear(&s_led_handle, 0);
    (void)ws2812_clear(&s_led_handle, 1);
}

static void fill_all(ws2812_color_t color)
{
    (void)ws2812_fill(&s_led_handle, 0, color);
    (void)ws2812_fill(&s_led_handle, 1, color);
}

static void fill_chain(uint8_t chain_idx, ws2812_color_t color)
{
    (void)ws2812_fill(&s_led_handle, chain_idx, color);
}

static uint8_t idle_breath_brightness(uint32_t now_ms)
{
    uint32_t ramp_span = STATUS_LIGHT_IDLE_MAX_BRIGHTNESS - STATUS_LIGHT_IDLE_MIN_BRIGHTNESS;
    uint32_t cycle_steps = ramp_span * 2u + STATUS_LIGHT_IDLE_DWELL_STEPS;
    uint32_t step = (now_ms / STATUS_LIGHT_IDLE_STEP_MS) % cycle_steps;

    if (step < ramp_span) {
        return (uint8_t)(STATUS_LIGHT_IDLE_MIN_BRIGHTNESS + step);
    }

    if (step < (ramp_span * 2u)) {
        return (uint8_t)(STATUS_LIGHT_IDLE_MAX_BRIGHTNESS - (step - ramp_span));
    }

    return STATUS_LIGHT_IDLE_MIN_BRIGHTNESS;
}

static void draw_boot(uint32_t now_ms)
{
    uint8_t phase = (uint8_t)((now_ms / 120u) % STATUS_LIGHT_LED_COUNT);
    clear_all();
    (void)ws2812_set_pixel(&s_led_handle, 0, phase, ws2812_rgb(0u, 200u, 255u));
    (void)ws2812_set_pixel(&s_led_handle, 1, STATUS_LIGHT_LED_COUNT - 1u - phase, ws2812_rgb(0u, 200u, 255u));
    ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_HIGH);
}

static void draw_wait_subboard(uint32_t now_ms)
{
    ws2812_color_t amber = ws2812_rgb(255u, 150u, 0u);
    uint32_t phase = (now_ms / 400u) & 1u;
    fill_all(amber);
    ws2812_set_brightness(&s_led_handle,
                          phase == 0u ? STATUS_LIGHT_BRIGHTNESS_MID : STATUS_LIGHT_BRIGHTNESS_DIM);
}

static void draw_subboard_ready(uint32_t now_ms)
{
    uint8_t breathe = (uint8_t)(16u + ((now_ms / 80u) % 16u));
    fill_all(ws2812_rgb(40u, 120u, 255u));
    ws2812_set_brightness(&s_led_handle, breathe);
}

static void draw_kws_running(uint32_t now_ms)
{
    uint8_t phase = (uint8_t)((now_ms / 100u) % STATUS_LIGHT_LED_COUNT);
    uint8_t breathe = (uint8_t)(18u + ((now_ms / 90u) % 18u));
    bool recording = (s_runtime_flags & STATUS_LIGHT_FLAG_RECORDING) != 0u;
    bool tracking = (s_runtime_flags & STATUS_LIGHT_FLAG_TRACKING) != 0u;
    bool fill_light = (s_runtime_flags & STATUS_LIGHT_FLAG_FILL_LIGHT) != 0u;

    clear_all();

    if (!recording && !tracking && !fill_light) {
        fill_all(ws2812_rgb(0u, 255u, 0u));
        ws2812_set_brightness(&s_led_handle, idle_breath_brightness(now_ms));
        return;
    }

    if (fill_light) {
        (void)ws2812_set_pixel(&s_led_handle, 0, 0u, ws2812_rgb(255u, 210u, 120u));
        (void)ws2812_set_pixel(&s_led_handle, 0, STATUS_LIGHT_LED_COUNT - 1u, ws2812_rgb(255u, 210u, 120u));
        (void)ws2812_set_pixel(&s_led_handle, 1, 0u, ws2812_rgb(255u, 210u, 120u));
        (void)ws2812_set_pixel(&s_led_handle, 1, STATUS_LIGHT_LED_COUNT - 1u, ws2812_rgb(255u, 210u, 120u));
    }

    if (recording && tracking) {
        fill_chain(0, color_dim(ws2812_rgb(255u, 0u, 0u), 56u));
        ws2812_set_brightness(&s_led_handle, breathe);
        fill_chain(1, color_dim(ws2812_rgb(0u, 120u, 255u), 36u));
        (void)ws2812_set_pixel(&s_led_handle, 1, phase, ws2812_rgb(0u, 120u, 255u));
        (void)ws2812_set_pixel(&s_led_handle, 1, STATUS_LIGHT_LED_COUNT - 1u - phase, ws2812_rgb(0u, 120u, 255u));
        return;
    }

    if (recording) {
        fill_all(color_dim(ws2812_rgb(255u, 0u, 0u), 48u));
        if (fill_light) {
            (void)ws2812_set_pixel(&s_led_handle, 0, 0u, ws2812_rgb(255u, 210u, 120u));
            (void)ws2812_set_pixel(&s_led_handle, 1, STATUS_LIGHT_LED_COUNT - 1u, ws2812_rgb(255u, 210u, 120u));
        }
        ws2812_set_brightness(&s_led_handle, breathe);
        return;
    }

    if (tracking) {
        fill_all(color_dim(ws2812_rgb(0u, 120u, 255u), 36u));
        if (fill_light) {
            (void)ws2812_set_pixel(&s_led_handle, 0, 0u, ws2812_rgb(255u, 210u, 120u));
            (void)ws2812_set_pixel(&s_led_handle, 1, STATUS_LIGHT_LED_COUNT - 1u, ws2812_rgb(255u, 210u, 120u));
        }
        (void)ws2812_set_pixel(&s_led_handle, 0, phase, ws2812_rgb(0u, 120u, 255u));
        (void)ws2812_set_pixel(&s_led_handle, 1, STATUS_LIGHT_LED_COUNT - 1u - phase, ws2812_rgb(0u, 120u, 255u));
        ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_HIGH);
        return;
    }

    fill_all(ws2812_rgb(255u, 210u, 120u));
    ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_MID);
}

static void draw_error(uint32_t now_ms)
{
    uint32_t phase = (now_ms / 160u) & 1u;
    fill_all(ws2812_rgb(255u, 0u, 0u));
    ws2812_set_brightness(&s_led_handle,
                          phase == 0u ? STATUS_LIGHT_BRIGHTNESS_HIGH : 0u);
}

static void draw_keyword_pulse(uint32_t now_ms)
{
    uint32_t elapsed_ms = now_ms - s_pulse_started_ms;
    uint8_t phase = (uint8_t)((elapsed_ms / 80u) % STATUS_LIGHT_LED_COUNT);
    uint8_t blink = (uint8_t)((elapsed_ms / 60u) & 1u);

    clear_all();
    switch (s_pulse_feedback) {
    case APP_STATUS_LIGHT_FEEDBACK_PHOTO:
        if (blink == 0u) {
            fill_all(ws2812_rgb(255u, 255u, 255u));
        }
        ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_HIGH);
        break;
    case APP_STATUS_LIGHT_FEEDBACK_RECORD_START:
        fill_all(color_dim(ws2812_rgb(255u, 0u, 0u), 48u));
        for (uint8_t index = 0u; index <= phase; ++index) {
            (void)ws2812_set_pixel(&s_led_handle, 0, index, ws2812_rgb(255u, 0u, 0u));
            (void)ws2812_set_pixel(&s_led_handle, 1, index, ws2812_rgb(255u, 0u, 0u));
        }
        ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_HIGH);
        break;
    case APP_STATUS_LIGHT_FEEDBACK_RECORD_STOP:
        fill_all(color_dim(ws2812_rgb(255u, 100u, 0u), 36u));
        for (uint8_t index = 0u; index <= phase; ++index) {
            uint8_t reverse_index = STATUS_LIGHT_LED_COUNT - 1u - index;
            (void)ws2812_set_pixel(&s_led_handle, 0, reverse_index, ws2812_rgb(255u, 120u, 0u));
            (void)ws2812_set_pixel(&s_led_handle, 1, reverse_index, ws2812_rgb(255u, 120u, 0u));
        }
        ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_HIGH);
        break;
    case APP_STATUS_LIGHT_FEEDBACK_TRACK_START:
        fill_all(color_dim(ws2812_rgb(0u, 120u, 255u), 40u));
        (void)ws2812_set_pixel(&s_led_handle, 0, phase, ws2812_rgb(0u, 120u, 255u));
        (void)ws2812_set_pixel(&s_led_handle, 1, STATUS_LIGHT_LED_COUNT - 1u - phase, ws2812_rgb(0u, 120u, 255u));
        ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_HIGH);
        break;
    case APP_STATUS_LIGHT_FEEDBACK_TRACK_STOP:
        fill_all(ws2812_rgb(255u, 220u, 0u));
        if (blink != 0u) {
            clear_all();
        }
        ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_HIGH);
        break;
    case APP_STATUS_LIGHT_FEEDBACK_FILL_LIGHT_ON:
        fill_all(ws2812_rgb(255u, 210u, 120u));
        ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_HIGH);
        break;
    case APP_STATUS_LIGHT_FEEDBACK_FILL_LIGHT_OFF:
        (void)ws2812_set_pixel(&s_led_handle, 0, 0u, ws2812_rgb(0u, 160u, 255u));
        (void)ws2812_set_pixel(&s_led_handle, 0, STATUS_LIGHT_LED_COUNT - 1u, ws2812_rgb(0u, 160u, 255u));
        (void)ws2812_set_pixel(&s_led_handle, 1, 0u, ws2812_rgb(0u, 160u, 255u));
        (void)ws2812_set_pixel(&s_led_handle, 1, STATUS_LIGHT_LED_COUNT - 1u, ws2812_rgb(0u, 160u, 255u));
        ws2812_set_brightness(&s_led_handle, blink == 0u ? STATUS_LIGHT_BRIGHTNESS_HIGH : STATUS_LIGHT_BRIGHTNESS_DIM);
        break;
    default:
        fill_all(ws2812_rgb(80u, 255u, 80u));
        ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_HIGH);
        break;
    }
}

int app_status_light_init(uint32_t (*get_millis_fn)(void))
{
    int ret;

    s_get_millis = get_millis_fn;
    ret = ws2812_init(&s_led_handle);
    if (ret < 0) {
        return ret;
    }

    ret = ws2812_add_chain(&s_led_handle,
                           (void *)GPIO_BASE,
                           BOARD_RGB_DIN1_PIN,
                           STATUS_LIGHT_LED_COUNT,
                           s_chain1_buffer);
    if (ret < 0) {
        return ret;
    }

    ret = ws2812_add_chain(&s_led_handle,
                           (void *)GPIO_BASE,
                           BOARD_RGB_DIN2_PIN,
                           STATUS_LIGHT_LED_COUNT,
                           s_chain2_buffer);
    if (ret < 0) {
        return ret;
    }

    clear_all();
    ws2812_set_brightness(&s_led_handle, STATUS_LIGHT_BRIGHTNESS_DIM);
    (void)ws2812_show_all(&s_led_handle);
    s_initialized = true;
    s_mode = APP_STATUS_LIGHT_MODE_BOOT;
    s_last_tick_ms = 0u;
    s_pulse_started_ms = 0u;
    s_pulse_until_ms = 0u;
    s_pulse_feedback = APP_STATUS_LIGHT_FEEDBACK_NONE;
    s_runtime_flags = 0u;
    return 0;
}

void app_status_light_set_mode(app_status_light_mode_t mode)
{
    if (s_mode != mode) {
        MASTER_LOG_INFO("[MASTER][LIGHT] mode %s -> %s\r\n",
                        mode_name(s_mode),
                        mode_name(mode));
    }
    s_mode = mode;
}

void app_status_light_set_recording(bool enabled)
{
    if (enabled) {
        s_runtime_flags |= STATUS_LIGHT_FLAG_RECORDING;
    } else {
        s_runtime_flags &= (uint8_t)(~STATUS_LIGHT_FLAG_RECORDING);
    }
}

void app_status_light_set_tracking(bool enabled)
{
    if (enabled) {
        s_runtime_flags |= STATUS_LIGHT_FLAG_TRACKING;
    } else {
        s_runtime_flags &= (uint8_t)(~STATUS_LIGHT_FLAG_TRACKING);
    }
}

void app_status_light_set_fill_light(bool enabled)
{
    if (enabled) {
        s_runtime_flags |= STATUS_LIGHT_FLAG_FILL_LIGHT;
    } else {
        s_runtime_flags &= (uint8_t)(~STATUS_LIGHT_FLAG_FILL_LIGHT);
    }
}

void app_status_light_notify_feedback(app_status_light_feedback_t feedback)
{
    s_pulse_started_ms = millis();
    s_pulse_until_ms = s_pulse_started_ms + STATUS_LIGHT_PULSE_MS;
    s_pulse_feedback = feedback;
}

void app_status_light_tick(void)
{
    uint32_t now_ms;

    if (!s_initialized) {
        return;
    }

    now_ms = millis();
    if ((uint32_t)(now_ms - s_last_tick_ms) < STATUS_LIGHT_TICK_MS) {
        return;
    }
    s_last_tick_ms = now_ms;

    if ((s_pulse_until_ms != 0u) && ((int32_t)(s_pulse_until_ms - now_ms) > 0)) {
        draw_keyword_pulse(now_ms);
        (void)ws2812_show_all(&s_led_handle);
        return;
    }
    s_pulse_until_ms = 0u;
    s_pulse_feedback = APP_STATUS_LIGHT_FEEDBACK_NONE;

    switch (s_mode) {
    case APP_STATUS_LIGHT_MODE_BOOT:
        draw_boot(now_ms);
        break;
    case APP_STATUS_LIGHT_MODE_WAIT_SUBBOARD:
        draw_wait_subboard(now_ms);
        break;
    case APP_STATUS_LIGHT_MODE_SUBBOARD_READY:
        draw_subboard_ready(now_ms);
        break;
    case APP_STATUS_LIGHT_MODE_KWS_RUNNING:
        draw_kws_running(now_ms);
        break;
    case APP_STATUS_LIGHT_MODE_ERROR:
        draw_error(now_ms);
        break;
    case APP_STATUS_LIGHT_MODE_DISABLED:
    default:
        clear_all();
        ws2812_set_brightness(&s_led_handle, 0u);
        break;
    }

    (void)ws2812_show_all(&s_led_handle);
}