#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "camera_ov5640.h"
#include "control_proto.h"
#include "detection_proto.h"
#include "gpio.h"
#include "human_tracking_app.h"
#include "human_tracking_overlay.h"
#include "human_tracking_runtime_proto.h"
#include "human_tracking_target.h"
#include "mailbox.h"
#include "mailbox_proto.h"
#include "rcc.h"
#include "s300.h"
#include "uart.h"
#include "uart_s300.h"
#include "video.h"

#if BOARD_CAMERA_FORMAT == 0
    #define APP_CAM_FMT CAMREA_RGB565
#else
    #define APP_CAM_FMT CAMREA_YUV422
#endif

#define HT_HELLO_RETRY_MS             200u
#define HT_RESOURCE_RETRY_MS          300u
#define HT_HEARTBEAT_INTERVAL_MS      1000u
#define HT_RESPONSE_TIMEOUT_MS        2000u
#define HT_FRAME_LOG_EVERY_N_FRAMES   30u

#define HT_STREAM_ID_MAIN         0x01u
#define HT_CONFIG_SLOT_ID         0x31u
#define HT_BUFFER_SLOT_ID         0x41u
#define HT_REQUIRED_VIDEO_RESOURCES \
    (CONTROL_RESOURCE_CAMERA_READY | CONTROL_RESOURCE_MM_READY | CONTROL_RESOURCE_LCD_READY)

typedef enum {
    HT_STATE_RESET = 0,
    HT_STATE_HANDSHAKING,
    HT_STATE_DSP_READY,
    HT_STATE_CM4_RESOURCE_READY,
    HT_STATE_CONFIGURED,
    HT_STATE_RUNNING,
    HT_STATE_ERROR,
} HumanTrackingState_t;

typedef enum {
    HT_PENDING_NONE = 0,
    HT_PENDING_CONFIG_ACK,
    HT_PENDING_BUFFER_ACK,
    HT_PENDING_START_ACK,
    HT_PENDING_TRACK_START_ACK,
    HT_PENDING_TRACK_STOP_ACK,
    HT_PENDING_TRACK_RESET_ACK,
} HumanTrackingPending_t;

typedef enum {
    HT_TRACK_MODE_IDLE = 0,
    HT_TRACK_MODE_FOLLOWING,
} HumanTrackingTrackMode_t;

static uint32_t (*s_get_millis)(void) = 0;
static HumanTrackingState_t s_state = HT_STATE_RESET;
static HumanTrackingPending_t s_pending = HT_PENDING_NONE;
static HumanTrackingTrackMode_t s_track_mode = HT_TRACK_MODE_IDLE;
static uint8_t s_session_id = 0u;
static uint8_t s_heartbeat_seq = 0u;
static uint32_t s_state_since_ms = 0u;
static uint32_t s_last_hello_ms = 0u;
static uint32_t s_last_resource_ms = 0u;
static uint32_t s_last_heartbeat_ms = 0u;
static uint8_t s_video_resource_flags = 0u;

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
}

static void app_delay_ms(uint32_t delay)
{
    uint32_t start_ms = millis();
    while ((uint32_t)(millis() - start_ms) < delay) {
        __NOP();
    }
}

static S300_UART_TypeDef *debug_uart_dev(void)
{
    switch (BOARD_DEBUG_UART_IDX) {
    case 0: return UART0;
    case 1: return UART1;
    case 2: return UART2;
    case 3: return UART3;
    default: return UART2;
    }
}

static int debug_uart_getchar_noblock(uint8_t *out_char)
{
    S300_UART_TypeDef *uart;

    if (out_char == 0) {
        return 0;
    }

    uart = debug_uart_dev();
    if ((uart->USR & 0x08u) == 0u) {
        return 0;
    }

    *out_char = (uint8_t)(uart->RBR_THR_DLL & 0xFFu);
    return 1;
}

static const char *state_name(HumanTrackingState_t state)
{
    switch (state) {
    case HT_STATE_RESET: return "RESET";
    case HT_STATE_HANDSHAKING: return "HANDSHAKING";
    case HT_STATE_DSP_READY: return "DSP_READY";
    case HT_STATE_CM4_RESOURCE_READY: return "CM4_RESOURCE_READY";
    case HT_STATE_CONFIGURED: return "CONFIGURED";
    case HT_STATE_RUNNING: return "RUNNING";
    case HT_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static const char *track_mode_name(HumanTrackingTrackMode_t mode)
{
    return (mode == HT_TRACK_MODE_FOLLOWING) ? "FOLLOWING" : "IDLE";
}

static const char *tracker_state_name(uint8_t tracker_state)
{
    switch (tracker_state) {
    case TRACKER_STATE_DISABLED: return "DISABLED";
    case TRACKER_STATE_IDLE: return "IDLE";
    case TRACKER_STATE_TENTATIVE: return "TENTATIVE";
    case TRACKER_STATE_TRACKING: return "TRACKING";
    case TRACKER_STATE_LOST: return "LOST";
    default: return "UNKNOWN";
    }
}

static void enter_state(HumanTrackingState_t next_state)
{
    if (s_state != next_state) {
        printf("[HT-SM] STATE %s -> %s\r\n", state_name(s_state), state_name(next_state));
        s_state = next_state;
        s_state_since_ms = millis();
        if (next_state != HT_STATE_RUNNING) {
            human_tracking_overlay_reset();
        }
        if ((next_state == HT_STATE_RESET) || (next_state == HT_STATE_ERROR)) {
            s_track_mode = HT_TRACK_MODE_IDLE;
            human_tracking_target_reset();
            human_tracking_overlay_set_tracking_active(false);
        }
    }
}

static void fill_overlay_buffers(void)
{
    volatile uint16_t *framebuffer = (volatile uint16_t *)DISP_RFRAME0_ADDR;
    volatile uint16_t *alpha16 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    uint32_t pixels = DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT;
    uint32_t alpha_words = pixels / 2u;

    for (uint32_t index = 0; index < pixels; index++) {
        framebuffer[index] = 0x07E0u;
    }

    for (uint32_t index = 0; index < alpha_words; index++) {
        alpha16[index] = 0x0000u;
    }

    *(volatile uint32_t *)(DSP_VIDEO_SS_BASE + 0x50u) = 1u;
}

static void trigger_core_reg_update(void)
{
    REG32(DSP_VIDEO_SS_BASE + 0x70u) = 1u;
}

#if !defined(BOARD_LCD_SPI_ENABLE_ON_INIT) || (BOARD_LCD_SPI_ENABLE_ON_INIT != 0)
static void trigger_spi_reg_update(void)
{
    REG32(DSP_VIDEO_SS_BASE + 0x1E0u) = 1u;
}
#endif

static void trigger_mm_runtime_enable(void)
{
    trigger_core_reg_update();

#if defined(BOARD_LCD_SPI_ENABLE_ON_INIT) && (BOARD_LCD_SPI_ENABLE_ON_INIT == 0)
    printf("[HT-SM] CM4 applied MM runtime enable: core only, local LCD SPI path disabled\r\n");
#else
    trigger_spi_reg_update();
    printf("[HT-SM] CM4 applied MM runtime enable: core + lcd spi\r\n");
#endif
}

static int init_video_path(void)
{
    uint8_t resource_flags = 0u;
    int cam_ret = camera_ov5640_preinit();
    if (cam_ret != 0) {
        printf("[HT-SM][ERR] OV5640 init failed (%d), video resources not ready\r\n", cam_ret);
        return -1;
    }

    resource_flags |= CONTROL_RESOURCE_CAMERA_READY;

    printf("[HT-SM] init video\r\n");
    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);
    resource_flags |= CONTROL_RESOURCE_MM_READY | CONTROL_RESOURCE_LCD_READY;

    fill_overlay_buffers();

    s_video_resource_flags = resource_flags;
    return 0;
}

static void control_mailbox_prepare(void)
{
    (void)init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    (void)init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    printf("[HT-SM] Mailbox re-synced\r\n");
}

static void dsp_uart_init(void)
{
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    set_gpio_function(GPIOA, 27, FUNCTION_3);
    set_gpio_function(GPIOA, 26, FUNCTION_3);
    init_uart(3, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), 115200);
    printf("[HT-SM] UART3 initialized for DSP\r\n");
}

static void print_serial_help(void)
{
    printf("[HT-SM] Serial cmd: s=start x=stop r=reset p=status h=help\r\n");
}

static void log_video_resources_ready(void)
{
    printf("[HT-SM] CM4 video resources ready: camera=%u mm=%u lcd=%u\r\n",
           (unsigned)((s_video_resource_flags & CONTROL_RESOURCE_CAMERA_READY) != 0u),
           (unsigned)((s_video_resource_flags & CONTROL_RESOURCE_MM_READY) != 0u),
           (unsigned)((s_video_resource_flags & CONTROL_RESOURCE_LCD_READY) != 0u));
    printf("[HT-SM] waiting DSP runtime notify for MM runtime enable\r\n");
    printf("[HT-SM] Overlay %ux%u, detection_base=0x%08lX\r\n",
           (unsigned)DISP_IMAGE_WIDTH,
           (unsigned)DISP_IMAGE_HEIGHT,
           (unsigned long)DSP_DETECTION_BASE_ADDR);
}

static bool handle_runtime_message(uint32_t msg)
{
    uint32_t enable_req;

    if (!ht_runtime_msg_is_mm_enable_req(msg)) {
        return false;
    }

    if ((s_state != HT_STATE_CONFIGURED) && (s_state != HT_STATE_RUNNING)) {
        printf("[HT-SM][WARN] ignore MM enable req before stream start, state=%s msg=0x%08lX\r\n",
               state_name(s_state),
               (unsigned long)msg);
        return true;
    }

    if ((s_video_resource_flags & HT_REQUIRED_VIDEO_RESOURCES) != HT_REQUIRED_VIDEO_RESOURCES) {
        printf("[HT-SM][WARN] ignore MM enable req=0x%08lX, video resources incomplete\r\n",
               (unsigned long)msg);
        return true;
    }

    enable_req = ht_runtime_msg_get_mm_enable_req(msg);
    if ((enable_req != HT_RT_MM_ENABLE_REQ) &&
        (enable_req != HT_RT_SYNC_REQ_SPI_REG_UPDATE)) {
        printf("[HT-SM][WARN] unknown MM enable payload=0x%08lX\r\n",
               (unsigned long)enable_req);
        return true;
    }

    trigger_mm_runtime_enable();
    return true;
}

static void dsp_start_new_session(void)
{
    s_session_id++;
    s_track_mode = HT_TRACK_MODE_IDLE;
    human_tracking_target_reset();
    human_tracking_overlay_reset();
    set_dsp_warm_reset(true);
    app_delay_ms(50u);
    control_mailbox_prepare();
    printf("[HT-SM] DSP warm reset triggered, session=0x%02X\r\n", s_session_id);
}

static int read_dsp_message(uint32_t *out_msg)
{
    if (out_msg == 0) {
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
        printf("[HT-SM] TX %-20s 0x%08lX\r\n", label, (unsigned long)msg);
    } else {
        printf("[HT-SM] TX %-20s failed (%d)\r\n", label, ret);
    }
}

static void send_hello(void)
{
    send_control_msg(CONTROL_SYS_HELLO(s_session_id, CONTROL_PROTOCOL_VERSION), "SYS.HELLO");
    s_last_hello_ms = millis();
}

static void send_resource_ready(void)
{
    if ((s_video_resource_flags & HT_REQUIRED_VIDEO_RESOURCES) != HT_REQUIRED_VIDEO_RESOURCES) {
        printf("[HT-SM][ERR] Skip SYS.CM4_RESOURCE_READY, CM4 video resources incomplete: flags=0x%02X\r\n",
               (unsigned)s_video_resource_flags);
        return;
    }

    send_control_msg(
        CONTROL_SYS_CM4_RESOURCE_READY(
            s_session_id,
            CONTROL_INPUT_VIDEO,
            s_video_resource_flags,
            HT_CONFIG_SLOT_ID),
        "SYS.CM4_RESOURCE_READY");

    s_last_resource_ms = millis();
}

static void send_config_apply(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_CONFIG, s_session_id,
                         CONTROL_CMD_CONFIG_APPLY, HT_CONFIG_SLOT_ID),
        "CMD.CONFIG_APPLY");
    s_pending = HT_PENDING_CONFIG_ACK;
}

static void send_buffer_bind(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_BUFFER, s_session_id,
                         CONTROL_CMD_BUFFER_BIND, HT_BUFFER_SLOT_ID),
        "CMD.BUFFER_BIND");
    s_pending = HT_PENDING_BUFFER_ACK;
}

static void send_start_stream(void)
{
    send_control_msg(
        CONTROL_SYS_START_STREAM(s_session_id, HT_STREAM_ID_MAIN, 0x01u),
        "SYS.START_STREAM");
    s_pending = HT_PENDING_START_ACK;
}

static void send_heartbeat(void)
{
    send_control_msg(
        CONTROL_SYS_HEARTBEAT(s_session_id, s_heartbeat_seq, CONTROL_RUN_STATE_RUNNING),
        "SYS.HEARTBEAT");
    s_heartbeat_seq++;
    s_last_heartbeat_ms = millis();
}

static void send_track_command(uint8_t opcode, const char *label, HumanTrackingPending_t pending)
{
    if (s_state != HT_STATE_RUNNING) {
        printf("[HT-SM][WARN] ignore %s before RUNNING\r\n", label);
        return;
    }

    if (s_pending != HT_PENDING_NONE) {
        printf("[HT-SM][WARN] ignore %s while pending=%u\r\n", label, (unsigned)s_pending);
        return;
    }

    send_control_msg(CONTROL_CMD_TRACK(s_session_id, opcode, 0u), label);
    s_pending = pending;
}

static void send_track_start(void)
{
    send_track_command(CONTROL_CMD_TRACK_START, "CMD.TRACK_START", HT_PENDING_TRACK_START_ACK);
}

static void send_track_stop(void)
{
    send_track_command(CONTROL_CMD_TRACK_STOP, "CMD.TRACK_STOP", HT_PENDING_TRACK_STOP_ACK);
}

static void send_track_reset(void)
{
    send_track_command(CONTROL_CMD_TRACK_RESET, "CMD.TRACK_RESET", HT_PENDING_TRACK_RESET_ACK);
}

static void print_tracking_status(void)
{
    human_tracking_target_snapshot_t snapshot;
    human_tracking_gimbal_control_t gimbal_control;

    human_tracking_target_capture(s_track_mode == HT_TRACK_MODE_FOLLOWING, &snapshot);
    human_tracking_target_fill_gimbal_control(&snapshot, millis(), &gimbal_control);
    printf("[HT-SM] status app=%s mode=%s effective=%s target=%u frame=%lu count=%lu selected=%ld raw_tracker=%s raw_flags=0x%02X id=%u score=%u miss=%u cx=%ld cy=%ld err_x=%ld err_y=%ld box=%ldx%ld pred=%u lost=%u ycmd=%.3f pcmd=%.3f dz=%u/%u frozen=%u fms=%lu timeout=%u\r\n",
           state_name(s_state),
           track_mode_name(s_track_mode),
           human_tracking_target_view_name(snapshot.effective_view),
           (unsigned)snapshot.overlay.has_target,
           (unsigned long)snapshot.overlay.frame_id,
           (unsigned long)snapshot.overlay.count,
           (long)snapshot.overlay.selected_idx,
           tracker_state_name(snapshot.overlay.tracker_state),
           (unsigned)snapshot.overlay.tracker_flags,
           (unsigned)snapshot.overlay.primary_track_id,
           (unsigned)snapshot.overlay.primary_score_pct,
           (unsigned)snapshot.overlay.primary_miss_count,
           (long)gimbal_control.target.target_cx,
           (long)gimbal_control.target.target_cy,
           (long)gimbal_control.target.error_x,
           (long)gimbal_control.target.error_y,
           (long)gimbal_control.target.box_w,
           (long)gimbal_control.target.box_h,
           (unsigned)gimbal_control.target.predicted,
           (unsigned)gimbal_control.target.lost,
           (double)gimbal_control.yaw_cmd,
           (double)gimbal_control.pitch_cmd,
           (unsigned)gimbal_control.yaw_in_deadzone,
           (unsigned)gimbal_control.pitch_in_deadzone,
           (unsigned)gimbal_control.frozen,
           (unsigned long)gimbal_control.freeze_age_ms,
           (unsigned)gimbal_control.freeze_timed_out);
}

static void process_serial_command(void)
{
    uint8_t cmd;

    while (debug_uart_getchar_noblock(&cmd)) {
        if ((cmd == '\r') || (cmd == '\n') || (cmd == ' ')) {
            continue;
        }

        if ((cmd >= 'A') && (cmd <= 'Z')) {
            cmd = (uint8_t)(cmd - 'A' + 'a');
        }

        switch (cmd) {
        case 's':
            send_track_start();
            break;
        case 'x':
            send_track_stop();
            break;
        case 'r':
            send_track_reset();
            break;
        case 'p':
            print_tracking_status();
            break;
        case 'h':
        case '?':
            print_serial_help();
            break;
        default:
            printf("[HT-SM] unknown cmd '%c'\r\n", (char)cmd);
            print_serial_help();
            break;
        }
    }
}

static void update_tracking_status_from_overlay(void)
{
    human_tracking_target_snapshot_t snapshot;
    human_tracking_gimbal_control_t gimbal_control;

    human_tracking_target_capture(s_track_mode == HT_TRACK_MODE_FOLLOWING, &snapshot);
    human_tracking_target_fill_gimbal_control(&snapshot, millis(), &gimbal_control);

    if (human_tracking_target_consume_snapshot(&snapshot, HT_FRAME_LOG_EVERY_N_FRAMES)) {
        printf("[HT-SM] DSP frame=%lu target=%u selected=%ld mode=%s effective=%s raw_tracker=%s raw_flags=0x%02X id=%u score=%u miss=%u cx=%ld cy=%ld err_x=%ld err_y=%ld box=%ldx%ld pred=%u lost=%u ycmd=%.3f pcmd=%.3f dz=%u/%u frozen=%u fms=%lu timeout=%u\r\n",
               (unsigned long)snapshot.overlay.frame_id,
               (unsigned)snapshot.overlay.has_target,
               (long)snapshot.overlay.selected_idx,
               track_mode_name(s_track_mode),
               human_tracking_target_view_name(snapshot.effective_view),
               tracker_state_name(snapshot.overlay.tracker_state),
               (unsigned)snapshot.overlay.tracker_flags,
               (unsigned)snapshot.overlay.primary_track_id,
               (unsigned)snapshot.overlay.primary_score_pct,
               (unsigned)snapshot.overlay.primary_miss_count,
               (long)gimbal_control.target.target_cx,
               (long)gimbal_control.target.target_cy,
               (long)gimbal_control.target.error_x,
               (long)gimbal_control.target.error_y,
               (long)gimbal_control.target.box_w,
               (long)gimbal_control.target.box_h,
               (unsigned)gimbal_control.target.predicted,
               (unsigned)gimbal_control.target.lost,
               (double)gimbal_control.yaw_cmd,
               (double)gimbal_control.pitch_cmd,
               (unsigned)gimbal_control.yaw_in_deadzone,
               (unsigned)gimbal_control.pitch_in_deadzone,
               (unsigned)gimbal_control.frozen,
               (unsigned long)gimbal_control.freeze_age_ms,
               (unsigned)gimbal_control.freeze_timed_out);
    }
}

static void handle_ack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t status = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[HT-SM] RX ACK kind=%u code=0x%02X status=%u\r\n",
           (unsigned)kind, (unsigned)code, (unsigned)status);

    if (kind == CONTROL_RSP_KIND_CMD) {
        if ((s_pending == HT_PENDING_TRACK_START_ACK) &&
            (code == CONTROL_CMD_TRACK_START) &&
            (status == CONTROL_ACK_OK)) {
            s_pending = HT_PENDING_NONE;
            s_track_mode = HT_TRACK_MODE_FOLLOWING;
            human_tracking_overlay_set_tracking_active(true);
            printf("[HT-SM] tracking start ACK, mode=%s\r\n", track_mode_name(s_track_mode));
            return;
        }

        if ((s_pending == HT_PENDING_TRACK_STOP_ACK) &&
            (code == CONTROL_CMD_TRACK_STOP) &&
            (status == CONTROL_ACK_OK)) {
            s_pending = HT_PENDING_NONE;
            s_track_mode = HT_TRACK_MODE_IDLE;
            human_tracking_overlay_set_tracking_active(false);
            printf("[HT-SM] tracking stop ACK, mode=%s\r\n", track_mode_name(s_track_mode));
            return;
        }

        if ((s_pending == HT_PENDING_TRACK_RESET_ACK) &&
            (code == CONTROL_CMD_TRACK_RESET) &&
            (status == CONTROL_ACK_OK)) {
            s_pending = HT_PENDING_NONE;
            s_track_mode = HT_TRACK_MODE_IDLE;
            human_tracking_overlay_set_tracking_active(false);
            printf("[HT-SM] tracking reset ACK, mode=%s\r\n", track_mode_name(s_track_mode));
            return;
        }

        if ((s_pending == HT_PENDING_CONFIG_ACK) &&
            (code == CONTROL_CMD_CONFIG_APPLY) &&
            (status == CONTROL_ACK_OK)) {
            send_buffer_bind();
            return;
        }

        if ((s_pending == HT_PENDING_BUFFER_ACK) &&
            (code == CONTROL_CMD_BUFFER_BIND) &&
            (status == CONTROL_ACK_OK)) {
            s_pending = HT_PENDING_NONE;
            enter_state(HT_STATE_CONFIGURED);
            send_start_stream();
            return;
        }
    }

    if (kind == CONTROL_RSP_KIND_SYS) {
        if ((s_pending == HT_PENDING_START_ACK) &&
            (code == CONTROL_SYS_SUBTYPE_START_STREAM) &&
            (status == CONTROL_ACK_OK)) {
            s_pending = HT_PENDING_NONE;
            enter_state(HT_STATE_RUNNING);
            printf("[HT-SM] Stream start ACK received, waiting for tracking result mailbox\r\n");
        }
    }
}

static void handle_nack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t error = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[HT-SM] RX NACK kind=%u code=0x%02X error=%u\r\n",
           (unsigned)kind, (unsigned)code, (unsigned)error);

    if ((kind == CONTROL_RSP_KIND_CMD) &&
        ((s_pending == HT_PENDING_TRACK_START_ACK) ||
         (s_pending == HT_PENDING_TRACK_STOP_ACK) ||
         (s_pending == HT_PENDING_TRACK_RESET_ACK))) {
        s_pending = HT_PENDING_NONE;
        if ((code == CONTROL_CMD_TRACK_STOP) || (code == CONTROL_CMD_TRACK_RESET)) {
            s_track_mode = HT_TRACK_MODE_IDLE;
            human_tracking_overlay_set_tracking_active(false);
        }
        printf("[HT-SM][WARN] track cmd rejected, stay mode=%s\r\n", track_mode_name(s_track_mode));
        return;
    }

    s_pending = HT_PENDING_NONE;
    enter_state(HT_STATE_ERROR);
}

static void handle_status(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    printf("[HT-SM] RX STATUS run_state=%u brief=%u\r\n",
           (unsigned)CONTROL_STATUS_GET_RUN_STATE(arg),
           (unsigned)CONTROL_STATUS_GET_BRIEF(arg));
}

static void handle_sys_message(uint32_t msg)
{
    uint8_t subtype = (uint8_t)CONTROL_GET_SUBTYPE(msg);
    uint16_t arg = CONTROL_GET_ARG(msg);

    if (subtype == CONTROL_SYS_SUBTYPE_HELLO_ACK) {
        printf("[HT-SM] RX HELLO_ACK session=0x%02X boot=%u feature=%u state=%u\r\n",
               (unsigned)CONTROL_GET_SESSION(msg),
               (unsigned)CONTROL_HELLO_ACK_GET_BOOT_REASON(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_FEATURE_LEVEL(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_RUN_STATE(arg));

        if ((s_state == HT_STATE_HANDSHAKING) ||
            (s_state == HT_STATE_RESET)) {
            enter_state(HT_STATE_DSP_READY);
            log_video_resources_ready();
            send_resource_ready();
            enter_state(HT_STATE_CM4_RESOURCE_READY);
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_DSP_MODEL_READY) {
        printf("[HT-SM] RX DSP_MODEL_READY algo=%u flags=%u run_state=%u\r\n",
               (unsigned)CONTROL_MODEL_READY_GET_ALGO_ID(arg),
               (unsigned)CONTROL_MODEL_READY_GET_FLAGS(arg),
               (unsigned)CONTROL_MODEL_READY_GET_RUN_STATE(arg));

        if ((s_state == HT_STATE_CM4_RESOURCE_READY) &&
            (s_pending == HT_PENDING_NONE)) {
            send_config_apply();
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_HEARTBEAT) {
        printf("[HT-SM] RX DSP HEARTBEAT seq=%u status=%u\r\n",
               (unsigned)CONTROL_HEARTBEAT_GET_SEQ(arg),
               (unsigned)CONTROL_HEARTBEAT_GET_STATUS(arg));
        return;
    }

    printf("[HT-SM] RX SYS subtype=%u arg=0x%04X\r\n",
           (unsigned)subtype, (unsigned)arg);
}

static void process_mailbox(void)
{
    while (1) {
        uint32_t msg;

        if (read_dsp_message(&msg) != 0) {
            break;
        }

        if (handle_runtime_message(msg)) {
            continue;
        }

        switch (CONTROL_GET_TYPE(msg)) {
        case CONTROL_MSG_TYPE_SYS:
        case CONTROL_MSG_TYPE_ACK:
        case CONTROL_MSG_TYPE_NACK:
        case CONTROL_MSG_TYPE_STATUS:
            if (!control_msg_session_matches(msg, s_session_id)) {
                printf("[HT-SM] Drop stale control msg: 0x%08lX (session=0x%02X current=0x%02X)\r\n",
                       (unsigned long)msg,
                       (unsigned)CONTROL_GET_SESSION(msg),
                       (unsigned)s_session_id);
                break;
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
                break;
            }
            break;

        default:
            if ((s_state == HT_STATE_RUNNING) && human_tracking_overlay_handle_mailbox_message(msg)) {
                update_tracking_status_from_overlay();
            }
            break;
        }
    }
}

static void step_state_machine(void)
{
    uint32_t now_ms = millis();

    switch (s_state) {
    case HT_STATE_RESET:
        dsp_start_new_session();
        s_pending = HT_PENDING_NONE;
        send_hello();
        enter_state(HT_STATE_HANDSHAKING);
        break;

    case HT_STATE_HANDSHAKING:
        if ((uint32_t)(now_ms - s_last_hello_ms) >= HT_HELLO_RETRY_MS) {
            send_hello();
        }
        break;

    case HT_STATE_CM4_RESOURCE_READY:
        if (((uint32_t)(now_ms - s_last_resource_ms) >= HT_RESOURCE_RETRY_MS) &&
            (s_pending == HT_PENDING_NONE)) {
            send_resource_ready();
        }
        break;

    case HT_STATE_RUNNING:
        if ((uint32_t)(now_ms - s_last_heartbeat_ms) >= HT_HEARTBEAT_INTERVAL_MS) {
            send_heartbeat();
        }
        human_tracking_overlay_tick();
        break;

    case HT_STATE_DSP_READY:
    case HT_STATE_CONFIGURED:
    case HT_STATE_ERROR:
    default:
        break;
    }

    if (((uint32_t)(now_ms - s_state_since_ms) >= HT_RESPONSE_TIMEOUT_MS) &&
        (s_state != HT_STATE_RUNNING) &&
        (s_state != HT_STATE_RESET)) {
        process_mailbox();

        if ((s_state == HT_STATE_RUNNING) ||
            (s_state == HT_STATE_RESET)) {
            return;
        }

        if ((uint32_t)(millis() - s_state_since_ms) < HT_RESPONSE_TIMEOUT_MS) {
            return;
        }

        printf("[HT-SM] State timeout in %s, restart session\r\n", state_name(s_state));
        enter_state(HT_STATE_RESET);
    }
}

void human_tracking_app_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;
    s_video_resource_flags = 0u;

    if (init_video_path() != 0) {
        enter_state(HT_STATE_ERROR);
        printf("[HT-SM][ERR] Human tracking demo requires CM4-owned camera/MM/LCD resources\r\n");
        return;
    }

    human_tracking_overlay_init(get_millis_fn);
    control_mailbox_prepare();
    dsp_uart_init();
    s_state_since_ms = millis();
    printf("[HT-SM] Human tracking state machine demo started\r\n");
    print_serial_help();
}

void human_tracking_app_tick(void)
{
    process_mailbox();
    process_serial_command();
    step_state_machine();
}