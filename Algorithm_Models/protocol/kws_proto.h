/**
 * @file kws_proto.h
 * @brief KWS (语音关键词识别) 算法的邮箱消息 payload 定义
 * 
 * 本文件定义了 M4 和 DSP 之间传递 KWS 识别结果的具体数据结构。
 * 该文件应同时被 M4 侧和 DSP 侧代码引用，确保双方数据结构一致。
 * 
 * 适用算法：
 *   - Keyword_Spotting (语音关键词识别)
 * 
 * 协议流程（轮询模式）：
 *   1. M4 将音频数据写入共享内存 (dsp_in_block)
 *   2. M4 设置 input_ready_flag = 1
 *   3. DSP 检测到 input_ready_flag，处理音频数据
 *   4. DSP 将 KWS 结果写入共享内存 (kws_result)
 *   5. DSP 设置 output_ready_flag = 1
 *   6. M4 读取 KWS 结果
 * 
 * 协议流程（Mailbox 模式，可选）：
 *   1. DSP 完成一帧 KWS 后，通过 Mailbox 发送 MAILBOX_MSG_TYPE_KWS | offset
 *   2. M4 读取 Mailbox，提取 offset，从共享内存解析 KWSResult
 * 
 * 版本: 1.0
 * 
 * @see mailbox_proto.h 通用邮箱消息格式
 */
#ifndef KWS_PROTO_H
#define KWS_PROTO_H

#include <stdint.h>
#include "mailbox_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 协议版本与配置
 *===========================================================================*/

/** 协议版本号 */
#define KWS_PROTOCOL_VERSION    0x0100u  /* v1.0 */

/** 最大支持的关键词数量 */
#define KWS_MAX_KEYWORDS        16

/** 魔数，用于校验数据有效性 */
#define KWS_RESULT_MAGIC        0x4B575352u  /* "KWSR" in ASCII */

/*===========================================================================
 * 共享内存布局定义（默认地址，可覆盖）
 *===========================================================================*/

/** 音频输入块地址 (M4 -> DSP) */
#ifndef KWS_DSP_IN_BLOCK_ADDR
#define KWS_DSP_IN_BLOCK_ADDR       0x44040000u
#endif

/** 音频输出块地址 (DSP -> M4) */
#ifndef KWS_DSP_OUT_BLOCK_ADDR
#define KWS_DSP_OUT_BLOCK_ADDR      0x44040200u
#endif

/** KWS 结果地址 */
#ifndef KWS_RESULT_ADDR
#define KWS_RESULT_ADDR             0x44040400u
#endif

/** DSP 就绪标志地址 */
#ifndef KWS_DSP_READY_FLAG_ADDR
#define KWS_DSP_READY_FLAG_ADDR     0x44040430u
#endif

/** 输入就绪标志地址 */
#ifndef KWS_INPUT_READY_FLAG_ADDR
#define KWS_INPUT_READY_FLAG_ADDR   0x44040434u
#endif

/** 输出就绪标志地址 */
#ifndef KWS_OUTPUT_READY_FLAG_ADDR
#define KWS_OUTPUT_READY_FLAG_ADDR  0x44040438u
#endif

/*===========================================================================
 * 关键词定义
 *===========================================================================*/

/** 关键词索引枚举 */
typedef enum {
    KWS_KEYWORD_UNKNOWN           = 0,   /**< 未识别 */
    KWS_KEYWORD_TAKE_PHOTO        = 1,   /**< 拍张照片 */
    KWS_KEYWORD_START_RECORDING   = 2,   /**< 开始录像 */
    KWS_KEYWORD_STOP_RECORDING    = 3,   /**< 停止录像 */
    KWS_KEYWORD_START_TRACKING    = 4,   /**< 启动跟随 */
    KWS_KEYWORD_STOP_TRACKING     = 5,   /**< 结束跟随 */
    KWS_KEYWORD_LIGHT_ON          = 6,   /**< 打开补光灯 */
    KWS_KEYWORD_LIGHT_OFF         = 7,   /**< 关闭补光灯 */
    KWS_KEYWORD_MAX
} KWSKeyword_t;

/*===========================================================================
 * KWS 识别结果结构
 *===========================================================================*/

/**
 * @brief 单帧 KWS 识别结果
 * 
 * 该结构支持两种使用模式：
 * 1. 简单模式：只使用 keyword_idx 和 confidence
 * 2. 概率模式：读取所有关键词的概率 scores[]
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;               /**< 魔数 (KWS_RESULT_MAGIC) */
    uint32_t version;             /**< 协议版本 (KWS_PROTOCOL_VERSION) */
    uint32_t frame_id;            /**< 帧序号（DSP 递增）*/
    uint32_t timestamp;           /**< 时间戳（毫秒，可选）*/
    uint16_t keyword_idx;         /**< 识别到的关键词索引 (KWSKeyword_t) */
    uint16_t confidence;          /**< 置信度 [0-100] */
    uint8_t  scores[KWS_MAX_KEYWORDS];  /**< 各关键词的概率 [0-255] */
    uint8_t  reserved[12];        /**< 预留字段 */
} KWSResult_t;

/*===========================================================================
 * 同步标志结构（轮询模式使用）
 *===========================================================================*/

/**
 * @brief KWS 同步标志
 * 
 * 用于 M4 与 DSP 之间的握手同步
 */
typedef struct __attribute__((packed)) {
    volatile uint32_t dsp_ready;      /**< DSP 就绪标志 (1=ready) */
    volatile uint32_t input_ready;    /**< 输入数据就绪 (1=ready) */
    volatile uint32_t output_ready;   /**< 输出数据就绪 (1=ready) */
    volatile uint32_t dsp_cycles;     /**< DSP 处理周期数（性能统计）*/
} KWSSyncFlags_t;

/*===========================================================================
 * 验证宏
 *===========================================================================*/

/** 检查 KWSResult 是否有效 */
#define KWS_RESULT_IS_VALID(ptr) \
    ((ptr) != NULL && (ptr)->magic == KWS_RESULT_MAGIC)

/** 检查关键词是否为有效命令（非 UNKNOWN）*/
#define KWS_IS_VALID_KEYWORD(idx) \
    ((idx) > KWS_KEYWORD_UNKNOWN && (idx) < KWS_KEYWORD_MAX)

/*===========================================================================
 * 便捷访问宏（用于旧代码兼容）
 *===========================================================================*/

/** 获取 KWS 结果指针 */
#define KWS_GET_RESULT_PTR() \
    ((volatile KWSResult_t*)KWS_RESULT_ADDR)

/** 获取同步标志指针 */
#define KWS_GET_SYNC_FLAGS_PTR() \
    ((volatile KWSSyncFlags_t*)KWS_DSP_READY_FLAG_ADDR)

#ifdef __cplusplus
}
#endif

#endif /* KWS_PROTO_H */
