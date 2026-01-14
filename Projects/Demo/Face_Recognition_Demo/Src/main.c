/**
 * @file main.c
 * @brief Face Recognition Demo - 人脸识别演示程序
 *
 * 本程序移植自 DSP_Debug，实现以下功能：
 * - 摄像头(OV5640)初始化和图像采集
 * - 显示屏初始化和图像显示
 * - 与DSP通信进行人脸特征提取
 * - 人脸特征比对和分数计算
 * - 串口命令行交互界面
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "s300.h"
#include "rcc.h"
#include "board.h"
#include "mailbox.h"
#include "psram.h"
#include "uart.h"
#include "video.h"
#include "dsp_protocol.h"
#include "camera_ov5640.h"

/* --- UART Ring Buffer --- */
#define RX_BUFFER_SIZE 256
static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;

/* --- Shared Message Buffer --- */
/* Allocated in PSRAM (Moved to 0x80500000 to avoid Display Buffers at 0x8040xxxx) */
#define DSP_SHARED_MSG_ADDR 0x80500000

/* --- Image Buffer Address --- */
/* Use DSP_SRAM1 for image data to avoid overwriting model in DSP_SRAM0 */
/* DSP_SRAM0 (0x44000000): model_sram0_boot.bin (~454KB) */
/* DSP_SRAM1 (0x44040000): available for image buffer */
#define IMAGE_BUFFER_ADDR   DSP_SRAM1_BASE

/**
 * @brief UART3中断处理函数
 */
void UART3_IRQHandler(void)
{
    S300_UART_TypeDef *U = UART3;
    uint32_t iir = U->IIR_FCR;
    uint32_t int_id = iir & 0x0F;

    /* RX Data Available (0x04) or Character Timeout (0x0C) */
    if ((int_id == 0x04) || (int_id == 0x0C))
    {
        while (U->LSR & 0x01)
        {
            uint8_t data = (uint8_t)(U->RBR_THR_DLL & 0xFF);
            uint32_t next_head = (rx_head + 1) % RX_BUFFER_SIZE;
            if (next_head != rx_tail)
            {
                rx_buffer[rx_head] = data;
                rx_head = next_head;
            }
        }
    }
}

/**
 * @brief 非阻塞读取UART字符
 */
static int uart_getchar_noblock(uint8_t *c)
{
    if (rx_head == rx_tail) return 0;
    *c = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;
    return 1;
}

/* --- SysTick --- */
static volatile uint32_t g_tick_ms = 0;

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t millis(void)
{
    return g_tick_ms;
}

/* --- Mailbox Helper --- */
static int mb_send_addr_wait(uint32_t addr)
{
    /* Ensure data is written to memory before notifying DSP */
    __DSB();

    /* Debug: Print what we are sending */
    dsp_msg_t *msg = (dsp_msg_t *)addr;
    printf("[M4] Sending Mailbox: Addr=0x%08X, Type=0x%08X\n", (unsigned int)addr, (unsigned int)msg->type);

    /* Simple blocking send with timeout */
    uint32_t t0 = millis();
    while (write_mailbox(MAILBOX_BASE, addr) != 0)
    {
        if ((millis() - t0) > 100) /* 100ms timeout */
        {
            printf("[ERR] Mailbox Send Timeout (0x%08X)\n", (unsigned int)addr);
            return -1;
        }
    }
    return 0;
}

/* --- Feature Storage (Ring Buffer for up to 10 targets) --- */
#define MAX_TARGETS 10
static int8_t target_features[MAX_TARGETS][128];
static int target_count = 0;      /* Number of stored targets (0-10) */
static int target_write_idx = 0;  /* Next write position (FIFO) */
static int8_t compare_feature[128];
static bool has_compare = false;

typedef enum { EXTRACT_NONE, EXTRACT_TARGET, EXTRACT_COMPARE } extract_type_t;
static volatile extract_type_t g_extract_type = EXTRACT_NONE;

/* --- Font 5x7 for Display --- */
static const uint8_t font5x7[96][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x10, 0x08, 0x08, 0x10, 0x08}  // ~
};

/* --- Display Helper Functions --- */
static void draw_pixel_rotated(int vx, int vy, uint16_t color)
{
    if (vx < 0 || vx >= 160 || vy < 0 || vy >= 128) return;

    int buffer_x = 127 - vy;
    int buffer_y = vx;

    volatile uint16_t *dst = (volatile uint16_t *)DISP_RFRAME0_ADDR;
    dst[buffer_y * DISP_IMAGE_WIDTH + buffer_x] = color;
}

static void draw_char(int vx, int vy, char c, uint16_t color)
{
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = font5x7[c - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                draw_pixel_rotated(vx + col, vy + row, color);
            }
        }
    }
}

static void draw_string(int vx, int vy, const char *str, uint16_t color)
{
    while (*str) {
        draw_char(vx, vy, *str, color);
        vx += 6; // 5 + 1 spacing
        str++;
    }
}

static void draw_rect_fill(int vx, int vy, int w, int h, uint16_t color)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            draw_pixel_rotated(vx + x, vy + y, color);
        }
    }
}

/* --- Similarity Calculation --- */
static int calculate_single_similarity(int8_t *feat1, int8_t *feat2)
{
    int32_t dot = 0;
    for (int i = 0; i < 128; i++) {
        dot += (int32_t)feat1[i] * (int32_t)feat2[i];
    }
    int score = (dot * 100) / 16129;
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    return score;
}

static void calculate_similarity(void)
{
    if (target_count == 0 || !has_compare) {
        printf("[SIM] No targets registered!\n");
        return;
    }

    int best_score = -1;
    int best_id = -1;

    for (int t = 0; t < target_count; t++) {
        int score = calculate_single_similarity(target_features[t], compare_feature);
        if (score > best_score) {
            best_score = score;
            best_id = t;
        }
    }

    printf("[SIM] Best Match: ID=%d, Score=%d (of %d targets)\n", best_id, best_score, target_count);

    /* Draw Score on Display */
    char score_buf[16];
    sprintf(score_buf, "Score: %d  ", best_score); // Trailing spaces to clear previous

    /* Score color: Green (>=60) or Red (<60) */
    uint16_t score_color = (best_score >= 60) ? 0x07E0 : 0xF800;  /* RGB565: Green=0x07E0, Red=0xF800 */

    /* Clear and draw score above compare image */
    draw_rect_fill(92, 26, 68, 8, 0x0000); // Black background
    draw_string(92, 26, score_buf, score_color);

    /* Trigger display update */
    REG32(DSP_VIDEO_SS_BASE + 0x50) = 1u;
    while ((REG32(DSP_VIDEO_SS_BASE + 0x50) & 0x1u) != 0u);
}

/* --- Display Functions --- */
static void set_alpha_buffer(uint8_t alpha)
{
    /* PSRAM supports 16-bit aligned access. Write 2 pixels at a time. */
    volatile uint16_t *a0 = (volatile uint16_t *)DISP_RALPHA0_ADDR;
    volatile uint16_t *a1 = (volatile uint16_t *)DISP_RALPHA1_ADDR;
    uint16_t val = (uint16_t)alpha | ((uint16_t)alpha << 8);
    uint32_t n_words = (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT) / 2;

    for (uint32_t i = 0; i < n_words; i++) {
        a0[i] = val;
        a1[i] = val;
    }
}

static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | ((uint16_t)b >> 3);
}

static void update_display(uint32_t img_addr, bool is_target)
{
    uint8_t *src = (uint8_t *)img_addr;
    uint16_t *dst = (uint16_t *)DISP_RFRAME0_ADDR;

    /*
     * Landscape orientation (160x128).
     * Physical screen is 128x160 portrait, rotated 90 degrees CCW.
     */
    int start_vx = is_target ? 12 : 92;          /* Left side vs Right side */
    int start_vy = (128 - 56) / 2;               /* Centered vertically */

    printf("[M4] Updating Display (Landscape): %s at visual (%d, %d)\n",
           is_target ? "Target" : "Compare", start_vx, start_vy);

    /* Draw Target/Compare Label */
    if (is_target) {
        draw_rect_fill(start_vx, start_vy - 10, 40, 8, 0x0000);
        draw_string(start_vx, start_vy - 10, "Target", 0xFFFF);
    }

    for (int iy = 0; iy < 56; iy++) {
        for (int ix = 0; ix < 56; ix++) {
            /* Source image is 112x112 RGB888, scale 1/2 to 56x56 */
            int sy = iy * 2;
            int sx = ix * 2;
            int src_idx = (sy * 112 + sx) * 3;
            uint8_t r = src[src_idx];
            uint8_t g = src[src_idx + 1];
            uint8_t b = src[src_idx + 2];

            uint16_t rgb565 = rgb888_to_rgb565(r, g, b);

            int vx = start_vx + ix;
            int vy = start_vy + iy;

            int buffer_x = 127 - vy;
            int buffer_y = vx;

            dst[buffer_y * DISP_IMAGE_WIDTH + buffer_x] = rgb565;
        }
    }

    /* Trigger display update */
    REG32(DSP_VIDEO_SS_BASE + 0x50) = 1u;
    while ((REG32(DSP_VIDEO_SS_BASE + 0x50) & 0x1u) != 0u);
}

/* --- File Transfer Mode --- */
static char file_transfer_mode(void)
{
    printf("Starting File Transfer Mode...\n");
    printf("Waiting for 'T' command from PC tool...\n");

    /* Small delay to ensure any pending RX chars arrive before we flush */
    for (volatile int i = 0; i < 10000; i++);

    /* Disable UART Interrupt */
    NVIC_DisableIRQ(UART3_IRQn);

    /* Flush RX FIFO */
    while (UART3->LSR & 1) {
        (void)UART3->RBR_THR_DLL;
    }

    /* Simple polling receive */
    uint8_t cmd = 0;
    while (!(UART3->LSR & 1));
    cmd = (uint8_t)(UART3->RBR_THR_DLL & 0xFF);

    uint32_t write_addr = IMAGE_BUFFER_ADDR;
    if (cmd == 'T') {
        write_addr = IMAGE_BUFFER_ADDR;
    } else if (cmd == 'C') {
        write_addr = IMAGE_BUFFER_ADDR;
    } else {
        printf("Invalid command: 0x%02X\n", cmd);
        goto exit;
    }

    /* ACK command */
    while (!(UART3->LSR & 0x40));
    UART3->RBR_THR_DLL = 'K';
    while (!(UART3->LSR & 0x40));

    /* Receive size (4 bytes) */
    uint32_t file_size = 0;
    uint8_t *p_size = (uint8_t *)&file_size;
    for (int i = 0; i < 4; i++) {
        while (!(UART3->LSR & 1));
        p_size[i] = (uint8_t)(UART3->RBR_THR_DLL & 0xFF);
    }

    /* ACK erase (simulated) */
    while (!(UART3->LSR & 0x40));
    UART3->RBR_THR_DLL = 'K';
    while (!(UART3->LSR & 0x40));

    /* Receive Data */
    uint32_t received = 0;
    static uint8_t chunk_buf[4096];

    while (received < file_size) {
        uint32_t chunk_size = file_size - received;
        if (chunk_size > 4096) chunk_size = 4096;

        for (uint32_t i = 0; i < chunk_size; i++) {
            while (!(UART3->LSR & 1));
            chunk_buf[i] = (uint8_t)(UART3->RBR_THR_DLL & 0xFF);
        }

        /* Write to DSP SRAM0 */
        memcpy((void *)write_addr, chunk_buf, chunk_size);

        write_addr += chunk_size;
        received += chunk_size;

        /* ACK chunk */
        while (!(UART3->LSR & 0x40));
        UART3->RBR_THR_DLL = 'K';
        while (!(UART3->LSR & 0x40));
    }

    printf("Transfer Complete. %u bytes written to 0x%08X\n",
           (unsigned int)received, (unsigned int)IMAGE_BUFFER_ADDR);

    /* Reset RX buffer indices */
    rx_head = 0;
    rx_tail = 0;

    /* Flush RX FIFO one last time */
    while (UART3->LSR & 1) {
        (void)UART3->RBR_THR_DLL;
    }

    /* Re-enable UART Interrupt */
    NVIC_ClearPendingIRQ(UART3_IRQn);
    NVIC_EnableIRQ(UART3_IRQn);

    return (char)cmd;

exit:
    /* Reset RX buffer indices */
    rx_head = 0;
    rx_tail = 0;

    /* Flush RX FIFO */
    while (UART3->LSR & 1) {
        (void)UART3->RBR_THR_DLL;
    }

    /* Re-enable UART Interrupt */
    NVIC_ClearPendingIRQ(UART3_IRQn);
    NVIC_EnableIRQ(UART3_IRQn);

    return 0;
}

/* --- Command Parser --- */
#define CMD_BUF_SIZE 64
static char cmd_buf[CMD_BUF_SIZE];
static int cmd_idx = 0;

static void print_help(void)
{
    printf("\nFace Recognition Demo Commands:\n");
    printf("  help              : Show this help\n");
    printf("  dsp start         : Release DSP reset\n");
    printf("  dsp stop          : Hold DSP reset\n");
    printf("  face ping         : Check DSP status\n");
    printf("  face init <addr> <size> : Set Model (Addr, Size)\n");
    printf("  face extract <addr>     : Extract feature from 112x112 RGB888 image\n");
    printf("  load              : Enter File Transfer Mode (Write to DSP SRAM1)\n");
    printf("  status            : Show status\n");
}

static void process_command(char *cmd)
{
    if (strcmp(cmd, "help") == 0) {
        print_help();
    } else if (strcmp(cmd, "load") == 0) {
        char type = file_transfer_mode();
        if (type == 'T') {
            g_extract_type = EXTRACT_TARGET;
            printf("Triggering DSP Extract for TARGET (0x%08X)...\n", (unsigned int)IMAGE_BUFFER_ADDR);

            /* Update Display */
            update_display(IMAGE_BUFFER_ADDR, true);

            /* Ensure data is written to memory */
            __DSB();
            volatile uint8_t *p_check = (uint8_t *)IMAGE_BUFFER_ADDR;
            (void)*p_check; /* Read back to force drain */

            dsp_msg_t *msg = (dsp_msg_t *)DSP_SHARED_MSG_ADDR;
            msg->type = CMD_EXTRACT_FEATURE;
            msg->args[0] = IMAGE_BUFFER_ADDR;
            mb_send_addr_wait(DSP_SHARED_MSG_ADDR);
        } else if (type == 'C') {
            g_extract_type = EXTRACT_COMPARE;
            printf("Triggering DSP Extract for COMPARE (0x%08X)...\n", (unsigned int)IMAGE_BUFFER_ADDR);

            /* Update Display */
            update_display(IMAGE_BUFFER_ADDR, false);

            /* Ensure data is written to memory */
            __DSB();
            volatile uint8_t *p_check = (uint8_t *)IMAGE_BUFFER_ADDR;
            (void)*p_check; /* Read back to force drain */

            dsp_msg_t *msg = (dsp_msg_t *)DSP_SHARED_MSG_ADDR;
            msg->type = CMD_EXTRACT_FEATURE;
            msg->args[0] = IMAGE_BUFFER_ADDR;
            mb_send_addr_wait(DSP_SHARED_MSG_ADDR);
        }
    } else if (strcmp(cmd, "dsp start") == 0) {
        printf("Starting DSP...\n");
        set_dsp_warm_reset(false);
    } else if (strcmp(cmd, "dsp stop") == 0) {
        printf("Stopping DSP (Reset)...\n");
        set_dsp_warm_reset(true);
    } else if (strcmp(cmd, "face ping") == 0) {
        printf("Sending PING...\n");
        dsp_msg_t *msg = (dsp_msg_t *)DSP_SHARED_MSG_ADDR;
        msg->type = CMD_PING;
        mb_send_addr_wait(DSP_SHARED_MSG_ADDR);
    } else if (strncmp(cmd, "face init ", 10) == 0) {
        uint32_t addr, size;
        if (sscanf(cmd + 10, "%lx %lx", (unsigned long *)&addr, (unsigned long *)&size) == 2) {
            printf("Sending CMD_SET_MODEL (Addr=0x%X, Size=0x%X)...\n",
                   (unsigned int)addr, (unsigned int)size);
            dsp_msg_t *msg = (dsp_msg_t *)DSP_SHARED_MSG_ADDR;
            msg->type = CMD_SET_MODEL;
            msg->args[0] = addr;
            msg->args[1] = size;
            mb_send_addr_wait(DSP_SHARED_MSG_ADDR);
        } else {
            printf("Usage: face init <hex_addr> <hex_size>\n");
        }
    } else if (strncmp(cmd, "face extract ", 13) == 0) {
        uint32_t addr;
        if (sscanf(cmd + 13, "%lx", (unsigned long *)&addr) == 1) {
            printf("Sending CMD_EXTRACT_FEATURE (ImageAddr=0x%X)...\n", (unsigned int)addr);
            dsp_msg_t *msg = (dsp_msg_t *)DSP_SHARED_MSG_ADDR;
            msg->type = CMD_EXTRACT_FEATURE;
            msg->args[0] = addr;
            mb_send_addr_wait(DSP_SHARED_MSG_ADDR);
        } else {
            printf("Usage: face extract <hex_addr>\n");
        }
    } else if (strcmp(cmd, "status") == 0) {
        printf("Tick: %u ms\n", (unsigned int)millis());
        printf("Targets: %d/%d\n", target_count, MAX_TARGETS);
        printf("Has Compare: %s\n", has_compare ? "Yes" : "No");
    } else if (strlen(cmd) > 0) {
        printf("Unknown command: %s\n", cmd);
    }
}

static void process_mailbox_rx(uint32_t val)
{
    /* The value received is an offset in DSP DTCM. Convert to M4 address. */
    uint32_t abs_addr = DSP_DTCM_BASE + val;

    /* Data Synchronization Barrier */
    __DSB();
    __ISB();

    dsp_msg_t *msg = (dsp_msg_t *)abs_addr;

    /* Small delay to let DSP finish printing */
    for (volatile int i = 0; i < 100000; i++);

    switch (msg->type) {
        case EVT_PONG:
            printf("[DSP] PONG (Alive)\n");
            break;
        case EVT_READY:
            printf("[DSP] READY\n");
            break;
        case EVT_ACK:
            printf("[DSP] ACK\n");
            break;
        case EVT_NACK:
            printf("[DSP] NACK (Error)\n");
            break;
        case EVT_FEATURE_READY:
        {
            uint32_t feature_offset = msg->args[0];
            uint32_t feature_addr = DSP_DTCM_BASE + feature_offset;
            printf("[DSP] FEATURE READY: Offset=0x%08X, AbsAddr=0x%08X\n",
                   (unsigned int)feature_offset, (unsigned int)feature_addr);

            __DSB();
            __ISB();

            int8_t *p_feature = (int8_t *)feature_addr;

            /* Validate feature data */
            int zero_count = 0;
            for (int i = 0; i < 128; i++) {
                if (p_feature[i] == 0) zero_count++;
            }

            if (zero_count == 128) {
                printf("[WARN] Feature vector is all zeros! DSP may not have finished.\n");
                for (volatile int d = 0; d < 200000; d++);
                __DSB();
                __ISB();
            }

            printf("[DSP] Feature Vector (int8): ");
            for (int i = 0; i < 10; i++) {
                printf("%d ", p_feature[i]);
            }
            printf("...\n");

            /* Store feature and calculate similarity */
            if (g_extract_type == EXTRACT_TARGET) {
                memcpy(target_features[target_write_idx], p_feature, 128);
                printf("[M4] Target feature stored at slot %d.\n", target_write_idx);
                target_write_idx = (target_write_idx + 1) % MAX_TARGETS;
                if (target_count < MAX_TARGETS) target_count++;
                printf("[M4] Total targets: %d/%d\n", target_count, MAX_TARGETS);
            } else if (g_extract_type == EXTRACT_COMPARE) {
                memcpy(compare_feature, p_feature, 128);
                has_compare = true;
                printf("[M4] Compare feature stored.\n");
                calculate_similarity();
            }
            g_extract_type = EXTRACT_NONE;
        }
        break;
        default:
            printf("[DSP] Unknown Msg Type: 0x%08X (at 0x%08X)\n",
                   (unsigned int)msg->type, (unsigned int)val);
            break;
    }
}

/* --- Main Entry Point --- */
int main(void)
{
    board_init();
    printf("\r\n[S300][Face_Recognition_Demo] Booting...\r\n");

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        printf("[ERR] SysTick_Config failed!\r\n");
    }

    /* Initialize PLLs */
    rcc_init_mm_pll(8, 400, 0, 3, 2); /* 100MHz */
    rcc_init_dsp_pll(6, 800, 0, 2, 2); /* 400MHz */

    /* Initialize PSRAM */
    printf("[S300][Face_Recognition_Demo] Init PSRAM...\r\n");
    init_psram(4, 1);

    /* Initialize Mailbox */
    printf("[S300][Face_Recognition_Demo] Init Mailbox...\r\n");
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);

    /* Initialize Camera (Power on & I2C) - 使用模块化摄像头初始化 */
    printf("[S300][Face_Recognition_Demo] Init Camera...\r\n");
    camera_ov5640_preinit();

    /* Initialize Display */
    printf("[S300][Face_Recognition_Demo] Init Display...\r\n");
    init_video(EM_DVP, CAMREA_RGB565, C1080X720P);

    /* Set Alpha to 0xFF (Opaque) - PSRAM safe */
    set_alpha_buffer(0xFF);

    /* Clear screen to black - PSRAM safe (32-bit writes) */
    volatile uint32_t *f0 = (volatile uint32_t *)DISP_RFRAME0_ADDR;
    volatile uint32_t *f1 = (volatile uint32_t *)DISP_RFRAME1_ADDR;
    uint32_t f_words = (DISP_IMAGE_WIDTH * DISP_IMAGE_HEIGHT * 2) / 4;
    for (uint32_t i = 0; i < f_words; i++) {
        f0[i] = 0;
        f1[i] = 0;
    }

    /* Trigger initial update */
    REG32(DSP_VIDEO_SS_BASE + 0x50) = 1u;
    while ((REG32(DSP_VIDEO_SS_BASE + 0x50) & 0x1u) != 0u);

    /*
     * Enable MM Display Output
     * 人脸识别demo图像输入不来自MM（通过串口传输），但显示仍需MM输出。
     * 由于DSP侧不使能MM，需要在M4侧手动触发MM寄存器同步。
     *
     * CORE_REG_UPDATE (0x70): 触发核心配置寄存器与核心时钟域同步
     * SPI_REG_UPDATE (0x1E0): 触发SPI配置寄存器与SPI时钟域同步
     */
    printf("[S300][Face_Recognition_Demo] Enable MM Display...\r\n");
    REG32(DSP_VIDEO_SS_BASE + 0x70) = 1;   /* CORE_REG_UPDATE */
    REG32(DSP_VIDEO_SS_BASE + 0x1E0) = 1;  /* SPI_REG_UPDATE */

    /* Enable UART3 Interrupts */
    set_uart_interrupt(UART_IDX3, false, true);
    NVIC_ClearPendingIRQ(UART3_IRQn);
    NVIC_SetPriority(UART3_IRQn, 3);
    NVIC_EnableIRQ(UART3_IRQn);

    /* Default: Reset DSP */
    printf("[S300][Face_Recognition_Demo] Resetting DSP...\r\n");
    set_dsp_warm_reset(true);

    printf("[S300][Face_Recognition_Demo] Ready. Type 'help' for commands.\r\n");
    printf("> ");
    fflush(stdout);

    while (1) {
        uint8_t c;
        if (uart_getchar_noblock((uint8_t *)&c)) {
            /* Echo back */
            write_uart(UART_IDX3, UARTTYPE_STD_SERIAL, c);

            if (c == '\r' || c == '\n') {
                printf("\n");
                cmd_buf[cmd_idx] = '\0';
                process_command(cmd_buf);
                cmd_idx = 0;
                printf("> ");
                fflush(stdout);
            } else if (c == '\b' || c == 0x7F) { /* Backspace */
                if (cmd_idx > 0) {
                    cmd_idx--;
                }
            } else {
                if (cmd_idx < CMD_BUF_SIZE - 1) {
                    cmd_buf[cmd_idx++] = (char)c;
                }
            }
        }

        /* Poll Mailbox for incoming messages */
        if (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
            uint32_t val = read_mailbox(MAILBOX_BASE);
            process_mailbox_rx(val);
            printf("> ");
            fflush(stdout);
        }
    }
    return 0;
}
