#ifndef _VIDEO_H_
#define _VIDEO_H_

#include <stdint.h>
#include "s300.h"
#include "board.h"
#include "video_config.h"

/* 兼容旧代码中使用的基本类型与寄存器访问宏 */
#ifndef UINT32
typedef uint32_t UINT32;
#endif
#ifndef UINT8
typedef uint8_t UINT8;
#endif
#ifndef REG32
#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#endif

/* *
 *	1.config the clock for PLL
 *	int init_mm_pll(stPLLPRO pro);
 *    int init_dsp_pll(stPLLPRO pro);
 *    set_mm_clock_enable(emBoolean en);
 *
 *	2. Configure the i2c function，Refer to the programming manual for specific serial port pins, see i2c.h
 *	 make sure the define of SLV_ADDR in ov5640.h,the SLV_ADDR is address for i2c extern device,there is the camera 
 *
 * 	3.Configure the gpio function，Refer to the programming manual for specific serial port pins, see gpio.h
 *		a.make sure the pin in  for CAMREA_RESETB and  CAMREA_PWDN to control the camera 
 *		b.Define initialization variables, gpio ports, pin numbers, and reuse function arrays 
 *		emGPIO portGPIO[] = {GPIOA,GPIOA};
 *		UINT8 pinGPIO[] = {12,13};// 12:CAMREA_PWDN ,13:CAMREA_RESETB
 *		emGPIOFUNC gpiofunc[] = {FUNCTION_2,FUNCTION_2};
 *		c. Example Initialize the gpio function
 *         	for(i = 0;i < sizeof(pinGPIO);i++)
 *             set_gpio_function(portGPIO[i],pinGPIO[i],gpiofunc[i]);
 *  
 *	4.init the externdevice of camera,there is a sample for ov5640
 *		init_camera(EM_DVP,EM_I2C0,SLV_ADDR,portGPIO,pinGPIO, CAMREA_YUV422)
 *	5.init the video
 *		void init_video(EM_DVP,CAMREA_YUV422,C1080X720P);
 *  6.Developers can develop according to demand
*/
typedef enum _DVP_{
    EM_DVP  = 0x0000
}emDVP;

typedef enum _camera_format_description_{
    CAMREA_RGB565  = 0x0000,
    CAMREA_YUV422  = 0x0001
}emCameraFormatPro;

typedef enum _mm_{
    EM_MM0 = 0x0000
}emMM;

typedef enum _mm_video_config_{
    C1080X720P      = 0x1,
    BIG_PIC_MODE    = 0x2,
    USE_DVP_CLK     = 0x4
}emMMProcessPro;

typedef enum
{
  LCD_ST7735S = 0, // HS180S10B
  LCD_ST7789 = 1,  // D200C2407V0
  LCD_UNKNOWN
} emLcdType;

void init_video_with_type(emDVP dvp, emCameraFormatPro formatPro, emMMProcessPro mmPro, emLcdType lcdType);

// 定义结构体
typedef struct
{
    int width;
    int height;
} ImageDimensions;

typedef struct
{
    float width;
    float height;
} DownscaleRatio;

typedef struct
{
    int x;
    int y;
} CropCoordinates;

// 定义枚举类型用于裁剪方式
typedef enum
{
    CROP_CENTER, // 裁剪中心
    CROP_TOP_LEFT,  // 裁剪左上角
    CROP_BOTTOM_RIGHT  // 裁剪右下角
} CropType;

void init_video(emDVP dvp,emCameraFormatPro formatPro,emMMProcessPro mmPro);
void init_mm_subsystem(emMM mm,emCameraFormatPro cameraPro,emMMProcessPro mmPro);
void init_mm_subsystem_small(emMM mm,emCameraFormatPro cameraPro,emMMProcessPro mmPro);
void delay_ms(UINT32 d);
void init_high_camera_st77_lcd(emMM mm,emCameraFormatPro cameraPro,emMMProcessPro mmPro);

#endif //_VIDEO_H_