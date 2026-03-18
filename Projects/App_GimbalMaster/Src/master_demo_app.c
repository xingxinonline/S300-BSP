#include "master_demo_app.h"

#include <stdint.h>
#include <stdio.h>

#include "app_card1_subboard.h"
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

    if (app_fill_light_init() != 0) {
        MASTER_LOG_WARN("[MASTER] fill light control init failed, continue without runtime light control\r\n");
    }

    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);
    master_detection_overlay_init(millis);
    MASTER_LOG_INFO("[MASTER] video path ready\r\n");
    return 0;
}

bool master_demo_app_is_subboard_running(void)
{
    return app_card1_subboard_is_running() || app_card3_subboard_is_running();
}

uint8_t master_demo_app_get_subboard_state(void)
{
    uint8_t card1_state = app_card1_subboard_get_public_state();
    uint8_t card3_state = app_card3_subboard_get_public_state();

    if (card1_state == SUBBOARD_STARTUP_STATE_RUNNING || card3_state == SUBBOARD_STARTUP_STATE_RUNNING) {
        return SUBBOARD_STARTUP_STATE_RUNNING;
    }

    if (card1_state != 0xFFu) {
        return card1_state;
    }

    return card3_state;
}

int master_demo_app_init(uint32_t (*get_millis_fn)(void))
{
    app_card1_subboard_ops_t card1_ops;
    app_card3_subboard_ops_t card3_ops;

    s_get_millis = get_millis_fn;

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
    card1_ops.overlay_tick = master_detection_overlay_tick;
    card1_ops.overlay_is_active = master_detection_overlay_is_active;
    card1_ops.overlay_clear = master_detection_overlay_clear;
    card1_ops.overlay_draw = master_detection_overlay_draw;

    card3_ops.millis_fn = millis;
    card3_ops.read_regs_at = read_regs_at;
    card3_ops.write_reg8_at = write_reg8_at;
    card3_ops.prepare_video_path = video_path_prepare;
    card3_ops.trigger_mm_runtime_enable = trigger_mm_runtime_enable;

    if (app_card1_subboard_init(&card1_ops) != 0) {
        return -1;
    }
    if (app_card3_subboard_init(&card3_ops) != 0) {
        return -1;
    }

    return 0;
}

void master_demo_app_tick(void)
{
    app_card1_subboard_tick();
    app_card3_subboard_tick();
}