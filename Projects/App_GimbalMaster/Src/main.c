#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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

static void handle_kws_keyword_report(uint8_t keyword_idx, uint8_t confidence, uint32_t chunk_idx)
{
    (void)confidence;
    (void)chunk_idx;

    if (keyword_idx == 6u) {
        (void)app_fill_light_set_enabled(true);
    } else if (keyword_idx == 7u) {
        (void)app_fill_light_set_enabled(false);
    }

    app_status_light_notify_kws_hit(keyword_idx);
}

static void update_status_light(void)
{
    uint8_t subboard_state = master_demo_app_get_subboard_state();

    if ((subboard_state == SUBBOARD_STARTUP_STATE_ERROR) ||
        (g_state == CM4_KWS_STATE_ERROR)) {
        app_status_light_set_mode(APP_STATUS_LIGHT_MODE_ERROR);
    } else if (!master_demo_app_is_subboard_running()) {
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
    gpio_set_function(GPIOA, 31u, FUNCTION_2);
    gpio_set_direction(GPIOA, 31u, 0u);
    gpio_set_mode(GPIOA, 31u, GPIO_DOWN);
    printf("[KWS-CTRL] MIC switch GPIO31=%u\r\n", gpio_get_value(GPIOA, 31u) ? 1u : 0u);
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
    send_control_msg(
        CONTROL_SYS_HEARTBEAT(g_session_id, g_heartbeat_seq, CONTROL_RUN_STATE_RUNNING),
        "SYS.HEARTBEAT");
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
    printf("[KWS-CTRL] RX STATUS run_state=%u brief=%u\r\n",
           (unsigned)CONTROL_STATUS_GET_RUN_STATE(arg),
           (unsigned)CONTROL_STATUS_GET_BRIEF(arg));
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
        printf("[KWS-CTRL] RX DSP HEARTBEAT seq=%u status=%u\r\n",
               (unsigned)CONTROL_HEARTBEAT_GET_SEQ(arg),
               (unsigned)CONTROL_HEARTBEAT_GET_STATUS(arg));
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

static int kws_runtime_init(void)
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
    g_kws_started = true;
    printf("[KWS-CTRL] KWS runtime started after subboard reached RUNNING\r\n");
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
    printf("[MASTER] waiting for subboard RUNNING before starting local KWS\r\n");
    app_status_light_set_mode(APP_STATUS_LIGHT_MODE_WAIT_SUBBOARD);

    while (1) {
        master_demo_app_tick();
        update_status_light();

        if (!g_kws_started) {
            if (master_demo_app_is_subboard_running()) {
                if (kws_runtime_init() != 0) {
                    printf("[KWS-CTRL] KWS runtime init failed\r\n");
                    app_status_light_set_mode(APP_STATUS_LIGHT_MODE_ERROR);
                    while (1) {
                        app_status_light_tick();
                        __WFI();
                    }
                }
            }

            continue;
        }

        process_dsp_messages();
        step_state_machine();
        kws_control_stream_step();
    }
}
