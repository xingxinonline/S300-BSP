#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ov5640.h"
#include "video.h"
#include "board.h"
#include "video_config.h"
#include "rcc.h"

#ifndef BOARD_CAMERA_FPS
#define BOARD_CAMERA_FPS 30
#endif

#if (BOARD_CAMERA_FPS == 10)
#define OV5640_TIMING_VTS                 2952u
#elif (BOARD_CAMERA_FPS == 15)
#define OV5640_TIMING_VTS                 1968u
#elif (BOARD_CAMERA_FPS == 20)
#define OV5640_TIMING_VTS                 1476u
#elif (BOARD_CAMERA_FPS == 25)
#define OV5640_TIMING_VTS                 1181u
#elif (BOARD_CAMERA_FPS == 30)
#define OV5640_TIMING_VTS                 984u
#else
#error "Unsupported BOARD_CAMERA_FPS, expected one of 10, 15, 20, 25, 30"
#endif

#define OV5640_TIMING_VTS_H               ((OV5640_TIMING_VTS >> 8) & 0xFFu)
#define OV5640_TIMING_VTS_L               (OV5640_TIMING_VTS & 0xFFu)
#define OV5640_AE_MAX_EXPOSURE            (OV5640_TIMING_VTS * 6u)
#define OV5640_AE_MAX_60HZ_H              ((OV5640_AE_MAX_EXPOSURE >> 8) & 0xFFu)
#define OV5640_AE_MAX_60HZ_L              (OV5640_AE_MAX_EXPOSURE & 0xFFu)
#define OV5640_AE_MAX_50HZ_H              OV5640_AE_MAX_60HZ_H
#define OV5640_AE_MAX_50HZ_L              OV5640_AE_MAX_60HZ_L

/* 取材自原始驱动的初始化表，压缩为关键寄存器配置以演示移植；
 * 若需完整画质，请替换为完整表（可将大数组拆到独立 .inc 以减小编译单元体积）。
 */
static const uint16_t ov5640_init_cfg[][2] =
{
    //15fps VGA YUV output  // 24MHz input clock, 24MHz PCLK
    {0x3103, 0x11},//system clock from pad, bit[1]
    {0x3008, 0x82},//software reset, bit[7]// delay 5ms
    {0x3008, 0x42},//software power down, bit[6]
    {0x3103, 0x03},//system clock from PLL, bit[1]
    {0x3017, 0xFF},// FREX VNSYNC HREF PCLK D[9:6] output
    {0x3018, 0xFF},// D[5:0], GPIO[1:0] output enable
    {0x3034, 0x1A},//bit[6:4] PLL charge pump control  ,MIPI 10-bit
    {0x3037, 0x13},// PLL root divider, bit[4], PLL pre-divider, bit[3:0]
    {0x3108, 0x01},// PCLK root divider, bit[5:4], SCLK2x root divider, bit[3:2]
    {0x3630, 0x36},
    {0x3631, 0x0E},
    {0x3632, 0xE2},
    {0x3633, 0x12},
    {0x3621, 0xE0},
    {0x3704, 0xA0},
    {0x3703, 0x5A},
    {0x3715, 0x78},
    {0x3717, 0x01},
    {0x370B, 0x60},
    {0x3705, 0x1A},
    {0x3905, 0x02},
    {0x3906, 0x10},
    {0x3901, 0x0A},
    {0x3731, 0x12},
    {0x3600, 0x08}, // VCM control
    {0x3601, 0x33}, // VCM control
    {0x302D, 0x60}, // system control
    {0x3620, 0x52},
    {0x371B, 0x20},
    {0x471C, 0x50},
    {0x3A13, 0x43}, // pre-gain = 1.047x
    {0x3A18, 0x00}, // gain ceiling
    {0x3A19, 0xF8}, // gain ceiling = 15.5x
    {0x3635, 0x13},
    {0x3636, 0x03},
    {0x3634, 0x40},
    {0x3622, 0x01},
    // 50/60Hz detection     50/60Hz 灯光条纹过滤
    {0x3C01, 0x34},// Band auto, bit[7]
    {0x3C04, 0x28},// threshold low sum
    {0x3C05, 0x98},// threshold high sum
    {0x3C06, 0x00},// light meter 1 threshold[15:8]
    {0x3C07, 0x08},// light meter 1 threshold[7:0]
    {0x3C08, 0x00},// light meter 2 threshold[15:8]
    {0x3C09, 0x1C},// light meter 2 threshold[7:0]
    {0x3C0A, 0x9C},// sample number[15:8]
    {0x3C0B, 0x40},// sample number[7:0]
    {0x3810, 0x00},// Timing Hoffset[11:8]
    {0x3811, 0x10},// Timing Hoffset[7:0]
    {0x3812, 0x00},// Timing Voffset[10:8]
    {0x3708, 0x64},
    {0x4001, 0x02},// BLC start from line 2
    {0x4005, 0x1A},// BLC always update
    {0x3000, 0x00},// enable blocks
    {0x3004, 0xFF},// enable clocks
    {0x300E, 0x58},// MIPI power down, DVP enable
    {0x302E, 0x00},
    {0x4300, 0x30},// 正点原子bit[7:4] 0x3 YUV422 ,黑金 bit[7:4] 0x6 RGB565
    {0x501F, 0x00},//Bit[2:0]: Format select 000: ISP YUV422,黑金 001: ISP RGB
    {0x440E, 0x00},
    {0x5000, 0xA7},// Lenc on, raw gamma on, BPC on, WPC on, CIP on // AEC target    自动曝光控制
    {0x3A0F, 0x30},// stable range in high
    {0x3A10, 0x28},// stable range in low
    {0x3A1B, 0x30},// stable range out high
    {0x3A1E, 0x26},// stable range out low
    {0x3A11, 0x60},// fast zone high
    {0x3A1F, 0x14},// fast zone low// Lens correction for ?   镜头补偿
    {0x5800, 0x23},
    {0x5801, 0x14},
    {0x5802, 0x0F},
    {0x5803, 0x0F},
    {0x5804, 0x12},
    {0x5805, 0x26},
    {0x5806, 0x0C},
    {0x5807, 0x08},
    {0x5808, 0x05},
    {0x5809, 0x05},
    {0x580A, 0x08},
    {0x580B, 0x0D},
    {0x580C, 0x08},
    {0x580D, 0x03},
    {0x580E, 0x00},
    {0x580F, 0x00},
    {0x5810, 0x03},
    {0x5811, 0x09},
    {0x5812, 0x07},
    {0x5813, 0x03},
    {0x5814, 0x00},
    {0x5815, 0x01},
    {0x5816, 0x03},
    {0x5817, 0x08},
    {0x5818, 0x0D},
    {0x5819, 0x08},
    {0x581A, 0x05},
    {0x581B, 0x06},
    {0x581C, 0x08},
    {0x581D, 0x0E},
    {0x581E, 0x29},
    {0x581F, 0x17},
    {0x5820, 0x11},
    {0x5821, 0x11},
    {0x5822, 0x15},
    {0x5823, 0x28},
    {0x5824, 0x46},
    {0x5825, 0x26},
    {0x5826, 0x08},
    {0x5827, 0x26},
    {0x5828, 0x64},
    {0x5829, 0x26},
    {0x582A, 0x24},
    {0x582B, 0x22},
    {0x582C, 0x24},
    {0x582D, 0x24},
    {0x582E, 0x06},
    {0x582F, 0x22},
    {0x5830, 0x40},
    {0x5831, 0x42},
    {0x5832, 0x24},
    {0x5833, 0x26},
    {0x5834, 0x24},
    {0x5835, 0x22},
    {0x5836, 0x22},
    {0x5837, 0x26},
    {0x5838, 0x44},
    {0x5839, 0x24},
    {0x583A, 0x26},
    {0x583B, 0x28},
    {0x583C, 0x42},
    {0x583D, 0xCE}, // lenc BR offset // AWB   自动白平衡
    {0x5180, 0xFF}, // AWB B block
    {0x5181, 0xF2}, // AWB control
    {0x5182, 0x00}, // [7:4] max local counter, [3:0] max fast counter
    {0x5183, 0x14}, // AWB advanced
    {0x5184, 0x25},
    {0x5185, 0x24},
    {0x5186, 0x09},
    {0x5187, 0x09},
    {0x5188, 0x09},
    {0x5189, 0x75},
    {0x518A, 0x54},
    {0x518B, 0xE0},
    {0x518C, 0xB2},
    {0x518D, 0x42},
    {0x518E, 0x3D},
    {0x518F, 0x56},
    {0x5190, 0x46},
    {0x5191, 0xF8}, // AWB top limit
    {0x5192, 0x04}, // AWB bottom limit
    {0x5193, 0x70}, // red limit
    {0x5194, 0xF0}, // green limit
    {0x5195, 0xF0}, // blue limit
    {0x5196, 0x03}, // AWB control
    {0x5197, 0x01}, // local limit
    {0x5198, 0x04},
    {0x5199, 0x12},
    {0x519A, 0x04},
    {0x519B, 0x00},
    {0x519C, 0x06},
    {0x519D, 0x82},
    {0x519E, 0x38}, // AWB control // Gamma    伽玛曲线
    {0x5480, 0x01}, // Gamma bias plus on, bit[0]
    {0x5481, 0x08},
    {0x5482, 0x14},
    {0x5483, 0x28},
    {0x5484, 0x51},
    {0x5485, 0x65},
    {0x5486, 0x71},
    {0x5487, 0x7D},
    {0x5488, 0x87},
    {0x5489, 0x91},
    {0x548A, 0x9A},
    {0x548B, 0xAA},
    {0x548C, 0xB8},
    {0x548D, 0xCD},
    {0x548E, 0xDD},
    {0x548F, 0xEA},
    {0x5490, 0x1D}, // color matrix   色彩矩阵
    {0x5381, 0x1E}, // CMX1 for Y
    {0x5382, 0x5B}, // CMX2 for Y
    {0x5383, 0x08}, // CMX3 for Y
    {0x5384, 0x0A}, // CMX4 for U
    {0x5385, 0x7E}, // CMX5 for U
    {0x5386, 0x88}, // CMX6 for U
    {0x5387, 0x7C}, // CMX7 for V
    {0x5388, 0x6C}, // CMX8 for V
    {0x5389, 0x10}, // CMX9 for V
    {0x538A, 0x01}, // sign[9]
    {0x538B, 0x98}, // sign[8:1] // UV adjust   UV色彩饱和度调整
    {0x5580, 0x06}, // saturation on, bit[1]
    {0x5583, 0x40},
    {0x5584, 0x10},
    {0x5589, 0x10},
    {0x558A, 0x00},
    {0x558B, 0xF8},
    {0x501D, 0x40}, // enable manual offset of contrast// CIP  锐化和降噪
    {0x5300, 0x08}, // CIP sharpen MT threshold 1
    {0x5301, 0x30}, // CIP sharpen MT threshold 2
    {0x5302, 0x10}, // CIP sharpen MT offset 1
    {0x5303, 0x00}, // CIP sharpen MT offset 2
    {0x5304, 0x08}, // CIP DNS threshold 1
    {0x5305, 0x30}, // CIP DNS threshold 2
    {0x5306, 0x08}, // CIP DNS offset 1
    {0x5307, 0x16}, // CIP DNS offset 2
    {0x5309, 0x08}, // CIP sharpen TH threshold 1
    {0x530A, 0x30}, // CIP sharpen TH threshold 2
    {0x530B, 0x04}, // CIP sharpen TH offset 1
    {0x530C, 0x06}, // CIP sharpen TH offset 2
    {0x5025, 0x00},
    {0x3008, 0x02}, // wake up from standby, bit[6]
    {0x4740, 0x20} //正点原子 bit5：1 PCLK active high bit1:HREF 0 active low bit0：VSYNC 1 active high
};

static const uint16_t ov5640_yuv422_cfg[][2] =
{
    {0x4300, 0x61},//bit[7:4] 0x3 YUV422 ,bit[3:0] 0x0 Output sequence: YUYV
    {0x501F, 0x01},//Format select 0x01 ISP  RGB , 0x00 ISP YUV422
    {0x3035, 0x11},//PLL - VGA 30fps (datasheet 4.1.2)
    {0x3036, 0x46},//PLL - PCLK = 56MHz @ 24MHz input
    {0x3C07, 0x08},//light meter 1 threshold L (datasheet)
    {0x3820, 0x41},//Sensor flip , ISP flip;Bit[2]: ISP vflip  Bit[1]: Sensor vflip
    {0x3821, BOARD_OV5640_MIRROR_CFG},//Timing Control Bit[2]: ISP mirror Bit[1]: Sensor mirror
    {0x3814, 0x31},//X INC - 2x2 subsample
    {0x3815, 0x31},//Y INC - 2x2 subsample
    {0x3800, 0x00},//图像窗口 X start [11:8]
    {0x3801, 0x00},//图像窗口 X start [7:0] = 0
    {0x3802, 0x00},//图像窗口 Y start [10:8]
    {0x3803, 0x04},//图像窗口 Y start [7:0] = 4 (datasheet VGA)
    {0x3804, 0x0A},//图像窗口 X end [11:8]
    {0x3805, 0x3F},//图像窗口 X end [7:0] = 2623
    {0x3806, 0x07},//图像窗口 Y end [10:8]
    {0x3807, 0x9B},//图像窗口 Y end [7:0] = 1947 (datasheet VGA)
    {0x3808, (SENSOR_IMAGE_WIDTH >> 8) & 0xff}, //DVP output width[11:8]
    {0x3809, SENSOR_IMAGE_WIDTH & 0xff}, //DVP output width[7:0]
    {0x380A, (SENSOR_IMAGE_HEIGHT >> 8) & 0xff}, //DVP output height[10:8]
    {0x380B, SENSOR_IMAGE_HEIGHT & 0xff}, //DVP output height[7:0]
    {0x380C, 0x07},  // HTS[15:8] = 0x07
    {0x380D, 0x68},  // HTS[7:0]  = 0x68, HTS = 1896 (datasheet VGA)
    {0x380E, OV5640_TIMING_VTS_H},  // VTS[15:8]
    {0x380F, OV5640_TIMING_VTS_L},  // VTS[7:0]
    {0x3813, 0x06}, //Timing Voffset (datasheet VGA)
    {0x3618, 0x00},
    {0x3612, 0x29},
    {0x3709, 0x52},
    {0x370C, 0x03},
    {0x3A02, OV5640_AE_MAX_60HZ_H},//60Hz max exposure
    {0x3A03, OV5640_AE_MAX_60HZ_L},//60Hz max exposure
    {0x3A14, OV5640_AE_MAX_50HZ_H},//50Hz max exposure
    {0x3A15, OV5640_AE_MAX_50HZ_L},//50Hz max exposure
    {0x4004, 0x02},
    {0x3002, 0x1C},
    {0x3006, 0xC3},
    {0x4713, 0x03},
    {0x4407, 0x04},
    {0x460B, 0x37},
    {0x460C, 0x20},
    {0x4837, 0x16},
    {0x3824, 0x04},
    {0x5001, 0x83},
    {0x3503, 0x00}
};

static const uint16_t ov5640_colorbar[][2] = { {0x503D, 0x80}, {0x4741, 0x00} };
static const uint16_t ov5640_colorsq[][2] = { {0x503D, 0x82}, {0x4741, 0x00} };

static int wr(i2c_soft_t *i2c, uint8_t saddr, uint16_t reg, uint8_t val)
{
    return i2c_soft_mem_write(i2c, saddr, reg, true, &val, 1);
}

static void delay_ms_busy(uint32_t ms)
{
    /* 简单忙等待，按 24MHz 近似，避免引入定时器依赖 */
    volatile uint32_t n = ms * 24000u;
    while (n--) __asm volatile("nop");
}

int ov5640_init(i2c_soft_t *i2c, uint8_t saddr, ov5640_format_t fmt)
{
    /* 复位/上电序列应由上层 GPIO 控制完成，这里仅进行寄存器表配置 */
    for (size_t i = 0; i < sizeof(ov5640_init_cfg) / sizeof(ov5640_init_cfg[0]); ++i)
    {
        uint16_t reg = ov5640_init_cfg[i][0];
        uint8_t  val = (uint8_t)ov5640_init_cfg[i][1];
        if (wr(i2c, saddr, reg, val))
            return -1;
        /* 写完 0x3008=0x82 软复位后等待至少 5ms */
        if (reg == 0x3008u && val == 0x82u)
            delay_ms_busy(5);
    }
    /* 额外稳定时间 */
    delay_ms_busy(10);
    /* 格式选择：先按 YUV422 表设置，再覆盖 0x4300/0x501F 等关键位 */
    for (size_t i = 0; i < sizeof(ov5640_yuv422_cfg) / sizeof(ov5640_yuv422_cfg[0]); ++i)
    {
        uint16_t reg = ov5640_yuv422_cfg[i][0];
        uint8_t  val = (uint8_t)ov5640_yuv422_cfg[i][1];

        if (reg == 0x4300) {
            val = (uint8_t)fmt;
        } else if (reg == 0x501F) {
            if (fmt == OV5640_FMT_RGB565_R5G3_G3B5) {
                val = 0x01; // ISP RGB
            } else {
                val = 0x00; // ISP YUV422
            }
        }

        if (wr(i2c, saddr, reg, val)) return -1;
    }
    return 0;
}

int ov5640_set_color_bar(i2c_soft_t *i2c, uint8_t saddr, bool en)
{
    (void)en; /* 当前表为开启色条 */
    for (size_t i = 0; i < sizeof(ov5640_colorbar) / sizeof(ov5640_colorbar[0]); ++i)
        if (wr(i2c, saddr, ov5640_colorbar[i][0], (uint8_t)ov5640_colorbar[i][1])) return -1;
    return 0;
}

int ov5640_set_color_square(i2c_soft_t *i2c, uint8_t saddr, bool en)
{
    (void)en;
    for (size_t i = 0; i < sizeof(ov5640_colorsq) / sizeof(ov5640_colorsq[0]); ++i)
        if (wr(i2c, saddr, ov5640_colorsq[i][0], (uint8_t)ov5640_colorsq[i][1])) return -1;
    return 0;
}

int ov5640_set_light(i2c_soft_t *i2c, uint8_t saddr, bool en)
{
    /* 旧驱动逻辑：en 时依次写 0x3016/0x301C/0x3019 = 0x02；关灯时写 0x3019 = 0x00 */
    if (en)
    {
        if (wr(i2c, saddr, 0x3016, 0x02)) return -1;
        if (wr(i2c, saddr, 0x301C, 0x02)) return -1;
        if (wr(i2c, saddr, 0x3019, 0x02)) return -1;
    }
    else
    {
        if (wr(i2c, saddr, 0x3016, 0x00)) return -1; /* 保守：也清零 3016/301C */
        if (wr(i2c, saddr, 0x301C, 0x00)) return -1;
        if (wr(i2c, saddr, 0x3019, 0x00)) return -1;
    }
    return 0;
}

// OV5640 Cam Reset & Power Down Pin Config (Board Level)
#ifndef BOARD_CAM_PORT
#define BOARD_CAM_PORT GPIOA
#endif
#ifndef BOARD_CAM_RST_PIN
#define BOARD_CAM_RST_PIN 15u
#endif
#ifndef BOARD_CAM_PWDN_PIN
#define BOARD_CAM_PWDN_PIN 6u
#endif

/* 检查 PWDN 引脚是否有效 (0xFF 表示未使用) */
#define OV5640_PWDN_VALID() (BOARD_CAM_PWDN_PIN != 0xFF && BOARD_CAM_PWDN_PIN < 32)

void ov5640_hard_init(void)
{
    /* 
     * 配置 RST/PWDN 引脚为输出并执行上电时序
     */

    /* 配置 RST 引脚为输出 */
    gpio_set_function(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, FUNCTION_2);
    gpio_set_direction(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 1);
    
    /* 配置 PWDN 引脚为输出 (如果有效) */
    if (OV5640_PWDN_VALID()) {
        gpio_set_function(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, FUNCTION_2);
        gpio_set_direction(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 1);
    }

    // Power On Sequence
    gpio_set_data(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 0); // RST Low
    if (OV5640_PWDN_VALID()) {
        gpio_set_data(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 1); // PWDN High
    }
    for (volatile uint32_t i = 0; i < 800000u; i++) __asm volatile("nop");
    
    if (OV5640_PWDN_VALID()) {
        gpio_set_data(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 0); // PWDN Low
    }
    for (volatile uint32_t i = 0; i < 800000u; i++) __asm volatile("nop");
    
    gpio_set_data(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 1); // RST High
    for (volatile uint32_t i = 0; i < 2400000u; i++) __asm volatile("nop");
}
