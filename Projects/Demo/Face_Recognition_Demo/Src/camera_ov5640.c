/**
 * @file    camera_ov5640.c
 * @brief   OV5640 摄像头预初始化模块实现
 * @details 适配当前 SDK 架构，使用 board 层配置和驱动层 API
 */

#include <stdio.h>
#include "s300.h"
#include "rcc.h"
#include "gpio.h"
#include "i2c_soft.h"
#include "ov5640.h"
#include "camera_ov5640.h"
#include "board.h"

/* 根据 BOARD_CAMERA_FORMAT 选择输出格式 */
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

    /* I2C 软件初始化 - 需要 FUNCTION_2 (GPIO 模式) */
    i2c_soft_cfg_t cfg = {
        .port = BOARD_CAMERA_I2C_PORT,
        .pin_scl = BOARD_CAMERA_I2C_SCL_PIN,
        .pin_sda = BOARD_CAMERA_I2C_SDA_PIN,
        .func_scl = FUNCTION_2,  /* GPIO 模式用于软件 I2C */
        .func_sda = FUNCTION_2,
        .pull_mode = GPIO_UP,
        .bus_hz = 50000
    };
    int ret = i2c_soft_init(&i2c1, &cfg, SystemCoreClock);
    
    if (ret) {
        printf("[S300][Face_Recognition][CAM] i2c init fail %d\r\n", ret);
        return ret;
    }
    
    /* 使用驱动层函数完成摄像头上电时序 */
    ov5640_hard_init();
    
    /* 总线恢复（防止 SDA 被卡住） */
    (void)i2c_soft_bus_recover(&i2c1);

    /* 探测 I2C 地址 (0x3C 或 0x3D) */
    uint8_t saddr = 0x3C;
    int p3c = i2c_soft_probe(&i2c1, 0x3C);
    int p3d = i2c_soft_probe(&i2c1, 0x3D);
    if (p3c != 0 && p3d == 0) saddr = 0x3D;

    /* 读取 OV5640 Chip ID */
    uint8_t idh = 0, idl = 0;
    (void)i2c_soft_mem_read(&i2c1, saddr, 0x300Au, true, &idh, 1);
    (void)i2c_soft_mem_read(&i2c1, saddr, 0x300Bu, true, &idl, 1);
    printf("[S300][Face_Recognition][CAM] OV5640 ID: 0x%02X 0x%02X (addr=0x%02X)\r\n", idh, idl, saddr);

    /* 闪烁补光灯（可选，用于验证 I2C 通信） */
    int lr = ov5640_set_light(&i2c1, saddr, true);
    printf("[S300][Face_Recognition][CAM] Enable light: %s\n", lr == 0 ? "OK" : "FAIL");
    for (volatile uint32_t i = 0; i < 4800000u; ++i) __asm volatile("nop");
    int lf = ov5640_set_light(&i2c1, saddr, false);
    printf("[S300][Face_Recognition][CAM] Disable light: %s\n", lf == 0 ? "OK" : "FAIL");

    /* 初始化 OV5640 为指定格式 */
    ret = ov5640_init(&i2c1, saddr, APP_OV_FMT);
    printf("[S300][Face_Recognition][CAM] ov5640_init ret=%d\r\n", ret);
    
    return ret;
}
