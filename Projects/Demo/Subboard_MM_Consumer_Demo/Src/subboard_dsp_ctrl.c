#include "subboard_dsp_ctrl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "control_proto.h"
#include "detection_proto.h"
#include "mailbox.h"
#include "rcc.h"
#include "s300.h"
#include "subboard_runtime_proto.h"
#include "subboard_startup_proto.h"

#define SUB_DSP_HELLO_RETRY_MS        200u
#define SUB_DSP_RESOURCE_RETRY_MS     300u
#define SUB_DSP_HEARTBEAT_INTERVAL_MS 1000u
#define SUB_DSP_RESPONSE_TIMEOUT_MS   2000u

#define SUB_DSP_STREAM_ID_MAIN        0x01u
#define SUB_DSP_CONFIG_SLOT_ID        0x31u
#define SUB_DSP_BUFFER_SLOT_ID        0x41u

#define SUB_DSP_REQ_MASK_MM_RUNTIME   (1u << 0)

typedef enum {
    SUB_DSP_STATE_IDLE = 0,
    SUB_DSP_STATE_HANDSHAKING,
    SUB_DSP_STATE_CM4_RESOURCE_READY,
    SUB_DSP_STATE_DSP_READY,
    SUB_DSP_STATE_CONFIGURED,
    SUB_DSP_STATE_RUNNING,
    SUB_DSP_STATE_ERROR,
} SubboardDspState_t;

typedef enum {
    SUB_DSP_PENDING_NONE = 0,
    SUB_DSP_PENDING_CONFIG_ACK,
    SUB_DSP_PENDING_BUFFER_ACK,
    SUB_DSP_PENDING_START_ACK,
} SubboardDspPending_t;

static uint32_t (*s_get_millis)(void) = 0;
static SubboardDspState_t s_state = SUB_DSP_STATE_IDLE;
static SubboardDspPending_t s_pending = SUB_DSP_PENDING_NONE;
static bool s_mm_ready = false;
static uint8_t s_resource_flags = 0u;
static subboard_detection_result_t s_latest_result;
static uint8_t s_session_id = 0u;
static uint8_t s_heartbeat_seq = 0u;
static uint8_t s_pending_master_requests = 0u;
static uint32_t s_state_since_ms = 0u;
static uint32_t s_last_hello_ms = 0u;
static uint32_t s_last_resource_ms = 0u;
static uint32_t s_last_heartbeat_ms = 0u;

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
}

static int16_t to_i16_clamped(int32_t value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (int16_t)value;
}

static uint8_t score_to_u8(float score)
{
    int32_t value = (int32_t)(score * 100.0f + 0.5f);

    if (value < 0) {
        value = 0;
    }
    if (value > 100) {
        value = 100;
    }
    return (uint8_t)value;
}

static bool is_face_like_type(uint8_t raw_type)
{
    DetectionType_t type = detection_type_from_raw(raw_type);
    return (type == DETECTION_TYPE_FACE) || (type == DETECTION_TYPE_UNKNOWN);
}

static void clear_latest_result(void)
{
    memset(&s_latest_result, 0, sizeof(s_latest_result));
}

static int find_best_face(const DetectionResult_t *result)
{
    int best_idx = -1;
    float best_score = 0.0f;

    if ((result->selected_idx >= 0) &&
        (result->selected_idx < (int32_t)result->count) &&
        is_face_like_type(result->boxes[result->selected_idx].type)) {
        return result->selected_idx;
    }

    for (uint32_t index = 0u; index < result->count; index++) {
        const DetectionBox_t *box = &result->boxes[index];

        if (!is_face_like_type(box->type)) {
            continue;
        }

        if ((best_idx < 0) || (box->score > best_score)) {
            best_idx = (int)index;
            best_score = box->score;
        }
    }

    return best_idx;
}

static void update_result_from_multi(const DetectionResult_t *result)
{
    subboard_detection_result_t next;
    int best_idx;
    const DetectionBox_t *box;

    if (!DETECTION_RESULT_IS_VALID(result)) {
        clear_latest_result();
        return;
    }

    best_idx = find_best_face(result);
    if (best_idx < 0) {
        clear_latest_result();
        s_latest_result.count = (uint8_t)((result->count > 255u) ? 255u : result->count);
        return;
    }

    box = &result->boxes[best_idx];
    memset(&next, 0, sizeof(next));

    next.valid = 1u;
    next.count = (uint8_t)((result->count > 255u) ? 255u : result->count);
    next.type = box->type;
    next.selected_idx = (uint8_t)best_idx;
    next.x1 = to_i16_clamped(box->x1);
    next.y1 = to_i16_clamped(box->y1);
    next.x2 = to_i16_clamped(box->x2);
    next.y2 = to_i16_clamped(box->y2);
    next.cx = to_i16_clamped((box->x1 + box->x2) / 2);
    next.cy = to_i16_clamped((box->y1 + box->y2) / 2);
    next.vx = box->vx;
    next.vy = box->vy;
    next.face_id = box->track_id;
    next.confidence = score_to_u8(box->score);

    s_latest_result = next;
}

static void update_result_from_single(const DetectionBox_t *box)
{
    subboard_detection_result_t next;

    if ((box == NULL) || !is_face_like_type(box->type)) {
        clear_latest_result();
        return;
    }

    memset(&next, 0, sizeof(next));
    next.valid = 1u;
    next.count = 1u;
    next.type = box->type;
    next.selected_idx = 0u;
    next.x1 = to_i16_clamped(box->x1);
    next.y1 = to_i16_clamped(box->y1);
    next.x2 = to_i16_clamped(box->x2);
    next.y2 = to_i16_clamped(box->y2);
    next.cx = to_i16_clamped((box->x1 + box->x2) / 2);
    next.cy = to_i16_clamped((box->y1 + box->y2) / 2);
    next.vx = box->vx;
    next.vy = box->vy;
    next.face_id = box->track_id;
    next.confidence = score_to_u8(box->score);

    s_latest_result = next;
}

static void app_delay_ms(uint32_t delay)
{
    uint32_t start_ms = millis();
    while ((uint32_t)(millis() - start_ms) < delay) {
        __NOP();
    }
}

static const char *state_name(SubboardDspState_t state)
{
    switch (state) {
    case SUB_DSP_STATE_IDLE: return "IDLE";
    case SUB_DSP_STATE_HANDSHAKING: return "HANDSHAKING";
    case SUB_DSP_STATE_CM4_RESOURCE_READY: return "CM4_RESOURCE_READY";
    case SUB_DSP_STATE_DSP_READY: return "DSP_READY";
    case SUB_DSP_STATE_CONFIGURED: return "CONFIGURED";
    case SUB_DSP_STATE_RUNNING: return "RUNNING";
    case SUB_DSP_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static void enter_state(SubboardDspState_t next_state)
{
    if (s_state != next_state) {
        printf("[SUB-DSP] STATE %s -> %s\r\n", state_name(s_state), state_name(next_state));
        s_state = next_state;
        s_state_since_ms = millis();
    }
}

static void control_mailbox_prepare(void)
{
    (void)init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    (void)init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
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
        printf("[SUB-DSP] TX %-20s 0x%08lX\r\n", label, (unsigned long)msg);
    } else {
        printf("[SUB-DSP] TX %-20s failed (%d)\r\n", label, ret);
    }
}

static void send_hello(void)
{
    send_control_msg(CONTROL_SYS_HELLO(s_session_id, CONTROL_PROTOCOL_VERSION), "SYS.HELLO");
    s_last_hello_ms = millis();
}

static void send_resource_ready(void)
{
    send_control_msg(
        CONTROL_SYS_CM4_RESOURCE_READY(
            s_session_id,
            CONTROL_INPUT_VIDEO,
            s_resource_flags,
            SUB_DSP_CONFIG_SLOT_ID),
        "SYS.CM4_RESOURCE_READY");

    printf("[SUB-DSP] TX RESOURCE_READY flags=0x%02X (camera=%u mm=%u lcd=%u)\r\n",
           (unsigned)s_resource_flags,
           (unsigned)((s_resource_flags & CONTROL_RESOURCE_CAMERA_READY) != 0u),
           (unsigned)((s_resource_flags & CONTROL_RESOURCE_MM_READY) != 0u),
           (unsigned)((s_resource_flags & CONTROL_RESOURCE_LCD_READY) != 0u));

    s_last_resource_ms = millis();
}

static void send_config_apply(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_CONFIG, s_session_id,
                         CONTROL_CMD_CONFIG_APPLY, SUB_DSP_CONFIG_SLOT_ID),
        "CMD.CONFIG_APPLY");
    s_pending = SUB_DSP_PENDING_CONFIG_ACK;
}

static void send_buffer_bind(void)
{
    send_control_msg(
        CONTROL_CMD_MAKE(CONTROL_CMD_GRP_BUFFER, s_session_id,
                         CONTROL_CMD_BUFFER_BIND, SUB_DSP_BUFFER_SLOT_ID),
        "CMD.BUFFER_BIND");
    s_pending = SUB_DSP_PENDING_BUFFER_ACK;
}

static void send_start_stream(void)
{
    send_control_msg(
        CONTROL_SYS_START_STREAM(s_session_id, SUB_DSP_STREAM_ID_MAIN, 0x01u),
        "SYS.START_STREAM");
    s_pending = SUB_DSP_PENDING_START_ACK;
}

static void send_heartbeat(void)
{
    send_control_msg(
        CONTROL_SYS_HEARTBEAT(s_session_id, s_heartbeat_seq, CONTROL_RUN_STATE_RUNNING),
        "SYS.HEARTBEAT");
    s_heartbeat_seq++;
    s_last_heartbeat_ms = millis();
}

static void queue_master_request(uint8_t request)
{
    uint8_t bit = 0u;

    switch (request) {
    case SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME:
        bit = SUB_DSP_REQ_MASK_MM_RUNTIME;
        break;

    default:
        break;
    }

    if ((bit != 0u) && ((s_pending_master_requests & bit) == 0u)) {
        s_pending_master_requests |= bit;
        printf("[SUB-DSP] queued master request=0x%02X\r\n", request);
    }
}

static bool handle_runtime_message(uint32_t msg)
{
    uint32_t enable_req;

    if (!subboard_runtime_msg_is_mm_enable_req(msg)) {
        return false;
    }

    if ((s_state != SUB_DSP_STATE_CONFIGURED) && (s_state != SUB_DSP_STATE_RUNNING)) {
        printf("[SUB-DSP][WARN] ignore MM enable req before stream start, state=%s msg=0x%08lX\r\n",
               state_name(s_state),
               (unsigned long)msg);
        return true;
    }

    enable_req = subboard_runtime_msg_get_mm_enable_req(msg);
    if ((enable_req != SUBBOARD_RT_MM_ENABLE_REQ) &&
        (enable_req != SUBBOARD_RT_SYNC_REQ_SPI_REG_UPDATE)) {
        printf("[SUB-DSP][WARN] unknown MM enable payload=0x%08lX\r\n",
               (unsigned long)enable_req);
        return true;
    }

    queue_master_request(SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME);
    printf("[SUB-DSP] DSP requested MM runtime enable\r\n");
    return true;
}

static void handle_ack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t status = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[SUB-DSP] RX ACK kind=%u code=0x%02X status=%u\r\n",
           (unsigned)kind, (unsigned)code, (unsigned)status);

    if (kind == CONTROL_RSP_KIND_CMD) {
        if ((s_pending == SUB_DSP_PENDING_CONFIG_ACK) &&
            (code == CONTROL_CMD_CONFIG_APPLY) &&
            (status == CONTROL_ACK_OK)) {
            send_buffer_bind();
            return;
        }

        if ((s_pending == SUB_DSP_PENDING_BUFFER_ACK) &&
            (code == CONTROL_CMD_BUFFER_BIND) &&
            (status == CONTROL_ACK_OK)) {
            s_pending = SUB_DSP_PENDING_NONE;
            enter_state(SUB_DSP_STATE_CONFIGURED);
            send_start_stream();
            return;
        }
    }

    if (kind == CONTROL_RSP_KIND_SYS) {
        if ((s_pending == SUB_DSP_PENDING_START_ACK) &&
            (code == CONTROL_SYS_SUBTYPE_START_STREAM) &&
            (status == CONTROL_ACK_OK)) {
            s_pending = SUB_DSP_PENDING_NONE;
            enter_state(SUB_DSP_STATE_RUNNING);
        }
    }
}

static void handle_nack(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);
    uint8_t code = (uint8_t)CONTROL_ACK_GET_CODE(arg);
    uint8_t error = (uint8_t)CONTROL_ACK_GET_STATUS(arg);
    uint8_t kind = (uint8_t)CONTROL_GET_SUBTYPE(msg);

    printf("[SUB-DSP] RX NACK kind=%u code=0x%02X error=%u\r\n",
           (unsigned)kind, (unsigned)code, (unsigned)error);
    s_pending = SUB_DSP_PENDING_NONE;
    enter_state(SUB_DSP_STATE_ERROR);
}

static void handle_status(uint32_t msg)
{
    uint16_t arg = CONTROL_GET_ARG(msg);

    printf("[SUB-DSP] RX STATUS run_state=%u brief=%u\r\n",
           (unsigned)CONTROL_STATUS_GET_RUN_STATE(arg),
           (unsigned)CONTROL_STATUS_GET_BRIEF(arg));
}

static void handle_sys_message(uint32_t msg)
{
    uint8_t subtype = (uint8_t)CONTROL_GET_SUBTYPE(msg);
    uint16_t arg = CONTROL_GET_ARG(msg);

    if (subtype == CONTROL_SYS_SUBTYPE_HELLO_ACK) {
        printf("[SUB-DSP] RX HELLO_ACK session=0x%02X boot=%u feature=%u state=%u\r\n",
               (unsigned)CONTROL_GET_SESSION(msg),
               (unsigned)CONTROL_HELLO_ACK_GET_BOOT_REASON(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_FEATURE_LEVEL(arg),
               (unsigned)CONTROL_HELLO_ACK_GET_RUN_STATE(arg));

        if ((s_state == SUB_DSP_STATE_HANDSHAKING) && s_mm_ready) {
            send_resource_ready();
            enter_state(SUB_DSP_STATE_CM4_RESOURCE_READY);
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_DSP_MODEL_READY) {
        printf("[SUB-DSP] RX DSP_MODEL_READY algo=%u flags=%u run_state=%u\r\n",
               (unsigned)CONTROL_MODEL_READY_GET_ALGO_ID(arg),
               (unsigned)CONTROL_MODEL_READY_GET_FLAGS(arg),
               (unsigned)CONTROL_MODEL_READY_GET_RUN_STATE(arg));

        if ((s_state == SUB_DSP_STATE_CM4_RESOURCE_READY) &&
            (s_pending == SUB_DSP_PENDING_NONE)) {
            enter_state(SUB_DSP_STATE_DSP_READY);
            send_config_apply();
        }
        return;
    }

    if (subtype == CONTROL_SYS_SUBTYPE_HEARTBEAT) {
        printf("[SUB-DSP] RX DSP HEARTBEAT seq=%u status=%u\r\n",
               (unsigned)CONTROL_HEARTBEAT_GET_SEQ(arg),
               (unsigned)CONTROL_HEARTBEAT_GET_STATUS(arg));
        return;
    }

    printf("[SUB-DSP] RX SYS subtype=%u arg=0x%04X\r\n",
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
                printf("[SUB-DSP] drop stale control msg: 0x%08lX\r\n", (unsigned long)msg);
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
            if (MAILBOX_GET_MSG_TYPE(msg) == MAILBOX_MSG_TYPE_MULTI) {
                uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)MAILBOX_GET_PAYLOAD(msg);
                update_result_from_multi((const DetectionResult_t *)addr);
            } else if (MAILBOX_GET_MSG_TYPE(msg) == MAILBOX_MSG_TYPE_SINGLE) {
                uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)MAILBOX_GET_PAYLOAD(msg);
                update_result_from_single((const DetectionBox_t *)addr);
            } else if (MAILBOX_GET_MSG_TYPE(msg) == MAILBOX_MSG_TYPE_NO_RESULT) {
                clear_latest_result();
            } else {
                printf("[SUB-DSP] RX data/runtime msg=0x%08lX\r\n", (unsigned long)msg);
            }
            break;
        }
    }
}

static void step_state_machine(void)
{
    uint32_t now_ms = millis();

    switch (s_state) {
    case SUB_DSP_STATE_HANDSHAKING:
        if ((uint32_t)(now_ms - s_last_hello_ms) >= SUB_DSP_HELLO_RETRY_MS) {
            send_hello();
        }
        break;

    case SUB_DSP_STATE_CM4_RESOURCE_READY:
        if (((uint32_t)(now_ms - s_last_resource_ms) >= SUB_DSP_RESOURCE_RETRY_MS) &&
            (s_pending == SUB_DSP_PENDING_NONE)) {
            send_resource_ready();
        }
        break;

    case SUB_DSP_STATE_RUNNING:
        if ((uint32_t)(now_ms - s_last_heartbeat_ms) >= SUB_DSP_HEARTBEAT_INTERVAL_MS) {
            send_heartbeat();
        }
        break;

    case SUB_DSP_STATE_IDLE:
    case SUB_DSP_STATE_DSP_READY:
    case SUB_DSP_STATE_CONFIGURED:
    case SUB_DSP_STATE_ERROR:
    default:
        break;
    }

    if ((s_state != SUB_DSP_STATE_IDLE) &&
        (s_state != SUB_DSP_STATE_RUNNING) &&
        (s_state != SUB_DSP_STATE_ERROR) &&
        ((uint32_t)(now_ms - s_state_since_ms) >= SUB_DSP_RESPONSE_TIMEOUT_MS)) {
        printf("[SUB-DSP] state timeout in %s\r\n", state_name(s_state));
        enter_state(SUB_DSP_STATE_ERROR);
    }
}

void subboard_dsp_ctrl_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;
    subboard_dsp_ctrl_reset();
    control_mailbox_prepare();
}

void subboard_dsp_ctrl_reset(void)
{
    s_state = SUB_DSP_STATE_IDLE;
    s_pending = SUB_DSP_PENDING_NONE;
    s_heartbeat_seq = 0u;
    s_pending_master_requests = 0u;
    clear_latest_result();
    s_state_since_ms = millis();
    s_last_hello_ms = 0u;
    s_last_resource_ms = 0u;
    s_last_heartbeat_ms = 0u;
}

void subboard_dsp_ctrl_set_mm_ready(bool ready)
{
    s_mm_ready = ready;
}

void subboard_dsp_ctrl_set_resource_flags(uint8_t resource_flags)
{
    s_resource_flags = resource_flags;
}

int subboard_dsp_ctrl_start(void)
{
    if (!s_mm_ready) {
        return -2;
    }

    if ((s_state != SUB_DSP_STATE_IDLE) && (s_state != SUB_DSP_STATE_ERROR)) {
        return -1;
    }

    s_session_id++;
    s_pending = SUB_DSP_PENDING_NONE;
    s_heartbeat_seq = 0u;
    s_pending_master_requests = 0u;
    control_mailbox_prepare();
    rcc_set_dsp_warm_reset(true);
    app_delay_ms(50u);
    rcc_set_dsp_warm_reset(false);
    app_delay_ms(50u);
    control_mailbox_prepare();
    send_hello();
    enter_state(SUB_DSP_STATE_HANDSHAKING);
    return 0;
}

void subboard_dsp_ctrl_tick(void)
{
    if (s_state == SUB_DSP_STATE_IDLE) {
        return;
    }

    process_mailbox();
    step_state_machine();
}

bool subboard_dsp_ctrl_is_active(void)
{
    return s_state != SUB_DSP_STATE_IDLE;
}

bool subboard_dsp_ctrl_has_error(void)
{
    return s_state == SUB_DSP_STATE_ERROR;
}

uint8_t subboard_dsp_ctrl_get_public_state(void)
{
    switch (s_state) {
    case SUB_DSP_STATE_HANDSHAKING:
    case SUB_DSP_STATE_CM4_RESOURCE_READY:
        return SUBBOARD_STARTUP_STATE_DSP_STARTING;

    case SUB_DSP_STATE_DSP_READY:
    case SUB_DSP_STATE_CONFIGURED:
        return SUBBOARD_STARTUP_STATE_DSP_READY;

    case SUB_DSP_STATE_RUNNING:
        return SUBBOARD_STARTUP_STATE_RUNNING;

    case SUB_DSP_STATE_ERROR:
        return SUBBOARD_STARTUP_STATE_ERROR;

    case SUB_DSP_STATE_IDLE:
    default:
        return SUBBOARD_STARTUP_STATE_MM_READY;
    }
}

uint8_t subboard_dsp_ctrl_peek_master_request(void)
{
    if ((s_pending_master_requests & SUB_DSP_REQ_MASK_MM_RUNTIME) != 0u) {
        return SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME;
    }

    return SUBBOARD_STARTUP_REQ_NONE;
}

void subboard_dsp_ctrl_complete_master_request(uint8_t request)
{
    switch (request) {
    case SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME:
        s_pending_master_requests &= (uint8_t)~SUB_DSP_REQ_MASK_MM_RUNTIME;
        break;

    default:
        break;
    }
}

bool subboard_dsp_ctrl_get_latest_result(subboard_detection_result_t *out_result)
{
    if (out_result == NULL) {
        return false;
    }

    *out_result = s_latest_result;
    return true;
}