/**
 * @file    video_config.h
 * @brief   Video Processing Configuration for NE005 Card (Sub-Board)
 * @details 子板视频处理和 AI 模型输入配置
 *          根据子板功能分配使用不同的 AI 模型：
 *          - Card1: 人脸检测 (Face Detection)
 *          - Card2: 手势检测 (Gesture/Hand Detection)
 *          - Card3: 人形检测 (Human Detection)
 */

#ifndef S300_BSP_VIDEO_CONFIG_H
#define S300_BSP_VIDEO_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Sensor Configuration
 * 传感器配置 (OV5640)
 *===========================================================================*/

/** @brief 传感器原始分辨率 */
#define VIDEO_SENSOR_WIDTH          640
#define VIDEO_SENSOR_HEIGHT         480

/** @brief 传感器帧率 */
#define VIDEO_SENSOR_FPS            30

/** @brief 传感器像素格式 */
#define VIDEO_SENSOR_FORMAT         VIDEO_FORMAT_YUV422

/*===========================================================================
 * Binning Configuration
 * 分档配置 (传感器降采样)
 *===========================================================================*/

/** @brief 分档后分辨率 */
#define VIDEO_BINNING_WIDTH         320
#define VIDEO_BINNING_HEIGHT        240

/*===========================================================================
 * AI Model Input Configuration - Face Detection
 * 人脸检测模型输入配置 (Card1)
 *===========================================================================*/

/** @brief 人脸检测输入分辨率 */
#define AI_FACE_DET_INPUT_WIDTH     160
#define AI_FACE_DET_INPUT_HEIGHT    160

/** @brief 人脸检测模型路径 */
#define AI_FACE_DET_MODEL_PATH      "Face_Detection"

/*===========================================================================
 * AI Model Input Configuration - Gesture Detection
 * 手势检测模型输入配置 (Card2)
 *===========================================================================*/

/** @brief 手势检测输入分辨率 */
#define AI_GESTURE_INPUT_WIDTH      128
#define AI_GESTURE_INPUT_HEIGHT     128

/** @brief 手势检测模型路径 */
#define AI_GESTURE_MODEL_PATH       "Hand_Gesture"

/*===========================================================================
 * AI Model Input Configuration - Human Detection
 * 人形检测模型输入配置 (Card3)
 *===========================================================================*/

/** @brief 人形检测输入分辨率 */
#define AI_HUMAN_DET_INPUT_WIDTH    160
#define AI_HUMAN_DET_INPUT_HEIGHT   160

/** @brief 人形检测模型路径 */
#define AI_HUMAN_DET_MODEL_PATH     "Human_Detection"

/*===========================================================================
 * Frame Buffer Configuration
 * 帧缓冲配置
 *===========================================================================*/

/** @brief 帧缓冲数量 (双缓冲) */
#define VIDEO_FRAME_BUFFER_COUNT    2

/** @brief YUV422 每像素字节数 */
#define VIDEO_BYTES_PER_PIXEL_YUV422    2

/** @brief RGB888 每像素字节数 */
#define VIDEO_BYTES_PER_PIXEL_RGB888    3

/** @brief 帧缓冲大小计算 */
#define VIDEO_FRAME_BUFFER_SIZE     (VIDEO_SENSOR_WIDTH * VIDEO_SENSOR_HEIGHT * VIDEO_BYTES_PER_PIXEL_YUV422)

/*===========================================================================
 * AI Processing Configuration
 * AI 处理配置
 *===========================================================================*/

/** @brief 检测置信度阈值 */
#define AI_DETECTION_THRESHOLD      0.5f

/** @brief NMS (非极大值抑制) 阈值 */
#define AI_NMS_THRESHOLD            0.3f

/** @brief 最大检测目标数 */
#define AI_MAX_DETECTIONS           10

/*===========================================================================
 * Video Format Definitions
 * 视频格式定义
 *===========================================================================*/

typedef enum {
    VIDEO_FORMAT_YUV422 = 0,
    VIDEO_FORMAT_YUV420,
    VIDEO_FORMAT_RGB565,
    VIDEO_FORMAT_RGB888,
    VIDEO_FORMAT_GRAY8,
} video_format_t;

/*===========================================================================
 * Video Frame Structure
 * 视频帧结构
 *===========================================================================*/

typedef struct {
    uint8_t *data;          /**< 帧数据指针 */
    uint32_t width;         /**< 帧宽度 */
    uint32_t height;        /**< 帧高度 */
    video_format_t format;  /**< 像素格式 */
    uint32_t stride;        /**< 行跨度 (字节) */
    uint32_t timestamp;     /**< 时间戳 (ms) */
    uint32_t frame_id;      /**< 帧序号 */
} video_frame_t;

/*===========================================================================
 * AI Detection Result Structure
 * AI 检测结果结构
 *===========================================================================*/

typedef struct {
    float x;            /**< 边界框左上角 X (归一化 0-1) */
    float y;            /**< 边界框左上角 Y (归一化 0-1) */
    float w;            /**< 边界框宽度 (归一化 0-1) */
    float h;            /**< 边界框高度 (归一化 0-1) */
    float confidence;   /**< 置信度 (0-1) */
    int class_id;       /**< 类别 ID */
} ai_detection_t;

typedef struct {
    int count;                              /**< 检测到的目标数量 */
    ai_detection_t detections[AI_MAX_DETECTIONS]; /**< 检测结果数组 */
    uint32_t process_time_ms;               /**< 处理耗时 (ms) */
} ai_detection_result_t;

/*===========================================================================
 * Camera Orientation Configuration
 * 摄像头方向配置 (与主板保持一致)
 *===========================================================================*/

/**
 * @brief OV5640 镜像/翻转配置 (寄存器 0x3821)
 * 
 * 值说明:
 *   - 0x00 = 无镜像
 *   - 0x02 = Sensor mirror
 *   - 0x04 = ISP mirror
 *   - 0x06 = ISP mirror + Sensor mirror
 * 
 * 子板使用 0x06 与主板保持坐标系统一致
 */
#ifndef BOARD_OV5640_MIRROR_CFG
#define BOARD_OV5640_MIRROR_CFG     (0x06)
#endif

/**
 * @brief LCD 扫描方向 (可选调试显示)
 * 子板主要用于 AI 检测，LCD 仅用于调试
 */
#ifndef BOARD_LCD_SCAN_DIR
#define BOARD_LCD_SCAN_DIR          (0xE0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_VIDEO_CONFIG_H */
