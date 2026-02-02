/**
 * @file video_config.h
 * @brief 视频/图像处理相关配置 (Application Board)
 * 
 * 此文件包含视频子系统的默认配置参数。
 * 这些是业务层配置，从 board.h 中分离出来，
 * 应用可按需覆盖或使用自己的配置。
 * 
 * 针对 240x320 显示屏优化
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
#define BINNING_SIZE            (1) /* binning = 2 @ Sensor image size */
#endif

/* binning (针对 240x320 优化) */
#ifndef BINNING_IMAGE_WIDTH
#define BINNING_IMAGE_WIDTH         (288)
#endif
#ifndef BINNING_IMAGE_HEIGHT
#define BINNING_IMAGE_HEIGHT        (360)
#endif

/* sensor */
#ifndef SENSOR_IMAGE_WIDTH
#define SENSOR_IMAGE_WIDTH          (BINNING_IMAGE_WIDTH * (1U << BINNING_SIZE))
#endif
#ifndef SENSOR_IMAGE_HEIGHT
#define SENSOR_IMAGE_HEIGHT         (BINNING_IMAGE_HEIGHT * (1U << BINNING_SIZE))
#endif

/* downscale (针对 240x320 显示) */
#ifndef DOWNSCALE_IMAGE_WIDTH
#define DOWNSCALE_IMAGE_WIDTH       (240)
#endif
#ifndef DOWNSCALE_IMAGE_HEIGHT
#define DOWNSCALE_IMAGE_HEIGHT      (320)
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

/* snap */
#ifndef SNAP_IMAGE_WIDTH
#define SNAP_IMAGE_WIDTH            (240)
#endif
#ifndef SNAP_IMAGE_HEIGHT
#define SNAP_IMAGE_HEIGHT           (320)
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

/* PSRAM */
#ifndef DISP_RALPHA0_ADDR
#define DISP_RALPHA0_ADDR           (0x80700000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 1))
#endif
#ifndef DISP_RALPHA1_ADDR
#define DISP_RALPHA1_ADDR           (0x80700000 - (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 2))
#endif
#ifndef DISP_RFRAME0_ADDR
#define DISP_RFRAME0_ADDR           (0x80700000 - (SNAP_IMAGE_WIDTH * SNAP_IMAGE_HEIGHT * 4))
#endif
#ifndef DISP_RFRAME1_ADDR
#define DISP_RFRAME1_ADDR           (0x80700000 - (SNAP_IMAGE_WIDTH * SNAP_IMAGE_HEIGHT * 6))
#endif
#ifndef DISP_WFRAME0_ADDR
#define DISP_WFRAME0_ADDR           (0x80700000 - (SNAP_IMAGE_WIDTH * SNAP_IMAGE_HEIGHT * 8))
#endif
#ifndef DISP_WFRAME1_ADDR
#define DISP_WFRAME1_ADDR           (0x80700000 - (SNAP_IMAGE_WIDTH * SNAP_IMAGE_HEIGHT * 10))
#endif

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_CONFIG_H */
