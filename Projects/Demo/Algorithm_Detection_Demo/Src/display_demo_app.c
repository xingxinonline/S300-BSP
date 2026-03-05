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
#include "mailbox_proto.h"
#include "camera_ov5640.h"
#include "face_tracker.h"
#include "display_demo_app.h"
#include "board.h"

#if BOARD_CAMERA_FORMAT == 0
  #define APP_CAM_PRO CAMREA_RGB565
#else
  #define APP_CAM_PRO CAMREA_YUV422
#endif

/* Delay START_TRACK so DSP can finish handshake-phase mailbox reads. */
static uint32_t (*s_get_millis)(void) = 0;
static uint32_t s_start_track_deadline_ms = 0;
static bool s_start_track_pending = false;

#ifndef DSP_START_TRACK_DELAY_MS
#define DSP_START_TRACK_DELAY_MS 300u
#endif

#ifndef MAILBOX_HANDSHAKE_INIT
#define MAILBOX_HANDSHAKE_INIT 0x5A5A5A5Au
#endif

#ifndef MAILBOX_HANDSHAKE_ACK
#define MAILBOX_HANDSHAKE_ACK 0xA5A5A5A5u
#endif

#ifndef DSP_HANDSHAKE_TIMEOUT_MS
#define DSP_HANDSHAKE_TIMEOUT_MS 1000u
#endif

#ifndef DSP_HANDSHAKE_RETRY_MS
#define DSP_HANDSHAKE_RETRY_MS 100u
#endif

static bool s_handshake_done = false;

static bool wait_for_dsp_handshake_ack(void)
{
    if (s_get_millis != 0) {
        uint32_t t0 = s_get_millis();
        uint32_t next_init_ms = t0;
        while ((uint32_t)(s_get_millis() - t0) < DSP_HANDSHAKE_TIMEOUT_MS) {
            uint32_t now_ms = s_get_millis();
            if ((int32_t)(now_ms - next_init_ms) >= 0) {
                (void)write_mailbox(MAILBOX_BASE, MAILBOX_HANDSHAKE_INIT);
                next_init_ms = now_ms + DSP_HANDSHAKE_RETRY_MS;
            }

            if (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
                uint32_t msg = read_mailbox(MAILBOX_BASE);
                if (msg == MAILBOX_HANDSHAKE_ACK) {
                    printf("[S300][DisplayDemo] DSP handshake ACK received.\r\n");
                    return true;
                }
                if (msg == MAILBOX_HANDSHAKE_INIT) {
                    (void)write_mailbox(MAILBOX_BASE, MAILBOX_HANDSHAKE_ACK);
                    printf("[S300][DisplayDemo] DSP handshake INIT received, ACK sent.\r\n");
                    return true;
                }
            }
        }
    } else {
        for (volatile uint32_t spin = 0; spin < 5000000u; spin++) {
            if ((spin % 20000u) == 0u) {
                (void)write_mailbox(MAILBOX_BASE, MAILBOX_HANDSHAKE_INIT);
            }
            if (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
                uint32_t msg = read_mailbox(MAILBOX_BASE);
                if (msg == MAILBOX_HANDSHAKE_ACK) {
                    printf("[S300][DisplayDemo] DSP handshake ACK received.\r\n");
                    return true;
                }
                if (msg == MAILBOX_HANDSHAKE_INIT) {
                    (void)write_mailbox(MAILBOX_BASE, MAILBOX_HANDSHAKE_ACK);
                    printf("[S300][DisplayDemo] DSP handshake INIT received, ACK sent.\r\n");
                    return true;
                }
            }
        }
    }

    printf("[S300][DisplayDemo][WARN] DSP handshake ACK timeout (%ums).\r\n",
           (unsigned)DSP_HANDSHAKE_TIMEOUT_MS);
    return false;
}

void display_demo_app_init(uint32_t (*get_millis)(void))
{
    s_get_millis = get_millis;

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

    /* 先拉住再释放 warm reset，避免软复位后 DSP 状态不一致 */
    set_dsp_warm_reset(true);

    /* 简单延时，确保 warm reset 脉冲被 DSP 侧采样到 */
    for (volatile uint32_t i = 0; i < 50000u; i++) { }

    set_dsp_warm_reset(false);

    /* 给 DSP 留出启动窗口，再发送握手令牌 */
    for (volatile uint32_t i = 0; i < 100000u; i++) { }
    s_handshake_done = wait_for_dsp_handshake_ack();

    /* 延后下发 START_TRACK，避免与 DSP 初始化握手抢同一邮箱消息。 */
    s_start_track_pending = s_handshake_done;
    if (s_handshake_done && s_get_millis != 0) {
        s_start_track_deadline_ms = s_get_millis() + DSP_START_TRACK_DELAY_MS;
        printf("[S300][DisplayDemo] START_TRACK deferred %ums after handshake ACK.\r\n",
               (unsigned)DSP_START_TRACK_DELAY_MS);
    } else {
        s_start_track_deadline_ms = 0;
        if (!s_handshake_done) {
            printf("[S300][DisplayDemo][WARN] Skip START_TRACK because handshake is not complete.\r\n");
        }
    }

    /* 人脸追踪初始化（依赖 mailbox；提供时间回调实现） */
    face_tracker_init(get_millis);

    printf("[S300][MM_Test_Demo] Started.\r\n");
}

void display_demo_app_tick(void)
{
    if (s_start_track_pending) {
        uint32_t now_ms = (s_get_millis != 0) ? s_get_millis() : 0;
        if (s_get_millis == 0 || (int32_t)(now_ms - s_start_track_deadline_ms) >= 0) {
            if (write_mailbox(MAILBOX_BASE, MAILBOX_CMD_START_TRACK) < 0) {
                printf("[S300][DisplayDemo][WARN] MAILBOX_CMD_START_TRACK failed.\r\n");
            } else {
                printf("[S300][DisplayDemo] MAILBOX_CMD_START_TRACK sent.\r\n");
            }
            s_start_track_pending = false;
        }
    }

    /* uart_cmd_poll(); // 如需命令控制可启用 */
    face_tracker_poll();
}
