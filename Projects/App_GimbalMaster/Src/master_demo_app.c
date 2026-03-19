#include "master_demo_app.h"

#include <stdint.h>
#include <stdio.h>

#include "app_card1_subboard.h"
#include "app_card2_subboard.h"
#include "app_card3_subboard.h"
#include "app_fill_light.h"
#include "camera_ov5640.h"
#include "gpio.h"
#include "i2c_soft.h"
#include "master_detection_overlay.h"
#include "master_log.h"
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

#define MASTER_I2C_BUS_HZ              100000u
#define MASTER_I2C_RETRY               2u

static uint32_t (*s_get_millis)(void) = 0;
static i2c_soft_t s_i2c;
static bool s_i2c_ready = false;
static bool s_video_path_prepared = false;
static bool s_mm_runtime_enabled = false;
static bool s_mm_request_gate_open = false;

static master_demo_subboard_state_t make_subboard_state(uint8_t public_state,
                                                        bool init_complete,
                                                        bool init_success)
{
    master_demo_subboard_state_t state;
    bool online = (public_state != 0xFFu);

    state.public_state = public_state;
    state.online = online;
    state.running = (public_state == SUBBOARD_STARTUP_STATE_RUNNING);
    state.faulted = (online && (public_state == SUBBOARD_STARTUP_STATE_ERROR)) ||
                    (init_complete && !init_success);
    state.init_complete = init_complete;
    state.init_success = init_success;
    return state;
}

static uint32_t millis(void)
{
    return (s_get_millis != 0) ? s_get_millis() : 0u;
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

static int read_reg8_at(uint8_t slave_addr, uint8_t reg, uint8_t *value)
{
    int ret;
    uint32_t attempt;

    if ((value == NULL) || !s_i2c_ready) {
        return -1;
    }

    for (attempt = 0u; attempt < MASTER_I2C_RETRY; attempt++) {
        ret = i2c_soft_mem_read(&s_i2c,
                                slave_addr,
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

static int read_regs_at(uint8_t slave_addr, uint8_t reg, uint8_t *buffer, uint32_t length)
{
    int ret;
    uint32_t attempt;

    if ((buffer == NULL) || (length == 0u) || !s_i2c_ready) {
        return -1;
    }

    for (attempt = 0u; attempt < MASTER_I2C_RETRY; attempt++) {
        ret = i2c_soft_mem_read(&s_i2c,
                                slave_addr,
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

static int write_reg8_at(uint8_t slave_addr, uint8_t reg, uint8_t value)
{
    int ret;
    uint32_t attempt;

    if (!s_i2c_ready) {
        return -1;
    }

    for (attempt = 0u; attempt < MASTER_I2C_RETRY; attempt++) {
        ret = i2c_soft_mem_write(&s_i2c,
                                 slave_addr,
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
    if (s_mm_runtime_enabled) {
        MASTER_LOG_DEBUG("[MASTER] MM runtime already enabled, skip reapply\r\n");
        return;
    }

    trigger_core_reg_update();

#if defined(BOARD_LCD_SPI_ENABLE_ON_INIT) && (BOARD_LCD_SPI_ENABLE_ON_INIT == 0)
    MASTER_LOG_INFO("[MASTER] applied MM runtime enable: core only, local LCD SPI path disabled\r\n");
#else
    trigger_spi_reg_update();
    MASTER_LOG_INFO("[MASTER] applied MM runtime enable: core + lcd spi\r\n");
#endif

    s_mm_runtime_enabled = true;
}

static bool is_mm_request_gate_open(void)
{
    return s_mm_request_gate_open;
}

static int video_path_prepare(void)
{
    int ret;

    if (s_video_path_prepared) {
        MASTER_LOG_DEBUG("[MASTER] video path already ready, skip reinit\r\n");
        return 0;
    }

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

    if (app_fill_light_init() != 0) {
        MASTER_LOG_WARN("[MASTER] fill light control init failed, continue without runtime light control\r\n");
    }

    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);
    master_detection_overlay_init(millis);
    s_video_path_prepared = true;
    MASTER_LOG_INFO("[MASTER] video path ready\r\n");
    return 0;
}

bool master_demo_app_get_subboard_snapshot(master_demo_subboard_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    snapshot->card1 = make_subboard_state(app_card1_subboard_get_public_state(),
                                          app_card1_subboard_is_init_complete(),
                                          app_card1_subboard_is_init_successful());
    snapshot->card2 = make_subboard_state(app_card2_subboard_get_public_state(),
                                          app_card2_subboard_is_init_complete(),
                                          app_card2_subboard_is_init_successful());
    snapshot->card3 = make_subboard_state(app_card3_subboard_get_public_state(),
                                          app_card3_subboard_is_init_complete(),
                                          app_card3_subboard_is_init_successful());
    snapshot->any_running = snapshot->card1.running || snapshot->card2.running || snapshot->card3.running;
    snapshot->any_error = snapshot->card1.faulted || snapshot->card2.faulted || snapshot->card3.faulted;

    return true;
}

int master_demo_app_init(uint32_t (*get_millis_fn)(void))
{
    app_card1_subboard_ops_t card1_ops;
    app_card2_subboard_ops_t card2_ops;
    app_card3_subboard_ops_t card3_ops;

    s_get_millis = get_millis_fn;
    s_video_path_prepared = false;
    s_mm_runtime_enabled = false;
    s_mm_request_gate_open = false;

    MASTER_LOG_INFO("\r\n=================================================\r\n");
    MASTER_LOG_INFO("  S300 Gimbal Master App\r\n");
    MASTER_LOG_INFO("  Subboard startup coordination service\r\n");
    MASTER_LOG_INFO("=================================================\r\n");

    if (master_i2c_init() != 0) {
        return -1;
    }

    card1_ops.millis_fn = millis;
    card1_ops.read_reg8_at = read_reg8_at;
    card1_ops.read_regs_at = read_regs_at;
    card1_ops.write_reg8_at = write_reg8_at;
    card1_ops.prepare_video_path = video_path_prepare;
    card1_ops.trigger_mm_runtime_enable = trigger_mm_runtime_enable;
    card1_ops.is_mm_request_allowed = is_mm_request_gate_open;
    card1_ops.overlay_tick = master_detection_overlay_tick;
    card1_ops.overlay_is_active = master_detection_overlay_is_active;
    card1_ops.overlay_clear = master_detection_overlay_clear;
    card1_ops.overlay_draw = master_detection_overlay_draw;

    card2_ops.millis_fn = millis;
    card2_ops.read_reg8_at = read_reg8_at;
    card2_ops.read_regs_at = read_regs_at;
    card2_ops.write_reg8_at = write_reg8_at;
    card2_ops.prepare_video_path = video_path_prepare;
    card2_ops.trigger_mm_runtime_enable = trigger_mm_runtime_enable;
    card2_ops.is_mm_request_allowed = is_mm_request_gate_open;
    card2_ops.overlay_tick = NULL;
    card2_ops.overlay_is_active = NULL;
    card2_ops.overlay_clear = NULL;
    card2_ops.overlay_draw = NULL;

    card3_ops.millis_fn = millis;
    card3_ops.read_regs_at = read_regs_at;
    card3_ops.write_reg8_at = write_reg8_at;
    card3_ops.prepare_video_path = video_path_prepare;
    card3_ops.trigger_mm_runtime_enable = trigger_mm_runtime_enable;
    card3_ops.is_mm_request_allowed = is_mm_request_gate_open;

    if (app_card1_subboard_init(&card1_ops) != 0) {
        return -1;
    }
    if (app_card2_subboard_init(&card2_ops) != 0) {
        return -1;
    }
    if (app_card3_subboard_init(&card3_ops) != 0) {
        return -1;
    }

    return 0;
}

void master_demo_app_tick(void)
{
    master_demo_app_tick_target(MASTER_DEMO_POLL_ALL);
}

void master_demo_app_tick_target(master_demo_poll_target_t target)
{
    switch (target) {
    case MASTER_DEMO_POLL_CARD1:
        app_card1_subboard_tick();
        break;
    case MASTER_DEMO_POLL_CARD2:
        app_card2_subboard_tick();
        break;
    case MASTER_DEMO_POLL_CARD3:
        app_card3_subboard_tick();
        break;
    case MASTER_DEMO_POLL_ALL:
    default:
        app_card1_subboard_tick();
        app_card2_subboard_tick();
        app_card3_subboard_tick();
        break;
    }
}

int master_demo_app_finalize_subboard_init(void)
{
    if (video_path_prepare() != 0) {
        return -1;
    }

    trigger_mm_runtime_enable();
    s_mm_request_gate_open = true;
    MASTER_LOG_INFO("[MASTER] subboard init phase complete, MM request gate opened\r\n");
    return 0;
}