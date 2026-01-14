/*
 * 文件：display_demo_app.c
 * 说明：本 Demo 的“应用编排层”，将多子模块按顺序初始化，并提供统一的 tick 调度入口。
 * 初始化顺序：
 *   1) 摄像头预上电/探测（失败不致命，继续显示链路验证）；
 *   2) 视频子系统（面板/时序/显存绑定）；
 *   3) M4<->DSP 邮箱握手（复位DSP并发送启动令牌，保持与算法侧一致）；
 *   4) LVGL 初始化与显示绑定（双缓冲整帧）；
 *   5) Eyes UI 创建与参数设置（眼距等）；
 *   6) Face Tracker 初始化（依赖毫秒节拍与 eyes）。
 * 运行期：
 *   - display_demo_app_tick() 负责人脸跟踪轮询与 LVGL 定时器处理。
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "rcc.h"
#include "video.h"
#include "lvgl.h"
#include "ui_display.h"
#include "mailbox.h"
#include "camera_ov5640.h"
#include "face_tracker.h"
#include "eyes.h"
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

    /* M4 <-> DSP 邮箱通信与握手 */
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    set_dsp_warm_reset(true);
    write_mailbox(MAILBOX_BASE, 0x5A5A5A5A);

    /* LVGL + 显示绑定与背景色 */
    (void)ui_display_init();
    printf("[S300][DisplayDemo] LVGL %d.%d.%d (%s)\r\n", lv_version_major(), lv_version_minor(), lv_version_patch(), lv_version_info());
    ui_display_set_bg_color(0xffc21e);

    /* 眼睛 UI */
    eyes_set_spacing(38);
    eyes_create();

    /* 人脸追踪初始化（依赖 eyes + mailbox；提供时间回调实现） */
    face_tracker_init(get_millis);

    printf("[S300][DisplayDemo] LVGL started.\r\n");
    printf("[S300][DisplayDemo] UART echo enabled on debug UART (CR->CRLF).\r\n");
    printf("[S300][DisplayDemo] Command: goto <y_mid>  (move eyes midpoint vertically)\r\n");
}

void display_demo_app_tick(void)
{
    /* uart_cmd_poll(); // 如需命令控制可启用 */
    face_tracker_poll();
    lv_timer_handler();
}
