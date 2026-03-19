#include "app_card2_subboard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_card2_result_handler.h"
#include "master_log.h"
#include "subboard_startup_proto.h"

#ifndef MASTER_SUBBOARD_WAIT_TIMEOUT_MS
#define MASTER_SUBBOARD_WAIT_TIMEOUT_MS 3000u
#endif

#define CARD2_POLL_MS                  200u
#define CARD2_RUN_POLL_MS               30u

static app_card2_subboard_ops_t s_ops;
static bool s_ops_ready = false;
static bool s_video_prepared = false;
static bool s_prepare_sent = false;
static bool s_start_dsp_sent = false;
static uint32_t s_last_poll_ms = 0u;
static uint8_t s_last_proto_ver = 0xFFu;
static uint8_t s_last_state = 0xFFu;
static uint8_t s_last_error = 0xFFu;
static uint8_t s_last_heartbeat = 0xFFu;
static uint8_t s_last_request = 0xFFu;
static uint8_t s_last_request_ack = 0xFFu;
static uint8_t s_last_cmd_ack = 0xFFu;
static uint8_t s_last_cmd_result = 0xFFu;
static uint8_t s_public_state = 0xFFu;
static uint8_t s_capabilities = 0u;
static uint8_t s_last_capabilities = 0xFFu;
static bool s_init_started = false;
static bool s_init_complete = false;
static bool s_init_success = false;
static bool s_poll_suspended = false;
static bool s_snapshot_read_failed = false;
static uint32_t s_init_since_ms = 0u;
static const char *s_init_failure_reason = NULL;

static uint32_t card2_now_ms(void)
{
    return (s_ops.millis_fn != NULL) ? s_ops.millis_fn() : 0u;
}

static const char *public_state_name(uint8_t state)
{
    switch (state) {
    case SUBBOARD_STARTUP_STATE_BOOT: return "BOOT";
    case SUBBOARD_STARTUP_STATE_I2C_READY: return "I2C_READY";
    case SUBBOARD_STARTUP_STATE_WAIT_VIDEO: return "WAIT_VIDEO_READY";
    case SUBBOARD_STARTUP_STATE_WAIT_MASTER_MM: return "WAIT_MASTER_MM";
    case SUBBOARD_STARTUP_STATE_MM_READY: return "MM_READY";
    case SUBBOARD_STARTUP_STATE_DSP_STARTING: return "DSP_STARTING";
    case SUBBOARD_STARTUP_STATE_DSP_READY: return "DSP_READY";
    case SUBBOARD_STARTUP_STATE_RUNNING: return "RUNNING";
    case SUBBOARD_STARTUP_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static const char *result_name(uint8_t result)
{
    switch (result) {
    case SUBBOARD_STARTUP_RESULT_OK: return "OK";
    case SUBBOARD_STARTUP_RESULT_BUSY: return "BUSY";
    case SUBBOARD_STARTUP_RESULT_INVALID_STATE: return "INVALID_STATE";
    case SUBBOARD_STARTUP_RESULT_MM_FAILED: return "MM_FAILED";
    case SUBBOARD_STARTUP_RESULT_DSP_FAILED: return "DSP_FAILED";
    case SUBBOARD_STARTUP_RESULT_TIMEOUT: return "TIMEOUT";
    case SUBBOARD_STARTUP_RESULT_NOT_SUPPORTED: return "NOT_SUPPORTED";
    default: return "UNKNOWN";
    }
}

static const char *request_name(uint8_t request)
{
    switch (request) {
    case SUBBOARD_STARTUP_REQ_NONE: return "NONE";
    case SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE: return "REQUEST_MASTER_MM_ENABLE";
    case SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME: return "REQUEST_MASTER_MM_RUNTIME";
    case SUBBOARD_STARTUP_REQ_MASTER_SPI_SYNC: return "REQUEST_MASTER_SPI_SYNC";
    default: return "UNKNOWN";
    }
}

static void log_capabilities(uint8_t capabilities)
{
    if (capabilities == s_last_capabilities) {
        return;
    }

    MASTER_LOG_INFO("[MASTER][CARD2] capabilities=0x%02X detection=%u tracking_summary=%u tracking_control=%u gesture=%u\r\n",
                    (unsigned)capabilities,
                    (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_DETECTION_RESULT) != 0u),
                    (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_TRACKING_SUMMARY) != 0u),
                    (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_TRACKING_CONTROL) != 0u),
                    (unsigned)((capabilities & SUBBOARD_STARTUP_CAP_GESTURE_RESULT) != 0u));
    s_last_capabilities = capabilities;
}

static void reset_runtime_state(void)
{
    s_public_state = 0xFFu;
    s_capabilities = 0u;
    s_video_prepared = false;
    s_prepare_sent = false;
    s_start_dsp_sent = false;
    s_last_proto_ver = 0xFFu;
    s_last_state = 0xFFu;
    s_last_error = 0xFFu;
    s_last_heartbeat = 0xFFu;
    s_last_request = 0xFFu;
    s_last_request_ack = 0xFFu;
    s_last_cmd_ack = 0xFFu;
    s_last_cmd_result = 0xFFu;
    s_last_capabilities = 0xFFu;
    s_snapshot_read_failed = false;

    if (s_ops_ready) {
        app_card2_result_handler_reset();
    }
}

static void mark_init_success(uint32_t now_ms, uint8_t proto_ver)
{
    if (s_init_success) {
        return;
    }

    s_init_complete = true;
    s_init_success = true;
    s_poll_suspended = false;
    s_init_failure_reason = NULL;
    MASTER_LOG_INFO("[MASTER][CARD2] handshake success proto=0x%02X elapsed=%lu ms\r\n",
                    (unsigned)proto_ver,
                    (unsigned long)(now_ms - s_init_since_ms));
}

static void mark_init_failed(uint32_t now_ms, const char *reason)
{
    if (s_init_complete && !s_init_success) {
        return;
    }

    s_init_complete = true;
    s_init_success = false;
    s_poll_suspended = true;
    s_init_failure_reason = reason;
    reset_runtime_state();
    MASTER_LOG_WARN("[MASTER][CARD2] handshake failed: %s elapsed=%lu ms, suspend polling\r\n",
                    reason,
                    (unsigned long)(now_ms - s_init_since_ms));
}

static int read_reg8(uint8_t reg, uint8_t *value)
{
    return s_ops.read_reg8_at(SUBBOARD_STARTUP_SLAVE_ADDR_CARD2, reg, value);
}

static int read_regs(uint8_t reg, uint8_t *buffer, uint32_t length)
{
    return s_ops.read_regs_at(SUBBOARD_STARTUP_SLAVE_ADDR_CARD2, reg, buffer, length);
}

static int write_reg8(uint8_t reg, uint8_t value)
{
    return s_ops.write_reg8_at(SUBBOARD_STARTUP_SLAVE_ADDR_CARD2, reg, value);
}

static bool read_runtime_snapshot(uint8_t *state,
                                  uint8_t *error,
                                  uint8_t *heartbeat,
                                  uint8_t *request,
                                  uint8_t *request_ack,
                                  uint8_t *cmd_ack,
                                  uint8_t *cmd_result,
                                  uint8_t *capabilities)
{
    if ((read_reg8(SUBBOARD_STARTUP_REG_SYS_STATE, state) != 0) ||
        (read_reg8(SUBBOARD_STARTUP_REG_ERROR_CODE, error) != 0) ||
        (read_reg8(SUBBOARD_STARTUP_REG_HEARTBEAT, heartbeat) != 0) ||
        (read_reg8(SUBBOARD_STARTUP_REG_REQUEST, request) != 0) ||
        (read_reg8(SUBBOARD_STARTUP_REG_REQUEST_ACK, request_ack) != 0) ||
        (read_reg8(SUBBOARD_STARTUP_REG_CMD_ACK, cmd_ack) != 0) ||
        (read_reg8(SUBBOARD_STARTUP_REG_CMD_RESULT, cmd_result) != 0) ||
        (read_reg8(SUBBOARD_STARTUP_REG_CAPABILITIES, capabilities) != 0)) {
        if (!s_snapshot_read_failed) {
            MASTER_LOG_WARN("[MASTER][CARD2] runtime snapshot read failed, keep last known state\r\n");
            s_snapshot_read_failed = true;
        }
        return false;
    }

    if (s_snapshot_read_failed) {
        MASTER_LOG_INFO("[MASTER][CARD2] runtime snapshot read recovered\r\n");
        s_snapshot_read_failed = false;
    }

    return true;
}

static int send_prepare_video_cmd(void)
{
    if (write_reg8(SUBBOARD_STARTUP_REG_CMD_ARG, 0u) != 0) {
        return -1;
    }

    if (write_reg8(SUBBOARD_STARTUP_REG_CMD, SUBBOARD_STARTUP_CMD_PREPARE_VIDEO) != 0) {
        return -1;
    }

    return 0;
}

static int send_start_dsp_cmd(void)
{
    if (write_reg8(SUBBOARD_STARTUP_REG_CMD_ARG, 0u) != 0) {
        return -1;
    }

    if (write_reg8(SUBBOARD_STARTUP_REG_CMD, SUBBOARD_STARTUP_CMD_START_DSP) != 0) {
        return -1;
    }

    return 0;
}

static void log_snapshot(uint8_t state,
                         uint8_t error,
                         uint8_t heartbeat,
                         uint8_t request,
                         uint8_t request_ack,
                         uint8_t cmd_ack,
                         uint8_t cmd_result)
{
    if (state != s_last_state) {
        MASTER_LOG_INFO("[MASTER][CARD2] subboard_state=%s\r\n", public_state_name(state));
        s_last_state = state;
    }

    if (error != s_last_error) {
        if (error != SUBBOARD_STARTUP_ERR_NONE) {
            MASTER_LOG_WARN("[MASTER][CARD2] subboard_error=0x%02X\r\n", error);
        }
        s_last_error = error;
    }

    if (heartbeat != s_last_heartbeat) {
        MASTER_LOG_DEBUG("[MASTER][CARD2] subboard_heartbeat=%u\r\n", (unsigned)heartbeat);
        s_last_heartbeat = heartbeat;
    }

    if ((request != s_last_request) || (request_ack != s_last_request_ack)) {
        MASTER_LOG_INFO("[MASTER][CARD2] request=%s ack=%s\r\n",
                        request_name(request),
                        request_name(request_ack));
        s_last_request = request;
        s_last_request_ack = request_ack;
    }

    if ((cmd_ack != s_last_cmd_ack) || (cmd_result != s_last_cmd_result)) {
        MASTER_LOG_INFO("[MASTER][CARD2] cmd_ack=0x%02X result=%s\r\n",
                        cmd_ack,
                        result_name(cmd_result));
        s_last_cmd_ack = cmd_ack;
        s_last_cmd_result = cmd_result;
    }
}

static void handle_request(uint8_t request, uint8_t request_ack)
{
    if ((request == SUBBOARD_STARTUP_REQ_NONE) || (request_ack == request)) {
        return;
    }

    if ((s_ops.is_mm_request_allowed != NULL) && !s_ops.is_mm_request_allowed()) {
        return;
    }

    if (request == SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE) {
        if (!s_video_prepared) {
            if ((s_ops.prepare_video_path == NULL) || (s_ops.prepare_video_path() != 0)) {
                MASTER_LOG_WARN("[MASTER][CARD2] video path prepare failed, waiting retry\r\n");
                return;
            }
            s_video_prepared = true;
        }
    } else if (request == SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME) {
        if (s_ops.trigger_mm_runtime_enable != NULL) {
            s_ops.trigger_mm_runtime_enable();
        }
        MASTER_LOG_INFO("[MASTER][CARD2] executed REQUEST_MASTER_MM_RUNTIME\r\n");
    } else if (request == SUBBOARD_STARTUP_REQ_MASTER_SPI_SYNC) {
        if (s_ops.trigger_mm_runtime_enable != NULL) {
            s_ops.trigger_mm_runtime_enable();
        }
        MASTER_LOG_INFO("[MASTER][CARD2] executed legacy REQUEST_MASTER_SPI_SYNC as MM runtime enable\r\n");
    }

    if (write_reg8(SUBBOARD_STARTUP_REG_REQUEST_ACK, request) == 0) {
        MASTER_LOG_INFO("[MASTER][CARD2] acknowledged %s\r\n", request_name(request));
    } else {
        MASTER_LOG_WARN("[MASTER][CARD2] failed to acknowledge %s\r\n", request_name(request));
    }
}

static void handle_commands(uint8_t state,
                            uint8_t request_ack,
                            uint8_t cmd_ack,
                            uint8_t cmd_result)
{
    if (!s_prepare_sent &&
        (request_ack == SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE) &&
        ((state == SUBBOARD_STARTUP_STATE_WAIT_MASTER_MM) ||
         (state == SUBBOARD_STARTUP_STATE_WAIT_VIDEO) ||
         (state == SUBBOARD_STARTUP_STATE_I2C_READY))) {
        if (send_prepare_video_cmd() == 0) {
            MASTER_LOG_INFO("[MASTER][CARD2] sent PREPARE_VIDEO_CONSUMER\r\n");
            s_prepare_sent = true;
        } else {
            MASTER_LOG_WARN("[MASTER][CARD2] failed to send PREPARE_VIDEO_CONSUMER\r\n");
        }
    }

    if (!s_start_dsp_sent &&
        (state == SUBBOARD_STARTUP_STATE_MM_READY) &&
        (cmd_ack == SUBBOARD_STARTUP_CMD_PREPARE_VIDEO) &&
        (cmd_result == SUBBOARD_STARTUP_RESULT_OK)) {
        if (send_start_dsp_cmd() == 0) {
            MASTER_LOG_INFO("[MASTER][CARD2] sent START_DSP\r\n");
            s_start_dsp_sent = true;
        } else {
            MASTER_LOG_WARN("[MASTER][CARD2] failed to send START_DSP\r\n");
        }
    }
}

static void handle_running_state(void)
{
    subboard_detection_result_t result;

    app_card2_result_handler_tick();

    if (read_regs(SUBBOARD_STARTUP_REG_RESULT, (uint8_t *)&result, sizeof(result)) != 0) {
        return;
    }

    app_card2_result_handler_handle_result(&result);
}

int app_card2_subboard_init(const app_card2_subboard_ops_t *ops)
{
    app_card2_result_handler_ops_t result_handler_ops;

    if ((ops == NULL) ||
        (ops->millis_fn == NULL) ||
        (ops->read_reg8_at == NULL) ||
        (ops->read_regs_at == NULL) ||
        (ops->write_reg8_at == NULL)) {
        return -1;
    }

    s_ops = *ops;
    s_ops_ready = true;

    result_handler_ops.millis_fn = ops->millis_fn;
    result_handler_ops.overlay_tick = ops->overlay_tick;
    result_handler_ops.overlay_is_active = ops->overlay_is_active;
    result_handler_ops.overlay_clear = ops->overlay_clear;
    result_handler_ops.overlay_draw = ops->overlay_draw;
    if (app_card2_result_handler_init(&result_handler_ops) != 0) {
        s_ops_ready = false;
        return -1;
    }

    app_card2_subboard_reset();
    return 0;
}

void app_card2_subboard_reset(void)
{
    reset_runtime_state();
    s_last_poll_ms = 0u;
    s_init_started = false;
    s_init_complete = false;
    s_init_success = false;
    s_poll_suspended = false;
    s_init_since_ms = 0u;
    s_init_failure_reason = NULL;
}

void app_card2_subboard_tick(void)
{
    uint8_t proto_ver = 0u;
    uint8_t state = SUBBOARD_STARTUP_STATE_BOOT;
    uint8_t error = SUBBOARD_STARTUP_ERR_NONE;
    uint8_t heartbeat = 0u;
    uint8_t request = SUBBOARD_STARTUP_REQ_NONE;
    uint8_t request_ack = SUBBOARD_STARTUP_REQ_NONE;
    uint8_t cmd_ack = SUBBOARD_STARTUP_CMD_NONE;
    uint8_t cmd_result = SUBBOARD_STARTUP_RESULT_OK;
    uint8_t capabilities = 0u;
    uint32_t now_ms;
    uint32_t poll_ms;

    if (!s_ops_ready) {
        return;
    }

    if (s_poll_suspended) {
        return;
    }

    now_ms = card2_now_ms();
    if (!s_init_started) {
        s_init_started = true;
        s_init_since_ms = now_ms;
    }

    poll_ms = (s_last_state == SUBBOARD_STARTUP_STATE_RUNNING) ? CARD2_RUN_POLL_MS : CARD2_POLL_MS;
    if ((uint32_t)(now_ms - s_last_poll_ms) < poll_ms) {
        return;
    }
    s_last_poll_ms = now_ms;

    if (read_reg8(SUBBOARD_STARTUP_REG_PROTO_VER, &proto_ver) != 0) {
        MASTER_LOG_DEBUG("[MASTER][CARD2] waiting subboard at 0x%02X\r\n", SUBBOARD_STARTUP_SLAVE_ADDR_CARD2);
        reset_runtime_state();
        if (!s_init_success && ((uint32_t)(now_ms - s_init_since_ms) >= MASTER_SUBBOARD_WAIT_TIMEOUT_MS)) {
            mark_init_failed(now_ms, "timeout waiting protocol response");
        }
        return;
    }

    if (proto_ver != SUBBOARD_STARTUP_PROTO_VER) {
        MASTER_LOG_WARN("[MASTER][CARD2] unsupported proto=0x%02X expected=0x%02X\r\n",
                        (unsigned)proto_ver,
                        (unsigned)SUBBOARD_STARTUP_PROTO_VER);
        mark_init_failed(now_ms, "protocol version mismatch");
        return;
    }

    mark_init_success(now_ms, proto_ver);

    if (proto_ver != s_last_proto_ver) {
        MASTER_LOG_INFO("[MASTER][CARD2] subboard online, proto=0x%02X\r\n", proto_ver);
        s_last_proto_ver = proto_ver;
    }

    if (!read_runtime_snapshot(&state,
                               &error,
                               &heartbeat,
                               &request,
                               &request_ack,
                               &cmd_ack,
                               &cmd_result,
                               &capabilities)) {
        return;
    }

    s_public_state = state;
    s_capabilities = capabilities;
    log_snapshot(state, error, heartbeat, request, request_ack, cmd_ack, cmd_result);
    log_capabilities(capabilities);
    handle_request(request, request_ack);
    handle_commands(state, request_ack, cmd_ack, cmd_result);

    if (state == SUBBOARD_STARTUP_STATE_RUNNING) {
        handle_running_state();
    }
}

bool app_card2_subboard_is_running(void)
{
    return s_public_state == SUBBOARD_STARTUP_STATE_RUNNING;
}

uint8_t app_card2_subboard_get_public_state(void)
{
    return s_public_state;
}

uint8_t app_card2_subboard_get_capabilities(void)
{
    return s_capabilities;
}

bool app_card2_subboard_is_init_complete(void)
{
    return s_init_complete;
}

bool app_card2_subboard_is_init_successful(void)
{
    return s_init_success;
}

const char *app_card2_subboard_get_init_failure_reason(void)
{
    return s_init_failure_reason;
}