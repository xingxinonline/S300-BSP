/**
 * @file    main.c
 * @brief   FT6X36 电容触摸屏测试 Demo
 * @details 验证触摸驱动是否正常工作
 *          - 初始化触摸 I2C 和控制引脚
 *          - 执行硬件复位
 *          - 读取设备信息
 *          - 循环读取触摸数据并通过串口输出
 * 
 * @note    仅支持 app_board（应用板）
 */

#include "s300.h"
#include "board.h"
#include "rcc.h"
#include "gpio.h"
#include "uart.h"
#include "i2c_soft.h"
#include "ft6x36.h"
#include <stdio.h>

/*===========================================================================
 * Global Variables
 *===========================================================================*/

static i2c_soft_t g_touch_i2c;
static ft6x36_t   g_touch_dev;

/*===========================================================================
 * Private Functions
 *===========================================================================*/

/**
 * @brief 简单延时
 */
static void delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 19200u; j++) {
            __asm volatile("nop");
        }
    }
}

/**
 * @brief 打印设备信息
 */
static void print_device_info(const ft6x36_dev_info_t *info)
{
    printf("FT6X36 Device Info:\n");
    printf("  Chip ID:     0x%02X\n", info->chip_id);
    printf("  Firmware:    0x%02X\n", info->firmware_id);
    printf("  Vendor ID:   0x%02X\n", info->vendor_id);
    printf("  Lib Version: 0x%04X\n", info->lib_version);
}

/**
 * @brief 获取手势名称
 */
static const char* get_gesture_name(uint8_t gesture_id)
{
    switch (gesture_id) {
        case FT6X36_GEST_ID_MOVE_UP:    return "Swipe Up";
        case FT6X36_GEST_ID_MOVE_DOWN:  return "Swipe Down";
        case FT6X36_GEST_ID_MOVE_LEFT:  return "Swipe Left";
        case FT6X36_GEST_ID_MOVE_RIGHT: return "Swipe Right";
        case FT6X36_GEST_ID_ZOOM_IN:    return "Zoom In";
        case FT6X36_GEST_ID_ZOOM_OUT:   return "Zoom Out";
        default:                        return NULL;
    }
}

/*===========================================================================
 * Main Entry
 *===========================================================================*/

int main(void)
{
    int ret;
    
    /* 板级初始化 */
    board_init();
    delay_ms(100);
    
    printf("\n========================================\n");
    printf("  FT6X36 Touch Screen Test Demo\n");
    printf("  Board: app_board\n");
    printf("========================================\n\n");
    
    /* 初始化触摸控制引脚 (RST) */
    board_touch_ctrl_pins_init();
    
    /* 执行触摸屏硬件复位 */
    board_touch_reset();
    delay_ms(300);
    
    /* 初始化触摸 I2C */
    board_touch_i2c_pins_init();
    ret = board_touch_i2c_init(&g_touch_i2c);
    if (ret != 0) {
        printf("ERROR: I2C init failed!\n");
        goto error_loop;
    }
    
    /* 恢复 I2C 总线 */
    i2c_soft_bus_recover(&g_touch_i2c);
    delay_ms(10);
    
    /* 探测触摸 IC */
    ret = i2c_soft_probe(&g_touch_i2c, BOARD_TOUCH_I2C_ADDR);
    if (ret != 0) {
        printf("ERROR: Touch IC not found at 0x%02X!\n", BOARD_TOUCH_I2C_ADDR);
        goto error_loop;
    }
    printf("Touch IC found at 0x%02X\n", BOARD_TOUCH_I2C_ADDR);
    
    /* 初始化 FT6X36 驱动 */
    ret = ft6x36_init(&g_touch_dev, &g_touch_i2c, BOARD_TOUCH_I2C_ADDR);
    if (ret != 0) {
        printf("ERROR: FT6X36 init failed!\n");
        goto error_loop;
    }
    
    /* 配置屏幕参数
     * 屏幕左下角为显示原点，触控右上角为原点
     * 需要同时反转 X 和 Y
     */
    ft6x36_config(&g_touch_dev, 
                  BOARD_LCD_WIDTH, 
                  BOARD_LCD_HEIGHT,
                  false,  /* swap_xy */
                  true,   /* invert_x */
                  true);  /* invert_y */
    
    printf("Screen: %dx%d\n", BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
    
    /* 读取设备信息 */
    ft6x36_dev_info_t dev_info;
    if (ft6x36_read_device_info(&g_touch_dev, &dev_info) == 0) {
        print_device_info(&dev_info);
    }
    
    /* 设置触摸阈值 */
    ft6x36_set_threshold(&g_touch_dev, 22);
    
    /* 设置中断模式为轮询 */
    ft6x36_set_interrupt_mode(&g_touch_dev, FT6X36_G_MODE_INT_POLLING);
    
    printf("\nTouch the screen to see coordinates...\n\n");
    
    /* 主循环 */
    ft6x36_touch_data_t touch_data;
    uint8_t last_touch_count = 0;
    uint8_t last_gesture = 0;
    
    while (1) {
        ret = ft6x36_read_touch(&g_touch_dev, &touch_data);
        
        if (ret == 0) {
            /* 检测手势 */
            if (touch_data.gesture_id != FT6X36_GEST_ID_NONE && 
                touch_data.gesture_id != last_gesture) {
                const char *gesture_name = get_gesture_name(touch_data.gesture_id);
                if (gesture_name) {
                    printf(">>> Gesture: %s <<<\n", gesture_name);
                }
            }
            last_gesture = touch_data.gesture_id;
            
            /* 打印触摸数据 */
            if (touch_data.touch_count > 0) {
                for (int i = 0; i < touch_data.touch_count; i++) {
                    const ft6x36_point_t *pt = &touch_data.points[i];
                    printf("P%d: (%3d, %3d)\n", i + 1, pt->x, pt->y);
                }
            } else if (last_touch_count > 0) {
                printf("Released\n");
            }
            
            last_touch_count = touch_data.touch_count;
        }
        
        delay_ms(20);
    }
    
error_loop:
    printf("Entering error loop...\n");
    while (1) {
        __WFI();
    }
    
    return 0;
}
