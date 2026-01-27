
#include "video.h"
#include "s300.h"
#include "board.h"
#include "gpio.h"
#include "rcc.h"

#define EXTRACT_COMBINE_REG1(cfg_data_row, cfg_start_x) \
    ((((cfg_data_row) >> 8) & 0xFF) << 24) | \
    (((cfg_start_x) & 0xFF) << 16) | \
    ((((cfg_start_x) >> 8) & 0xFF) << 8) | \
    0x2A

#define EXTRACT_COMBINE_REG2(cfg_data_row, cfg_start_y) \
    (((cfg_start_y) & 0xFF) << 24) | \
    ((((cfg_start_y) >> 8) & 0xFF) << 16) | \
    (0x2B << 8) | \
    (((cfg_data_row) >> 0) & 0xFF)

#define EXTRACT_COMBINE_REG3(cfg_data_col) \
    (0x00 << 24) | \
    (0x2C << 16) | \
    ((((cfg_data_col) >> 0) & 0xFF) << 8) | \
    (((cfg_data_col) >> 8) & 0xFF)

/**
 * The function `init_video` is used to inital the regs for video ,the video sensor is ov5640.
 * 
 * @param dvp i2c device number 0-3 @see emDVPs 
 * @param formatPro is used to select format control ,the camera will be output format including YUV, RGB by desired, @see emCameraFormatPro
 * @param mmPro Used to select the processing mode for controlling image processing @see emMMProcessPro
 */


void init_video(emDVP dvp, emCameraFormatPro formatPro, emMMProcessPro mmPro)
{
    // 使用 board.h 中的配置，默认为 ST7735S
    init_video_with_type(dvp, formatPro, mmPro, (emLcdType)BOARD_LCD_TYPE);
}

static void lcd_spi_init_st7735s(void)
{
    /* spi init for ST7735S (HS180S10B) */
    // 定义 MADCTL 寄存器中控制旋转的三个关键位
    #define MADCTL_MY           (1 << 7)  // 页面地址顺序（上下翻转）
    #define MADCTL_MX           (1 << 6)  // 列地址顺序（左右翻转）
    #define MADCTL_MV           (1 << 5)  // 页面/列顺序（行列反转）

    /* 扫描方向配置：
     * 0x00 = 左上角起点，向右向下扫描 (正常)
     * MADCTL_MY = 左下角起点，向右向上扫描
     * MADCTL_MX = 右上角起点，向左向下扫描
     * MADCTL_MY|MX = 右下角起点 (180度旋转)
     * MADCTL_MV = 行列交换 (90度旋转)
     */
    #define MADCTL_SCAN_DIR     (MADCTL_MV | MADCTL_MY | MADCTL_MX)  /* 左上角起点 */

    REG32(DSP_VIDEO_SS_BASE + 0x100) = 0xFFFDFFFE; 
    REG32(DSP_VIDEO_SS_BASE + 0x104) = 0x072204E0; 
    REG32(DSP_VIDEO_SS_BASE + 0x108) = 0x25302E0A; 
    REG32(DSP_VIDEO_SS_BASE + 0x10c) = 0x2E26282A;
    REG32(DSP_VIDEO_SS_BASE + 0x110) = 0x0301003A; 
    REG32(DSP_VIDEO_SS_BASE + 0x114) = 0x1604E113; 
    REG32(DSP_VIDEO_SS_BASE + 0x118) = 0x262D0D06; 
    REG32(DSP_VIDEO_SS_BASE + 0x11c) = 0x25272723;
    REG32(DSP_VIDEO_SS_BASE + 0x120) = 0x01003B2D;

    REG32(DSP_VIDEO_SS_BASE + 0x124) = 0x7755B6BB; 
    REG32(DSP_VIDEO_SS_BASE + 0x128) = 0x28C01304;
    REG32(DSP_VIDEO_SS_BASE + 0x12c) = 0xC0C10408;
    REG32(DSP_VIDEO_SS_BASE + 0x130) = 0xC3000DC2;//0x3a283680;
    REG32(DSP_VIDEO_SS_BASE + 0x134) = 0x8DC42A8D;//0xb100b066;
    REG32(DSP_VIDEO_SS_BASE + 0x138) = 0x361AC5EE;//0x2b411b0;
    
    REG32(DSP_VIDEO_SS_BASE + 0x13c) = 0xB1063A00 | MADCTL_SCAN_DIR;//0xb70202b6;
    REG32(DSP_VIDEO_SS_BASE + 0x140) = 0xB23C3C05;
    REG32(DSP_VIDEO_SS_BASE + 0x144) = 0xB33C3C05;

    REG32(DSP_VIDEO_SS_BASE + 0x148) = 0xBF;       
    REG32(DSP_VIDEO_SS_BASE + 0x14c) = 0x053C3C05;
    REG32(DSP_VIDEO_SS_BASE + 0x150) = 0x03B43C3C;
    REG32(DSP_VIDEO_SS_BASE + 0x154) = 0x11;

    REG32(DSP_VIDEO_SS_BASE + 0x190) = 0x3de;
}

static void lcd_spi_init_st7789(void)
{
    /* spi init for ST7789 (D200C2407V0) */
    
    #ifndef DISP_ROTATION
    #define DISP_ROTATION 0xC0  /* 默认旋转 180° (MADCTL_MY | MADCTL_MX) */
    #endif

    /* spi init */
    REG32(DSP_VIDEO_SS_BASE + 0x100) = 0xed56abea; 
    REG32(DSP_VIDEO_SS_BASE + 0x104) = 0x063a0036 | (DISP_ROTATION << 8);  /* 0x063ac036 for 180° rotation */
    REG32(DSP_VIDEO_SS_BASE + 0x108) = 0x000c0cb2; 
    REG32(DSP_VIDEO_SS_BASE + 0x10c) = 0x35b73333;
    REG32(DSP_VIDEO_SS_BASE + 0x110) = 0x2cc032bb; 
    REG32(DSP_VIDEO_SS_BASE + 0x114) = 0xc3ff01c2; 
    REG32(DSP_VIDEO_SS_BASE + 0x118) = 0xc620c419; 
    REG32(DSP_VIDEO_SS_BASE + 0x11c) = 0xa1a4d00f;
    REG32(DSP_VIDEO_SS_BASE + 0x120) = 0x0200d0e0;

    REG32(DSP_VIDEO_SS_BASE + 0x124) = 0xf3fff7ff; 
    REG32(DSP_VIDEO_SS_BASE + 0x128) = 0x32280a07;
    REG32(DSP_VIDEO_SS_BASE + 0x12c) = 0x0e064244;
    REG32(DSP_VIDEO_SS_BASE + 0x130) = 0xe1171412;
    REG32(DSP_VIDEO_SS_BASE + 0x134) = 0x070200d0;
    REG32(DSP_VIDEO_SS_BASE + 0x138) = 0x5431280a;

    REG32(DSP_VIDEO_SS_BASE + 0x13c) = 0x171c0e47;
    REG32(DSP_VIDEO_SS_BASE + 0x140) = 0x29211e1b;
    REG32(DSP_VIDEO_SS_BASE + 0x190) = 0xfffffbde;
}

// 兼容函数，保留旧 API
void init_high_camera_st77_lcd(emMM mm, emCameraFormatPro cameraPro, emMMProcessPro mmPro)
{
    init_video_with_type(EM_DVP, cameraPro, mmPro, LCD_ST7735S);
}

void init_video_with_type(emDVP dvp, emCameraFormatPro cameraPro, emMMProcessPro mmPro, emLcdType lcdType)
{
  (void)dvp;
  // set_cortex_m4_core_reset(EM_BOOL_FALSE);//  *(volatile UINT32*)(RCC_BASE+ 0x18) =  0xFFFFFFFF;
  // *(volatile UINT32*)(DSP_RCC_BASE + 0x40)  = 0x0;
  // *(volatile UINT32*)(DSP_RCC_BASE + 0x40)  = 0xFFFFFFFF;

    REG32(DSP_RCC_BASE + 0x08) &= ~0x20;
    REG32(DSP_RCC_BASE + 0x0c) &= ~0x20;
    REG32(DSP_RCC_BASE + 0x40) &= ~0x11;
    delay_ms(10);
    REG32(DSP_RCC_BASE + 0x08) |= 0x20;
    REG32(DSP_RCC_BASE + 0x0c) |= 0x20;
    REG32(DSP_RCC_BASE + 0x40) |= 0x11;
    delay_ms(10);

#if defined(BOARD_LCD_BL_PIN) && (BOARD_LCD_BL_PIN != 0xFF)
    // Init Backlight
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_GPIO, true);
    gpio_set_function(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, FUNCTION_2);
    gpio_set_direction(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, 1);
    gpio_set_data(BOARD_LCD_BL_PORT, BOARD_LCD_BL_PIN, BOARD_LCD_BL_ACTIVE_LEVEL);
#endif

    // Common Regs
    // REG32(DSP_VIDEO_SS_BASE + 0x00) = (0 | (1 << 8));//0x100:YUV , 0x101:RGB565
    if (cameraPro == CAMREA_RGB565) {
        REG32(DSP_VIDEO_SS_BASE + 0x00) = 0x101;
    } else {
        REG32(DSP_VIDEO_SS_BASE + 0x00) = 0x100;
    }


    REG32(DSP_VIDEO_SS_BASE + 0x04) = (SENSOR_IMAGE_WIDTH | (SENSOR_IMAGE_HEIGHT << 16));
    REG32(DSP_VIDEO_SS_BASE + 0x08) = BINNING_SIZE;
    REG32(DSP_VIDEO_SS_BASE + 0x0c) = (BINNING_IMAGE_WIDTH | (BINNING_IMAGE_HEIGHT << 16));
    REG32(DSP_VIDEO_SS_BASE + 0x10) = (DOWNSCALE_IMAGE_WIDTH | (DOWNSCALE_IMAGE_HEIGHT << 16));
    REG32(DSP_VIDEO_SS_BASE + 0x14) = (int)((float)BINNING_IMAGE_WIDTH * DOWNSCALE_FACTOR / DOWNSCALE_IMAGE_WIDTH);
    REG32(DSP_VIDEO_SS_BASE + 0x18) = (int)((float)BINNING_IMAGE_HEIGHT * DOWNSCALE_FACTOR / DOWNSCALE_IMAGE_HEIGHT);
    REG32(DSP_VIDEO_SS_BASE + 0x1c) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x20) = (DISP_IMAGE_WIDTH | (DISP_IMAGE_HEIGHT << 16));
    REG32(DSP_VIDEO_SS_BASE + 0x24) = 0x0;

    REG32(DSP_VIDEO_SS_BASE + 0x28) = (SNAP_IMAGE_WIDTH | (SNAP_IMAGE_HEIGHT << 16));
    REG32(DSP_VIDEO_SS_BASE + 0x2c) = 1 | (0 << 8);

    REG32(DSP_VIDEO_SS_BASE + 0x30) = DISP_WFRAME0_ADDR;
    REG32(DSP_VIDEO_SS_BASE + 0x34) = DISP_WFRAME1_ADDR;

    REG32(DSP_VIDEO_SS_BASE + 0x40) = DISP_RFRAME0_ADDR;
    REG32(DSP_VIDEO_SS_BASE + 0x44) = DISP_RFRAME1_ADDR;
    REG32(DSP_VIDEO_SS_BASE + 0x48) = DISP_RALPHA0_ADDR;
    REG32(DSP_VIDEO_SS_BASE + 0x4c) = DISP_RALPHA1_ADDR;

    REG32(DSP_VIDEO_SS_BASE + 0x50) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x54) = 0x0;

    REG32(DSP_VIDEO_SS_BASE + 0x58) = (SNAP_IMAGE_WIDTH | (SNAP_IMAGE_HEIGHT << 16));
    REG32(DSP_VIDEO_SS_BASE + 0x5c) = RD_SOURCE_FRAME_START_X | (RD_SOURCE_FRAME_START_Y << 16);

    REG32(DSP_VIDEO_SS_BASE + 0x60) = 0x0;

    // Common Regs for Extract/Combine
    REG32(DSP_VIDEO_SS_BASE + 0x194) = EXTRACT_COMBINE_REG1(DISP_IMAGE_WIDTH + DISP_START_X - 1, DISP_START_X);
    REG32(DSP_VIDEO_SS_BASE + 0x198) = EXTRACT_COMBINE_REG2(DISP_IMAGE_WIDTH + DISP_START_X - 1, DISP_START_Y);
    REG32(DSP_VIDEO_SS_BASE + 0x19c) = EXTRACT_COMBINE_REG3(DISP_IMAGE_HEIGHT + DISP_START_Y - 1);
    
    REG32(DSP_VIDEO_SS_BASE + 0x1a0) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1a4) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1a8) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1ac) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1b0) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1c4) = (DISP_IMAGE_WIDTH | (DISP_IMAGE_HEIGHT << 16));
    REG32(DSP_VIDEO_SS_BASE + 0x1d0) = 0x10000;
    
    // Config first, then Enable
    if (lcdType == LCD_ST7735S)
    {
        // 1. Set Config Registers (SPI sequence)
        lcd_spi_init_st7735s(); 

        // 2. Set Timing/Polarity for Init (Low Speed 0x49)
        REG32(DSP_VIDEO_SS_BASE + 0x1c0) = (73 | (1 << 30)); 

        // 3. Enable Video Subsystem
        REG32(DSP_VIDEO_SS_BASE + 0x1e0) = 0x1;
        
        // 4. Wait for reset/init sequence
        delay_ms(5);

        // 5. Send Display On (0x29) - Overwriting previous Sequence registers
        REG32(DSP_VIDEO_SS_BASE + 0x100) = 0x0;
        REG32(DSP_VIDEO_SS_BASE + 0x104) = 0x29;

        // 6. Switch to High Speed for Data
        REG32(DSP_VIDEO_SS_BASE + 0x1c0) = (0x1 | (11 << 23) | (1 << 29));
    }
    else if (lcdType == LCD_ST7789)
    {
        /* ST7789 初始化时序 (参考原始配置) */
        
        /* 1. 发送软复位命令 (0x11: Sleep Out) */
        REG32(DSP_VIDEO_SS_BASE + 0x100) = 0x0;
        REG32(DSP_VIDEO_SS_BASE + 0x104) = 0x11;
        REG32(DSP_VIDEO_SS_BASE + 0x1c0) = 1 | (1 << 30);
        REG32(DSP_VIDEO_SS_BASE + 0x1e0) = 0x1;
        delay_ms(120);  /* 等待 LCD 退出睡眠模式 */
        
        /* 2. 配置 SPI 初始化寄存器 */
        lcd_spi_init_st7789();
        
        /* 3. 配置显示区域寄存器 (针对 240x320) */
        REG32(DSP_VIDEO_SS_BASE + 0x194) = 0x0000002A;  /* X 坐标命令 */
        REG32(DSP_VIDEO_SS_BASE + 0x198) = 0x00002BEF;  /* X 结束 + Y 命令 */
        REG32(DSP_VIDEO_SS_BASE + 0x19c) = 0x002C3F01;  /* Y 结束 + 写入命令 */
        
        /* 4. 设置时序控制 */
        REG32(DSP_VIDEO_SS_BASE + 0x1c0) = 0x2580003c;
        REG32(DSP_VIDEO_SS_BASE + 0x1d0) = 0x10000;
        REG32(DSP_VIDEO_SS_BASE + 0x1e0) = 0x0;
    }
    else 
    {
         REG32(DSP_VIDEO_SS_BASE + 0x1e0) = 0x1;
    }

    REG32(DSP_VIDEO_SS_BASE + 0x70) = 0x0;
    
    // Debug
    // print_video_registers();
}



/**
 * The function `init_mm_subsystem` is used to set mm subystem config .
 * 
 * @param mm i2c device number 0-3 @see emMM
 * @param cameraPro Select the camera data format to process @see emCameraFormatPro
 * @param mmPro Used to select the processing mode for controlling image processing @see emMMProcessPro
 */
void init_mm_subsystem(emMM mm,emCameraFormatPro cameraPro,emMMProcessPro mmPro)
{
  (void)mm;
	// UINT32 temp;

  // set_cortex_m4_core_reset(EM_BOOL_TRUE);

  // *(volatile UINT32*)(DSP_RCC_BASE + 0x40)  = 0x0;
  // *(volatile UINT32*)(DSP_RCC_BASE + 0x40)  = 0xFFFFFFFF;

  if(cameraPro == CAMREA_RGB565 )
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x00) =  0x101;//0x100:YUV , 0x101:RGB565
  if(cameraPro == CAMREA_YUV422 )
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x00) =  0x100;//0x100:YUV , 0x101:RGB565


  if((mmPro & C1080X720P) == C1080X720P)
  {
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x04) =  0x2d00500;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x08) =  0x2;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x0c) =  0xb40140;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x10) =  0xa000f0;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x14) =  0x2aaa;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x18) =  0x2400;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x1c) =  0x0;

    if((mmPro & BIG_PIC_MODE) == BIG_PIC_MODE)
      *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x20) =  0x14001e0; //该值可考虑开放给客户通过宏进行配置
    else
      *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x20) =  0xa000f0;//0x14001e0; //裁剪尺寸

    *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x24) =  0x0;
    
    if((mmPro & BIG_PIC_MODE) == BIG_PIC_MODE)
      *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x20) =  0x14001e0; //裁剪尺寸
    else
      *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x28) =  0xa000f0;//0x14001e0; //裁剪尺寸

    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x2c) =  0x0;
  }else{
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x04) =  0x7890a20;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x08) =  0x2;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x0c) =  0x1e60288;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x10) =  0x14001e0;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x14) =  0x2b33;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x18) =  0x3099;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1c) =  0x0;
    if((mmPro & BIG_PIC_MODE) == BIG_PIC_MODE)
      *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x20) =  0x14001e0; //裁剪尺寸
    else
      *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x20) =  0xa000f0;//0x14001e0; //裁剪尺寸
 
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x24) =  0x0;

    if((mmPro & BIG_PIC_MODE) == BIG_PIC_MODE)
      *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x20) =  0x14001e0; //裁剪尺寸
    else
      *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x28) =  0xa000f0;//0x14001e0; //裁剪尺寸
  
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x2c) =  0x0;
  }
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x30) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x34) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x38) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x3c) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x40) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x44) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x48) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x4c) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x50) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x54) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x60) =  0x1;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x70) =  0x1;//刷新前面寄存器的配置
  // *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x70) =  0x1;//刷新前面寄存器的配置

  // MM SPI
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x100) =  0xfffefffe;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x104) =  0xf0700e0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x108) =  0x3c0a1b0d;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x10c) =  0xe074a78;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x110) =  0xf1e1b09;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x114) =  0x242200e1;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x118) =  0x36071206;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x11c) =  0xa064747;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x120) =  0xf373007;

  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x124) =  0xf56b55d6;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x128) =  0xc11010c0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x12c) =  0x2a00c541;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x130) =  0x3a283680;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x134) =  0xb100b066;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x138) =  0x2b411b0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x13c) =  0xb70202b6;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x140) =  0xf700e9c6;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x144) =  0x822c51a9;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x148) =  0x0;

  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x14c) =  0x11;

  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x190) =  0x3de;

  if((mmPro & BIG_PIC_MODE) == BIG_PIC_MODE) {
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x194) =  0x100002a; //SPI 命令，调整SPI屏幕显示大小
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x198) =  0x2bdf;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x19c) =  0x2c3f01;
  }else{
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x194) =  0x2a;//0x100002a; //SPI 命令，调整SPI屏幕显示大小
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x198) =  0x2bef;//0x2bdf;
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x19c) =  0x2c9f00;//0x2c3f01;
  }
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1a0) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1a4) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1a8) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1ac) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1b0) =  0x0;

  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1c0) =  0x40000041;
  
  if((mmPro & BIG_PIC_MODE) == BIG_PIC_MODE)
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1c4) =  0x14001e0; //配置MM传给SPI的图像大小
  else
    *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1c4) =  0xa000f0;//0x14001e0; //配置MM传给SPI的图像大小

  // if((mmPro & USE_DVP_CLK) == USE_DVP_CLK)//for debug
  //   *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1d0) =  0x10000;//设置分频比
  // else
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1d0) =  0x10000;

  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1e0) =  0x1;

  delay_ms(5);
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x100) =  0x0;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x104) =  0x29;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1c0) =  0x25800001;
  *(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x1e0) =  0x1;

  //*(volatile UINT32*)(DSP_VIDEO_SS_BASE+ 0x70) =  0x1;
}


/**
 * The function `delayms_mm` is used to generate delays for mm subsystem.
 * 
 * @param d is used to set the time of delay
 */
// void delay_ms(UINT32 d)
// {
//   UINT32 i =0;
//   // UINT32 perclkdiv=0;
//   // UINT32 clk_num =0;
//   // perclkdiv= 1000000000/sys_clock;
//   // clk_num= d*1000000/perclkdiv;
//   // for(i=clk_num;i>0;i--);
//   #if CONFIG_EN_OS_HARDWARE_SUPPORT
//   for(i=d*100;i>0;i--)
//     delay();
//   #else
//   for(i=d*100;i>0;i--)
//     delay();
//    #endif

// }

void delay_ms(UINT32 d)
{
  UINT32 i =0;
  UINT32 perclktime=0;
  UINT32 clkNum =0;
  perclktime= 1000000000/20000000;
  clkNum= d*1000000/perclktime;
  for(i=clkNum;i>0;i--);

}

//=======后续拆分init_mm_subsystem=======视频可以显示表明初始化成功=====

// void init_spi_lcd(emMMProcessPro mmPro)
// {
 
// }
/**
 * The function `mm_init_small` is used to set mm subystem config .
 * 
 * @param mm i2c device number 0-3 @see emMM
 * @param cameraPro Select the camera data format to process @see emCameraFormatPro
 * @param mmPro Used to select the processing mode for controlling image processing @see emMMProcessPro
 */
// void init_mm_subsystem(emMM mm,emCameraFormatPro cameraPro,emMMProcessPro mmPro)

void calculate_binning(ImageDimensions input_size, int *binning_value, ImageDimensions *actual_binning_size)
{
    int binning_options[] = {0, 2, 4, 8, 16};
    int num_binning_options = sizeof(binning_options) / sizeof(binning_options[0]);
    int binning_width, binning_height;
    // 根据输入图片大小计算合适的binning值
    for (int i = 0; i < num_binning_options; ++i)
    {
        int binning = binning_options[i];
        if (binning == 0)
        {
            // 如果binning值为0，保持原尺寸
            binning_width = input_size.width;
            binning_height = input_size.height;
        }
        else
        {
            binning_width = input_size.width / binning;
            binning_height = input_size.height / binning;
        }
        if (binning_width <= BINNING_LINE_MAX_SIZE && binning_height <= BINNING_LINE_MAX_SIZE)
        {
            *binning_value = binning;
            actual_binning_size->width = binning_width;
            actual_binning_size->height = binning_height;
            return;
        }
    }
    // 如果没有找到满足条件的binning值，使用最大可能的binning值
    *binning_value = binning_options[num_binning_options - 1];
    actual_binning_size->width = input_size.width / (*binning_value);
    actual_binning_size->height = input_size.height / (*binning_value);
}

void calculate_downscale(ImageDimensions actual_binning_size, ImageDimensions output_size, DownscaleRatio *downscale)
{
    // 根据输入和输出大小计算downscale比例，不支持放大，若输入长或宽小于输出，则downscale为8192
    if (actual_binning_size.width <= output_size.width)
    {
        downscale->width = DOWNSCALE_FACTOR;
    }
    else
    {
        downscale->width = (float)actual_binning_size.width * DOWNSCALE_FACTOR / output_size.width;
    }
    if (actual_binning_size.height <= output_size.height)
    {
        downscale->height = DOWNSCALE_FACTOR;
    }
    else
    {
        downscale->height = (float)actual_binning_size.height * DOWNSCALE_FACTOR / output_size.height;
    }
}
#include <stddef.h>
void calculate_crop_coordinates(ImageDimensions downscale_size, ImageDimensions target_size, CropType crop_type, CropCoordinates *crop_coordinates)
{
    if (crop_coordinates == NULL)
    {
        return;
    }
    // 如果输入图片尺寸小于目标图片尺寸，则不裁剪
    if (downscale_size.width <= target_size.width && downscale_size.height <= target_size.height)
    {
        crop_coordinates->x = 0;
        crop_coordinates->y = 0;
        return;
    }
    switch (crop_type)
    {
    case CROP_CENTER:
        // 中心裁剪
        crop_coordinates->x = (downscale_size.width - target_size.width) / 2;
        crop_coordinates->y = (downscale_size.height - target_size.height) / 2;
        break;
    case CROP_TOP_LEFT:
        // 左上角裁剪
        crop_coordinates->x = 0;
        crop_coordinates->y = 0;
        break;
    case CROP_BOTTOM_RIGHT:
        // 右下角裁剪
        crop_coordinates->x = downscale_size.width - target_size.width;
        crop_coordinates->y = downscale_size.height - target_size.height;
        break;
    default:
        // 默认使用中心裁剪
        crop_coordinates->x = (downscale_size.width - target_size.width) / 2;
        crop_coordinates->y = (downscale_size.height - target_size.height) / 2;
        break;
    }
}

void init_mm_subsystem_small(emMM mm,emCameraFormatPro cameraPro,emMMProcessPro mmPro)
{
  (void)mm;
  (void)cameraPro;
  (void)mmPro;
    // if(cameraPro == CAMREA_RGB565 )
    //   *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x00) = (1 | (1 << 8));//0x100:YUV , 0x101:RGB565
    // if(cameraPro == CAMREA_YUV422 )
    REG32(DSP_VIDEO_SS_BASE + 0x00) = (0 | (1 << 8));//0x100:YUV , 0x101:RGB565

    REG32(DSP_VIDEO_SS_BASE + 0x04) = (SENSOR_IMAGE_WIDTH | (SENSOR_IMAGE_HEIGHT << 16));//0x2d00500;//0x7890a20;//(SENSOR_IMAGE_WIDTH | (IMAGE_HEIGHT << 16));
    REG32(DSP_VIDEO_SS_BASE + 0x08) = BINNING_SIZE;//BINNING_RATIO;
    REG32(DSP_VIDEO_SS_BASE + 0x0c) = (BINNING_IMAGE_WIDTH | (BINNING_IMAGE_HEIGHT << 16));//0xb40140;//0x1680280;//WIDTH_AFTER_BINNING | (HEIGHT_AFTER_BINNING << 16);
    REG32(DSP_VIDEO_SS_BASE + 0x10) = (DOWNSCALE_IMAGE_WIDTH | (DOWNSCALE_IMAGE_HEIGHT << 16));//0x1400140;//0x1400168;//0xa000f0;//0x14001e0;//WIDTH_AFTER_DOWNSCALE | (HEIGHT_AFTER_DOWNSCALE << 16);
    REG32(DSP_VIDEO_SS_BASE + 0x14) = (int)((float)BINNING_IMAGE_WIDTH * DOWNSCALE_FACTOR / DOWNSCALE_IMAGE_WIDTH);//(int)downscale_ratio.width;//0x38e3;//0x2aaa;//0x2000;//0x2aaa;//0x2b33;//HORIZONAL_DOWNSCALE_RATIO;
    REG32(DSP_VIDEO_SS_BASE + 0x18) = (int)((float)BINNING_IMAGE_HEIGHT * DOWNSCALE_FACTOR / DOWNSCALE_IMAGE_HEIGHT);//0x2400;//0x2000;//0x2400;//0x3099;//VERTICAL_DOWNSCALE_RATIO;
    REG32(DSP_VIDEO_SS_BASE + 0x1c) = 0x0;//CROP_DISP_START_X | (CROP_DISP_START_Y << 16);
    REG32(DSP_VIDEO_SS_BASE + 0x20) = (DISP_IMAGE_WIDTH | (DISP_IMAGE_HEIGHT << 16));//0x1400168;//0xb40140;//0xa000f0;//0x14001e0;//0xa000f0; //裁剪尺寸//CROP_DISP_WIDTH | (CROP_DISP_HEIGHT << 16);
    REG32(DSP_VIDEO_SS_BASE + 0x24) = 0x0;//CROP_SNAP_START_X | (CROP_SNAP_START_Y << 16);
    REG32(DSP_VIDEO_SS_BASE + 0x28) = (SNAP_IMAGE_WIDTH | (SNAP_IMAGE_HEIGHT << 16));//0x1400168;//0xb40140;//0xa000f0;//0x14001e0;//0xa000f0; //裁剪尺寸//CROP_SNAP_WIDTH | (CROP_SNAP_HEIGHT << 16);
    REG32(DSP_VIDEO_SS_BASE + 0x2c) = 1 | (0 << 8);
    REG32(DSP_VIDEO_SS_BASE + 0x30) = 0x44080000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 3);//0x80000000;
    REG32(DSP_VIDEO_SS_BASE + 0x34) = 0x44080000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 3);//0x80100000;
    REG32(DSP_VIDEO_SS_BASE + 0x38) = 0x0;//0;
    REG32(DSP_VIDEO_SS_BASE + 0x3c) = 0x0;//0;
    REG32(DSP_VIDEO_SS_BASE + 0x40) = 0x44080000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 5);//0x44035000;//0x80200000;
    REG32(DSP_VIDEO_SS_BASE + 0x44) = 0x44080000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 5);//0x44035000;//0x80300000;
    REG32(DSP_VIDEO_SS_BASE + 0x48) = 0x44080000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 1);//0x44060000;//0x80250000;
    REG32(DSP_VIDEO_SS_BASE + 0x4c) = 0x44080000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 1);//0x44060000;//0x80350000;
    REG32(DSP_VIDEO_SS_BASE + 0x50) = 0x0;//0;
    REG32(DSP_VIDEO_SS_BASE + 0x54) = 0x0;//0;
    REG32(DSP_VIDEO_SS_BASE + 0x58) = (DISP_IMAGE_WIDTH | (DISP_IMAGE_HEIGHT << 16));
    REG32(DSP_VIDEO_SS_BASE + 0x5c) = RD_SOURCE_FRAME_START_X | (RD_SOURCE_FRAME_START_Y << 16);
    REG32(DSP_VIDEO_SS_BASE + 0x60) = 0x0;//0;
    //  *(volatile UINT32*)(DSP_VIDEO_SS_BASE + 0x70) = 0x1;//1;
    /* spi init */
    REG32(DSP_VIDEO_SS_BASE + 0x100) = 0xFFFEFFFE; //0xfffefffe;
    REG32(DSP_VIDEO_SS_BASE + 0x104) = 0x0F0700E0; //0xf0700e0;
    REG32(DSP_VIDEO_SS_BASE + 0x108) = 0x3C0A1B0D; //0x3c0a1b0d;
    REG32(DSP_VIDEO_SS_BASE + 0x10c) = 0x0E074A78; //0xe074a78;
    REG32(DSP_VIDEO_SS_BASE + 0x110) = 0x0F1E1B09; //0xf1e1b09;
    REG32(DSP_VIDEO_SS_BASE + 0x114) = 0x242200E1; //0x242200e1;
    REG32(DSP_VIDEO_SS_BASE + 0x118) = 0x36071206; //0x36071206;
    REG32(DSP_VIDEO_SS_BASE + 0x11c) = 0x0A064747; //0xa064747;
    REG32(DSP_VIDEO_SS_BASE + 0x120) = 0x0F373007; //0xf373007;
    REG32(DSP_VIDEO_SS_BASE + 0x124) = 0xF56B55D6; //0xf56b55d6;
    REG32(DSP_VIDEO_SS_BASE + 0x128) = 0xC11010c0;//0xc11010c0;
    REG32(DSP_VIDEO_SS_BASE + 0x12c) = 0x2A00C541;//0x2a00c541;
    REG32(DSP_VIDEO_SS_BASE + 0x130) = 0x3A083680;//0x3a283680;
    REG32(DSP_VIDEO_SS_BASE + 0x134) = 0xB100B066;//0xb100b066;
    REG32(DSP_VIDEO_SS_BASE + 0x138) = 0x02B411B0;//0x2b411b0;
    REG32(DSP_VIDEO_SS_BASE + 0x13c) = 0xB70202B6;//0xb70202b6;
    REG32(DSP_VIDEO_SS_BASE + 0x140) = 0xF700E9C6;//0xf700e9c6;
    REG32(DSP_VIDEO_SS_BASE + 0x144) = 0x822c51A9;//0x822c51a9;
    REG32(DSP_VIDEO_SS_BASE + 0x148) = 0x0;       //0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x14c) = 0x11;
    REG32(DSP_VIDEO_SS_BASE + 0x190) = 0x3de;
    // REG32(DSP_VIDEO_SS_BASE + 0x194) = 0x2a;
    // REG32(DSP_VIDEO_SS_BASE + 0x198) = 0x2bef;
    // REG32(DSP_VIDEO_SS_BASE + 0x19c) = 0x2c9f00;
    // REG32(DSP_VIDEO_SS_BASE + 0x194) = 0x0100002a;
    // REG32(DSP_VIDEO_SS_BASE + 0x198) = 0x2b3f;
    // REG32(DSP_VIDEO_SS_BASE + 0x19c) = 0x2cb300;
    // REG32(DSP_VIDEO_SS_BASE+ 0x194) =  0x100002a; //SPI 命令，调整SPI屏幕显示大小
    // REG32(DSP_VIDEO_SS_BASE+ 0x198) =  0x2b3f;
    // REG32(DSP_VIDEO_SS_BASE+ 0x19c) =  0x2c8f01;
    REG32(DSP_VIDEO_SS_BASE + 0x194) = EXTRACT_COMBINE_REG1(DISP_IMAGE_WIDTH + DISP_START_X - 1, DISP_START_X);
    REG32(DSP_VIDEO_SS_BASE + 0x198) = EXTRACT_COMBINE_REG2(DISP_IMAGE_WIDTH + DISP_START_X - 1, DISP_START_Y);
    REG32(DSP_VIDEO_SS_BASE + 0x19c) = EXTRACT_COMBINE_REG3(DISP_IMAGE_HEIGHT + DISP_START_Y - 1);
    REG32(DSP_VIDEO_SS_BASE + 0x1a0) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1a4) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1a8) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1ac) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1b0) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x1c0) = 65 | (1 << 30);
    REG32(DSP_VIDEO_SS_BASE + 0x1c4) = (DISP_IMAGE_WIDTH | (DISP_IMAGE_HEIGHT << 16));//0x1400168;//0xb40140;//0xa000f0;//0x14001e0;//0xa000f0;
    REG32(DSP_VIDEO_SS_BASE + 0x1d0) = 0x10000;
    REG32(DSP_VIDEO_SS_BASE + 0x1e0) = 0x1;
    delay_ms(5);//arm_delay_us(100000);
    REG32(DSP_VIDEO_SS_BASE + 0x100) = 0x0;
    REG32(DSP_VIDEO_SS_BASE + 0x104) = 0x29;
    REG32(DSP_VIDEO_SS_BASE + 0x1c0) = (0x1 | (11 << 23) | (1 << 29));
    // REG32(DSP_VIDEO_SS_BASE + 0x1e0) = 0x1;
}




