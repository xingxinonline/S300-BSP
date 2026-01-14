#include <stdio.h>
#include "s300.h"
#include "rcc.h"
#include "gpio.h"
#include "i2c_soft.h"
#include "ov5640.h"
#include "camera_ov5640.h"
#include "board.h"

#if BOARD_CAMERA_FORMAT == 0
  #define APP_OV_FMT OV5640_FMT_RGB565_R5G3_G3B5
#else
  #define APP_OV_FMT OV5640_FMT_YUV422_YUYV
#endif

int camera_ov5640_preinit(void)
{
    i2c_soft_t i2c1;
    
    /* 使用 board 层初始化摄像头引脚 (I2C + 控制引脚) */
#if BOARD_CAMERA_ENABLE
    board_camera_i2c_pins_init();
    board_camera_ctrl_pins_init();
#endif

    /* I2C 软件初始化 */
    i2c_soft_cfg_t cfg = {
        .port = BOARD_CAMERA_I2C_PORT,
        .pin_scl = BOARD_CAMERA_I2C_SCL_PIN,
        .pin_sda = BOARD_CAMERA_I2C_SDA_PIN,
        .func_scl = BOARD_CAMERA_I2C_FUNCTION,
        .func_sda = BOARD_CAMERA_I2C_FUNCTION,
        .pull_mode = GPIO_UP,
        .bus_hz = 50000
    };
    int ret = i2c_soft_init(&i2c1, &cfg, SystemCoreClock);
    
    if (ret) {
        printf("[S300][DisplayDemo][CAM] i2c init fail %d\r\n", ret);
        return ret;
    }
    
    // Use driver hard init
    ov5640_hard_init();
    
    (void)i2c_soft_bus_recover(&i2c1);

    uint8_t saddr = 0x3C;
    int p3c = i2c_soft_probe(&i2c1, 0x3C);
    int p3d = i2c_soft_probe(&i2c1, 0x3D);
    if (p3c != 0 && p3d == 0) saddr = 0x3D;

    uint8_t idh = 0, idl = 0;
    (void)i2c_soft_mem_read(&i2c1, saddr, 0x300Au, true, &idh, 1);
    (void)i2c_soft_mem_read(&i2c1, saddr, 0x300Bu, true, &idl, 1);
    printf("[S300][DisplayDemo][CAM] OV5640 ID: 0x%02X 0x%02X (addr=0x%02X)\r\n", idh, idl, saddr);
    int lr = ov5640_set_light(&i2c1, saddr, true);
    printf("Enable light: %s\n", lr == 0 ? "OK" : "FAIL");
    for (volatile uint32_t i = 0; i < 4800000u; ++i) __asm volatile("nop");
    int lf = ov5640_set_light(&i2c1, saddr, false);
    printf("Disable light: %s\n", lf == 0 ? "OK" : "FAIL");
    ret = ov5640_init(&i2c1, saddr, APP_OV_FMT);
    printf("[S300][DisplayDemo][CAM] ov5640_init ret=%d\r\n", ret);
    return ret;
}
