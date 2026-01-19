#include <stdio.h>
#include "s300.h"
#include "rcc.h"
#include "gpio.h"
#include "i2c_soft.h"
#include "ov5640.h"
#include "camera_ov5640.h"
#include "board.h"

int camera_ov5640_preinit(void)
{
    /* Initialize GPIO Clock */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_GPIO, true);

    /* Camera Control Pins Init (Manual init using board.h macros) */
    /* PWDN */
    set_gpio_function(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, BOARD_CAM_CTRL_FUNCTION);
    set_gpio_mode(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, GPIO_UP);
    set_gpio_direction(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 1);
    
    /* RST */
    set_gpio_function(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, BOARD_CAM_CTRL_FUNCTION);
    set_gpio_mode(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, GPIO_UP);
    set_gpio_direction(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 1);

    /* Camera I2C Pins Init */
    set_gpio_function(BOARD_CAMERA_I2C_PORT, BOARD_CAMERA_I2C_SCL_PIN, BOARD_CAMERA_I2C_FUNCTION);
    set_gpio_function(BOARD_CAMERA_I2C_PORT, BOARD_CAMERA_I2C_SDA_PIN, BOARD_CAMERA_I2C_FUNCTION);

    /* Power On Sequence */
    /* Hardware Reset / Power Cycle */
    set_gpio_data(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 0);
    set_gpio_data(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 1);
    for (volatile uint32_t i = 0; i < 800000u; i++) __asm volatile("nop");
    set_gpio_data(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 0);
    for (volatile uint32_t i = 0; i < 800000u; i++) __asm volatile("nop");
    set_gpio_data(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 1);
    for (volatile uint32_t i = 0; i < 2400000u; i++) __asm volatile("nop");

    /* I2C Init */
    i2c_soft_t i2c1;
    i2c_soft_cfg_t cfg = {
        .port = BOARD_CAMERA_I2C_PORT,
        .pin_scl = BOARD_CAMERA_I2C_SCL_PIN,
        .pin_sda = BOARD_CAMERA_I2C_SDA_PIN,
        .func_scl = BOARD_CAMERA_I2C_FUNCTION,
        .func_sda = BOARD_CAMERA_I2C_FUNCTION,
        .pull_mode = GPIO_UP,
        .bus_hz = 50000 /* 50kHz for slow OV5640 I2C */
    };
    
    i2c_soft_init(&i2c1, &cfg, SystemCoreClock); /* Using custom pins and system clock */
    
    (void)i2c_soft_bus_recover(&i2c1);

    uint8_t saddr = 0x3C;
    int p3c = i2c_soft_probe(&i2c1, 0x3C);
    int p3d = i2c_soft_probe(&i2c1, 0x3D);
    if (p3c != 0 && p3d == 0) saddr = 0x3D;

    uint8_t idh = 0, idl = 0;
    (void)i2c_soft_mem_read(&i2c1, saddr, 0x300Au, true, &idh, 1);
    (void)i2c_soft_mem_read(&i2c1, saddr, 0x300Bu, true, &idl, 1);
    printf("[S300][Gesture][CAM] OV5640 ID: 0x%02X 0x%02X (addr=0x%02X)\r\n", idh, idl, saddr);
    
    /* Light control if supported by driver/board (optional) */
    ov5640_set_light(&i2c1, saddr, false);

    /* Force YUV422 for Gesture Demo */
    int ret = ov5640_init(&i2c1, saddr, OV5640_FMT_YUV422_YUYV);
    printf("[S300][Gesture][CAM] ov5640_init ret=%d\r\n", ret);
    
    return ret;
}
