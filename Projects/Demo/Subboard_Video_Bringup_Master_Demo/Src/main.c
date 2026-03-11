#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "camera_ov5640.h"
#include "gpio.h"
#include "i2c_soft.h"
#include "psram.h"
#include "rcc.h"
#include "s300.h"
#include "subboard_startup_proto.h"
#include "video.h"

#if BOARD_CAMERA_FORMAT == 0
#define APP_CAM_FMT CAMREA_RGB565
#else
#define APP_CAM_FMT CAMREA_YUV422
#endif

#define MASTER_I2C_BUS_HZ 100000u
#define MASTER_POLL_MS    200u
#define MASTER_I2C_RETRY    2u

static volatile uint32_t g_tick_ms = 0u;
static i2c_soft_t g_i2c;
static bool g_i2c_ready = false;
static bool g_video_prepared = false;
static bool g_prepare_sent = false;

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t millis(void)
{
    return g_tick_ms;
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
    case SUBBOARD_STARTUP_REQ_MASTER_CORE_SYNC: return "REQUEST_MASTER_CORE_SYNC";
    case SUBBOARD_STARTUP_REQ_MASTER_SPI_SYNC: return "REQUEST_MASTER_SPI_SYNC";
    default: return "UNKNOWN";
    }
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

    ret = i2c_soft_init(&g_i2c, &cfg, SystemCoreClock);
    if (ret != 0) {
        printf("[MASTER] i2c init failed=%d\r\n", ret);
        return -1;
    }

    i2c_soft_bus_recover(&g_i2c);
    g_i2c_ready = true;
    return 0;
}

static int read_reg8(uint8_t reg, uint8_t *value)
{
    int ret;
    uint32_t attempt;

    if ((value == NULL) || !g_i2c_ready) {
        return -1;
    }

    for (attempt = 0u; attempt < MASTER_I2C_RETRY; attempt++) {
        ret = i2c_soft_mem_read(&g_i2c,
                                SUBBOARD_STARTUP_SLAVE_ADDR_CARD1,
                                reg,
                                false,
                                value,
                                1u);
        if (ret == 0) {
            return 0;
        }

        i2c_soft_bus_recover(&g_i2c);
    }

    return -1;
}

static int write_reg8(uint8_t reg, uint8_t value)
{
    int ret;
    uint32_t attempt;

    if (!g_i2c_ready) {
        return -1;
    }

    for (attempt = 0u; attempt < MASTER_I2C_RETRY; attempt++) {
        ret = i2c_soft_mem_write(&g_i2c,
                                 SUBBOARD_STARTUP_SLAVE_ADDR_CARD1,
                                 reg,
                                 false,
                                 &value,
                                 1u);
        if (ret == 0) {
            return 0;
        }

        i2c_soft_bus_recover(&g_i2c);
    }

    return -1;
}

static void dump_startup_regs_once(void)
{
    const uint8_t regs[] = {
        SUBBOARD_STARTUP_REG_STATUS,
        SUBBOARD_STARTUP_REG_SYS_STATE,
        SUBBOARD_STARTUP_REG_ERROR_CODE,
        SUBBOARD_STARTUP_REG_HEARTBEAT,
        SUBBOARD_STARTUP_REG_PROTO_VER,
        SUBBOARD_STARTUP_REG_REQUEST,
        SUBBOARD_STARTUP_REG_REQUEST_ARG,
        SUBBOARD_STARTUP_REG_REQUEST_ACK,
    };
    uint8_t value;

    printf("[MASTER][I2CDBG]");
    for (uint32_t i = 0; i < (sizeof(regs) / sizeof(regs[0])); i++) {
        if (read_reg8(regs[i], &value) == 0) {
            printf(" reg%02X=%02X", regs[i], value);
        } else {
            printf(" reg%02X=ERR", regs[i]);
        }
    }
    printf("\r\n");
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

static int video_path_prepare(void)
{
    int ret;

    ret = rcc_init_mm_pll(8, 400, 0, 3, 2);
    if (ret != RCC_STATUS_OK) {
        printf("[MASTER] rcc_init_mm_pll failed=%d\r\n", ret);
        return -1;
    }

    init_psram(4, 1);

    ret = camera_ov5640_preinit();
    if (ret != 0) {
        printf("[MASTER] camera_ov5640_preinit failed=%d\r\n", ret);
        return -1;
    }

    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);
    printf("[MASTER] video path ready\r\n");
    return 0;
}

int main(void)
{
    uint32_t last_debug_ms;
    uint8_t last_proto_ver = 0xFFu;
    uint8_t last_state = 0xFFu;
    uint8_t last_error = 0xFFu;
    uint8_t last_heartbeat = 0xFFu;
    uint8_t last_request = 0xFFu;
    uint8_t last_request_ack = 0xFFu;
    uint8_t last_cmd_ack = 0xFFu;
    uint8_t last_cmd_result = 0xFFu;

    board_init();

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        while (1) {
        }
    }

    printf("\r\n=================================================\r\n");
    printf("  S300 Subboard Video Bring-up Master Demo\r\n");
    printf("=================================================\r\n");

    if (master_i2c_init() != 0) {
        while (1) {
        }
    }

    last_debug_ms = millis();

    while (1) {
        uint8_t proto_ver = 0u;
        uint8_t state = SUBBOARD_STARTUP_STATE_BOOT;
        uint8_t error = SUBBOARD_STARTUP_ERR_NONE;
        uint8_t heartbeat = 0u;
        uint8_t request = SUBBOARD_STARTUP_REQ_NONE;
        uint8_t request_ack = SUBBOARD_STARTUP_REQ_NONE;
        uint8_t cmd_ack = SUBBOARD_STARTUP_CMD_NONE;
        uint8_t cmd_result = SUBBOARD_STARTUP_RESULT_OK;

        if (read_reg8(SUBBOARD_STARTUP_REG_PROTO_VER, &proto_ver) != 0) {
            printf("[MASTER] waiting subboard at 0x%02X\r\n", SUBBOARD_STARTUP_SLAVE_ADDR_CARD1);
            g_video_prepared = false;
            g_prepare_sent = false;
            goto next_poll;
        }

        if (proto_ver != last_proto_ver) {
            printf("[MASTER] subboard online, proto=0x%02X\r\n", proto_ver);
            dump_startup_regs_once();
            last_proto_ver = proto_ver;
        }

        if ((millis() - last_debug_ms) >= 1000u) {
            dump_startup_regs_once();
            last_debug_ms = millis();
        }

        (void)read_reg8(SUBBOARD_STARTUP_REG_SYS_STATE, &state);
        (void)read_reg8(SUBBOARD_STARTUP_REG_ERROR_CODE, &error);
        (void)read_reg8(SUBBOARD_STARTUP_REG_HEARTBEAT, &heartbeat);
        (void)read_reg8(SUBBOARD_STARTUP_REG_REQUEST, &request);
        (void)read_reg8(SUBBOARD_STARTUP_REG_REQUEST_ACK, &request_ack);
        (void)read_reg8(SUBBOARD_STARTUP_REG_CMD_ACK, &cmd_ack);
        (void)read_reg8(SUBBOARD_STARTUP_REG_CMD_RESULT, &cmd_result);

        if (state != last_state) {
            printf("[MASTER] subboard_state=%s\r\n", public_state_name(state));
            last_state = state;
        }

        if (error != last_error) {
            printf("[MASTER] subboard_error=0x%02X\r\n", error);
            last_error = error;
        }

        if (heartbeat != last_heartbeat) {
            printf("[MASTER] subboard_heartbeat=%u\r\n", (unsigned)heartbeat);
            last_heartbeat = heartbeat;
        }

        if ((request != last_request) || (request_ack != last_request_ack)) {
            printf("[MASTER] request=%s ack=%s\r\n",
                   request_name(request),
                   request_name(request_ack));
            last_request = request;
            last_request_ack = request_ack;
        }

        if ((cmd_ack != last_cmd_ack) || (cmd_result != last_cmd_result)) {
            printf("[MASTER] cmd_ack=0x%02X result=%s\r\n",
                   cmd_ack,
                   result_name(cmd_result));
            last_cmd_ack = cmd_ack;
            last_cmd_result = cmd_result;
        }

        if ((request == SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE) &&
            (request_ack != SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE)) {
            if (!g_video_prepared) {
                if (video_path_prepare() != 0) {
                    printf("[MASTER] video path prepare failed, waiting retry\r\n");
                    goto next_poll;
                }
                g_video_prepared = true;
            }

            if (write_reg8(SUBBOARD_STARTUP_REG_REQUEST_ACK,
                           SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE) == 0) {
                printf("[MASTER] acknowledged REQUEST_MASTER_MM_ENABLE\r\n");
            } else {
                printf("[MASTER] failed to acknowledge REQUEST_MASTER_MM_ENABLE\r\n");
            }
        }

        if (!g_prepare_sent &&
            (request_ack == SUBBOARD_STARTUP_REQ_MASTER_MM_ENABLE) &&
            ((state == SUBBOARD_STARTUP_STATE_WAIT_MASTER_MM) ||
             (state == SUBBOARD_STARTUP_STATE_WAIT_VIDEO) ||
             (state == SUBBOARD_STARTUP_STATE_I2C_READY))) {
            if (send_prepare_video_cmd() == 0) {
                printf("[MASTER] sent PREPARE_VIDEO_CONSUMER\r\n");
                g_prepare_sent = true;
            } else {
                printf("[MASTER] failed to send PREPARE_VIDEO_CONSUMER\r\n");
            }
        }

        if (state == SUBBOARD_STARTUP_STATE_MM_READY) {
            printf("[MASTER] bring-up success: subboard MM_READY\r\n");
        }

next_poll:
        {
            uint32_t start_ms = millis();
            while ((millis() - start_ms) < MASTER_POLL_MS) {
            }
        }
    }
}