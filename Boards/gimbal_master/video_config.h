/**
 * @file video_config.h
 * @brief 视频/图像处理相关配置
 * 
 * 此文件包含视频子系统的默认配置参数。
 * 这些是业务层配置，从 board.h 中分离出来，
 * 应用可按需覆盖或使用自己的配置。
 * 
 * NE005 智能云台主板视频配置
 */
#ifndef VIDEO_CONFIG_H
#define VIDEO_CONFIG_H

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Video / Image Processing Configuration
 *===========================================================================*/

#ifndef RD_SOURCE_FRAME_START_X
#define RD_SOURCE_FRAME_START_X (0)
#endif
#ifndef RD_SOURCE_FRAME_START_Y
#define RD_SOURCE_FRAME_START_Y (0)
#endif

#ifndef BINNING_SIZE
#define BINNING_SIZE            (2) // binning = 2 @ Sensor image size
#endif

/* binning - 后级处理后的图像尺寸 */
#ifndef BINNING_IMAGE_WIDTH
#define BINNING_IMAGE_WIDTH         (320)   /* 1280/2 = 640 */
#endif
#ifndef BINNING_IMAGE_HEIGHT
#define BINNING_IMAGE_HEIGHT        (240)   /* 960/2 = 480 */
#endif

/* sensor */
#ifndef SENSOR_IMAGE_WIDTH
#define SENSOR_IMAGE_WIDTH          (BINNING_IMAGE_WIDTH * (1U << BINNING_SIZE))
#endif
#ifndef SENSOR_IMAGE_HEIGHT
#define SENSOR_IMAGE_HEIGHT         (BINNING_IMAGE_HEIGHT * (1U << BINNING_SIZE))
#endif

/* downscale - 用于 AI 推理的图像尺寸 (匹配屏幕 160x128) */
#ifndef DOWNSCALE_IMAGE_WIDTH
#define DOWNSCALE_IMAGE_WIDTH       (160)
#endif
#ifndef DOWNSCALE_IMAGE_HEIGHT
#define DOWNSCALE_IMAGE_HEIGHT      (128)
#endif

/* display offset */
#ifndef DISP_START_X
#define DISP_START_X                (0)
#endif
#ifndef DISP_START_Y
#define DISP_START_Y                (0)
#endif

/* Map DISP_IMAGE_* to generic BOARD_DISPLAY_* */
#define DISP_IMAGE_WIDTH            BOARD_DISPLAY_WIDTH
#define DISP_IMAGE_HEIGHT           BOARD_DISPLAY_HEIGHT

/* snap - 用于人脸识别的抓拍尺寸 */
#ifndef SNAP_IMAGE_WIDTH
#define SNAP_IMAGE_WIDTH            (160)
#endif
#ifndef SNAP_IMAGE_HEIGHT
#define SNAP_IMAGE_HEIGHT           (128)
#endif

#ifndef BINNING_LINE_MAX_SIZE
#define BINNING_LINE_MAX_SIZE       (1280)
#endif
#ifndef DOWNSCALE_FACTOR
#define DOWNSCALE_FACTOR            (8192)
#endif

#ifndef OFFLINE_IMAGE_BASE_ADDRESS
#define OFFLINE_IMAGE_BASE_ADDRESS  (0x44000000)
#endif

/*===========================================================================
 * SRAM0/SRAM1 Memory Layout for Video
 *===========================================================================*/

/* SRAM0 */
#ifndef DISP_RALPHA0_ADDR
#define DISP_RALPHA0_ADDR           (0x44080000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 1))
#endif
#ifndef DISP_RALPHA1_ADDR
#define DISP_RALPHA1_ADDR           (0x44080000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 2))
#endif

/* SRAM1 */
#ifndef DISP_RFRAME0_ADDR
#define DISP_RFRAME0_ADDR           (0x44080000 - (SNAP_IMAGE_WIDTH * SNAP_IMAGE_HEIGHT * 4))
#endif
#ifndef DISP_RFRAME1_ADDR
#define DISP_RFRAME1_ADDR           (0x44080000 - (SNAP_IMAGE_WIDTH * SNAP_IMAGE_HEIGHT * 6))
#endif
#ifndef DISP_WFRAME0_ADDR
#define DISP_WFRAME0_ADDR           (0x44080000 - (SNAP_IMAGE_WIDTH * SNAP_IMAGE_HEIGHT * 8))
#endif
#ifndef DISP_WFRAME1_ADDR
#define DISP_WFRAME1_ADDR           (0x44080000 - (SNAP_IMAGE_WIDTH * SNAP_IMAGE_HEIGHT * 10))
#endif

/*===========================================================================
 * AI Model Input Configuration (用于云台跟踪)
 *===========================================================================*/

/** @brief 人脸检测模型输入尺寸 */
#ifndef FACE_DET_INPUT_WIDTH
#define FACE_DET_INPUT_WIDTH        (128)
#endif
#ifndef FACE_DET_INPUT_HEIGHT
#define FACE_DET_INPUT_HEIGHT       (160)
#endif

/** @brief 人形检测模型输入尺寸 */
#ifndef HUMAN_DET_INPUT_WIDTH
#define HUMAN_DET_INPUT_WIDTH       (128)
#endif
#ifndef HUMAN_DET_INPUT_HEIGHT
#define HUMAN_DET_INPUT_HEIGHT      (160)
#endif

/** @brief 手势检测模型输入尺寸 */
#ifndef GESTURE_DET_INPUT_WIDTH
#define GESTURE_DET_INPUT_WIDTH     (128)
#endif
#ifndef GESTURE_DET_INPUT_HEIGHT
#define GESTURE_DET_INPUT_HEIGHT    (128)
#endif

/** @brief 人脸识别模型输入尺寸 */
#ifndef FACE_REC_INPUT_WIDTH
#define FACE_REC_INPUT_WIDTH        (112)
#endif
#ifndef FACE_REC_INPUT_HEIGHT
#define FACE_REC_INPUT_HEIGHT       (112)
#endif

/*===========================================================================
 * LCD / Camera Orientation Configuration (云台跟踪适配)
 *===========================================================================*/

/**
 * @brief LCD 扫描方向 (ST7735S MADCTL 寄存器)
 * 
 * 值说明:
 *   - 0x00 = 左上角起点，向右向下扫描 (正常)
 *   - 0xC0 = 180° 旋转 (MADCTL_MY | MADCTL_MX)
 *   - 0xE0 = 行列交换+镜像 (MADCTL_MV | MADCTL_MY | MADCTL_MX)
 * 
 * 云台主板使用 0xE0 以适配跟踪坐标映射
 */
#ifndef BOARD_LCD_SCAN_DIR
#define BOARD_LCD_SCAN_DIR          (0xE0)
#endif

/**
 * @brief OV5640 镜像/翻转配置 (寄存器 0x3821)
 * 
 * 值说明:
 *   - 0x00 = 无镜像
 *   - 0x02 = Sensor mirror
 *   - 0x04 = ISP mirror
 *   - 0x06 = ISP mirror + Sensor mirror
 * 
 * 云台主板使用 0x06 以配合 LCD 扫描方向
 */
#ifndef BOARD_OV5640_MIRROR_CFG
#define BOARD_OV5640_MIRROR_CFG     (0x06)
#endif

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_CONFIG_H */
