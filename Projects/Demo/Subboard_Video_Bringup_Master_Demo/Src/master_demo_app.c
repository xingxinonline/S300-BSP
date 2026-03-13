#include "master_demo_app.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "camera_ov5640.h"
#include "gpio.h"
#include "i2c_soft.h"
#include "master_log.h"
#include "master_detection_overlay.h"
#include "psram.h"
#include "rcc.h"
#include "subboard_detection_result.h"
#include "subboard_startup_proto.h"
#include "video.h"

#if BOARD_CAMERA_FORMAT == 0
#define APP_CAM_FMT CAMREA_RGB565
#else
#define APP_CAM_FMT CAMREA_YUV422
#endif

#define MASTER_I2C_BUS_HZ  100000u
#define MASTER_POLL_MS     200u
#define MASTER_RUN_POLL_MS 30u
#define MASTER_I2C_RETRY   2u
#define MASTER_OVERLAY_CLEAR_DEBOUNCE_MS 120u

static uint32_t (*s_get_millis)(void) = 0;
static i2c_soft_t s_i2c;
static bool s_i2c_ready = false;
static bool s_video_prepared = false;
static bool s_prepare_sent = false;
static bool s_start_dsp_sent = false;
static bool s_overlay_active = false;
static uint32_t s_overlay_invalid_since_ms = 0u;
static subboard_detection_result_t s_last_result;
static uint32_t s_last_poll_ms = 0u;
static uint8_t s_last_proto_ver = 0xFFu;
static uint8_t s_last_state = 0xFFu;
static uint8_t s_last_error = 0xFFu;
static uint8_t s_last_heartbeat = 0xFFu;
static uint8_t s_last_request = 0xFFu;
static uint8_t s_last_request_ack = 0xFFu;
static uint8_t s_last_cmd_ack = 0xFFu;
static uint8_t s_last_cmd_result = 0xFFu;

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
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

static bool result_is_displayable(const subboard_detection_result_t *result)
{
    if (result == NULL) {
        return false;
    }

    return (result->valid != 0u) &&
           (result->count != 0u) &&
           (result->confidence != 0u) &&
           (result->x2 > result->x1) &&
           (result->y2 > result->y1);
}

static int master_i2c_init(void)
{
    i2c_soft_cfg_t cfg;
    int ret;

    set_cortex_m4_apb1_clock(RCC_CM4_APB1_GPIO, true);
    cfg.port = GPIOA;
    cfg.pin_scl = 0u;
    cfg.pin_sda = 1u;
    cfg.func_scl = FUNCTION_2;
    cfg.func_sda = FUNCTION_2;
    cfg.pull_mode = GPIO_UP;
    cfg.bus_hz = MASTER_I2C_BUS_HZ;

    ret = i2c_soft_init(&s_i2c, &cfg, SystemCoreClock);
    if (ret != 0) {
        MASTER_LOG_WARN("[MASTER] i2c init failed=%d\r\n", ret);
        return -1;
    }

    i2c_soft_bus_recover(&s_i2c);
    s_i2c_ready = true;
    return 0;
}

static int read_reg8(uint8_t reg, uint8_t *value)
{
    int ret;
    uint32_t attempt;

    if ((value == NULL) || !s_i2c_ready) {
        return -1;
    }

    for (attempt = 0u; attempt < MASTER_I2C_RETRY; attempt++) {
        ret = i2c_soft_mem_read(&s_i2c,
                                SUBBOARD_STARTUP_SLAVE_ADDR_CARD1,
                                reg,
                                false,
                                value,
                                1u);
        if (ret == 0) {
            return 0;
        }

        i2c_soft_bus_recover(&s_i2c);
    }

    return -1;
}

static int read_regs(uint8_t reg, uint8_t *buffer, uint32_t length)
{
    int ret;
    uint32_t attempt;

    if ((buffer == NULL) || (length == 0u) || !s_i2c_ready) {
        return -1;
    }

    for (attempt = 0u; attempt < MASTER_I2C_RETRY; attempt++) {
        ret = i2c_soft_mem_read(&s_i2c,
                                SUBBOARD_STARTUP_SLAVE_ADDR_CARD1,
                                reg,
                                false,
                                buffer,
                                length);
        if (ret == 0) {
            return 0;
        }

        i2c_soft_bus_recover(&s_i2c);
    }

    return -1;
}

static int write_reg8(uint8_t reg, uint8_t value)
{
    int ret;
    uint32_t attempt;

    if (!s_i2c_ready) {
        return -1;
    }

    for (attempt = 0u; attempt < MASTER_I2C_RETRY; attempt++) {
        ret = i2c_soft_mem_write(&s_i2c,
                                 SUBBOARD_STARTUP_SLAVE_ADDR_CARD1,
                                 reg,
                                 false,
                                 &value,
                                 1u);
        if (ret == 0) {
            return 0;
        }

        i2c_soft_bus_recover(&s_i2c);
    }

    return -1;
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

static void trigger_core_reg_update(void)
{
    REG32(DSP_VIDEO_SS_BASE + 0x70u) = 1u;
}

static void trigger_spi_reg_update(void)
{
    REG32(DSP_VIDEO_SS_BASE + 0x1E0u) = 1u;
}

static void trigger_mm_runtime_enable(void)
{
    trigger_core_reg_update();

#if defined(BOARD_LCD_SPI_ENABLE_ON_INIT) && (BOARD_LCD_SPI_ENABLE_ON_INIT == 0)
    MASTER_LOG_INFO("[MASTER] applied MM runtime enable: core only, local LCD SPI path disabled\r\n");
#else
    trigger_spi_reg_update();
    MASTER_LOG_INFO("[MASTER] applied MM runtime enable: core + lcd spi\r\n");
#endif
}

static int video_path_prepare(void)
{
    int ret;

    ret = rcc_init_mm_pll(8, 400, 0, 3, 2);
    if (ret != RCC_STATUS_OK) {
        MASTER_LOG_WARN("[MASTER] rcc_init_mm_pll failed=%d\r\n", ret);
        return -1;
    }

    init_psram(4, 1);

    ret = camera_ov5640_preinit();
    if (ret != 0) {
        MASTER_LOG_WARN("[MASTER] camera_ov5640_preinit failed=%d\r\n", ret);
        return -1;
    }

    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);
    master_detection_overlay_init(millis);
    MASTER_LOG_INFO("[MASTER] video path ready\r\n");
    return 0;
}

static void reset_online_state(void)
{
    s_video_prepared = false;
    s_prepare_sent = false;
    s_start_dsp_sent = false;
    s_overlay_active = false;
    s_overlay_invalid_since_ms = 0u;
    s_last_proto_ver = 0xFFu;
    s_last_state = 0xFFu;
    s_last_error = 0xFFu;
    s_last_heartbeat = 0xFFu;
    s_last_request = 0xFFu;
    s_last_request_ack = 0xFFu;
    s_last_cmd_ack = 0xFFu;
    s_last_cmd_result = 0xFFu;
    memset(&s_last_result, 0, sizeof(s_last_result));
    master_detection_overlay_clear();
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
        MASTER_LOG_INFO("[MASTER] subboard_state=%s\r\n", public_state_name(state));
        s_last_state = state;
    }

    if (error != s_last_error) {
        if (error != SUBBOARD_STARTUP_ERR_NONE) {
            MASTER_LOG_WARN("[MASTER] subboard_error=0x%02X\r\n", error);
        }
        s_last_error = error;
    }

    if (heartbeat != s_last_heartbeat) {
        MASTER_LOG_DEBUG("[MASTER] subboard_heartbeat=%u\r\n", (unsigned)heartbeat);
        s_last_heartbeat = heartbeat;
    }

    if ((request != s_last_request) || (request_ack != s_last_request_ack)) {
        MASTER_LOG_INFO("[MASTER] request=%s ack=%s\r\n",
                        request_name(request),
                        request_name(request_ack));
        s_last_request = request;
        s_last_request_ack = request_ack;
    }

    if ((cmd_ack != s_last_cmd_ack) || (cmd_result != s_last_cmd_result)) {
        MASTER_LOG_INFO("[MASTER] cmd_ack=0x%02X result=%s\r\n",
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

    if (request == SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE) {
        if (!s_video_prepared) {
            if (video_path_prepare() != 0) {
                MASTER_LOG_WARN("[MASTER] video path prepare failed, waiting retry\r\n");
                return;
            }
            s_video_prepared = true;
        }
    } else if (request == SUBBOARD_STARTUP_REQ_MASTER_MM_RUNTIME) {
        trigger_mm_runtime_enable();
        MASTER_LOG_INFO("[MASTER] executed REQUEST_MASTER_MM_RUNTIME\r\n");
    } else if (request == SUBBOARD_STARTUP_REQ_MASTER_SPI_SYNC) {
        trigger_mm_runtime_enable();
        MASTER_LOG_INFO("[MASTER] executed legacy REQUEST_MASTER_SPI_SYNC as MM runtime enable\r\n");
    }

    if (write_reg8(SUBBOARD_STARTUP_REG_REQUEST_ACK, request) == 0) {
        MASTER_LOG_INFO("[MASTER] acknowledged %s\r\n", request_name(request));
    } else {
        MASTER_LOG_WARN("[MASTER] failed to acknowledge %s\r\n", request_name(request));
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
            MASTER_LOG_INFO("[MASTER] sent PREPARE_VIDEO_CONSUMER\r\n");
            s_prepare_sent = true;
        } else {
            MASTER_LOG_WARN("[MASTER] failed to send PREPARE_VIDEO_CONSUMER\r\n");
        }
    }

    if (!s_start_dsp_sent &&
        (state == SUBBOARD_STARTUP_STATE_MM_READY) &&
        (cmd_ack == SUBBOARD_STARTUP_CMD_PREPARE_VIDEO) &&
        (cmd_result == SUBBOARD_STARTUP_RESULT_OK)) {
        if (send_start_dsp_cmd() == 0) {
            MASTER_LOG_INFO("[MASTER] sent START_DSP\r\n");
            s_start_dsp_sent = true;
        } else {
            MASTER_LOG_WARN("[MASTER] failed to send START_DSP\r\n");
        }
    }
}

static void handle_running_state(void)
{
    subboard_detection_result_t result;
    bool displayable;
    uint32_t now_ms = millis();

    master_detection_overlay_tick();
    if (s_overlay_active && !master_detection_overlay_is_active()) {
        MASTER_LOG_INFO("[MASTER] overlay cleared\r\n");
        s_overlay_active = false;
        s_overlay_invalid_since_ms = 0u;
    }

    if (read_regs(SUBBOARD_STARTUP_REG_RESULT,
                  (uint8_t *)&result,
                  sizeof(result)) != 0) {
        return;
    }

    displayable = result_is_displayable(&result);
    if (displayable) {
        s_overlay_invalid_since_ms = 0u;
    } else if (s_overlay_active) {
        if (s_overlay_invalid_since_ms == 0u) {
            s_overlay_invalid_since_ms = now_ms;
        }

        if ((uint32_t)(now_ms - s_overlay_invalid_since_ms) >= MASTER_OVERLAY_CLEAR_DEBOUNCE_MS) {
            master_detection_overlay_clear();
            if (s_overlay_active && !master_detection_overlay_is_active()) {
                MASTER_LOG_INFO("[MASTER] overlay cleared\r\n");
                s_overlay_active = false;
            }
            s_overlay_invalid_since_ms = 0u;
        }
    }

    if (memcmp(&result, &s_last_result, sizeof(result)) == 0) {
        return;
    }

    s_last_result = result;
    if (displayable) {
        master_detection_overlay_draw(&result);
        if (!s_overlay_active) {
            MASTER_LOG_INFO("[MASTER] overlay shown: count=%u conf=%u box=(%d,%d)-(%d,%d) face_id=%u\r\n",
                            (unsigned)result.count,
                            (unsigned)result.confidence,
                            (int)result.x1,
                            (int)result.y1,
                            (int)result.x2,
                            (int)result.y2,
                            (unsigned)result.face_id);
            s_overlay_active = true;
        }
        MASTER_LOG_DEBUG("[MASTER] face result: count=%u conf=%u box=(%d,%d)-(%d,%d) face_id=%u\r\n",
                         (unsigned)result.count,
                         (unsigned)result.confidence,
                         (int)result.x1,
                         (int)result.y1,
                         (int)result.x2,
                         (int)result.y2,
                         (unsigned)result.face_id);
    } else {
        MASTER_LOG_DEBUG("[MASTER] face result cleared\r\n");
    }
}

int master_demo_app_init(uint32_t (*get_millis_fn)(void))
{
    s_get_millis = get_millis_fn;

    MASTER_LOG_INFO("\r\n=================================================\r\n");
    MASTER_LOG_INFO("  S300 Subboard Video Bring-up Master Demo\r\n");
    MASTER_LOG_INFO("=================================================\r\n");

    reset_online_state();
    return master_i2c_init();
}

void master_demo_app_tick(void)
{
    uint8_t proto_ver = 0u;
    uint8_t state = SUBBOARD_STARTUP_STATE_BOOT;
    uint8_t error = SUBBOARD_STARTUP_ERR_NONE;
    uint8_t heartbeat = 0u;
    uint8_t request = SUBBOARD_STARTUP_REQ_NONE;
    uint8_t request_ack = SUBBOARD_STARTUP_REQ_NONE;
    uint8_t cmd_ack = SUBBOARD_STARTUP_CMD_NONE;
    uint8_t cmd_result = SUBBOARD_STARTUP_RESULT_OK;
    uint32_t now_ms = millis();
    uint32_t poll_ms = (s_last_state == SUBBOARD_STARTUP_STATE_RUNNING) ?
                       MASTER_RUN_POLL_MS :
                       MASTER_POLL_MS;

    if ((uint32_t)(now_ms - s_last_poll_ms) < poll_ms) {
        return;
    }
    s_last_poll_ms = now_ms;

    if (read_reg8(SUBBOARD_STARTUP_REG_PROTO_VER, &proto_ver) != 0) {
        MASTER_LOG_DEBUG("[MASTER] waiting subboard at 0x%02X\r\n", SUBBOARD_STARTUP_SLAVE_ADDR_CARD1);
        reset_online_state();
        return;
    }

    if (proto_ver != s_last_proto_ver) {
        MASTER_LOG_INFO("[MASTER] subboard online, proto=0x%02X\r\n", proto_ver);
        s_last_proto_ver = proto_ver;
    }

    (void)read_reg8(SUBBOARD_STARTUP_REG_SYS_STATE, &state);
    (void)read_reg8(SUBBOARD_STARTUP_REG_ERROR_CODE, &error);
    (void)read_reg8(SUBBOARD_STARTUP_REG_HEARTBEAT, &heartbeat);
    (void)read_reg8(SUBBOARD_STARTUP_REG_REQUEST, &request);
    (void)read_reg8(SUBBOARD_STARTUP_REG_REQUEST_ACK, &request_ack);
    (void)read_reg8(SUBBOARD_STARTUP_REG_CMD_ACK, &cmd_ack);
    (void)read_reg8(SUBBOARD_STARTUP_REG_CMD_RESULT, &cmd_result);

    log_snapshot(state, error, heartbeat, request, request_ack, cmd_ack, cmd_result);
    handle_request(request, request_ack);
    handle_commands(state, request_ack, cmd_ack, cmd_result);

    if (state == SUBBOARD_STARTUP_STATE_RUNNING) {
        handle_running_state();
    }
}