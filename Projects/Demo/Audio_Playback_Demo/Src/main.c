/**
 * @file    main.c
 * @brief   Audio Playback Demo - 音频播放/录音演示
 *
 * 功能说明:
 *   1. 音频直通模式 (Passthrough): 麦克风输入 -> 耳机/扬声器输出
 *   2. 正弦波播放模式: 生成 1kHz 正弦波测试音
 *   3. 录音回放模式: 录制一段音频后循环播放
 *
 * 硬件连接 (Generic EVB):
 *   - PA3: I2S1_MCLK
 *   - PA6: I2S1_BCLK
 *   - PA7: I2S1_LRCLK
 *   - PA8: I2S1_DO (输出到 Codec)
 *   - PA9: I2S1_DI (来自 Codec)
 *   - PA4: WM8978 I2C_SCL (软件 I2C)
 *   - PA5: WM8978 I2C_SDA (软件 I2C)
 *
 * 串口命令:
 *   'p' - 切换到直通模式 (Passthrough)
 *   's' - 切换到正弦波播放模式 (Sine)
 *   'r' - 开始录音
 *   'l' - 播放录音 (Loop)
 *   '+' - 增加音量
 *   '-' - 减小音量
 *   'h' - 显示帮助
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "s300.h"
#include "rcc.h"
#include "gpio.h"
#include "uart.h"
#include "dma.h"
#include "i2s.h"
#include "i2c_soft.h"
#include "wm8978.h"
#include "board.h"

/*===========================================================================
 * 配置参数
 *===========================================================================*/

/* I2S 引脚配置
 * 参考 gitlab/feature/i2s-kws-audio-processing:
 *   PA3(MCLK)   -> FUNCTION_1 (特殊)
 *   PA6-9       -> FUNCTION_3
 */
#define BOARD_I2S_PORT              GPIOA
#define BOARD_I2S_MCLK_PIN          3
#define BOARD_I2S_BCLK_PIN          6
#define BOARD_I2S_LRCLK_PIN         7
#define BOARD_I2S_DO_PIN            8
#define BOARD_I2S_DI_PIN            9
#define BOARD_I2S_FUNCTION          FUNCTION_3
#define BOARD_I2S_MCLK_FUNCTION     FUNCTION_1  /* MCLK 使用 FUNCTION_1 */

/* DMA 缓冲配置 */
#define DMA_BUFFER_LEN              512
#define DMA_BUFFER_COUNT            4

/* 录音缓冲 (约 2 秒 @ 16kHz) */
#define RECORD_BUFFER_SIZE          (16000 * 2 * 2)  /* 16kHz * 2ch * 2sec */

/* 正弦波参数 */
#define SINE_FREQ_HZ                1000
#define SAMPLE_RATE                 16000

/*===========================================================================
 * 工作模式
 *===========================================================================*/

typedef enum {
    MODE_PASSTHROUGH = 0,  /* 直通模式 */
    MODE_SINE_WAVE,        /* 正弦波播放 */
    MODE_RECORDING,        /* 录音中 */
    MODE_PLAYBACK,         /* 录音回放 */
} audio_mode_t;

/*===========================================================================
 * 全局变量
 *===========================================================================*/

static i2c_soft_t g_wm8978_i2c;

/* DMA 缓冲区 */
static int16_t g_dma_rx_buf[DMA_BUFFER_COUNT * DMA_BUFFER_LEN] __attribute__((aligned(4)));
static int16_t g_dma_tx_buf[DMA_BUFFER_COUNT * DMA_BUFFER_LEN] __attribute__((aligned(4)));

/* 环形缓冲索引 */
static volatile uint8_t g_rx_write_idx = 0;
static volatile uint8_t g_rx_read_idx = 0;
static volatile int32_t g_rx_valid = 0;

static volatile uint8_t g_tx_write_idx = 0;
static volatile uint8_t g_tx_read_idx = 0;
static volatile int32_t g_tx_valid = 0;

/* 录音缓冲 */
static int16_t g_record_buf[RECORD_BUFFER_SIZE / sizeof(int16_t)] __attribute__((section(".bss")));
static volatile uint32_t g_record_pos = 0;
static volatile uint32_t g_playback_pos = 0;
static volatile uint32_t g_record_len = 0;

/* 当前模式和音量 */
static volatile audio_mode_t g_mode = MODE_PASSTHROUGH;
static volatile uint8_t g_volume = 40;

/* 运行标志 */
static volatile bool g_running = false;

/*===========================================================================
 * 初始化函数
 *===========================================================================*/

/**
 * @brief 初始化 I2S 引脚
 */
static void audio_i2s_pins_init(void)
{
    /* MCLK 使用 FUNCTION_1 (参考原始代码) */
    gpio_set_function(BOARD_I2S_PORT, BOARD_I2S_MCLK_PIN, BOARD_I2S_MCLK_FUNCTION);
    gpio_set_direction(BOARD_I2S_PORT, BOARD_I2S_MCLK_PIN, true);
    gpio_set_mode(BOARD_I2S_PORT, BOARD_I2S_MCLK_PIN, GPIO_DOWN);

    /* 其他 I2S 引脚使用 FUNCTION_3 */
    const uint8_t pins[] = { BOARD_I2S_BCLK_PIN, BOARD_I2S_LRCLK_PIN, BOARD_I2S_DO_PIN, BOARD_I2S_DI_PIN };
    const bool is_out[] = { true, true, true, false };

    for (int i = 0; i < 4; i++) {
        gpio_set_function(BOARD_I2S_PORT, pins[i], BOARD_I2S_FUNCTION);
        gpio_set_direction(BOARD_I2S_PORT, pins[i], is_out[i]);
        gpio_set_mode(BOARD_I2S_PORT, pins[i], GPIO_DOWN);
    }
    printf("[Audio] I2S pins: PA3(MCLK,F1), PA6-9(BCLK/LRCLK/DO/DI,F3)\\r\\n");
}

/**
 * @brief 初始化 WM8978 Codec
 */
static int audio_wm8978_init(void)
{
    int ret;

    /* 
     * I2C 软件模拟索引配置 (来自 i2c_soft.c):
     *   idx=0: PA14(SCL), PA15(SDA)
     *   idx=1: PA0(SCL),  PA1(SDA)  - 原始 gitlab/feature/i2s-kws-audio-processing 使用 EM_I2C1
     *   idx=2: PA2(SCL),  PA3(SDA)
     *   idx=3: PA4(SCL),  PA5(SDA)
     *
     * 根据原始代码使用 EM_I2C1 = 1，所以使用 idx=1 (PA0/PA1)
     */
    printf("[Audio] Initializing WM8978 I2C: PA0(SCL), PA1(SDA)...\r\n");
    ret = i2c_soft_init_default_idx(&g_wm8978_i2c, 1, 50000);
    if (ret != 0) {
        printf("[Audio] I2C init failed: %d\r\n", ret);
        return ret;
    }

    /* 初始化 WM8978 */
    ret = wm8978_init(&g_wm8978_i2c);
    if (ret != 0) {
        printf("[Audio] WM8978 init failed: %d\r\n", ret);
        printf("[Audio] Please check WM8978 connection to PA0(SCL), PA1(SDA)\r\n");
        return ret;
    }

    /* 配置音量 */
    wm8978_set_hp_vol(&g_wm8978_i2c, g_volume, g_volume);
    wm8978_set_spk_vol(&g_wm8978_i2c, 50);

    /* 配置 ADC/DAC */
    wm8978_set_adda(&g_wm8978_i2c, true, true);

    /* 配置输入: MIC + LINE IN */
    wm8978_set_input(&g_wm8978_i2c, true, true, false);

    /* 配置输出: DAC 到输出 */
    wm8978_set_output(&g_wm8978_i2c, true, false);

    /* 配置 MIC 增益 */
    wm8978_set_mic_gain(&g_wm8978_i2c, 46);

    /* 配置 I2S 格式: fmt=2 (I2S 标准), len=0 (16-bit) */
    wm8978_i2s_cfg(&g_wm8978_i2c, 2, 0);

    printf("[Audio] WM8978 configured (vol=%d)\r\n", g_volume);
    return 0;
}

/**
 * @brief 初始化 Audio PLL 和 I2S
 */
static void audio_pll_i2s_init(void)
{
    /* 配置 Audio PLL: 输出 12MHz MCLK */
    rcc_init_audio_pll(3, 129, 500000, 7, 6);
    printf("[Audio] Audio PLL configured\r\n");

    /* 使能 I2S1 时钟 */
    rcc_set_audio_clock(I2S_IDX1, true);

    /* 配置 I2S1 基本参数 */
    i2s_basic_init(I2S_IDX1, 12000000, I2S_WORD_16);

    /* 使能 I2S */
    i2s_enable(I2S_IDX1, true);

    printf("[Audio] I2S1 configured\r\n");
}

/**
 * @brief 配置 DMA (使用 SDK i2s_dma_mode)
 */
static void audio_dma_init(void)
{
    /* 使能 DMA0 中断 */
    NVIC_SetPriority(DMA0_IRQn, 1);
    NVIC_EnableIRQ(DMA0_IRQn);

    /* 配置 I2S DMA 模式 (通道 0=录音, 通道 1=播放) */
    i2s_dma_mode(I2S_IDX1, 0, 1,
                 (uint8_t *)g_dma_rx_buf,
                 (uint8_t *)g_dma_tx_buf,
                 DMA_BUFFER_LEN * 2);

    printf("[Audio] DMA configured\r\n");
}

/*===========================================================================
 * 音频处理函数
 *===========================================================================*/

/* 正弦波查找表 (256 点, 一个周期) */
static const int16_t g_sine_table[256] = {
    0, 201, 402, 603, 803, 1004, 1204, 1403, 1602, 1800, 1997, 2194, 2389, 2583, 2776, 2967,
    3157, 3346, 3533, 3718, 3901, 4083, 4262, 4440, 4615, 4788, 4959, 5128, 5294, 5458, 5619, 5778,
    5934, 6088, 6238, 6386, 6531, 6673, 6812, 6949, 7082, 7212, 7339, 7463, 7583, 7701, 7815, 7925,
    8033, 8137, 8237, 8335, 8428, 8518, 8605, 8688, 8768, 8844, 8916, 8985, 9050, 9112, 9170, 9224,
    9274, 9321, 9364, 9403, 9439, 9471, 9499, 9523, 9544, 9561, 9574, 9583, 9589, 9591, 9590, 9585,
    9576, 9564, 9548, 9528, 9505, 9478, 9448, 9414, 9377, 9336, 9292, 9244, 9193, 9139, 9081, 9020,
    8955, 8888, 8817, 8743, 8666, 8586, 8502, 8416, 8327, 8235, 8140, 8042, 7942, 7839, 7733, 7625,
    7514, 7401, 7285, 7167, 7047, 6924, 6799, 6672, 6543, 6412, 6279, 6144, 6007, 5869, 5728, 5586,
    5443, 5298, 5152, 5004, 4855, 4705, 4554, 4401, 4248, 4094, 3939, 3783, 3626, 3469, 3311, 3153,
    2994, 2835, 2676, 2516, 2356, 2196, 2036, 1876, 1716, 1556, 1397, 1238, 1079, 921, 763, 606,
    450, 294, 139, -15, -169, -322, -474, -626, -777, -927, -1076, -1224, -1371, -1517, -1662, -1806,
    -1949, -2090, -2230, -2369, -2506, -2642, -2777, -2910, -3041, -3171, -3300, -3427, -3552, -3675, -3797, -3917,
    -4035, -4152, -4266, -4379, -4490, -4599, -4706, -4811, -4914, -5015, -5114, -5211, -5306, -5398, -5489, -5578,
    -5664, -5748, -5830, -5910, -5988, -6063, -6136, -6207, -6276, -6342, -6406, -6468, -6527, -6584, -6639, -6692,
    -6742, -6790, -6835, -6878, -6919, -6957, -6993, -7027, -7058, -7087, -7113, -7137, -7159, -7178, -7195, -7210,
    -7222, -7232, -7239, -7244, -7247, -7248, -7246, -7242, -7235, -7227, -7216, -7202, -7187, -7169, -7149, -7127
};

/**
 * @brief 生成正弦波采样 (使用查表法)
 */
static void generate_sine_wave(int16_t *buf, uint32_t samples)
{
    /* 256 点表对应一个周期, 1kHz @ 16kHz 采样率 = 每16个样本一个周期 */
    /* phase_inc = 256 * SINE_FREQ_HZ / SAMPLE_RATE = 256 * 1000 / 16000 = 16 */
    static uint32_t phase = 0;
    const uint32_t phase_inc = 256 * SINE_FREQ_HZ / SAMPLE_RATE;

    for (uint32_t i = 0; i < samples; i += 2) {
        int16_t sample = g_sine_table[phase & 0xFF];
        buf[i] = sample;      /* Left */
        buf[i + 1] = sample;  /* Right */
        phase += phase_inc;
    }
}

/**
 * @brief 更新音量
 */
static void update_volume(int delta)
{
    int new_vol = (int)g_volume + delta;
    if (new_vol < 0) new_vol = 0;
    if (new_vol > 63) new_vol = 63;
    g_volume = (uint8_t)new_vol;
    wm8978_set_hp_vol(&g_wm8978_i2c, g_volume, g_volume);
    printf("[Audio] Volume: %d\r\n", g_volume);
}

/**
 * @brief 打印帮助信息
 */
static void print_help(void)
{
    printf("\r\n");
    printf("=== Audio Playback Demo Commands ===\r\n");
    printf("  p - Passthrough mode (mic -> speaker)\r\n");
    printf("  s - Sine wave playback (1kHz)\r\n");
    printf("  r - Start recording (~2 sec)\r\n");
    printf("  l - Playback recorded audio (loop)\r\n");
    printf("  + - Increase volume\r\n");
    printf("  - - Decrease volume\r\n");
    printf("  h - Show this help\r\n");
    printf("Current mode: ");
    switch (g_mode) {
        case MODE_PASSTHROUGH: printf("Passthrough\r\n"); break;
        case MODE_SINE_WAVE:   printf("Sine Wave\r\n"); break;
        case MODE_RECORDING:   printf("Recording...\r\n"); break;
        case MODE_PLAYBACK:    printf("Playback\r\n"); break;
    }
    printf("Volume: %d\r\n", g_volume);
    printf("\r\n");
}

/**
 * @brief 处理串口命令
 */
static void process_uart_cmd(char c)
{
    switch (c) {
        case 'p':
        case 'P':
            g_mode = MODE_PASSTHROUGH;
            printf("[Audio] Switched to Passthrough mode\r\n");
            break;

        case 's':
        case 'S':
            g_mode = MODE_SINE_WAVE;
            printf("[Audio] Switched to Sine wave mode\r\n");
            break;

        case 'r':
        case 'R':
            g_record_pos = 0;
            g_record_len = 0;
            g_mode = MODE_RECORDING;
            printf("[Audio] Recording started...\r\n");
            break;

        case 'l':
        case 'L':
            if (g_record_len > 0) {
                g_playback_pos = 0;
                g_mode = MODE_PLAYBACK;
                printf("[Audio] Playing back %u samples\r\n", (unsigned)g_record_len);
            } else {
                printf("[Audio] No recording available! Press 'r' to record first.\r\n");
            }
            break;

        case '+':
        case '=':
            update_volume(5);
            break;

        case '-':
        case '_':
            update_volume(-5);
            break;

        case 'h':
        case 'H':
        case '?':
            print_help();
            break;

        default:
            break;
    }
}

/*===========================================================================
 * 中断处理
 *===========================================================================*/

/**
 * @brief DMA0 中断服务函数
 */
void DMA0_IRQHandler(void)
{
    S300_DMA_TypeDef *D = DMAC0;
    uint32_t status = D->StatusTfr;

    /* 清除中断 */
    D->ClearTfr = status;

    /* RX 完成 (CH0) */
    if (status & 0x01) {
        g_rx_valid++;
        g_rx_write_idx = (g_rx_write_idx + 1) % DMA_BUFFER_COUNT;

        /* 更新 DMA 目标地址 */
        D->CH[0].DAR = (uint32_t)&g_dma_rx_buf[g_rx_write_idx * DMA_BUFFER_LEN];
        D->CH[0].CTL_H = DMA_BUFFER_LEN / 2;
        D->ChEnReg = 0x0101;
    }

    /* TX 完成 (CH1) */
    if (status & 0x02) {
        g_tx_valid--;
        g_tx_read_idx = (g_tx_read_idx + 1) % DMA_BUFFER_COUNT;

        /* 更新 DMA 源地址 */
        D->CH[1].SAR = (uint32_t)&g_dma_tx_buf[g_tx_read_idx * DMA_BUFFER_LEN];
        D->CH[1].CTL_H = DMA_BUFFER_LEN / 2;
        D->ChEnReg = 0x0202;
    }
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

int main(void)
{
    int ret;

    /* 板级初始化 */
    board_init();

    printf("\r\n");
    printf("========================================\r\n");
    printf("  S300 Audio Playback Demo\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    /* 初始化 I2S 引脚 */
    audio_i2s_pins_init();

    /* 初始化 WM8978 */
    ret = audio_wm8978_init();
    if (ret != 0) {
        printf("[Main] WM8978 init failed!\r\n");
        while (1) __WFI();
    }

    /* 初始化 Audio PLL 和 I2S */
    audio_pll_i2s_init();

    /* 初始化 DMA */
    audio_dma_init();

    /* 显示帮助 */
    print_help();

    g_running = true;
    printf("[Main] Entering audio loop (default: Passthrough)...\r\n");

    /* 主循环 */
    while (g_running) {
        /* 处理串口输入 */
        if (BOARD_DEBUG_UART->LSR & 0x01) {
            char c = (char)(BOARD_DEBUG_UART->RBR_THR_DLL & 0xFF);
            process_uart_cmd(c);
        }

        /* 等待 RX 数据就绪 */
        if (g_rx_valid <= 0) {
            continue;
        }

        /* 获取当前 RX 缓冲区 */
        int16_t *rx_ptr = &g_dma_rx_buf[g_rx_read_idx * DMA_BUFFER_LEN];
        int16_t *tx_ptr = &g_dma_tx_buf[g_tx_write_idx * DMA_BUFFER_LEN];

        /* 根据模式处理音频 */
        switch (g_mode) {
            case MODE_PASSTHROUGH:
                /* 直接复制 */
                memcpy(tx_ptr, rx_ptr, DMA_BUFFER_LEN * sizeof(int16_t));
                break;

            case MODE_SINE_WAVE:
                /* 生成正弦波 */
                generate_sine_wave(tx_ptr, DMA_BUFFER_LEN);
                break;

            case MODE_RECORDING:
                /* 录音 + 直通 */
                memcpy(tx_ptr, rx_ptr, DMA_BUFFER_LEN * sizeof(int16_t));

                if (g_record_pos + DMA_BUFFER_LEN <= RECORD_BUFFER_SIZE / sizeof(int16_t)) {
                    memcpy(&g_record_buf[g_record_pos], rx_ptr, DMA_BUFFER_LEN * sizeof(int16_t));
                    g_record_pos += DMA_BUFFER_LEN;
                    g_record_len = g_record_pos;
                } else {
                    /* 录音完成 */
                    g_mode = MODE_PASSTHROUGH;
                    printf("[Audio] Recording complete: %u samples\r\n", (unsigned)g_record_len);
                }
                break;

            case MODE_PLAYBACK:
                /* 回放录音 */
                if (g_playback_pos + DMA_BUFFER_LEN <= g_record_len) {
                    memcpy(tx_ptr, &g_record_buf[g_playback_pos], DMA_BUFFER_LEN * sizeof(int16_t));
                    g_playback_pos += DMA_BUFFER_LEN;
                } else {
                    /* 循环播放 */
                    g_playback_pos = 0;
                    memcpy(tx_ptr, &g_record_buf[g_playback_pos], DMA_BUFFER_LEN * sizeof(int16_t));
                    g_playback_pos += DMA_BUFFER_LEN;
                }
                break;
        }

        /* 更新索引 */
        g_rx_read_idx = (g_rx_read_idx + 1) % DMA_BUFFER_COUNT;
        g_rx_valid--;

        g_tx_write_idx = (g_tx_write_idx + 1) % DMA_BUFFER_COUNT;
        g_tx_valid++;
    }

    return 0;
}
