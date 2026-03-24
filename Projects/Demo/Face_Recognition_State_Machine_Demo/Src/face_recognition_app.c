#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "camera_ov5640.h"
#include "control_proto.h"
#include "face_recognition_app.h"
#include "face_recognition_overlay.h"
#include "face_recognition_runtime_proto.h"
#include "gpio.h"
#include "mailbox.h"
#include "mailbox_proto.h"
#include "rcc.h"
#include "s300.h"
#include "uart.h"
#include "video.h"

#if BOARD_CAMERA_FORMAT == 0
    #define APP_CAM_FMT CAMREA_RGB565
#else
    #define APP_CAM_FMT CAMREA_YUV422
#endif

#define FR_HELLO_RETRY_MS         200u
#define FR_RESOURCE_RETRY_MS      300u
#define FR_HEARTBEAT_INTERVAL_MS  1000u
#define FR_RESPONSE_TIMEOUT_MS    2000u

#define FR_STREAM_ID_MAIN         0x01u
#define FR_CONFIG_SLOT_ID         0x31u
#define FR_BUFFER_SLOT_ID         0x41u
#define FR_REQUIRED_VIDEO_RESOURCES \
    (CONTROL_RESOURCE_CAMERA_READY | CONTROL_RESOURCE_MM_READY | CONTROL_RESOURCE_LCD_READY)

typedef enum {
    FR_STATE_RESET = 0,
    FR_STATE_HANDSHAKING,
    FR_STATE_DSP_READY,
    FR_STATE_CM4_RESOURCE_READY,
    FR_STATE_CONFIGURED,
    FR_STATE_RUNNING,
    FR_STATE_ERROR,
} FaceRecognitionState_t;

typedef enum {
    FR_PENDING_NONE = 0,
    FR_PENDING_CONFIG_ACK,
    FR_PENDING_BUFFER_ACK,
    FR_PENDING_START_ACK,
    FR_PENDING_SESSION_START_ACK,
} FaceRecognitionPending_t;

static uint32_t (*s_get_millis)(void) = 0;
static FaceRecognitionState_t s_state = FR_STATE_RESET;
static FaceRecognitionPending_t s_pending = FR_PENDING_NONE;
static uint8_t s_session_id = 0u;
static uint8_t s_heartbeat_seq = 0u;
static uint32_t s_state_since_ms = 0u;
static uint32_t s_last_hello_ms = 0u;
static uint32_t s_last_resource_ms = 0u;
static uint32_t s_last_heartbeat_ms = 0u;
static uint8_t s_video_resource_flags = 0u;
static bool s_fr_session_started = false;

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

static const char *state_name(FaceRecognitionState_t state)
{
    switch (state) {
    case FR_STATE_RESET: return "RESET";
    case FR_STATE_HANDSHAKING: return "HANDSHAKING";
    case FR_STATE_DSP_READY: return "DSP_READY";
    case FR_STATE_CM4_RESOURCE_READY: return "CM4_RESOURCE_READY";
    case FR_STATE_CONFIGURED: return "CONFIGURED";
    case FR_STATE_RUNNING: return "RUNNING";
    case FR_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static void enter_state(FaceRecognitionState_t next_state)
{
    if (s_state != next_state) {
        printf("[FR-SM] STATE %s -> %s\r\n", state_name(s_state), state_name(next_state));
        s_state = next_state;
        s_state_since_ms = millis();
        if (next_state != FR_STATE_RUNNING) {
            face_recognition_overlay_reset();
        }
    }
}

static void fill_overlay_buffers(void)
{
    volatile uint16_t *framebuffer0 = (volatile uint16_t *)DISP_RFRAME0_ADDR;
    volatile uint16_t *framebuffer1 = (volatile uint16_t *)DISP_RFRAME1_ADDR;
    volatile uint16_t *alpha0 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    volatile uint16_t *alpha1 = (volatile uint16_t *)DISP_RALPHA1_ADDR;
    uint32_t pixels = DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT;
    uint32_t alpha_words = pixels / 2u;

    for (uint32_t index = 0; index < pixels; index++) {
        framebuffer0[index] = 0x07E0u;
        framebuffer1[index] = 0x07E0u;
    }

    for (uint32_t index = 0; index < alpha_words; index++) {
        alpha0[index] = 0x0000u;
        alpha1[index] = 0x0000u;
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
    printf("[FR-SM] CM4 applied MM runtime enable: core only, local LCD SPI path disabled\r\n");
#else
    trigger_spi_reg_update();
    printf("[FR-SM] CM4 applied MM runtime enable: core + lcd spi\r\n");
#endif
}

static int init_video_path(void)
{
    uint8_t resource_flags = 0u;
    int cam_ret = camera_ov5640_preinit();
    if (cam_ret != 0) {
        printf("[FR-SM][ERR] OV5640 init failed (%d), video resources not ready\r\n", cam_ret);
        return -1;
    }

    resource_flags |= CONTROL_RESOURCE_CAMERA_READY;

    printf("[FR-SM] init video\r\n");
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
    printf("[FR-SM] Mailbox re-synced\r\n");
}

static void dsp_uart_init(void)
{
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    set_gpio_function(GPIOA, 27, FUNCTION_3);
    set_gpio_function(GPIOA, 26, FUNCTION_3);
    init_uart(3, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), 115200);
    printf("[FR-SM] UART3 initialized for DSP\r\n");
}

static void log_video_resources_ready(void)
{
    printf("[FR-SM] CM4 video resources ready: camera=%u mm=%u lcd=%u\r\n",
           (unsigned)((s_video_resource_flags & CONTROL_RESOURCE_CAMERA_READY) != 0u),
           (unsigned)((s_video_resource_flags & CONTROL_RESOURCE_MM_READY) != 0u),
           (unsigned)((s_video_resource_flags & CONTROL_RESOURCE_LCD_READY) != 0u));
    printf("[FR-SM] waiting DSP runtime notify for MM runtime enable\r\n");
    printf("[FR-SM] Overlay %ux%u, feature compare enabled\r\n",
           (unsigned)DISP_IMAGE_WIDTH,
           (unsigned)DISP_IMAGE_HEIGHT);
}

static bool handle_runtime_message(uint32_t msg)
{
    uint32_t enable_req;

    if (!fr_runtime_msg_is_mm_enable_req(msg)) {
        return false;
    }

    if ((s_state != FR_STATE_CONFIGURED) && (s_state != FR_STATE_RUNNING)) {
        printf("[FR-SM][WARN] ignore MM enable req before stream start, state=%s msg=0x%08lX\r\n",
               state_name(s_state),
               (unsigned long)msg);
        return true;
    }

    if ((s_video_resource_flags & FR_REQUIRED_VIDEO_RESOURCES) != FR_REQUIRED_VIDEO_RESOURCES) {
        printf("[FR-SM][WARN] ignore MM enable req=0x%08lX, video resources incomplete\r\n",
               (unsigned long)msg);
        return true;
    }

    enable_req = fr_runtime_msg_get_mm_enable_req(msg);
    if ((enable_req != FR_RT_MM_ENABLE_REQ) &&
        (enable_req != FR_RT_SYNC_REQ_SPI_REG_UPDATE)) {
        printf("[FR-SM][WARN] unknown MM enable payload=0x%08lX\r\n",
               (unsigned long)enable_req);
        return true;
    }

    trigger_mm_runtime_enable();
    return true;
}

static void dsp_start_new_session(void)
{
    s_session_id++;
    face_recognition_overlay_reset();
    set_dsp_warm_reset(true);
    app_delay_ms(50u);
    control_mailbox_prepare();
    printf("[FR-SM] DSP warm reset triggered, session=0x%02X\r\n", s_session_id);
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
        printf("[FR-SM] TX %-20s 0x%08lX\r\n", label, (unsigned long)msg);
    } else {
        printf("[FR-SM] TX %-20s failed (%d)\r\n", label, ret);
    }
}

static void send_hello(void)
{
    send_control_msg(CONTROL_SYS_HELLO(s_session_id, CONTROL_PROTOCOL_VERSION), "SYS.HELLO");
    s_last_hello_ms = millis();
}

static void send_resource_ready(void)
{
    if ((s_video_resource_flags & FR_REQUIRED_VIDEO_RESOURCES) != FR_REQUIRED_VIDEO_RESOURCES) {
        printf("[FR-SM][ERR] Skip SYS.CM4_RESOURCE_READY, CM4 video resources incomplete: flags=0x%02X\r\n",
               (unsigned)s_video_resource_flags);
        return;
    }

    send_control_msg(
        CONTROL_SYS_CM4_RESOURCE_READY(
            s_session_id,
            CONTROL_INPUT_VIDEO,
            s_video_resource_flags,
            FR_CONFIG_SLOT_ID),
        "SYS.CM4_RESOURCE_READY");

    s_last_resource_ms = millis();
}

static void send_config_apply(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_CONFIG, s_session_id,
                         CONTROL_CMD_CONFIG_APPLY, FR_CONFIG_SLOT_ID),
        "CMD.CONFIG_APPLY");
    s_pending = FR_PENDING_CONFIG_ACK;
}

static void send_buffer_bind(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_BUFFER, s_session_id,
                         CONTROL_CMD_BUFFER_BIND, FR_BUFFER_SLOT_ID),
        "CMD.BUFFER_BIND");
    s_pending = FR_PENDING_BUFFER_ACK;
}

static void send_start_stream(void)
{
    send_control_msg(
        CONTROL_SYS_START_STREAM(s_session_id, FR_STREAM_ID_MAIN, 0x01u),
        "SYS.START_STREAM");
    s_pending = FR_PENDING_START_ACK;
}

static void send_fr_start_session(void)
{
    send_control_msg(
        CONTROL_CMD_FR(s_session_id, CONTROL_CMD_FR_START_SESSION, 0u),
        "CMD.FR_START_SESSION");
    s_pending = FR_PENDING_SESSION_START_ACK;
}

static void send_heartbeat(void)
{
    send_control_msg(
        CONTROL_SYS_HEARTBEAT(s_session_id, s_heartbeat_seq, CONTROL_RUN_STATE_RUNNING),
        "SYS.HEARTBEAT");
    s_heartbeat_seq++;
    s_last_heartbeat_ms = millis();
}

static void handle_ack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t status = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[FR-SM] RX ACK kind=%u code=0x%02X status=%u\r\n",
           (unsigned)kind, (unsigned)code, (unsigned)status);

    if (kind == CONTROL_RSP_KIND_CMD) {
        if ((s_pending == FR_PENDING_CONFIG_ACK) &&
            (code == CONTROL_CMD_CONFIG_APPLY) &&
            (status == CONTROL_ACK_OK)) {
            send_buffer_bind();
            return;
        }

        if ((s_pending == FR_PENDING_BUFFER_ACK) &&
            (code == CONTROL_CMD_BUFFER_BIND) &&
            (status == CONTROL_ACK_OK)) {
            s_pending = FR_PENDING_NONE;
            enter_state(FR_STATE_CONFIGURED);
            send_start_stream();
            return;
        }
    }

    if (kind == CONTROL_RSP_KIND_SYS) {
        if ((s_pending == FR_PENDING_START_ACK) &&
            (code == CONTROL_SYS_SUBTYPE_START_STREAM) &&
            (status == CONTROL_ACK_OK)) {
            enter_state(FR_STATE_RUNNING);
            printf("[FR-SM] Stream start ACK received, starting FR session\r\n");
            send_fr_start_session();
            return;
        }
    }

    if (kind == CONTROL_RSP_KIND_CMD) {
        if ((s_pending == FR_PENDING_SESSION_START_ACK) &&
            (code == CONTROL_CMD_FR_START_SESSION) &&
            (status == CONTROL_ACK_OK)) {
            s_pending = FR_PENDING_NONE;
            s_fr_session_started = true;
            printf("[FR-SM] FR session started, waiting for v2.4 detection summaries\r\n");
        }
    }
}

static void handle_nack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t error = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[FR-SM] RX NACK kind=%u code=0x%02X error=%u\r\n",
           (unsigned)kind, (unsigned)code, (unsigned)error);
    s_pending = FR_PENDING_NONE;
    enter_state(FR_STATE_ERROR);
}

static void handle_status(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    printf("[FR-SM] RX STATUS run_state=%u brief=%u\r\n",
           (unsigned)CONTROL_STATUS_GET_RUN_STATE(arg),
           (unsigned)CONTROL_STATUS_GET_BRIEF(arg));
}

static void handle_sys_message(uint32_t msg)
{
    uint8_t subtype = (uint8_t)CONTROL_GET_SUBTYPE(msg);
    uint16_t arg = CONTROL_GET_ARG(msg);

    if (subtype == CONTROL_SYS_SUBTYPE_HELLO_ACK) {
        printf("[FR-SM] RX HELLO_ACK session=0x%02X boot=%u feature=%u state=%u\r\n",
               (unsigned)CONTROL_GET_SESSION(msg),
               (unsigned)CONTROL_HELLO_ACK_GET_BOOT_REASON(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_FEATURE_LEVEL(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_RUN_STATE(arg));

        if ((s_state == FR_STATE_HANDSHAKING) ||
            (s_state == FR_STATE_RESET)) {
            enter_state(FR_STATE_DSP_READY);
            log_video_resources_ready();
            send_resource_ready();
            enter_state(FR_STATE_CM4_RESOURCE_READY);
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_DSP_MODEL_READY) {
        printf("[FR-SM] RX DSP_MODEL_READY algo=%u flags=%u run_state=%u\r\n",
               (unsigned)CONTROL_MODEL_READY_GET_ALGO_ID(arg),
               (unsigned)CONTROL_MODEL_READY_GET_FLAGS(arg),
               (unsigned)CONTROL_MODEL_READY_GET_RUN_STATE(arg));

        if ((s_state == FR_STATE_CM4_RESOURCE_READY) &&
            (s_pending == FR_PENDING_NONE)) {
            send_config_apply();
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_HEARTBEAT) {
        printf("[FR-SM] RX DSP HEARTBEAT seq=%u status=%u\r\n",
               (unsigned)CONTROL_HEARTBEAT_GET_SEQ(arg),
               (unsigned)CONTROL_HEARTBEAT_GET_STATUS(arg));
        return;
    }

    printf("[FR-SM] RX SYS subtype=%u arg=0x%04X\r\n",
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
                printf("[FR-SM] Drop stale control msg: 0x%08lX (session=0x%02X current=0x%02X)\r\n",
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
            if (s_state == FR_STATE_RUNNING) {
                (void)face_recognition_overlay_handle_mailbox_message(msg);
            }
            break;
        }
    }
}

static void step_state_machine(void)
{
    uint32_t now_ms = millis();

    switch (s_state) {
    case FR_STATE_RESET:
        dsp_start_new_session();
        s_pending = FR_PENDING_NONE;
        s_fr_session_started = false;
        send_hello();
        enter_state(FR_STATE_HANDSHAKING);
        break;

    case FR_STATE_HANDSHAKING:
        if ((uint32_t)(now_ms - s_last_hello_ms) >= FR_HELLO_RETRY_MS) {
            send_hello();
        }
        break;

    case FR_STATE_CM4_RESOURCE_READY:
        if (((uint32_t)(now_ms - s_last_resource_ms) >= FR_RESOURCE_RETRY_MS) &&
            (s_pending == FR_PENDING_NONE)) {
            send_resource_ready();
        }
        break;

    case FR_STATE_RUNNING:
        if ((uint32_t)(now_ms - s_last_heartbeat_ms) >= FR_HEARTBEAT_INTERVAL_MS) {
            send_heartbeat();
        }
        face_recognition_overlay_tick();
        break;

    case FR_STATE_DSP_READY:
    case FR_STATE_CONFIGURED:
    case FR_STATE_ERROR:
    default:
        break;
    }

    if (((uint32_t)(now_ms - s_state_since_ms) >= FR_RESPONSE_TIMEOUT_MS) &&
        (s_state != FR_STATE_RUNNING) &&
        (s_state != FR_STATE_RESET)) {
        process_mailbox();

        if ((s_state == FR_STATE_RUNNING) ||
            (s_state == FR_STATE_RESET)) {
            return;
        }

        if ((uint32_t)(millis() - s_state_since_ms) < FR_RESPONSE_TIMEOUT_MS) {
            return;
        }

        printf("[FR-SM] State timeout in %s, restart session\r\n", state_name(s_state));
        enter_state(FR_STATE_RESET);
    }
}

void face_recognition_app_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;
    s_video_resource_flags = 0u;

    if (init_video_path() != 0) {
        enter_state(FR_STATE_ERROR);
        printf("[FR-SM][ERR] Face recognition demo requires CM4-owned camera/MM/LCD resources\r\n");
        return;
    }

    face_recognition_overlay_init(get_millis_fn);
    control_mailbox_prepare();
    dsp_uart_init();
    s_state_since_ms = millis();
    printf("[FR-SM] Face recognition state machine demo started\r\n");
}

void face_recognition_app_tick(void)
{
    process_mailbox();
    step_state_machine();
}