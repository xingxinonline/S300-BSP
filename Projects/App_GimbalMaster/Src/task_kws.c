/**
 * @file task_kws.c
 * @brief KWS 任务实现（对齐 KWS_CM4_Demo 轮询握手流程）
 */

#include "task_kws.h"

#include "app_config.h"
#include "app_log.h"
#include "track_events.h"

#include "FreeRTOS.h"
#include "task.h"

#include "s300.h"
#include "rcc.h"
#include "board.h"
#include "audio_app.h"
#include "audio_codec.h"
#include "gpio.h"
#include "mailbox.h"
#include "mailbox_proto.h"
#include "kws_proto.h"
#include "i2c_soft.h"
#include "ov5640.h"

#include <stdint.h>
#include <stdbool.h>

#define DSP_AUDIO_IN_ADDR               0x44040000u
#define DSP_AUDIO_OUT_ADDR              0x44040200u
#define DSP_KWS_CMD_ADDR                0x44040400u
#define DSP_READY_FLAG_ADDR             0x44040430u
#define DSP_INPUT_READY_FLAG_ADDR       0x44040434u
#define DSP_OUTPUT_READY_FLAG_ADDR      0x44040438u

#define DSP_READY_WAIT_LOOPS            20000000u
#define DSP_OUTPUT_WAIT_LOOPS           600000u
#define KWS_WARN_LOG_INTERVAL_FRAMES    128u
#define KWS_SCORE_THRESHOLD             82u
#define KWS_CONSECUTIVE_TRIGGER_COUNT   3u

static TaskHandle_t g_kws_task_handle = NULL;

static volatile int16_t *g_dsp_audio_in = (volatile int16_t *)DSP_AUDIO_IN_ADDR;
static volatile int16_t *g_dsp_audio_out = (volatile int16_t *)DSP_AUDIO_OUT_ADDR;
static volatile uint8_t *g_dsp_kws_cmd = (volatile uint8_t *)DSP_KWS_CMD_ADDR;
static volatile uint32_t *g_dsp_ready_flag = (volatile uint32_t *)DSP_READY_FLAG_ADDR;
static volatile uint32_t *g_dsp_input_ready_flag = (volatile uint32_t *)DSP_INPUT_READY_FLAG_ADDR;
static volatile uint32_t *g_dsp_output_ready_flag = (volatile uint32_t *)DSP_OUTPUT_READY_FLAG_ADDR;

static int16_t g_audio_block_right[AUDIO_BLOCK_LEN];
static uint32_t g_dsp_ready_timeout_cnt;
static uint32_t g_dsp_output_timeout_cnt;
static uint16_t g_kws_consecutive_count;
static uint16_t g_kws_target_idx;
static uint8_t g_kws_counting_started;
static TickType_t g_kws_last_emit_tick[KWS_KEYWORD_MAX];
static i2c_soft_t g_cam_i2c;
static uint8_t g_cam_i2c_ready;
static uint8_t g_cam_saddr = OV5640_I2C_ADDR;

static int kws_camera_i2c_init_if_needed(void)
{
    i2c_soft_cfg_t cfg;
    int p3c;
    int p3d;
    int ret;

    if (g_cam_i2c_ready) {
        return 0;
    }

    cfg.port = BOARD_CAMERA_I2C_PORT;
    cfg.pin_scl = BOARD_CAMERA_I2C_SCL_PIN;
    cfg.pin_sda = BOARD_CAMERA_I2C_SDA_PIN;
    cfg.func_scl = FUNCTION_2;
    cfg.func_sda = FUNCTION_2;
    cfg.pull_mode = GPIO_UP;
    cfg.bus_hz = BOARD_CAMERA_I2C_FREQ;

    ret = i2c_soft_init(&g_cam_i2c, &cfg, SystemCoreClock);
    if (ret != 0) {
        app_log_printf("[KWS][LIGHT] i2c init fail=%d\r\n", ret);
        return -1;
    }

    i2c_soft_bus_recover(&g_cam_i2c);
    p3c = i2c_soft_probe(&g_cam_i2c, 0x3C);
    p3d = i2c_soft_probe(&g_cam_i2c, 0x3D);

    if ((p3c != 0) && (p3d != 0)) {
        app_log_puts("[KWS][LIGHT] OV5640 not found on 0x3C/0x3D\r\n");
        return -1;
    }

    g_cam_saddr = (p3c == 0) ? 0x3C : 0x3D;
    g_cam_i2c_ready = 1u;

    app_log_printf("[KWS][LIGHT] camera i2c ready, saddr=0x%02X\r\n", (unsigned int)g_cam_saddr);
    return 0;
}

static int kws_set_fill_light(uint8_t enable)
{
    int ret;

    if (kws_camera_i2c_init_if_needed() != 0) {
        return -1;
    }

    ret = ov5640_set_light(&g_cam_i2c, g_cam_saddr, enable != 0u);
    if (ret == 0) {
        return 0;
    }

    i2c_soft_bus_recover(&g_cam_i2c);
    ret = ov5640_set_light(&g_cam_i2c, g_cam_saddr, enable != 0u);
    return ret;
}

static void handle_light_keyword(uint16_t keyword, uint8_t emit)
{
    int ret;

    if (!emit) {
        return;
    }

    if (keyword == KWS_KEYWORD_LIGHT_ON) {
        ret = kws_set_fill_light(1u);
        if (ret == 0) {
            app_log_puts("[KWS][LIGHT] ON\r\n");
        } else {
            app_log_printf("[KWS][LIGHT] ON failed=%d\r\n", ret);
        }
        return;
    }

    if (keyword == KWS_KEYWORD_LIGHT_OFF) {
        ret = kws_set_fill_light(0u);
        if (ret == 0) {
            app_log_puts("[KWS][LIGHT] OFF\r\n");
        } else {
            app_log_printf("[KWS][LIGHT] OFF failed=%d\r\n", ret);
        }
    }
}

__attribute__((noinline)) void dsp_firmware_load_point(void)
{
    __asm volatile("nop");
}

static uint8_t wait_flag_set(volatile uint32_t *flag, uint32_t wait_loops)
{
    uint32_t loop = 0;

    while ((*flag == 0u) && (loop < wait_loops)) {
        loop++;
    }

    return (*flag != 0u) ? 1u : 0u;
}

static void kws_mic_switch_gpio_init(void)
{
#if defined(BOARD_AUDIO_CODEC_ES7210)
    gpio_set_function(GPIOA, 31u, FUNCTION_2);
    gpio_set_direction(GPIOA, 31u, 0u);
    gpio_set_mode(GPIOA, 31u, GPIO_DOWN);
#endif
}

void DMA0_IRQHandler(void)
{
    audio_dma0_irq_handler();
}

static int kws_audio_init(void)
{
    int ret;

    kws_mic_switch_gpio_init();

    ret = audio_init();
    if (ret != 0) {
        app_log_printf("[AUDIO] audio_init failed=%d\r\n", ret);
        return -1;
    }

    (void)audio_codec_apply_kws_mic_profile(false);

    app_log_puts("[AUDIO] ES7210 init OK\r\n");
    app_log_puts("[AUDIO] I2S DMA running\r\n");
    return 0;
}

static TrackEvent_t keyword_to_event(uint16_t keyword, int *matched)
{
    switch ((KWSKeyword_t)keyword) {
    case KWS_KEYWORD_START_TRACKING:
        *matched = 1;
        return TRACK_EVT_START;

    case KWS_KEYWORD_STOP_TRACKING:
        *matched = 1;
        return TRACK_EVT_STOP;

    case KWS_KEYWORD_TAKE_PHOTO:
        *matched = 1;
        return TRACK_EVT_PHOTO;

    case KWS_KEYWORD_START_RECORDING:
        *matched = 1;
        return TRACK_EVT_RECORD_START;

    case KWS_KEYWORD_STOP_RECORDING:
        *matched = 1;
        return TRACK_EVT_RECORD_STOP;

    default:
        *matched = 0;
        return TRACK_EVT_STOP;
    }
}

static uint8_t should_emit_keyword(uint16_t keyword)
{
    TickType_t now;
    TickType_t cooldown;

    if (keyword >= KWS_KEYWORD_MAX) {
        return 0u;
    }

    now = xTaskGetTickCount();
    cooldown = pdMS_TO_TICKS(APP_KWS_EVENT_COOLDOWN_MS);
    if ((now - g_kws_last_emit_tick[keyword]) < cooldown) {
        return 0u;
    }

    g_kws_last_emit_tick[keyword] = now;
    return 1u;
}

static void handle_kws_result(const volatile KWSResult_t *result)
{
    int matched = 0;
    uint8_t emit = 0u;
    uint8_t is_light = 0u;

    if (!KWS_RESULT_IS_VALID(result)) {
        return;
    }

    TrackEvent_t evt = keyword_to_event(result->keyword_idx, &matched);
    is_light = (result->keyword_idx == KWS_KEYWORD_LIGHT_ON ||
                result->keyword_idx == KWS_KEYWORD_LIGHT_OFF);

    if (matched || is_light) {
        emit = should_emit_keyword(result->keyword_idx);
    }

    if (is_light) {
        handle_light_keyword(result->keyword_idx, emit);
    }

    if (matched && emit) {
        app_log_printf("[KWS][mbox] keyword=%u confidence=%u frame=%lu\r\n",
                       (unsigned int)result->keyword_idx,
                       (unsigned int)result->confidence,
                       (unsigned long)result->frame_id);
    } else if (is_light && emit) {
        app_log_printf("[KWS][mbox] keyword=%u confidence=%u frame=%lu\r\n",
                       (unsigned int)result->keyword_idx,
                       (unsigned int)result->confidence,
                       (unsigned long)result->frame_id);
    } else if (!matched && !is_light && should_emit_keyword(result->keyword_idx)) {
        app_log_printf("[KWS][mbox] keyword=%u ignored(unmapped)\r\n",
                       (unsigned int)result->keyword_idx);
    }

    if (matched && emit && !track_events_post(evt)) {
        app_log_puts("[KWS] event queue full\r\n");
    }
}

static void handle_kws_scores(const uint8_t *scores)
{
    uint8_t max_val = 0u;
    uint16_t idx = 0u;

    for (uint16_t i = 0; i < KWS_KEYWORD_MAX; i++) {
        if (scores[i] > max_val) {
            max_val = scores[i];
            idx = i;
        }
    }

    if (!g_kws_counting_started || idx == g_kws_target_idx) {
        if (!g_kws_counting_started) {
            g_kws_target_idx = idx;
            g_kws_counting_started = 1u;
        }
        g_kws_consecutive_count++;

        if (g_kws_consecutive_count < KWS_CONSECUTIVE_TRIGGER_COUNT) {
            return;
        }

        g_kws_consecutive_count = 0u;
        g_kws_counting_started = 0u;

        if (idx > 0u && max_val >= KWS_SCORE_THRESHOLD) {
            int matched = 0;
            uint8_t emit = 0u;
            uint8_t is_light = 0u;
            TrackEvent_t evt = keyword_to_event((uint16_t)idx, &matched);
            uint32_t confidence_pct = ((uint32_t)max_val * 100u + 127u) / 255u;

            is_light = (idx == KWS_KEYWORD_LIGHT_ON || idx == KWS_KEYWORD_LIGHT_OFF);

            if (matched || is_light) {
                emit = should_emit_keyword((uint16_t)idx);
            }

            if (is_light) {
                handle_light_keyword((uint16_t)idx, emit);
            }

            if (matched && emit) {
                app_log_printf("[KWS][poll] keyword=%u score=%u confidence=%lu%%\r\n",
                               (unsigned int)idx,
                               (unsigned int)max_val,
                               (unsigned long)confidence_pct);
            } else if (is_light && emit) {
                app_log_printf("[KWS][poll] keyword=%u score=%u confidence=%lu%%\r\n",
                               (unsigned int)idx,
                               (unsigned int)max_val,
                               (unsigned long)confidence_pct);
            }

            if (!matched && !is_light && should_emit_keyword((uint16_t)idx)) {
                app_log_printf("[KWS][poll] keyword=%u score=%u confidence=%lu%% ignored(unmapped)\r\n",
                               (unsigned int)idx,
                               (unsigned int)max_val,
                               (unsigned long)confidence_pct);
            }

            if (matched && emit && !track_events_post(evt)) {
                app_log_puts("[KWS] event queue full\r\n");
            }
        }
    } else {
        g_kws_consecutive_count = 1u;
        g_kws_target_idx = idx;
    }
}

static void poll_mailbox_results(void)
{
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

        if (msg_type == MAILBOX_MSG_TYPE_KWS) {
            uintptr_t addr = (uintptr_t)DSP_KWS_BASE_ADDR + (uintptr_t)payload;
            const volatile KWSResult_t *result = (const volatile KWSResult_t *)addr;
            handle_kws_result(result);
        }
    }
}

static void kws_task_entry(void *arg)
{
    audio_ctx_t *ctx = audio_get_ctx();
    int16_t *in_ptr;
    int16_t *out_ptr;
    uint32_t warn_log_div = 0;

    (void)arg;

    app_log_puts("[KWS] task started\r\n");
    app_log_puts("[KWS] waiting dsp ready...\r\n");

    while (wait_flag_set(g_dsp_ready_flag, DSP_READY_WAIT_LOOPS) == 0u) {
        g_dsp_ready_timeout_cnt++;
        warn_log_div++;
        if ((warn_log_div % KWS_WARN_LOG_INTERVAL_FRAMES) == 1u) {
            app_log_printf("[KWS] wait dsp ready timeout, cnt=%lu\r\n",
                           (unsigned long)g_dsp_ready_timeout_cnt);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    app_log_puts("[KWS] dsp ready\r\n");

    for (;;) {
        if (ctx->in_valid_num <= 0) {
            poll_mailbox_results();
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        in_ptr = ctx->in_buf + ctx->in_read_idx * AUDIO_DMA_BUFFER_LEN;
        audio_deinterleave(in_ptr, (int16_t *)g_dsp_audio_in, g_audio_block_right, AUDIO_BLOCK_LEN);

        ctx->in_read_idx = (ctx->in_read_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
        ctx->in_valid_num--;

        out_ptr = ctx->out_buf + ctx->out_write_idx * AUDIO_DMA_BUFFER_LEN;
        audio_interleave((const int16_t *)g_dsp_audio_in,
                         (const int16_t *)g_dsp_audio_in,
                         out_ptr,
                         AUDIO_BLOCK_LEN);

        ctx->out_write_idx = (ctx->out_write_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
        if (ctx->out_valid_num < AUDIO_DMA_BUFFER_COUNT) {
            ctx->out_valid_num++;
        }

        *g_dsp_input_ready_flag = 1u;
        if (wait_flag_set(g_dsp_output_ready_flag, DSP_OUTPUT_WAIT_LOOPS) == 0u) {
            g_dsp_output_timeout_cnt++;
            *g_dsp_input_ready_flag = 0u;

            warn_log_div++;
            if ((warn_log_div % KWS_WARN_LOG_INTERVAL_FRAMES) == 1u) {
                app_log_printf("[KWS] wait dsp output timeout, cnt=%lu\r\n",
                               (unsigned long)g_dsp_output_timeout_cnt);
            }

            poll_mailbox_results();
            continue;
        }

        *g_dsp_output_ready_flag = 0u;
        handle_kws_scores((const uint8_t *)g_dsp_kws_cmd);
        poll_mailbox_results();

        (void)g_dsp_audio_out;
    }
}

int task_kws_init(void)
{
    int ret;

    ret = kws_audio_init();
    if (ret < 0) {
        app_log_puts("[KWS] audio init failed\r\n");
        return -1;
    }

    ret = rcc_init_dsp_pll(6, 800, 0, 2, 2);
    if (ret < 0) {
        app_log_printf("[KWS] rcc_init_dsp_pll failed=%d\r\n", ret);
        return -1;
    }

    ret = init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    if (ret < 0) {
        app_log_printf("[KWS] init_mailbox failed=%d\r\n", ret);
        return -1;
    }

    *g_dsp_ready_flag = 0u;
    *g_dsp_input_ready_flag = 0u;
    *g_dsp_output_ready_flag = 0u;

    app_log_puts("[KWS] hold DSP warm reset\r\n");
    set_dsp_warm_reset(true);
    RCC->CM4_SYS_SOFT_RSTN |= 1u;

    dsp_firmware_load_point();

    set_dsp_warm_reset(false);
    app_log_puts("[KWS] release DSP warm reset\r\n");

    for (volatile int i = 0; i < 100000; i++) {
    }

    if (write_mailbox(MAILBOX_BASE, MAILBOX_CMD_START) < 0) {
        app_log_puts("[KWS] MAILBOX_CMD_START failed\r\n");
    } else {
        app_log_puts("[KWS] MAILBOX_CMD_START sent\r\n");
    }

    app_log_puts("[KWS] dsp+mailbox ready\r\n");
    return 0;
}

int task_kws_start(void)
{
    BaseType_t ret;

    if (g_kws_task_handle != NULL) {
        return 0;
    }

    ret = xTaskCreate(kws_task_entry,
                      "kws",
                      APP_KWS_TASK_STACK_WORDS,
                      NULL,
                      APP_KWS_TASK_PRIORITY,
                      &g_kws_task_handle);

    if (ret != pdPASS) {
        app_log_puts("[KWS] task create failed\r\n");
        return -1;
    }

    return 0;
}
