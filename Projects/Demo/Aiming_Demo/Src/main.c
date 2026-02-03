/**
 * @file    main.c
 * @brief   S300 视觉指向 Demo (摄像头固定模式)
 * 
 * @details 本 Demo 演示摄像头固定、云台指向目标的模式：
 *          - 摄像头安装在固定位置，观察场景
 *          - 云台独立安装，需要指向摄像头看到的目标
 *          - 与追踪模式相反：目标右 → 云台右转
 * 
 *          应用场景：
 *          - 视觉引导系统
 *          - 目标瞄准系统
 *          - 激光指示器
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

/* Control modules */
#include "gimbal_ctrl.h"
#include "aiming_ctrl.h"

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

static volatile uint32_t g_tick_ms = 0;

/*===========================================================================
 * 中断处理
 *===========================================================================*/

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static inline uint32_t millis(void)
{
    return g_tick_ms;
}

/*===========================================================================
 * 视频初始化
 *===========================================================================*/

static void video_subsystem_init(void)
{
    int cam_ret = camera_ov5640_preinit();
    if (cam_ret != 0) {
        printf("[Aiming] WARN: OV5640 init failed (%d)\r\n", cam_ret);
    }

    printf("[Aiming] Init video subsystem...\r\n");
    init_video(EM_DVP, APP_CAM_FMT, C1080X720P);

    /* 初始化显存 */
    {
        volatile uint16_t *fb = (volatile uint16_t *)DISP_RFRAME0_ADDR;
        volatile uint16_t *alpha16 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
        uint32_t pixels = DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT;
        uint32_t alpha_words = pixels / 2;
        
        for (uint32_t i = 0; i < pixels; i++) {
            fb[i] = 0x001F;  /* 蓝色背景 (区分追踪模式) */
        }
        
        for (uint32_t i = 0; i < alpha_words; i++) {
            alpha16[i] = 0x0000;
        }

        *(volatile uint32_t *)(DSP_VIDEO_SS_BASE + 0x50) = 1u;
        printf("[Aiming] Framebuffer initialized.\r\n");
    }

    /* 邮箱初始化 */
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    set_dsp_warm_reset(true);
    write_mailbox(MAILBOX_BASE, 0x5A5A5A5A);
    printf("[Aiming] DSP mailbox initialized.\r\n");
}

/*===========================================================================
 * 检测结果处理
 *===========================================================================*/

static uint32_t s_last_detect_time = 0;
static int32_t s_last_x1, s_last_y1, s_last_x2, s_last_y2;
static bool s_has_last_box = false;

#define DETECT_TIMEOUT_MS   200

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

static void draw_rect_border(int x1, int y1, int x2, int y2, uint8_t alpha)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= DISP_IMAGE_WIDTH) x2 = DISP_IMAGE_WIDTH - 1;
    if (y2 >= DISP_IMAGE_HEIGHT) y2 = DISP_IMAGE_HEIGHT - 1;
    if (x1 > x2 || y1 > y2) return;

    for (int x = x1; x <= x2; x++) {
        set_pixel_alpha((uint32_t)x, (uint32_t)y1, alpha);
        set_pixel_alpha((uint32_t)x, (uint32_t)y2, alpha);
    }
    for (int y = y1; y <= y2; y++) {
        set_pixel_alpha((uint32_t)x1, (uint32_t)y, alpha);
        set_pixel_alpha((uint32_t)x2, (uint32_t)y, alpha);
    }
}

/**
 * @brief 处理单个检测框并更新瞄准控制
 */
static void process_detection_box(const DetectionBox_t *box)
{
    int32_t x1 = box->x1, y1 = box->y1, x2 = box->x2, y2 = box->y2;
    
    if (x2 < x1) { int32_t t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { int32_t t = y1; y1 = y2; y2 = t; }

    bool valid = true;
    if (x1 < 0 || y1 < 0 || x2 > DISP_IMAGE_WIDTH || y2 > DISP_IMAGE_HEIGHT) valid = false;
    if ((x2 - x1) <= 2 || (y2 - y1) <= 2) valid = false;

    if (valid)
    {
        s_last_detect_time = millis();
        
        if (s_has_last_box) {
            draw_rect_border(s_last_x1, s_last_y1, s_last_x2, s_last_y2, 0x00);
        }
        
        draw_rect_border(x1, y1, x2, y2, 0xFF);
        
        s_last_x1 = x1;
        s_last_y1 = y1;
        s_last_x2 = x2;
        s_last_y2 = y2;
        s_has_last_box = true;
        
        aiming_target_t target;
        target.x1 = x1;
        target.y1 = y1;
        target.x2 = x2;
        target.y2 = y2;
        target.score = box->score;
        target.valid = true;
        target.timestamp = s_last_detect_time;
        
        aiming_update_target(&target);
    }
}

static void process_detection_mailbox(void)
{
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

        switch (msg_type) {
        case MAILBOX_MSG_TYPE_MULTI: {
            /* 新协议：多目标检测结果 */
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionResult_t *result = (const DetectionResult_t*)addr;
            
            if (!DETECTION_RESULT_IS_VALID(result)) {
                break;
            }
            
            /* 优先使用 DSP 选中的目标，否则使用第一个 */
            if (result->count > 0) {
                int idx = (result->selected_idx >= 0 && 
                          result->selected_idx < (int)result->count) 
                         ? result->selected_idx : 0;
                process_detection_box(&result->boxes[idx]);
            } else {
                if (s_has_last_box) {
                    draw_rect_border(s_last_x1, s_last_y1, s_last_x2, s_last_y2, 0x00);
                    s_has_last_box = false;
                }
            }
            break;
        }
        
        case MAILBOX_MSG_TYPE_SINGLE: {
            /* 旧协议兼容 */
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionBox_t *box = (const DetectionBox_t*)addr;
            process_detection_box(box);
            break;
        }
        
        case MAILBOX_MSG_TYPE_NO_RESULT:
            /* 本帧无检测结果 */
            break;
            
        default:
            break;
        }
    }
    
    /* 超时清除 */
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
    board_init();
    printf("\r\n");
    printf("======================================\r\n");
    printf("  S300 Aiming Demo\r\n");
    printf("  Mode: Fixed Camera, Gimbal Aiming\r\n");
    printf("======================================\r\n");

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        printf("[Aiming] ERR: SysTick_Config failed!\r\n");
    }

    rcc_init_mm_pll(8, 400, 0, 3, 2);
    rcc_init_dsp_pll(6, 800, 0, 2, 2);

    init_psram(4, 1);

    video_subsystem_init();

    printf("[Aiming] Init gimbal...\r\n");
    gimbal_init(millis);
    
    {
        volatile uint32_t wait = millis();
        while (millis() - wait < 1000) {}
    }
    
    printf("[Aiming] Center gimbal...\r\n");
    gimbal_center();
    
    {
        volatile uint32_t wait = millis();
        while (millis() - wait < 600) {}
    }

    printf("[Aiming] Init aiming controller (image: %dx%d)...\r\n", 
           DISP_IMAGE_WIDTH, DISP_IMAGE_HEIGHT);
    aiming_init(millis, DISP_IMAGE_WIDTH, DISP_IMAGE_HEIGHT);
    
    /* PID 参数 (指向模式需要更快响应) */
    aiming_set_pid(0.15f, 0.005f, 0.03f);
    aiming_set_deadzone(8);
    
    /* 设置视场角 (可根据实际摄像头调整) */
    aiming_set_fov(60.0f, 45.0f);

    printf("[Aiming] System ready. Waiting for detection...\r\n");
    printf("--------------------------------------\r\n");

    while (1)
    {
        process_detection_mailbox();
        aiming_poll();
        
        {
            volatile uint32_t t0 = millis();
            while ((millis() - t0) < 5u) {}
        }
    }
}
