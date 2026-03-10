/**
 * @file main.c
 * @brief CM4 控制面协议测试 Demo
 *
 * 本 Demo 只验证 CM4 与 DSP 之间的控制面状态机：
 *   - 基于 session_id 建立可信会话
 *   - 演示 HELLO / HELLO_ACK / RESOURCE_READY / START_STREAM / HEARTBEAT
 *   - 演示视频/音频流的伪搬运通知
 *
 * 本 Demo 不执行真实 AI 推理，也不依赖真实视频或音频外设。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "control_proto.h"
#include "gpio.h"
#include "mailbox.h"
#include "rcc.h"
#include "s300.h"
#include "uart.h"

/*===========================================================================
 * 配置
 *===========================================================================*/

#define CTRL_HELLO_RETRY_MS             200u
#define CTRL_RESOURCE_RETRY_MS          300u
#define CTRL_HEARTBEAT_INTERVAL_MS      1000u
#define CTRL_PSEUDO_VIDEO_INTERVAL_MS   400u
#define CTRL_PSEUDO_AUDIO_INTERVAL_MS   250u
#define CTRL_RESPONSE_TIMEOUT_MS        2000u

#define CTRL_STREAM_ID_MAIN             0x01u
#define CTRL_CONFIG_SLOT_ID             0x11u
#define CTRL_BUFFER_SLOT_ID             0x22u

/*===========================================================================
 * 状态定义
 *===========================================================================*/

typedef enum {
    CM4_DEMO_STATE_RESET = 0,
    CM4_DEMO_STATE_HANDSHAKING,
    CM4_DEMO_STATE_DSP_READY,
    CM4_DEMO_STATE_CM4_RESOURCE_READY,
    CM4_DEMO_STATE_CONFIGURED,
    CM4_DEMO_STATE_RUNNING,
    CM4_DEMO_STATE_ERROR,
} Cm4DemoState_t;

typedef enum {
    CM4_PENDING_NONE = 0,
    CM4_PENDING_CONFIG_ACK,
    CM4_PENDING_BUFFER_ACK,
    CM4_PENDING_START_ACK,
} Cm4Pending_t;

/*===========================================================================
 * 全局变量
 *===========================================================================*/

static volatile uint32_t g_tick_ms = 0;

static Cm4DemoState_t g_state = CM4_DEMO_STATE_RESET;
static Cm4Pending_t g_pending = CM4_PENDING_NONE;

static uint8_t g_session_id = 0;
static uint8_t g_heartbeat_seq = 0;
static uint8_t g_video_slot = 0;
static uint8_t g_audio_slot = 0;

static uint32_t g_state_since_ms = 0;
static uint32_t g_last_hello_ms = 0;
static uint32_t g_last_resource_ms = 0;
static uint32_t g_last_heartbeat_ms = 0;
static uint32_t g_last_video_ms = 0;
static uint32_t g_last_audio_ms = 0;

/*===========================================================================
 * SysTick
 *===========================================================================*/

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

/*===========================================================================
 * 基础初始化
 *===========================================================================*/

static int dsp_clock_init(void)
{
    int ret = rcc_init_dsp_pll(6, 800, 0, 2, 2);
    if (ret != RCC_STATUS_OK) {
        printf("[CM4-CTRL] DSP PLL init failed: %d\r\n", ret);
        return ret;
    }

    printf("[CM4-CTRL] DSP PLL initialized (400MHz)\r\n");
    return 0;
}

static void dsp_uart_init(void)
{
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    set_gpio_function(GPIOA, 27, FUNCTION_3);
    set_gpio_function(GPIOA, 26, FUNCTION_3);
    init_uart(3, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), 115200);
    printf("[CM4-CTRL] UART3 initialized for DSP\r\n");
}

static void control_mailbox_prepare(void)
{
    (void)init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    (void)init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    printf("[CM4-CTRL] Mailbox re-synced, stale messages dropped\r\n");
}

static void dsp_start_new_session(void)
{
    g_session_id++;
    rcc_set_dsp_warm_reset(true);
    delay_ms(50);
    control_mailbox_prepare();
    printf("[CM4-CTRL] DSP warm reset triggered, session=0x%02X\r\n", g_session_id);
}

/*===========================================================================
 * 邮箱读写
 *===========================================================================*/

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
        printf("[CM4-CTRL] TX %-20s 0x%08lX\r\n", label, (unsigned long)msg);
    } else {
        printf("[CM4-CTRL] TX %-20s failed (%d)\r\n", label, ret);
    }
}

/*===========================================================================
 * 状态与日志
 *===========================================================================*/

static const char *state_name(Cm4DemoState_t state)
{
    switch (state) {
    case CM4_DEMO_STATE_RESET: return "RESET";
    case CM4_DEMO_STATE_HANDSHAKING: return "HANDSHAKING";
    case CM4_DEMO_STATE_DSP_READY: return "DSP_READY";
    case CM4_DEMO_STATE_CM4_RESOURCE_READY: return "CM4_RESOURCE_READY";
    case CM4_DEMO_STATE_CONFIGURED: return "CONFIGURED";
    case CM4_DEMO_STATE_RUNNING: return "RUNNING";
    case CM4_DEMO_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static void enter_state(Cm4DemoState_t next)
{
    if (g_state != next) {
        printf("[CM4-CTRL] STATE %s -> %s\r\n", state_name(g_state), state_name(next));
        g_state = next;
        g_state_since_ms = millis();
    }
}

static void cm4_prepare_pseudo_resources(void)
{
    printf("[CM4-CTRL] Pseudo init video resources: camera/MM/LCD ready\r\n");
    printf("[CM4-CTRL] Pseudo init audio resources: codec/DMA/ring ready\r\n");
}

/*===========================================================================
 * 控制面发送动作
 *===========================================================================*/

static void send_hello(void)
{
    send_control_msg(CONTROL_SYS_HELLO(g_session_id, CONTROL_PROTOCOL_VERSION), "SYS.HELLO");
    g_last_hello_ms = millis();
}

static void send_resource_ready(void)
{
    uint8_t resource_flags = CONTROL_RESOURCE_CAMERA_READY |
                             CONTROL_RESOURCE_MM_READY |
                             CONTROL_RESOURCE_LCD_READY |
                             CONTROL_RESOURCE_AUDIO_READY;

    send_control_msg(
        CONTROL_SYS_CM4_RESOURCE_READY(
            g_session_id,
            CONTROL_INPUT_VIDEO_AUDIO,
            resource_flags,
            CTRL_CONFIG_SLOT_ID),
        "SYS.CM4_RESOURCE_READY");

    g_last_resource_ms = millis();
}

static void send_config_apply(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_CONFIG, g_session_id,
                         CONTROL_CMD_CONFIG_APPLY, CTRL_CONFIG_SLOT_ID),
        "CMD.CONFIG_APPLY");
    g_pending = CM4_PENDING_CONFIG_ACK;
}

static void send_buffer_bind(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_BUFFER, g_session_id,
                         CONTROL_CMD_BUFFER_BIND, CTRL_BUFFER_SLOT_ID),
        "CMD.BUFFER_BIND");
    g_pending = CM4_PENDING_BUFFER_ACK;
}

static void send_start_stream(void)
{
    send_control_msg(
        CONTROL_SYS_START_STREAM(g_session_id, CTRL_STREAM_ID_MAIN, 0x01u),
        "SYS.START_STREAM");
    g_pending = CM4_PENDING_START_ACK;
}

static void send_heartbeat(void)
{
    send_control_msg(
        CONTROL_SYS_HEARTBEAT(g_session_id, g_heartbeat_seq, CONTROL_RUN_STATE_RUNNING),
        "SYS.HEARTBEAT");
    g_heartbeat_seq++;
    g_last_heartbeat_ms = millis();
}

static void send_pseudo_video_ready(void)
{
    printf("[CM4-CTRL] Pseudo move VIDEO frame -> slot=%u\r\n", g_video_slot);
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_STREAM, g_session_id,
                         CONTROL_CMD_STREAM_FRAME_READY, g_video_slot),
        "CMD.FRAME_READY");
    g_video_slot ^= 1u;
    g_last_video_ms = millis();
}

static void send_pseudo_audio_ready(void)
{
    printf("[CM4-CTRL] Pseudo move AUDIO chunk -> slot=%u\r\n", g_audio_slot);
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_STREAM, g_session_id,
                         CONTROL_CMD_STREAM_AUDIO_READY, g_audio_slot),
        "CMD.AUDIO_READY");
    g_audio_slot ^= 1u;
    g_last_audio_ms = millis();
}

/*===========================================================================
 * 消息处理
 *===========================================================================*/

static void handle_ack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t status = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[CM4-CTRL] RX ACK kind=%u code=0x%02X status=%u\r\n",
           kind, code, status);

    if (kind == CONTROL_RSP_KIND_CMD) {
        if ((g_pending == CM4_PENDING_CONFIG_ACK) &&
            (code == CONTROL_CMD_CONFIG_APPLY) &&
            (status == CONTROL_ACK_OK)) {
            send_buffer_bind();
            return;
        }

        if ((g_pending == CM4_PENDING_BUFFER_ACK) &&
            (code == CONTROL_CMD_BUFFER_BIND) &&
            (status == CONTROL_ACK_OK)) {
            g_pending = CM4_PENDING_NONE;
            enter_state(CM4_DEMO_STATE_CONFIGURED);
            send_start_stream();
            return;
        }
    }

    if (kind == CONTROL_RSP_KIND_SYS) {
        if ((g_pending == CM4_PENDING_START_ACK) &&
            (code == CONTROL_SYS_SUBTYPE_START_STREAM) &&
            (status == CONTROL_ACK_OK)) {
            g_pending = CM4_PENDING_NONE;
            enter_state(CM4_DEMO_STATE_RUNNING);
            printf("[CM4-CTRL] Stream start ACK received, pseudo data flow enabled\r\n");
        }
    }
}

static void handle_nack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t error = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[CM4-CTRL] RX NACK kind=%u code=0x%02X error=%u\r\n",
           kind, code, error);
    g_pending = CM4_PENDING_NONE;
    enter_state(CM4_DEMO_STATE_ERROR);
}

static void handle_status(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    printf("[CM4-CTRL] RX STATUS run_state=%u brief=%u\r\n",
           (unsigned)CONTROL_STATUS_GET_RUN_STATE(arg),
           (unsigned)CONTROL_STATUS_GET_BRIEF(arg));
}

static void handle_sys_message(uint32_t msg)
{
    uint8_t subtype = (uint8_t)CONTROL_GET_SUBTYPE(msg);
    uint16_t arg = CONTROL_GET_ARG(msg);

    if (subtype == CONTROL_SYS_SUBTYPE_HELLO_ACK) {
        printf("[CM4-CTRL] RX HELLO_ACK session=0x%02X boot=%u feature=%u state=%u\r\n",
               (unsigned)CONTROL_GET_SESSION(msg),
               (unsigned)CONTROL_HELLO_ACK_GET_BOOT_REASON(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_FEATURE_LEVEL(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_RUN_STATE(arg));

        /*
         * Mailbox 采用轮询处理，HELLO_ACK 可能在本轮超时判定之后、
         * 但下一轮 session 真正启动之前才被读到。
         * 只要 session 仍匹配当前轮，就应当接受该 ACK，而不是把它丢掉。
         */
        if ((g_state == CM4_DEMO_STATE_HANDSHAKING) ||
            (g_state == CM4_DEMO_STATE_RESET)) {
            enter_state(CM4_DEMO_STATE_DSP_READY);
            cm4_prepare_pseudo_resources();
            send_resource_ready();
            enter_state(CM4_DEMO_STATE_CM4_RESOURCE_READY);
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_DSP_MODEL_READY) {
        printf("[CM4-CTRL] RX DSP_MODEL_READY algo=%u flags=%u run_state=%u\r\n",
               (unsigned)CONTROL_MODEL_READY_GET_ALGO_ID(arg),
               (unsigned)CONTROL_MODEL_READY_GET_FLAGS(arg),
               (unsigned)CONTROL_MODEL_READY_GET_RUN_STATE(arg));

        if ((g_state == CM4_DEMO_STATE_CM4_RESOURCE_READY) &&
            (g_pending == CM4_PENDING_NONE)) {
            send_config_apply();
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_HEARTBEAT) {
        printf("[CM4-CTRL] RX DSP HEARTBEAT seq=%u status=%u\r\n",
               (unsigned)CONTROL_HEARTBEAT_GET_SEQ(arg),
               (unsigned)CONTROL_HEARTBEAT_GET_STATUS(arg));
        return;
    }

    printf("[CM4-CTRL] RX SYS subtype=%u arg=0x%04X\r\n",
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
            printf("[CM4-CTRL] Drop stale message: 0x%08lX (session=0x%02X, current=0x%02X)\r\n",
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
            printf("[CM4-CTRL] RX unknown type: 0x%08lX\r\n", (unsigned long)msg);
            break;
        }
    }
}

/*===========================================================================
 * 状态推进
 *===========================================================================*/

static void step_state_machine(void)
{
    uint32_t now_ms = millis();

    switch (g_state) {
    case CM4_DEMO_STATE_RESET:
        dsp_start_new_session();
        g_pending = CM4_PENDING_NONE;
        send_hello();
        enter_state(CM4_DEMO_STATE_HANDSHAKING);
        break;

    case CM4_DEMO_STATE_HANDSHAKING:
        if ((now_ms - g_last_hello_ms) >= CTRL_HELLO_RETRY_MS) {
            send_hello();
        }
        break;

    case CM4_DEMO_STATE_CM4_RESOURCE_READY:
        if (((now_ms - g_last_resource_ms) >= CTRL_RESOURCE_RETRY_MS) &&
            (g_pending == CM4_PENDING_NONE)) {
            send_resource_ready();
        }
        break;

    case CM4_DEMO_STATE_RUNNING:
        if ((now_ms - g_last_heartbeat_ms) >= CTRL_HEARTBEAT_INTERVAL_MS) {
            send_heartbeat();
        }

        if ((now_ms - g_last_video_ms) >= CTRL_PSEUDO_VIDEO_INTERVAL_MS) {
            send_pseudo_video_ready();
        }

        if ((now_ms - g_last_audio_ms) >= CTRL_PSEUDO_AUDIO_INTERVAL_MS) {
            send_pseudo_audio_ready();
        }
        break;

    case CM4_DEMO_STATE_DSP_READY:
    case CM4_DEMO_STATE_CONFIGURED:
    case CM4_DEMO_STATE_ERROR:
    default:
        break;
    }

    if (((now_ms - g_state_since_ms) >= CTRL_RESPONSE_TIMEOUT_MS) &&
        (g_state != CM4_DEMO_STATE_RUNNING) &&
        (g_state != CM4_DEMO_STATE_RESET)) {
        /*
         * 轮询模式下，DSP 的 ACK 可能恰好在本轮 process_dsp_messages() 之后到达。
         * 在执行超时复位前再补收一次邮箱，避免首轮出现伪 timeout 日志。
         */
        process_dsp_messages();

        if ((g_state == CM4_DEMO_STATE_RUNNING) ||
            (g_state == CM4_DEMO_STATE_RESET)) {
            return;
        }

        if ((millis() - g_state_since_ms) < CTRL_RESPONSE_TIMEOUT_MS) {
            return;
        }

        printf("[CM4-CTRL] State timeout in %s, restart session\r\n", state_name(g_state));
        enter_state(CM4_DEMO_STATE_RESET);
    }
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

int main(void)
{
    board_init();
    SystemCoreClockUpdate();

    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        printf("[CM4-CTRL] SysTick config failed\r\n");
        while (1) {
            __NOP();
        }
    }

    printf("\r\n============================================\r\n");
    printf("  S300 CM4-DSP Control Protocol Demo\r\n");
    printf("============================================\r\n");
    printf("[CM4-CTRL] SystemCoreClock = %lu Hz\r\n", (unsigned long)SystemCoreClock);

    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    printf("[CM4-CTRL] Mailbox initialized\r\n");

    if (dsp_clock_init() != 0) {
        while (1) {
            __NOP();
        }
    }

    dsp_uart_init();
    g_state_since_ms = millis();

    while (1) {
        process_dsp_messages();
        step_state_machine();
    }
}