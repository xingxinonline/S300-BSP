/**
 * @file    main.c
 * @brief   S300 智能云台目标追踪 Demo
 * 
 * @details 本 Demo 集成：
 *          1. OV5640 摄像头采集
 *          2. DSP 人脸/人形检测
 *          3. 云台伺服跟踪控制
 * 
 *          硬件需求：
 *          - gimbal_master 板 (带 LCD 显示)
 *          - OV5640 摄像头模组
 *          - Hiwonder 总线舵机 (ID=6 Yaw, ID=4 Pitch)
 * 
 *          工作流程：
 *          1. M4 初始化摄像头、视频子系统、邮箱
 *          2. DSP 运行检测算法，通过邮箱发送检测结果
 *          3. M4 读取邮箱，提取目标位置
 *          4. PID 控制器计算云台运动增量
 *          5. 总线舵机驱动云台跟踪目标
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Platform */
#include "s300.h"

/* Board and subsystems */
#include "rcc.h"
#include "board.h"
#include "video.h"
#include "psram.h"
#include "mailbox.h"
#include "mailbox_proto.h"
#include "detection_proto.h"

/* Camera */
#include "camera_ov5640.h"

/* Tracking modules */
#include "gimbal_ctrl.h"
#include "target_tracker.h"

/*===========================================================================
 * 配置
 *===========================================================================*/

/** @brief 摄像头格式配置 */
#if BOARD_CAMERA_FORMAT == 0
  #define APP_CAM_FMT CAMREA_RGB565
#else
  #define APP_CAM_FMT CAMREA_YUV422
#endif

/*===========================================================================
 * 全局变量
 *===========================================================================*/

/** @brief 1ms 节拍计时 */
static volatile uint32_t g_tick_ms = 0;

/*===========================================================================
 * 中断处理
 *===========================================================================*/

void SysTick_Handler(void)
{
    g_tick_ms++;
}

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

static inline uint32_t millis(void)
{
    return g_tick_ms;
}

/*===========================================================================
 * 视频初始化
 *===========================================================================*/

static void video_subsystem_init(void)
{
    /* 摄像头上电与探测 */
    int cam_ret = camera_ov5640_preinit();
    if (cam_ret != 0) {
        printf("[Tracking] WARN: OV5640 init failed (%d), continue without camera.\r\n", cam_ret);
    }

    /* 视频子系统（包含面板初始化）*/
    printf("[Tracking] Init video subsystem...\r\n");
    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);

    /* 初始化显存与 Alpha 通道 */
    {
        volatile uint16_t *fb = (volatile uint16_t *)DISP_RFRAME0_ADDR;
        volatile uint16_t *alpha16 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
        uint32_t pixels = DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT;
        uint32_t alpha_words = pixels / 2;
        
        /* 填充背景色（绿色 0x07E0）*/
        for (uint32_t i = 0; i < pixels; i++) {
            fb[i] = 0x07E0;
        }
        
        /* Alpha 全透明 */
        for (uint32_t i = 0; i < alpha_words; i++) {
            alpha16[i] = 0x0000;
        }

        /* 触发 DSP 显示 Frame 0 */
        *(volatile uint32_t *)(DSP_VIDEO_SS_BASE + 0x50) = 1u;
        printf("[Tracking] Framebuffer initialized.\r\n");
    }

    /* M4 <-> DSP 邮箱通信 */
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    set_dsp_warm_reset(true);
    write_mailbox(MAILBOX_BASE, 0x5A5A5A5A);
    printf("[Tracking] DSP mailbox initialized.\r\n");
}

/*===========================================================================
 * 检测结果处理
 *===========================================================================*/

/** @brief 上次有效检测的时间戳 */
static uint32_t s_last_detect_time = 0;

/** @brief 上次绘制的矩形框 */
static int32_t s_last_x1, s_last_y1, s_last_x2, s_last_y2;
static bool s_has_last_box = false;

/** @brief 检测超时 (ms) */
#define DETECT_TIMEOUT_MS   200

/**
 * @brief 设置像素 Alpha 值 (PSRAM 16-bit 对齐访问)
 */
static void set_pixel_alpha(uint32_t x, uint32_t y, uint8_t alpha)
{
    volatile uint16_t *ab16 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    uint32_t pixel_idx = y * DISP_IMAGE_WIDTH + x;
    uint32_t word_idx = pixel_idx / 2;
    uint32_t byte_pos = pixel_idx & 1;
    
    uint16_t val = ab16[word_idx];
    if (byte_pos == 0) {
        val = (val & 0xFF00) | alpha;
    } else {
        val = (val & 0x00FF) | ((uint16_t)alpha << 8);
    }
    ab16[word_idx] = val;
}

/**
 * @brief 绘制矩形边框
 */
static void draw_rect_border(int x1, int y1, int x2, int y2, uint8_t alpha)
{
    /* 裁剪 */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= DISP_IMAGE_WIDTH) x2 = DISP_IMAGE_WIDTH - 1;
    if (y2 >= DISP_IMAGE_HEIGHT) y2 = DISP_IMAGE_HEIGHT - 1;
    if (x1 > x2 || y1 > y2) return;

    /* 上下边 */
    for (int x = x1; x <= x2; x++) {
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, alpha);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, alpha);
    }
    /* 左右边 */
    for (int y = y1; y <= y2; y++) {
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, alpha);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, alpha);
    }
}

/**
 * @brief 处理单个检测框
 */
static void process_detection_box(const DetectionBox_t *box, tracker_target_t *target)
{
    int32_t x1 = box->x1, y1 = box->y1, x2 = box->x2, y2 = box->y2;
    
    /* 坐标排序 */
    if (x2 < x1) { int32_t t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { int32_t t = y1; y1 = y2; y2 = t; }

    /* 有效性检查 */
    bool valid = true;
    if (x1 < 0 || y1 < 0 || x2 > DISP_IMAGE_WIDTH || y2 > DISP_IMAGE_HEIGHT) valid = false;
    if ((x2 - x1) <= 2 || (y2 - y1) <= 2) valid = false;

    if (valid)
    {
        s_last_detect_time = millis();
        
        /* 清除上一帧的框 */
        if (s_has_last_box) {
            draw_rect_border(s_last_x1, s_last_y1, s_last_x2, s_last_y2, 0x00);
        }
        
        /* 绘制新框 */
        draw_rect_border(x1, y1, x2, y2, 0xFF);
        
        /* 保存状态 */
        s_last_x1 = x1;
        s_last_y1 = y1;
        s_last_x2 = x2;
        s_last_y2 = y2;
        s_has_last_box = true;
        
        /* 更新跟踪目标 */
        target->x1 = x1;
        target->y1 = y1;
        target->x2 = x2;
        target->y2 = y2;
        target->score = box->score;
        target->valid = true;
        target->timestamp = s_last_detect_time;
    }
}

/**
 * @brief 处理 DSP 邮箱检测结果
 */
static void process_detection_mailbox(void)
{
    tracker_target_t target;
    target.valid = false;
    
    /* 读取所有邮箱消息 */
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

        switch (msg_type) {
        case MAILBOX_MSG_TYPE_MULTI: {
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionResult_t *result = (const DetectionResult_t*)addr;
            
            if (!DETECTION_RESULT_IS_VALID(result)) break;
            
            if (result->count > 0) {
                int idx = (result->selected_idx >= 0 && 
                          result->selected_idx < (int)result->count) 
                         ? result->selected_idx : 0;
                process_detection_box(&result->boxes[idx], &target);
            } else {
                if (s_has_last_box) {
                    draw_rect_border(s_last_x1, s_last_y1, s_last_x2, s_last_y2, 0x00);
                    s_has_last_box = false;
                }
            }
            break;
        }
        
        case MAILBOX_MSG_TYPE_SINGLE: {
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionBox_t *box = (const DetectionBox_t*)addr;
            process_detection_box(box, &target);
            break;
        }
        
        case MAILBOX_MSG_TYPE_NO_RESULT:
            break;
            
        default:
            break;
        }
    }
    
    /* 如果有有效目标，更新跟踪器 */
    if (target.valid) {
        tracker_update_target(&target);
    }
    
    /* 检查超时，清除旧框 */
    if (s_has_last_box && (millis() - s_last_detect_time > DETECT_TIMEOUT_MS))
    {
        draw_rect_border(s_last_x1, s_last_y1, s_last_x2, s_last_y2, 0x00);
        s_has_last_box = false;
    }
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

int main(void)
{
    /* 板级初始化 */
    board_init();
    printf("\r\n");
    printf("======================================\r\n");
    printf("  S300 Tracking Demo\r\n");
    printf("  Target: Face/Human Tracking Gimbal\r\n");
    printf("======================================\r\n");

    /* 系统时钟更新 */
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        printf("[Tracking] ERR: SysTick_Config failed!\r\n");
    }

    /* MM/DSP PLL 配置 */
    rcc_init_mm_pll(8, 400, 0, 3, 2);  /* MM 100MHz */
    rcc_init_dsp_pll(6, 800, 0, 2, 2); /* DSP 400MHz */

    /* PSRAM 初始化 */
    init_psram(4, 1);

    /* 视频子系统初始化 */
    video_subsystem_init();

    /* 云台初始化 */
    printf("[Tracking] Init gimbal...\r\n");
    gimbal_init(millis);
    
    /* 等待舵机就位 */
    {
        volatile uint32_t wait = millis();
        while (millis() - wait < 1000) { /* wait 1s */ }
    }
    
    /* 云台归中 */
    printf("[Tracking] Center gimbal...\r\n");
    gimbal_center();
    
    /* 等待归中完成 */
    {
        volatile uint32_t wait = millis();
        while (millis() - wait < 600) { /* wait 600ms */ }
    }

    /* 跟踪器初始化 */
    printf("[Tracking] Init tracker (image: %dx%d)...\r\n", 
           DISP_IMAGE_WIDTH, DISP_IMAGE_HEIGHT);
    tracker_init(millis, DISP_IMAGE_WIDTH, DISP_IMAGE_HEIGHT);
    
    /* 设置 PID 参数 (可根据实际情况调整) */
    tracker_set_pid(0.08f, 0.002f, 0.02f);
    tracker_set_deadzone(10);

    printf("[Tracking] System ready. Waiting for detection...\r\n");
    printf("--------------------------------------\r\n");

    /* 主循环 */
    while (1)
    {
        /* 处理检测邮箱 */
        process_detection_mailbox();
        
        /* 执行跟踪控制 */
        tracker_poll();
        
        /* 简单节拍控制 (~5ms 周期) */
        {
            volatile uint32_t t0 = millis();
            while ((millis() - t0) < 5u) { /* idle spin */ }
        }
    }
}
