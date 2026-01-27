#include "s300.h"
#include "rcc.h"
#include "gpio.h"
#include "uart.h"
#include "i2c_soft.h"
#include "ov5640.h"
#include <stdio.h>
#include "board.h"

#ifndef UART_DEBUG_IDX
    #define UART_DEBUG_IDX BOARD_DEBUG_UART_IDX
#endif

/* 可选：启用内置色条测试图，便于快速验证视频链路（1 开启 / 0 关闭） */
#ifndef OV5640_ENABLE_COLOR_BAR
    #define OV5640_ENABLE_COLOR_BAR 1
#endif
/* 可选：控制 OV5640 补光灯（1 开灯 / 0 关灯） */
#ifndef OV5640_ENABLE_LIGHT
    #define OV5640_ENABLE_LIGHT 1
#endif

static void debug_uart_init(void)
{
    // Use Board Macros
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true); // Assuming IDX 3 for now or use switch
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_GPIO, true);
    
    gpio_set_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_TX_PIN, BOARD_DEBUG_UART_FUNCTION);
    gpio_set_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_RX_PIN, BOARD_DEBUG_UART_FUNCTION);
    
    init_uart(UART_DEBUG_IDX, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), BOARD_DEBUG_UART_BAUDRATE);
    setvbuf(stdout, NULL, _IONBF, 0);
}

int main(void)
{
    debug_uart_init();
    printf("[OV5640] Soft I2C init demo\n");
    printf("  I2C SCL=GPIO%d, SDA=GPIO%d\n", BOARD_CAMERA_I2C_SCL_PIN, BOARD_CAMERA_I2C_SDA_PIN);
    printf("  CAM RST=GPIO%d, PWDN=0x%02X\n", BOARD_CAM_RST_PIN, BOARD_CAM_PWDN_PIN);
    
    /* 初始化软 I2C */
    i2c_soft_t cam_i2c;
    
    /* 注意：软件 I2C 需要 FUNCTION_2 (GPIO 模式)，不是 FUNCTION_3 (硬件 I2C) */
    i2c_soft_cfg_t cfg = {
        .port = BOARD_CAMERA_I2C_PORT,
        .pin_scl = BOARD_CAMERA_I2C_SCL_PIN,
        .pin_sda = BOARD_CAMERA_I2C_SDA_PIN,
        .func_scl = FUNCTION_2,  /* GPIO 模式用于软件 I2C */
        .func_sda = FUNCTION_2,
        .pull_mode = GPIO_UP,
        .bus_hz = 50000
    };
    int ret = i2c_soft_init(&cam_i2c, &cfg, SystemCoreClock);

    if (ret)
    {
        printf("i2c init fail %d\n", ret);
        for (;;) __WFI();
    }
    (*((volatile uint32_t*)(RCC_BASE + 0x0018))) |= 1;

    // Use driver hard init
    printf("Executing camera power-on sequence...\n");
    ov5640_hard_init();
    printf("Power-on sequence done\n");
    
    /* 恢复总线并探测 0x3C/0x3D，选择有效地址 */
    i2c_soft_bus_recover(&cam_i2c);
    uint8_t saddr = 0x3C;
    int p3c = i2c_soft_probe(&cam_i2c, 0x3C);
    int p3d = i2c_soft_probe(&cam_i2c, 0x3D);
    printf("Probe 0x3C=%d 0x3D=%d\n", p3c, p3d);
    if (p3c != 0 && p3d == 0) saddr = 0x3D;
    /* 读取 Chip ID（高 0x300A，低 0x300B，期望 0x56 0x40） */
    uint8_t idh = 0, idl = 0;
    i2c_soft_mem_read(&cam_i2c, saddr, 0x300A, true, &idh, 1);
    i2c_soft_mem_read(&cam_i2c, saddr, 0x300B, true, &idl, 1);
    printf("OV5640 ID: 0x%02X 0x%02X (saddr=0x%02X)\n", idh, idl, saddr);
    /* 初始化 OV5640 为 YUYV 输出（720p 缩放参数在表内） */
    ret = ov5640_init(&cam_i2c, saddr, OV5640_FMT_YUV422_YUYV);
    printf("ov5640_init ret=%d\n", ret);
    if (ret == 0)
    {
        /* 再次读取 ID，部分板卡上电后首次读低字节可能为 0，初始化后再读一次更稳妥 */
        uint8_t idh2 = 0, idl2 = 0;
        i2c_soft_mem_read(&cam_i2c, saddr, 0x300A, true, &idh2, 1);
        i2c_soft_mem_read(&cam_i2c, saddr, 0x300B, true, &idl2, 1);
        printf("OV5640 ID after init: 0x%02X 0x%02X\n", idh2, idl2);
#if OV5640_ENABLE_COLOR_BAR
        int cr = ov5640_set_color_bar(&cam_i2c, saddr, true);
        printf("Enable color bar: %s\n", cr == 0 ? "OK" : "FAIL");
#endif
#if OV5640_ENABLE_LIGHT
        int lr = ov5640_set_light(&cam_i2c, saddr, true);
        printf("Enable light: %s\n", lr == 0 ? "OK" : "FAIL");
    /* 简短预览一段时间后自动关闭，避免常亮 */
    for (volatile uint32_t i = 0; i < 4800000u; ++i) __asm volatile("nop");
    int lf = ov5640_set_light(&cam_i2c, saddr, false);
    printf("Disable light: %s\n", lf == 0 ? "OK" : "FAIL");
#endif
    }
    /* 可选：打开色条测试图 */
    // ov5640_set_color_bar(&i2c1, OV5640_I2C_ADDR, true);
    for (;;)
    {
        __WFI();
    }
}
