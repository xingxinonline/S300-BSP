/*
 * 文件：display_demo_app.c
 * 说明：本 Demo 的“应用编排层”，将多子模块按顺序初始化，并提供统一的 tick 调度入口。
 * 初始化顺序：
 *   1) 摄像头预上电/探测（失败不致命，继续显示链路验证）；
 *   2) 视频子系统（面板/时序/显存绑定）；
 *   3) M4<->DSP 邮箱握手（复位DSP并发送启动令牌，保持与算法侧一致）；
 *   4) Face Tracker 初始化（依赖毫秒节拍）。
 * 运行期：
 *   - display_demo_app_tick() 负责人脸跟踪轮询。
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "rcc.h"
#include "video.h"
#include "mailbox.h"
#include "camera_ov5640.h"
#include "face_tracker.h"
#include "display_demo_app.h"
#include "board.h"

#if BOARD_CAMERA_FORMAT == 0
  #define APP_CAM_PRO CAMREA_RGB565
#else
  #define APP_CAM_PRO CAMREA_YUV422
#endif

void display_demo_app_init(uint32_t (*get_millis)(void))
{
    /* 摄像头上电与探测（失败则仅初始化显示链路） */
    int cam_ret = camera_ov5640_preinit();
    if (cam_ret != 0) {
        printf("[S300][DisplayDemo][WARN] OV5640 init failed (%d), continue to init video for display path only.\r\n", cam_ret);
    }

    /* 视频子系统（包含面板初始化） */
    printf("[S300][DisplayDemo] init video...\r\n");
    init_video(EM_DVP, APP_CAM_PRO, C1080X720P);

    /* 初始化显存与 Alpha 通道，并激活 Frame 0 */
    {
        volatile uint16_t *fb = (volatile uint16_t *)DISP_RFRAME0_ADDR;
        /* PSRAM 不支持 8-bit 访问，必须使用 16-bit 对齐访问 */
        volatile uint16_t *alpha16 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
        uint32_t pixels = DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT;
        uint32_t alpha_words = pixels / 2;
        
        /* 填充背景色（绿色 0x07E0）*/
        for (uint32_t i = 0; i < pixels; i++) {
            fb[i] = 0x07E0;
        }
        
        /* Alpha（0x00 全透明）- 使用 16-bit 写入 */
        /* 这样只有 Alpha 被设置为非 0 的区域（如人脸框）才会显示出绿色 */
        for (uint32_t i = 0; i < alpha_words; i++) {
            alpha16[i] = 0x0000;  /* 2 pixels of alpha=0x00 */
        }

        /* 触发 DSP 显示 Frame 0 */
        *(volatile uint32_t *)(DSP_VIDEO_SS_BASE + 0x50) = 1u;
        printf("[S300][MM_Test_Demo] Framebuffer 0 activated (Green/Transparent).\r\n");
    }

    /* M4 <-> DSP 邮箱通信与握手 */
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    set_dsp_warm_reset(true);
    write_mailbox(MAILBOX_BASE, 0x5A5A5A5A);

    /* 人脸追踪初始化（依赖 mailbox；提供时间回调实现） */
    face_tracker_init(get_millis);

    /* 启动追踪（发送 CMD_START 给 DSP）*/
    face_tracker_start();

    printf("[S300][MM_Test_Demo] Started.\r\n");
}

void display_demo_app_tick(void)
{
    /* uart_cmd_poll(); // 如需命令控制可启用 */
    face_tracker_poll();
}
