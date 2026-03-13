#include "camera_ov5640.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "gpio.h"
#include "master_log.h"
#include "ov5640.h"
#include "rcc.h"
#include "s300.h"

#if BOARD_CAMERA_FORMAT == 0
#define APP_OV_FMT OV5640_FMT_RGB565_R5G3_G3B5
#else
#define APP_OV_FMT OV5640_FMT_YUV422_YUYV
#endif

static i2c_soft_t s_camera_i2c;
static uint8_t s_camera_addr = OV5640_I2C_ADDR;
static bool s_camera_ready = false;

static int init_camera_i2c(void)
{
    i2c_soft_cfg_t cfg = {
        .port = BOARD_CAMERA_I2C_PORT,
        .pin_scl = BOARD_CAMERA_I2C_SCL_PIN,
        .pin_sda = BOARD_CAMERA_I2C_SDA_PIN,
        .func_scl = FUNCTION_2,
        .func_sda = FUNCTION_2,
        .pull_mode = GPIO_UP,
        .bus_hz = BOARD_CAMERA_I2C_FREQ,
    };

    return i2c_soft_init(&s_camera_i2c, &cfg, SystemCoreClock);
}

int camera_ov5640_preinit(void)
{
    int ret;
    int p3c;
    int p3d;
    uint8_t idh = 0u;
    uint8_t idl = 0u;
    int lr;
    int lf;

#if BOARD_CAMERA_ENABLE
    board_camera_i2c_pins_init();
    board_camera_ctrl_pins_init();
#endif

    ret = init_camera_i2c();
    if (ret != 0) {
        MASTER_LOG_WARN("[MASTER][CAM] i2c init fail=%d\r\n", ret);
        s_camera_ready = false;
        return ret;
    }

    ov5640_hard_init();
    (void)i2c_soft_bus_recover(&s_camera_i2c);

    s_camera_addr = OV5640_I2C_ADDR;
    p3c = i2c_soft_probe(&s_camera_i2c, 0x3Cu);
    p3d = i2c_soft_probe(&s_camera_i2c, 0x3Du);
    if ((p3c != 0) && (p3d == 0)) {
        s_camera_addr = 0x3Du;
    }

    (void)i2c_soft_mem_read(&s_camera_i2c, s_camera_addr, 0x300Au, true, &idh, 1u);
    (void)i2c_soft_mem_read(&s_camera_i2c, s_camera_addr, 0x300Bu, true, &idl, 1u);
    MASTER_LOG_INFO("[MASTER][CAM] OV5640 ID: 0x%02X 0x%02X (addr=0x%02X)\r\n", idh, idl, s_camera_addr);

    lr = ov5640_set_light(&s_camera_i2c, s_camera_addr, true);
    MASTER_LOG_INFO("[MASTER][CAM] light self-test on: %s\r\n", lr == 0 ? "OK" : "FAIL");
    for (volatile uint32_t delay = 0u; delay < 4800000u; ++delay) {
        __asm volatile("nop");
    }
    lf = ov5640_set_light(&s_camera_i2c, s_camera_addr, false);
    MASTER_LOG_INFO("[MASTER][CAM] light self-test off: %s\r\n", lf == 0 ? "OK" : "FAIL");

    ret = ov5640_init(&s_camera_i2c, s_camera_addr, APP_OV_FMT);
    MASTER_LOG_INFO("[MASTER][CAM] ov5640_init ret=%d\r\n", ret);
    s_camera_ready = (ret == 0);
    return ret;
}

bool camera_ov5640_get_context(i2c_soft_t **i2c, uint8_t *saddr)
{
    if (!s_camera_ready) {
        return false;
    }

    if (i2c != NULL) {
        *i2c = &s_camera_i2c;
    }
    if (saddr != NULL) {
        *saddr = s_camera_addr;
    }

    return true;
}