#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_action_dispatch.h"
#include "app_card1_subboard.h"
#include "app_card2_subboard.h"
#include "app_card3_subboard.h"
#include "app_gimbal_debug.h"
#include "app_gimbal_control.h"
#include "app_media_control.h"
#include "app_runtime_state.h"
#include "app_status_light.h"
#include "app_fill_light.h"
#include "audio_app.h"
#include "board.h"
#include "control_proto.h"
#include "gpio.h"
#include "kws_control_stream.h"
#include "mailbox.h"
#include "master_demo_app.h"
#include "rcc.h"
#include "s300.h"
#include "subboard_startup_proto.h"
#include "uart.h"

#ifndef APP_GIMBAL_MASTER_MODEL_ID
#define APP_GIMBAL_MASTER_MODEL_ID "unknown"
#endif

#ifndef APP_GIMBAL_MASTER_MODEL_VERSION
#define APP_GIMBAL_MASTER_MODEL_VERSION "unknown"
#endif

#ifndef APP_GIMBAL_MASTER_MODEL_DATE
#define APP_GIMBAL_MASTER_MODEL_DATE "unknown"
#endif

#ifndef APP_GIMBAL_MASTER_MODEL_DIR
#define APP_GIMBAL_MASTER_MODEL_DIR "unknown"
#endif

#ifndef MASTER_SUBBOARD_WAIT_TIMEOUT_MS
#define MASTER_SUBBOARD_WAIT_TIMEOUT_MS 3000u
#endif

#ifndef MASTER_KWS_STATUS_LOG_STRIDE
#define MASTER_KWS_STATUS_LOG_STRIDE 0u
#endif

#ifndef MASTER_KWS_HEARTBEAT_LOG_STRIDE
#define MASTER_KWS_HEARTBEAT_LOG_STRIDE 0u
#endif

#define CTRL_HELLO_RETRY_MS         200u
#define CTRL_RESOURCE_RETRY_MS      300u
#define CTRL_HEARTBEAT_INTERVAL_MS  1000u
#define CTRL_RESPONSE_TIMEOUT_MS    2000u
#define CTRL_KWS_CONFIG_SLOT_ID     0x21u
#define CTRL_KWS_BUFFER_SLOT_ID     0x31u
#define CTRL_KWS_STREAM_ID          0x01u

typedef enum {
    CM4_KWS_STATE_RESET = 0,
    CM4_KWS_STATE_HANDSHAKING,
    CM4_KWS_STATE_DSP_READY,
    CM4_KWS_STATE_CM4_RESOURCE_READY,
    CM4_KWS_STATE_CONFIGURED,
    CM4_KWS_STATE_RUNNING,
    CM4_KWS_STATE_ERROR,
} Cm4KwsState_t;

typedef enum {
    CM4_KWS_PENDING_NONE = 0,
    CM4_KWS_PENDING_CONFIG_ACK,
    CM4_KWS_PENDING_BUFFER_ACK,
    CM4_KWS_PENDING_START_ACK,
} Cm4KwsPending_t;

typedef enum {
    SUBBOARD_WAIT_PHASE_CARD1 = 0,
    SUBBOARD_WAIT_PHASE_CARD2,
    SUBBOARD_WAIT_PHASE_CARD3,
    SUBBOARD_WAIT_PHASE_DONE,
} SubboardWaitPhase_t;

static volatile uint32_t g_tick_ms = 0u;

static Cm4KwsState_t g_state = CM4_KWS_STATE_RESET;
static Cm4KwsPending_t g_pending = CM4_KWS_PENDING_NONE;

static uint8_t g_session_id = 0u;
static uint8_t g_heartbeat_seq = 0u;

static uint32_t g_state_since_ms = 0u;
static uint32_t g_last_hello_ms = 0u;
static uint32_t g_last_resource_ms = 0u;
static uint32_t g_last_heartbeat_ms = 0u;
static bool g_kws_started = false;
static uint32_t g_subboard_wait_since_ms = 0u;
static bool g_subboard_wait_timed_out = false;
static SubboardWaitPhase_t g_subboard_wait_phase = SUBBOARD_WAIT_PHASE_CARD1;
static bool g_subboard_init_finalized = false;
static bool g_subboard_summary_log_valid = false;
static master_demo_subboard_snapshot_t g_last_subboard_summary_snapshot;
static uint8_t g_last_status_run_state = 0xFFu;
static uint8_t g_last_status_brief = 0xFFu;
static uint32_t g_status_log_count = 0u;
static uint32_t g_tx_heartbeat_log_count = 0u;
static uint32_t g_rx_heartbeat_log_count = 0u;
static master_demo_poll_target_t g_runtime_poll_target = MASTER_DEMO_POLL_CARD1;

static uint32_t millis(void);

static bool log_stride_hit(uint32_t count, uint32_t stride)
{
    if (stride == 0u) {
        return false;
    }

    return (count % stride) == 0u;
}

static const char *subboard_wait_phase_name(SubboardWaitPhase_t phase)
{
    switch (phase) {
    case SUBBOARD_WAIT_PHASE_CARD1: return "CARD1";
    case SUBBOARD_WAIT_PHASE_CARD2: return "CARD2";
    case SUBBOARD_WAIT_PHASE_CARD3: return "CARD3";
    case SUBBOARD_WAIT_PHASE_DONE: return "DONE";
    default: return "UNKNOWN";
    }
}

static const master_demo_subboard_state_t *current_wait_phase_state(const master_demo_subboard_snapshot_t *snapshot,
                                                                    bool have_snapshot)
{
    if (!have_snapshot || (snapshot == NULL)) {
        return NULL;
    }

    switch (g_subboard_wait_phase) {
    case SUBBOARD_WAIT_PHASE_CARD1:
        return &snapshot->card1;
    case SUBBOARD_WAIT_PHASE_CARD2:
        return &snapshot->card2;
    case SUBBOARD_WAIT_PHASE_CARD3:
        return &snapshot->card3;
    case SUBBOARD_WAIT_PHASE_DONE:
    default:
        return NULL;
    }
}

static uint8_t current_wait_phase_capabilities(void)
{
    switch (g_subboard_wait_phase) {
    case SUBBOARD_WAIT_PHASE_CARD1:
        return app_card1_subboard_get_capabilities();
    case SUBBOARD_WAIT_PHASE_CARD2:
        return app_card2_subboard_get_capabilities();
    case SUBBOARD_WAIT_PHASE_CARD3:
        return app_card3_subboard_get_capabilities();
    case SUBBOARD_WAIT_PHASE_DONE:
    default:
        return 0u;
    }
}

static const char *current_wait_phase_failure_reason(void)
{
    switch (g_subboard_wait_phase) {
    case SUBBOARD_WAIT_PHASE_CARD1:
        return app_card1_subboard_get_init_failure_reason();
    case SUBBOARD_WAIT_PHASE_CARD2:
        return app_card2_subboard_get_init_failure_reason();
    case SUBBOARD_WAIT_PHASE_CARD3:
        return app_card3_subboard_get_init_failure_reason();
    case SUBBOARD_WAIT_PHASE_DONE:
    default:
        return NULL;
    }
}

static void log_subboard_summary_line(const char *name,
                                      const master_demo_subboard_state_t *state,
                                      uint8_t capabilities)
{
    const char *failure_reason = NULL;

    if ((name == NULL) || (state == NULL)) {
        return;
    }

    if (state->init_complete && !state->init_success) {
        if (name[4] == '\0') {
            failure_reason = NULL;
        }
        if (name[4] == '1') {
            failure_reason = app_card1_subboard_get_init_failure_reason();
        } else if (name[4] == '2') {
            failure_reason = app_card2_subboard_get_init_failure_reason();
        } else if (name[4] == '3') {
            failure_reason = app_card3_subboard_get_init_failure_reason();
        }
    }

    printf("[MASTER] %s init_complete=%u init_success=%u online=%u state=0x%02X running=%u faulted=%u caps=0x%02X detection=%u tracking_summary=%u tracking_control=%u gesture=%u",
           name,
           state->init_complete ? 1u : 0u,
           state->init_success ? 1u : 0u,
           state->online ? 1u : 0u,
           (unsigned)state->public_state,
           state->running ? 1u : 0u,
           state->faulted ? 1u : 0u,
           (unsigned)capabilities,
           (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_DETECTION_RESULT) != 0u),
           (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_TRACKING_SUMMARY) != 0u),
           (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_TRACKING_CONTROL) != 0u),
           (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_GESTURE_RESULT) != 0u));

    if ((failure_reason != NULL) && (failure_reason[0] != '\0')) {
        printf(" failure_reason=%s", failure_reason);
    }

    printf("\r\n");
}

static void log_subboard_startup_summary(const master_demo_subboard_snapshot_t *snapshot,
                                         bool have_snapshot)
{
    bool changed;

    if (!have_snapshot || (snapshot == NULL)) {
        return;
    }

    changed = !g_subboard_summary_log_valid ||
              (snapshot->card1.public_state != g_last_subboard_summary_snapshot.card1.public_state) ||
              (snapshot->card1.online != g_last_subboard_summary_snapshot.card1.online) ||
              (snapshot->card1.running != g_last_subboard_summary_snapshot.card1.running) ||
              (snapshot->card1.faulted != g_last_subboard_summary_snapshot.card1.faulted) ||
              (snapshot->card1.init_complete != g_last_subboard_summary_snapshot.card1.init_complete) ||
              (snapshot->card1.init_success != g_last_subboard_summary_snapshot.card1.init_success) ||
              (snapshot->card2.public_state != g_last_subboard_summary_snapshot.card2.public_state) ||
              (snapshot->card2.online != g_last_subboard_summary_snapshot.card2.online) ||
              (snapshot->card2.running != g_last_subboard_summary_snapshot.card2.running) ||
              (snapshot->card2.faulted != g_last_subboard_summary_snapshot.card2.faulted) ||
              (snapshot->card2.init_complete != g_last_subboard_summary_snapshot.card2.init_complete) ||
              (snapshot->card2.init_success != g_last_subboard_summary_snapshot.card2.init_success) ||
              (snapshot->card3.public_state != g_last_subboard_summary_snapshot.card3.public_state) ||
              (snapshot->card3.online != g_last_subboard_summary_snapshot.card3.online) ||
              (snapshot->card3.running != g_last_subboard_summary_snapshot.card3.running) ||
              (snapshot->card3.faulted != g_last_subboard_summary_snapshot.card3.faulted) ||
              (snapshot->card3.init_complete != g_last_subboard_summary_snapshot.card3.init_complete) ||
              (snapshot->card3.init_success != g_last_subboard_summary_snapshot.card3.init_success);

    if (!changed) {
        return;
    }

    printf("[MASTER] subboard startup summary\r\n");
    log_subboard_summary_line("CARD1", &snapshot->card1, app_card1_subboard_get_capabilities());
    log_subboard_summary_line("CARD2", &snapshot->card2, app_card2_subboard_get_capabilities());
    log_subboard_summary_line("CARD3", &snapshot->card3, app_card3_subboard_get_capabilities());

    g_last_subboard_summary_snapshot = *snapshot;
    g_subboard_summary_log_valid = true;
}

static bool current_wait_phase_ready(const master_demo_subboard_snapshot_t *snapshot,
                                     bool have_snapshot)
{
    const master_demo_subboard_state_t *state = current_wait_phase_state(snapshot, have_snapshot);

    if (state == NULL) {
        return false;
    }

    if (state->init_complete) {
        return true;
    }

    return state->online;
}

static bool step_subboard_wait_phase(const master_demo_subboard_snapshot_t *snapshot,
                                     bool have_snapshot)
{
    const master_demo_subboard_state_t *phase_state;
    const char *failure_reason;
    bool phase_ready;
    uint8_t capabilities;

    if (g_subboard_wait_phase == SUBBOARD_WAIT_PHASE_DONE) {
        return true;
    }

    phase_state = current_wait_phase_state(snapshot, have_snapshot);
    phase_ready = current_wait_phase_ready(snapshot, have_snapshot);
    failure_reason = current_wait_phase_failure_reason();
    capabilities = current_wait_phase_capabilities();
    if (phase_ready && (phase_state != NULL) && phase_state->init_complete) {
        printf("[MASTER] subboard wait phase %s complete (init_success=%u caps=0x%02X detection=%u tracking_summary=%u tracking_control=%u gesture=%u",
               subboard_wait_phase_name(g_subboard_wait_phase),
               phase_state->init_success ? 1u : 0u,
               (unsigned)capabilities,
               (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_DETECTION_RESULT) != 0u),
               (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_TRACKING_SUMMARY) != 0u),
               (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_TRACKING_CONTROL) != 0u),
               (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_GESTURE_RESULT) != 0u));
        if (!phase_state->init_success && (failure_reason != NULL) && (failure_reason[0] != '\0')) {
            printf(" failure_reason=%s", failure_reason);
        }
        printf(")\r\n");
    } else if (phase_ready) {
        printf("[MASTER] subboard wait phase %s complete (online detected, waiting init result)\r\n",
               subboard_wait_phase_name(g_subboard_wait_phase));
    } else if ((millis() - g_subboard_wait_since_ms) < MASTER_SUBBOARD_WAIT_TIMEOUT_MS) {
        return false;
    } else {
        printf("[MASTER] subboard wait phase %s complete (%lu ms timeout)\r\n",
               subboard_wait_phase_name(g_subboard_wait_phase),
               (unsigned long)MASTER_SUBBOARD_WAIT_TIMEOUT_MS);
    }

    if (g_subboard_wait_phase == SUBBOARD_WAIT_PHASE_CARD3) {
        g_subboard_wait_phase = SUBBOARD_WAIT_PHASE_DONE;
        return true;
    }

    g_subboard_wait_phase = (SubboardWaitPhase_t)((int)g_subboard_wait_phase + 1);
    g_subboard_wait_since_ms = millis();
    printf("[MASTER] entering subboard wait phase %s\r\n",
           subboard_wait_phase_name(g_subboard_wait_phase));
    return false;
}

static master_demo_poll_target_t current_subboard_poll_target(void)
{
    switch (g_subboard_wait_phase) {
    case SUBBOARD_WAIT_PHASE_CARD1:
        return MASTER_DEMO_POLL_CARD1;
    case SUBBOARD_WAIT_PHASE_CARD2:
        return MASTER_DEMO_POLL_CARD2;
    case SUBBOARD_WAIT_PHASE_CARD3:
        return MASTER_DEMO_POLL_CARD3;
    case SUBBOARD_WAIT_PHASE_DONE:
    default:
        return MASTER_DEMO_POLL_ALL;
    }
}

static master_demo_poll_target_t next_runtime_subboard_poll_target(master_demo_poll_target_t target)
{
    switch (target) {
    case MASTER_DEMO_POLL_CARD1:
        return MASTER_DEMO_POLL_CARD2;
    case MASTER_DEMO_POLL_CARD2:
        return MASTER_DEMO_POLL_CARD3;
    case MASTER_DEMO_POLL_CARD3:
    default:
        return MASTER_DEMO_POLL_CARD1;
    }
}

static void handle_kws_keyword_report(uint8_t keyword_idx, uint8_t confidence, uint32_t chunk_idx)
{
    app_action_dispatch_kws_keyword(keyword_idx, confidence, chunk_idx);
}

static void update_status_light(void)
{
    master_demo_subboard_snapshot_t subboard_snapshot;
    bool has_snapshot = master_demo_app_get_subboard_snapshot(&subboard_snapshot);
    bool any_running = has_snapshot && subboard_snapshot.any_running;
    bool blocking_subboard_fault = has_snapshot && subboard_snapshot.any_error && !subboard_snapshot.any_running;
    bool ignore_subboard_fault = g_subboard_wait_timed_out && !any_running;

    if ((blocking_subboard_fault && !ignore_subboard_fault) ||
        (g_state == CM4_KWS_STATE_ERROR)) {
        app_status_light_set_mode(APP_STATUS_LIGHT_MODE_ERROR);
    } else if (!any_running && !g_subboard_wait_timed_out) {
        app_status_light_set_mode(APP_STATUS_LIGHT_MODE_WAIT_SUBBOARD);
    } else if ((g_kws_started == false) || (g_state != CM4_KWS_STATE_RUNNING)) {
        app_status_light_set_mode(APP_STATUS_LIGHT_MODE_SUBBOARD_READY);
    } else {
        app_status_light_set_mode(APP_STATUS_LIGHT_MODE_KWS_RUNNING);
    }

    app_status_light_tick();
}

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t millis(void)
{
    return g_tick_ms;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms) {
        __NOP();
    }
}

__attribute__((noinline)) void dsp_firmware_load_point(void)
{
    __asm volatile("nop");
}

void DMA0_IRQHandler(void)
{
    kws_control_stream_dma0_irq_handler();
}

static int dsp_clock_init(void)
{
    int ret = rcc_init_dsp_pll(6, 800, 0, 2, 2);
    if (ret != RCC_STATUS_OK) {
        printf("[KWS-CTRL] DSP PLL init failed: %d\r\n", ret);
        return ret;
    }

    printf("[KWS-CTRL] DSP PLL initialized (400MHz)\r\n");
    return 0;
}

static void dsp_uart_init(void)
{
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    set_gpio_function(GPIOA, 27, FUNCTION_3);
    set_gpio_function(GPIOA, 26, FUNCTION_3);
    init_uart(3, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), 115200);
    printf("[KWS-CTRL] UART3 initialized for DSP\r\n");
}

static void kws_mic_switch_gpio_init(void)
{
#if defined(BOARD_AUDIO_CODEC_ES7210)
    board_audio_mic_sw_pin_init();
    printf("[KWS-CTRL] MIC switch GPIO%u=%u (1=MIC4, 0=MIC1+MIC2)\r\n",
           (unsigned)BOARD_MIC_SW_PIN,
           gpio_get_value(BOARD_MIC_SW_PORT, BOARD_MIC_SW_PIN) ? 1u : 0u);
#endif
}

static void control_mailbox_prepare(void)
{
    (void)init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    (void)init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    printf("[KWS-CTRL] Mailbox re-synced, stale messages dropped\r\n");
}

static void dsp_start_new_session(void)
{
    g_session_id++;
    kws_control_stream_reset_session();
    rcc_set_dsp_warm_reset(true);
    delay_ms(50u);
    control_mailbox_prepare();
    printf("[KWS-CTRL] DSP warm reset triggered, session=0x%02X\r\n", g_session_id);
}

static int read_dsp_message(uint32_t *out_msg)
{
    if (out_msg == NULL) {
        return -1;
    }

    if (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
        *out_msg = read_mailbox(MAILBOX_BASE);
        return 0;
    }

    return -1;
}

static void send_control_msg(uint32_t msg, const char *label)
{
    int ret = write_mailbox(MAILBOX_BASE, msg);
    if (ret == 0) {
        printf("[KWS-CTRL] TX %-20s 0x%08lX\r\n", label, (unsigned long)msg);
    } else {
        printf("[KWS-CTRL] TX %-20s failed (%d)\r\n", label, ret);
    }
}

static const char *state_name(Cm4KwsState_t state)
{
    switch (state) {
    case CM4_KWS_STATE_RESET: return "RESET";
    case CM4_KWS_STATE_HANDSHAKING: return "HANDSHAKING";
    case CM4_KWS_STATE_DSP_READY: return "DSP_READY";
    case CM4_KWS_STATE_CM4_RESOURCE_READY: return "CM4_RESOURCE_READY";
    case CM4_KWS_STATE_CONFIGURED: return "CONFIGURED";
    case CM4_KWS_STATE_RUNNING: return "RUNNING";
    case CM4_KWS_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static void enter_state(Cm4KwsState_t next)
{
    if (g_state != next) {
        printf("[KWS-CTRL] STATE %s -> %s\r\n", state_name(g_state), state_name(next));
        g_state = next;
        g_state_since_ms = millis();
        kws_control_stream_set_running(next == CM4_KWS_STATE_RUNNING ? 1u : 0u);
    }
}

static void log_audio_resources_ready(void)
{
    printf("[KWS-CTRL] Audio resources ready: codec/I2S/DMA/ring\r\n");
    printf("[KWS-CTRL] DSP audio_in=0x%08lX kws_cmd=0x%08lX\r\n",
           (unsigned long)kws_control_stream_get_audio_in_addr(),
           (unsigned long)kws_control_stream_get_cmd_addr());
}

static void send_hello(void)
{
    send_control_msg(CONTROL_SYS_HELLO(g_session_id, CONTROL_PROTOCOL_VERSION), "SYS.HELLO");
    g_last_hello_ms = millis();
}

static void send_resource_ready(void)
{
    uint8_t resource_flags = CONTROL_RESOURCE_AUDIO_READY | CONTROL_RESOURCE_DMA_READY;

    send_control_msg(
        CONTROL_SYS_CM4_RESOURCE_READY(
            g_session_id,
            CONTROL_INPUT_AUDIO,
            resource_flags,
            CTRL_KWS_CONFIG_SLOT_ID),
        "SYS.CM4_RESOURCE_READY");

    g_last_resource_ms = millis();
}

static void send_config_apply(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_CONFIG, g_session_id,
                         CONTROL_CMD_CONFIG_APPLY, CTRL_KWS_CONFIG_SLOT_ID),
        "CMD.CONFIG_APPLY");
    g_pending = CM4_KWS_PENDING_CONFIG_ACK;
}

static void send_buffer_bind(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_BUFFER, g_session_id,
                         CONTROL_CMD_BUFFER_BIND, CTRL_KWS_BUFFER_SLOT_ID),
        "CMD.BUFFER_BIND");
    g_pending = CM4_KWS_PENDING_BUFFER_ACK;
}

static void send_start_stream(void)
{
    send_control_msg(
        CONTROL_SYS_START_STREAM(g_session_id, CTRL_KWS_STREAM_ID, 0x01u),
        "SYS.START_STREAM");
    g_pending = CM4_KWS_PENDING_START_ACK;
}

static void send_heartbeat(void)
{
    uint32_t msg = CONTROL_SYS_HEARTBEAT(g_session_id, g_heartbeat_seq, CONTROL_RUN_STATE_RUNNING);
    int ret = write_mailbox(MAILBOX_BASE, msg);

    if (ret == 0) {
        bool should_log = false;

        g_tx_heartbeat_log_count++;
        should_log = log_stride_hit(g_tx_heartbeat_log_count, MASTER_KWS_HEARTBEAT_LOG_STRIDE);

        if (should_log) {
            printf("[KWS-CTRL] TX %-20s 0x%08lX\r\n", "SYS.HEARTBEAT", (unsigned long)msg);
        }
    } else {
        printf("[KWS-CTRL] TX %-20s failed (%d)\r\n", "SYS.HEARTBEAT", ret);
    }

    g_heartbeat_seq++;
    g_last_heartbeat_ms = millis();
}

static void handle_ack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t status = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[KWS-CTRL] RX ACK kind=%u code=0x%02X status=%u\r\n",
           kind, code, status);

    if (kind == CONTROL_RSP_KIND_CMD) {
        if ((g_pending == CM4_KWS_PENDING_CONFIG_ACK) &&
            (code == CONTROL_CMD_CONFIG_APPLY) &&
            (status == CONTROL_ACK_OK)) {
            send_buffer_bind();
            return;
        }

        if ((g_pending == CM4_KWS_PENDING_BUFFER_ACK) &&
            (code == CONTROL_CMD_BUFFER_BIND) &&
            (status == CONTROL_ACK_OK)) {
            g_pending = CM4_KWS_PENDING_NONE;
            enter_state(CM4_KWS_STATE_CONFIGURED);
            send_start_stream();
            return;
        }
    }

    if (kind == CONTROL_RSP_KIND_SYS) {
        if ((g_pending == CM4_KWS_PENDING_START_ACK) &&
            (code == CONTROL_SYS_SUBTYPE_START_STREAM) &&
            (status == CONTROL_ACK_OK)) {
            g_pending = CM4_KWS_PENDING_NONE;
            enter_state(CM4_KWS_STATE_RUNNING);
            printf("[KWS-CTRL] Stream start ACK received, audio data plane enabled\r\n");
        }
    }
}

static void handle_nack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t error = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[KWS-CTRL] RX NACK kind=%u code=0x%02X error=%u\r\n",
           kind, code, error);
    g_pending = CM4_KWS_PENDING_NONE;
    enter_state(CM4_KWS_STATE_ERROR);
}

static void handle_status(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t run_state = (uint8_t)CONTROL_STATUS_GET_RUN_STATE(arg);
    uint8_t brief = (uint8_t)CONTROL_STATUS_GET_BRIEF(arg);
    bool should_log = false;

    g_status_log_count++;
    if (run_state != g_last_status_run_state) {
        should_log = true;
    } else if (log_stride_hit(g_status_log_count, MASTER_KWS_STATUS_LOG_STRIDE)) {
        should_log = true;
    }

    if (should_log) {
        printf("[KWS-CTRL] RX STATUS run_state=%u brief=%u\r\n",
               (unsigned)run_state,
               (unsigned)brief);
    }

    g_last_status_run_state = run_state;
    g_last_status_brief = brief;
}

static void handle_sys_message(uint32_t msg)
{
    uint8_t subtype = (uint8_t)CONTROL_GET_SUBTYPE(msg);
    uint16_t arg = CONTROL_GET_ARG(msg);

    if (subtype == CONTROL_SYS_SUBTYPE_HELLO_ACK) {
        printf("[KWS-CTRL] RX HELLO_ACK session=0x%02X boot=%u feature=%u state=%u\r\n",
               (unsigned)CONTROL_GET_SESSION(msg),
               (unsigned)CONTROL_HELLO_ACK_GET_BOOT_REASON(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_FEATURE_LEVEL(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_RUN_STATE(arg));

        if ((g_state == CM4_KWS_STATE_HANDSHAKING) ||
            (g_state == CM4_KWS_STATE_RESET)) {
            enter_state(CM4_KWS_STATE_DSP_READY);
            log_audio_resources_ready();
            send_resource_ready();
            enter_state(CM4_KWS_STATE_CM4_RESOURCE_READY);
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_DSP_MODEL_READY) {
        printf("[KWS-CTRL] RX DSP_MODEL_READY algo=%u flags=%u run_state=%u\r\n",
               (unsigned)CONTROL_MODEL_READY_GET_ALGO_ID(arg),
               (unsigned)CONTROL_MODEL_READY_GET_FLAGS(arg),
               (unsigned)CONTROL_MODEL_READY_GET_RUN_STATE(arg));

        if ((g_state == CM4_KWS_STATE_CM4_RESOURCE_READY) &&
            (g_pending == CM4_KWS_PENDING_NONE)) {
            send_config_apply();
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_HEARTBEAT) {
        bool should_log = false;

        g_rx_heartbeat_log_count++;
        should_log = log_stride_hit(g_rx_heartbeat_log_count, MASTER_KWS_HEARTBEAT_LOG_STRIDE);

        if (should_log) {
            printf("[KWS-CTRL] RX DSP HEARTBEAT seq=%u status=%u\r\n",
                   (unsigned)CONTROL_HEARTBEAT_GET_SEQ(arg),
                   (unsigned)CONTROL_HEARTBEAT_GET_STATUS(arg));
        }
        return;
    }

    printf("[KWS-CTRL] RX SYS subtype=%u arg=0x%04X\r\n",
           (unsigned)subtype, (unsigned)arg);
}

static void process_dsp_messages(void)
{
    while (1) {
        uint32_t msg;

        if (read_dsp_message(&msg) != 0) {
            break;
        }

        if (kws_control_stream_handle_mailbox_message(msg)) {
            continue;
        }

        if (!control_msg_session_matches(msg, g_session_id)) {
            printf("[KWS-CTRL] Drop stale message: 0x%08lX (session=0x%02X, current=0x%02X)\r\n",
                   (unsigned long)msg,
                   (unsigned)CONTROL_GET_SESSION(msg),
                   (unsigned)g_session_id);
            continue;
        }

        switch (CONTROL_GET_TYPE(msg)) {
        case CONTROL_MSG_TYPE_SYS:
            handle_sys_message(msg);
            break;
        case CONTROL_MSG_TYPE_ACK:
            handle_ack(msg);
            break;
        case CONTROL_MSG_TYPE_NACK:
            handle_nack(msg);
            break;
        case CONTROL_MSG_TYPE_STATUS:
            handle_status(msg);
            break;
        default:
            printf("[KWS-CTRL] RX unknown type: 0x%08lX\r\n", (unsigned long)msg);
            break;
        }
    }
}

static void step_state_machine(void)
{
    uint32_t now_ms = millis();

    switch (g_state) {
    case CM4_KWS_STATE_RESET:
        dsp_start_new_session();
        g_pending = CM4_KWS_PENDING_NONE;
        send_hello();
        enter_state(CM4_KWS_STATE_HANDSHAKING);
        break;

    case CM4_KWS_STATE_HANDSHAKING:
        if ((now_ms - g_last_hello_ms) >= CTRL_HELLO_RETRY_MS) {
            send_hello();
        }
        break;

    case CM4_KWS_STATE_CM4_RESOURCE_READY:
        if (((now_ms - g_last_resource_ms) >= CTRL_RESOURCE_RETRY_MS) &&
            (g_pending == CM4_KWS_PENDING_NONE)) {
            send_resource_ready();
        }
        break;

    case CM4_KWS_STATE_RUNNING:
        if ((now_ms - g_last_heartbeat_ms) >= CTRL_HEARTBEAT_INTERVAL_MS) {
            send_heartbeat();
        }
        break;

    case CM4_KWS_STATE_DSP_READY:
    case CM4_KWS_STATE_CONFIGURED:
    case CM4_KWS_STATE_ERROR:
    default:
        break;
    }

    if (((now_ms - g_state_since_ms) >= CTRL_RESPONSE_TIMEOUT_MS) &&
        (g_state != CM4_KWS_STATE_RUNNING) &&
        (g_state != CM4_KWS_STATE_RESET)) {
        process_dsp_messages();

        if ((g_state == CM4_KWS_STATE_RUNNING) ||
            (g_state == CM4_KWS_STATE_RESET)) {
            return;
        }

        if ((millis() - g_state_since_ms) < CTRL_RESPONSE_TIMEOUT_MS) {
            return;
        }

        printf("[KWS-CTRL] State timeout in %s, restart session\r\n", state_name(g_state));
        enter_state(CM4_KWS_STATE_RESET);
    }
}

static int kws_runtime_init(const char *startup_reason)
{
    int ret;

    kws_mic_switch_gpio_init();

    ret = audio_init();
    if (ret != 0) {
        printf("[KWS-CTRL] Audio init failed: %d\r\n", ret);
        return ret;
    }
    printf("[KWS-CTRL] Audio initialized\r\n");

    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    printf("[KWS-CTRL] Mailbox initialized\r\n");

    if (dsp_clock_init() != 0) {
        return -1;
    }

    dsp_uart_init();
    dsp_firmware_load_point();
    kws_control_stream_log_layout();
    kws_control_stream_set_keyword_report_callback(handle_kws_keyword_report);
    g_state = CM4_KWS_STATE_RESET;
    g_pending = CM4_KWS_PENDING_NONE;
    g_state_since_ms = millis();
    g_last_hello_ms = 0u;
    g_last_resource_ms = 0u;
    g_last_heartbeat_ms = 0u;
    g_heartbeat_seq = 0u;
    g_last_status_run_state = 0xFFu;
    g_last_status_brief = 0xFFu;
    g_status_log_count = 0u;
    g_tx_heartbeat_log_count = 0u;
    g_rx_heartbeat_log_count = 0u;
    g_kws_started = true;
    printf("[KWS-CTRL] KWS runtime started (%s)\r\n",
           startup_reason != NULL ? startup_reason : "unspecified");
    return 0;
}

int main(void)
{
    board_init();
    SystemCoreClockUpdate();

    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        printf("[KWS-CTRL] SysTick config failed\r\n");
        while (1) {
            __NOP();
        }
    }

    printf("\r\n======================================================\r\n");
    printf("  S300 Gimbal Master App\r\n");
    printf("  KWS runtime + subboard coordination service\r\n");
    printf("======================================================\r\n");
    printf("[KWS-CTRL] SystemCoreClock = %lu Hz\r\n", (unsigned long)SystemCoreClock);
    printf("[KWS-CTRL] Model id=%s version=%s date=%s\r\n",
           APP_GIMBAL_MASTER_MODEL_ID,
           APP_GIMBAL_MASTER_MODEL_VERSION,
           APP_GIMBAL_MASTER_MODEL_DATE);
    printf("[KWS-CTRL] Model dir=%s\r\n", APP_GIMBAL_MASTER_MODEL_DIR);

    app_gimbal_control_init();
    app_gimbal_debug_init(millis);
    app_media_control_init();
    app_runtime_state_reset();

    if (app_status_light_init(millis) != 0) {
        printf("[MASTER] status light init failed, continue without LED\r\n");
        app_status_light_set_mode(APP_STATUS_LIGHT_MODE_DISABLED);
    } else {
        app_status_light_set_mode(APP_STATUS_LIGHT_MODE_BOOT);
    }

    if (master_demo_app_init(millis) != 0) {
        printf("[MASTER] subboard coordination service init failed\r\n");
        app_status_light_set_mode(APP_STATUS_LIGHT_MODE_ERROR);
        while (1) {
            app_status_light_tick();
            __WFI();
        }
    }
    printf("[MASTER] subboard coordination service ready\r\n");
    printf("[MASTER] waiting card1/card2/card3 poll phases before video/MM enable\r\n");
    printf("[MASTER] subboard wait phase timeout = %lu ms\r\n", (unsigned long)MASTER_SUBBOARD_WAIT_TIMEOUT_MS);
    printf("[MASTER] subboard wait policy = init-success-first, timeout-fallback\r\n");
    app_status_light_set_mode(APP_STATUS_LIGHT_MODE_WAIT_SUBBOARD);
    g_subboard_wait_since_ms = millis();
    g_subboard_wait_timed_out = false;
    g_subboard_wait_phase = SUBBOARD_WAIT_PHASE_CARD1;
    g_subboard_init_finalized = false;
    g_subboard_summary_log_valid = false;
    g_runtime_poll_target = MASTER_DEMO_POLL_CARD1;
    printf("[MASTER] entering subboard wait phase %s\r\n",
           subboard_wait_phase_name(g_subboard_wait_phase));

    while (1) {
        master_demo_subboard_snapshot_t subboard_snapshot;
        bool have_subboard_snapshot;

        if (!g_kws_started && !g_subboard_init_finalized) {
            master_demo_app_tick_target(current_subboard_poll_target());
        } else if (!g_kws_started) {
            master_demo_app_tick();
        } else {
            process_dsp_messages();
            step_state_machine();
            kws_control_stream_step();

            master_demo_app_tick_target(g_runtime_poll_target);
            g_runtime_poll_target = next_runtime_subboard_poll_target(g_runtime_poll_target);
        }
        app_gimbal_debug_tick();
        update_status_light();
        have_subboard_snapshot = master_demo_app_get_subboard_snapshot(&subboard_snapshot);

        if (!g_kws_started) {
            log_subboard_startup_summary(&subboard_snapshot, have_subboard_snapshot);
        }

        if (!g_kws_started) {
            (void)have_subboard_snapshot;
            (void)subboard_snapshot;

            if (!g_subboard_init_finalized) {
                if (step_subboard_wait_phase(&subboard_snapshot, have_subboard_snapshot)) {
                    if (master_demo_app_finalize_subboard_init() != 0) {
                        printf("[MASTER] subboard init finalize failed\r\n");
                        app_status_light_set_mode(APP_STATUS_LIGHT_MODE_ERROR);
                        while (1) {
                            app_status_light_tick();
                            __WFI();
                        }
                    }

                    g_subboard_init_finalized = true;
                    g_subboard_wait_timed_out = true;
                } else {
                    continue;
                }
            }

            if (kws_runtime_init("subboard init phase complete") != 0) {
                    printf("[KWS-CTRL] KWS runtime init failed\r\n");
                    app_status_light_set_mode(APP_STATUS_LIGHT_MODE_ERROR);
                    while (1) {
                        app_status_light_tick();
                        __WFI();
                    }
                }

            continue;
        }
    }
}
