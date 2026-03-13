#include "subboard_dsp_ctrl.h"

#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "rcc.h"
#include "s300.h"
#include "subboard_log.h"
#include "subboard_dsp_control_plane.h"
#include "subboard_detection_adapter.h"
#include "subboard_dsp_mailbox.h"
#include "subboard_startup_proto.h"

#define SUB_DSP_HELLO_RETRY_MS        200u
#define SUB_DSP_RESOURCE_RETRY_MS     300u
#define SUB_DSP_HEARTBEAT_INTERVAL_MS 1000u
#define SUB_DSP_RESPONSE_TIMEOUT_MS   2000u

#define SUB_DSP_REQ_MASK_MM_RUNTIME   (1u << 0)

static uint32_t (*s_get_millis)(void) = 0;
static SubboardDspState_t s_state = SUB_DSP_STATE_IDLE;
static SubboardDspPending_t s_pending = SUB_DSP_PENDING_NONE;
static bool s_mm_ready = false;
static uint8_t s_resource_flags = 0u;
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

static void app_delay_ms(uint32_t delay)
{
    uint32_t start_ms = millis();
    while ((uint32_t)(millis() - start_ms) < delay) {
        __NOP();
    }
}

static const char *state_name(SubboardDspState_t state)
{
    return subboard_dsp_control_plane_state_name(state);
}

static void enter_state(SubboardDspState_t next_state)
{
    if (s_state != next_state) {
        SUB_LOG_INFO("[SUB-DSP] STATE %s -> %s\r\n", state_name(s_state), state_name(next_state));
        s_state = next_state;
        s_state_since_ms = millis();
    }
}

static void send_hello(void)
{
    subboard_dsp_mailbox_send_hello(s_session_id);
    s_last_hello_ms = millis();
}

static void send_resource_ready(void)
{
    subboard_dsp_mailbox_send_resource_ready(s_session_id, s_resource_flags);
    s_last_resource_ms = millis();
}

static void send_config_apply(void)
{
    subboard_dsp_mailbox_send_config_apply(s_session_id);
    s_pending = SUB_DSP_PENDING_CONFIG_ACK;
}

static void send_buffer_bind(void)
{
    subboard_dsp_mailbox_send_buffer_bind(s_session_id);
    s_pending = SUB_DSP_PENDING_BUFFER_ACK;
}

static void send_start_stream(void)
{
    subboard_dsp_mailbox_send_start_stream(s_session_id);
    s_pending = SUB_DSP_PENDING_START_ACK;
}

static void send_heartbeat(void)
{
    subboard_dsp_mailbox_send_heartbeat(s_session_id, s_heartbeat_seq);
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
        SUB_LOG_DEBUG("[SUB-DSP] queued master request=0x%02X\r\n", request);
    }
}

static void apply_control_effect(const SubboardDspControlEffect_t *effect)
{
    if (effect == NULL) {
        return;
    }

    if (effect->has_next_pending) {
        s_pending = effect->next_pending;
    }
    if (effect->has_next_state) {
        enter_state(effect->next_state);
    }

    if (effect->request_resource_ready) {
        send_resource_ready();
    }
    if (effect->request_config_apply) {
        send_config_apply();
    }
    if (effect->request_buffer_bind) {
        send_buffer_bind();
    }
    if (effect->request_start_stream) {
        send_start_stream();
    }
    if (effect->request_master_mm_runtime) {
        queue_master_request(SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME);
    }
}

static void process_mailbox(void)
{
    while (1) {
        SubboardDspControlEffect_t effect;
        uint32_t msg;

        if (subboard_dsp_mailbox_read(&msg) != 0) {
            break;
        }

        if (subboard_dsp_control_plane_handle_runtime_message(msg, s_state, &effect)) {
            apply_control_effect(&effect);
            continue;
        }

        subboard_dsp_control_plane_handle_control_message(msg,
                                                          s_session_id,
                                                          s_mm_ready,
                                                          s_state,
                                                          s_pending,
                                                          &effect);
        if (effect.consumed) {
            apply_control_effect(&effect);
            continue;
        }

        {
            if (MAILBOX_GET_MSG_TYPE(msg) == MAILBOX_MSG_TYPE_MULTI) {
                uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)MAILBOX_GET_PAYLOAD(msg);
                subboard_detection_adapter_update_from_multi((const DetectionResult_t *)addr);
            } else if (MAILBOX_GET_MSG_TYPE(msg) == MAILBOX_MSG_TYPE_SINGLE) {
                uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)MAILBOX_GET_PAYLOAD(msg);
                subboard_detection_adapter_update_from_single((const DetectionBox_t *)addr);
            } else if (MAILBOX_GET_MSG_TYPE(msg) == MAILBOX_MSG_TYPE_NO_RESULT) {
                subboard_detection_adapter_clear();
            } else {
                SUB_LOG_WARN("[SUB-DSP] RX data/runtime msg=0x%08lX\r\n", (unsigned long)msg);
            }
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
        SUB_LOG_WARN("[SUB-DSP] state timeout in %s\r\n", state_name(s_state));
        enter_state(SUB_DSP_STATE_ERROR);
    }
}

void subboard_dsp_ctrl_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;
    subboard_dsp_ctrl_reset();
    subboard_dsp_mailbox_prepare();
}

void subboard_dsp_ctrl_reset(void)
{
    s_state = SUB_DSP_STATE_IDLE;
    s_pending = SUB_DSP_PENDING_NONE;
    s_heartbeat_seq = 0u;
    s_pending_master_requests = 0u;
    subboard_detection_adapter_reset();
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
    subboard_dsp_mailbox_prepare();
    rcc_set_dsp_warm_reset(true);
    app_delay_ms(50u);
    rcc_set_dsp_warm_reset(false);
    app_delay_ms(50u);
    subboard_dsp_mailbox_prepare();
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
    return subboard_detection_adapter_get_latest(out_result);
}