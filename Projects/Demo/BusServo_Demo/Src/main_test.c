/**
 * @file    main_test.c
 * @brief   总线舵机读写测试程序
 * @details 参考 Hiwonder LeArm SDK 实现完整的舵机命令测试
 */

#include "s300.h"
#include "board.h"
#include "uart.h"
#include "uart_s300.h"
#include "gpio.h"
#include "rcc.h"
#include <stdio.h>
#include <string.h>

/*===========================================================================
 * 舵机命令定义 (参考 Hiwonder SDK)
 *===========================================================================*/

#define SERVO_MOVE_TIME_WRITE       1   /* 设置舵机位置与运行时间 */
#define SERVO_MOVE_TIME_READ        2   /* 读取舵机位置与运行时间 */
#define SERVO_MOVE_TIME_WAIT_WRITE  7   /* 设置延时运动 */
#define SERVO_MOVE_TIME_WAIT_READ   8   /* 读取延时运动 */
#define SERVO_MOVE_START            11  /* 开始运动 */
#define SERVO_MOVE_STOP             12  /* 停止运动 */
#define SERVO_ID_WRITE              13  /* 设置舵机ID */
#define SERVO_ID_READ               14  /* 读取舵机ID */
#define SERVO_ANGLE_OFFSET_ADJUST   17  /* 调整偏差 */
#define SERVO_ANGLE_OFFSET_WRITE    18  /* 保存偏差 */
#define SERVO_ANGLE_OFFSET_READ     19  /* 读取偏差 */
#define SERVO_ANGLE_LIMIT_WRITE     20  /* 设置角度限制 */
#define SERVO_ANGLE_LIMIT_READ      21  /* 读取角度限制 */
#define SERVO_VIN_LIMIT_WRITE       22  /* 设置电压限制 */
#define SERVO_VIN_LIMIT_READ        23  /* 读取电压限制 */
#define SERVO_TEMP_MAX_LIMIT_WRITE  24  /* 设置温度限制 */
#define SERVO_TEMP_MAX_LIMIT_READ   25  /* 读取温度限制 */
#define SERVO_TEMP_READ             26  /* 读取温度 */
#define SERVO_VIN_READ              27  /* 读取电压 */
#define SERVO_POS_READ              28  /* 读取位置 */
#define SERVO_MOTOR_MODE_WRITE      29  /* 设置电机/舵机模式 */
#define SERVO_MOTOR_MODE_READ       30  /* 读取电机/舵机模式 */
#define SERVO_LOAD_UNLOAD_WRITE     31  /* 设置加载/卸载 */
#define SERVO_LOAD_UNLOAD_READ      32  /* 读取加载/卸载状态 */
#define SERVO_LED_CTRL_WRITE        33  /* 设置LED */
#define SERVO_LED_CTRL_READ         34  /* 读取LED状态 */
#define SERVO_LED_ERROR_WRITE       35  /* 设置LED错误 */
#define SERVO_LED_ERROR_READ        36  /* 读取LED错误 */

/*===========================================================================
 * 配置
 *===========================================================================*/

#define SERVO_UART          UART3   /* 舵机 UART */
#define DIR_PIN             22      /* 方向控制引脚 GPIO22 */

/*===========================================================================
 * Private Functions
 *===========================================================================*/

/**
 * @brief 毫秒延时 (使用 SysTick)
 */
static void delay_ms(uint32_t ms)
{
    /* SysTick: 24-bit down counter, reload = SystemCoreClock/1000 - 1 for 1ms */
    uint32_t reload = SystemCoreClock / 1000 - 1;
    if (reload > 0xFFFFFF) reload = 0xFFFFFF;  /* 24-bit max */
    
    SysTick->LOAD = reload;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    
    for (uint32_t i = 0; i < ms; i++) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    }
    
    SysTick->CTRL = 0;
}

/**
 * @brief 微秒延时 (使用循环，带 DWT 校准)
 * @note  DWT 在 GDB 调试时可能被禁用，使用循环作为可靠后备
 */
static void delay_us(uint32_t us)
{
    /* 检查 DWT 是否正常工作 */
    uint32_t start = DWT->CYCCNT;
    __NOP(); __NOP(); __NOP(); __NOP();
    if (DWT->CYCCNT != start) {
        /* DWT 正常，使用硬件计数器 */
        uint32_t cycles = us * (SystemCoreClock / 1000000);
        while ((DWT->CYCCNT - start) < cycles);
    } else {
        /* DWT 被禁用，使用循环延时 (约 5 cycles/loop @ 192MHz) */
        volatile uint32_t cnt = us * (SystemCoreClock / 5000000);
        while (cnt--) {
            __NOP();
        }
    }
}

static void print_hex(const char *prefix, uint8_t *data, int len)
{
    printf("%s", prefix);
    for (int i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\r\n");
}

static void send_byte(uint8_t b)
{
    while (!(SERVO_UART->USR & 0x02u));
    SERVO_UART->RBR_THR_DLL = b;
}

static void wait_tx_done(void)
{
    while (!(SERVO_UART->USR & 0x04u)); /* TFE */
    while (SERVO_UART->USR & 0x01u);     /* BUSY */
}

static int recv_data(uint8_t *buf, int max_len, uint32_t first_timeout_ms, uint32_t byte_timeout_ms)
{
    int len = 0;
    uint32_t timeout;
    
    timeout = first_timeout_ms;
    while (timeout--) {
        if (SERVO_UART->USR & 0x08u) {
            buf[len++] = (uint8_t)SERVO_UART->RBR_THR_DLL;
            break;
        }
        delay_ms(1);
    }
    
    if (len == 0) return 0;
    
    while (len < max_len) {
        timeout = byte_timeout_ms;
        while (timeout--) {
            if (SERVO_UART->USR & 0x08u) {
                buf[len++] = (uint8_t)SERVO_UART->RBR_THR_DLL;
                break;
            }
            delay_ms(1);
        }
        if (timeout == 0 || timeout == 0xFFFFFFFF) break;
    }
    
    return len;
}

/**
 * @brief 发送舵机命令并接收响应
 * @param id        舵机ID
 * @param cmd       命令码
 * @param params    参数数组
 * @param param_len 参数长度
 * @param rx_buf    接收缓冲区
 * @param rx_max    接收缓冲区大小
 * @return          接收到的有效数据长度（从 0x55 0x55 开始）
 */
static int servo_cmd(uint8_t id, uint8_t cmd, uint8_t *params, int param_len,
                     uint8_t *rx_buf, int rx_max)
{
    uint8_t tx_buf[16];
    int tx_len = 0;
    
    /* 构建帧: 55 55 ID LEN CMD [PARAMS] CHECKSUM */
    tx_buf[tx_len++] = 0x55;
    tx_buf[tx_len++] = 0x55;
    tx_buf[tx_len++] = id;
    tx_buf[tx_len++] = param_len + 3;  /* LEN = param_len + 3 */
    tx_buf[tx_len++] = cmd;
    
    for (int i = 0; i < param_len; i++) {
        tx_buf[tx_len++] = params[i];
    }
    
    /* 计算校验和 */
    uint8_t sum = 0;
    for (int i = 2; i < tx_len; i++) {
        sum += tx_buf[i];
    }
    tx_buf[tx_len++] = ~sum;
    
    /* 清空 RX FIFO */
    while (SERVO_UART->USR & 0x08u) {
        (void)SERVO_UART->RBR_THR_DLL;
    }
    
    /* 设置发送模式 */
    gpio_set_data(GPIOA, DIR_PIN, 1);
    
    /* 发送数据 */
    for (int i = 0; i < tx_len; i++) {
        send_byte(tx_buf[i]);
    }
    
    /* 等待发送完成 */
    wait_tx_done();
    
    /* 50us 延时等待 MOS 开关稳定 */
    delay_us(30);
    
    /* 切换到接收模式 */
    gpio_set_data(GPIOA, DIR_PIN, 0);
    
    /* 如果不需要接收，直接返回 */
    if (rx_buf == NULL || rx_max == 0) {
        return 0;
    }
    
    /* 接收响应 */
    uint8_t raw_buf[32];
    int raw_len = recv_data(raw_buf, 24, 100, 5);
    
    /* 寻找同步头 */
    for (int i = 0; i < raw_len - 1; i++) {
        if (raw_buf[i] == 0x55 && raw_buf[i+1] == 0x55) {
            int valid_len = raw_len - i;
            if (valid_len > rx_max) valid_len = rx_max;
            memcpy(rx_buf, &raw_buf[i], valid_len);
            return valid_len;
        }
    }
    
    return 0;
}

/*===========================================================================
 * 舵机高级功能函数
 *===========================================================================*/

/**
 * @brief 设置舵机位置
 */
static void servo_move(uint8_t id, uint16_t position, uint16_t time_ms)
{
    uint8_t params[4];
    params[0] = position & 0xFF;
    params[1] = (position >> 8) & 0xFF;
    params[2] = time_ms & 0xFF;
    params[3] = (time_ms >> 8) & 0xFF;
    
    servo_cmd(id, SERVO_MOVE_TIME_WRITE, params, 4, NULL, 0);
    printf("[MOVE] ID=%d -> Pos=%d, Time=%dms\r\n", id, position, time_ms);
}

/**
 * @brief 读取舵机位置
 */
static int servo_read_position(uint8_t id, uint16_t *position)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_POS_READ, NULL, 0, rx_buf, 16);
    
    if (len >= 7 && rx_buf[4] == SERVO_POS_READ) {
        *position = rx_buf[5] | (rx_buf[6] << 8);
        return 0;
    }
    return -1;
}

/**
 * @brief 读取舵机温度
 */
static int servo_read_temp(uint8_t id, uint8_t *temp)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_TEMP_READ, NULL, 0, rx_buf, 16);
    
    if (len >= 6 && rx_buf[4] == SERVO_TEMP_READ) {
        *temp = rx_buf[5];
        return 0;
    }
    return -1;
}

/**
 * @brief 读取舵机电压
 */
static int servo_read_vin(uint8_t id, uint16_t *vin)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_VIN_READ, NULL, 0, rx_buf, 16);
    
    if (len >= 7 && rx_buf[4] == SERVO_VIN_READ) {
        *vin = rx_buf[5] | (rx_buf[6] << 8);
        return 0;
    }
    return -1;
}

/**
 * @brief 读取舵机偏差
 */
static int servo_read_deviation(uint8_t id, int8_t *deviation)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_ANGLE_OFFSET_READ, NULL, 0, rx_buf, 16);
    
    if (len >= 6 && rx_buf[4] == SERVO_ANGLE_OFFSET_READ) {
        *deviation = (int8_t)rx_buf[5];
        return 0;
    }
    return -1;
}

/**
 * @brief 读取舵机角度限制
 */
static int servo_read_angle_limit(uint8_t id, uint16_t *min_angle, uint16_t *max_angle)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_ANGLE_LIMIT_READ, NULL, 0, rx_buf, 16);
    
    if (len >= 9 && rx_buf[4] == SERVO_ANGLE_LIMIT_READ) {
        *min_angle = rx_buf[5] | (rx_buf[6] << 8);
        *max_angle = rx_buf[7] | (rx_buf[8] << 8);
        return 0;
    }
    return -1;
}

/**
 * @brief 读取舵机电压限制
 */
static int servo_read_vin_limit(uint8_t id, uint16_t *min_vin, uint16_t *max_vin)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_VIN_LIMIT_READ, NULL, 0, rx_buf, 16);
    
    if (len >= 9 && rx_buf[4] == SERVO_VIN_LIMIT_READ) {
        *min_vin = rx_buf[5] | (rx_buf[6] << 8);
        *max_vin = rx_buf[7] | (rx_buf[8] << 8);
        return 0;
    }
    return -1;
}

/**
 * @brief 读取舵机温度限制
 */
static int servo_read_temp_limit(uint8_t id, uint8_t *max_temp)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_TEMP_MAX_LIMIT_READ, NULL, 0, rx_buf, 16);
    
    if (len >= 6 && rx_buf[4] == SERVO_TEMP_MAX_LIMIT_READ) {
        *max_temp = rx_buf[5];
        return 0;
    }
    return -1;
}

/**
 * @brief 读取舵机加载状态
 */
static int servo_read_load(uint8_t id, uint8_t *load)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_LOAD_UNLOAD_READ, NULL, 0, rx_buf, 16);
    
    if (len >= 6 && rx_buf[4] == SERVO_LOAD_UNLOAD_READ) {
        *load = rx_buf[5];
        return 0;
    }
    return -1;
}

/**
 * @brief 设置舵机加载/卸载
 */
static void servo_set_load(uint8_t id, uint8_t load)
{
    uint8_t params[1] = { load };
    servo_cmd(id, SERVO_LOAD_UNLOAD_WRITE, params, 1, NULL, 0);
}

/**
 * @brief 停止舵机
 */
static void servo_stop(uint8_t id)
{
    servo_cmd(id, SERVO_MOVE_STOP, NULL, 0, NULL, 0);
}

/**
 * @brief 读取并打印舵机完整信息
 */
static void servo_print_info(uint8_t id)
{
    uint16_t pos, vin;
    uint8_t temp;
    int8_t deviation;
    uint16_t min_angle, max_angle;
    uint16_t min_vin, max_vin;
    uint8_t max_temp;
    uint8_t load;
    
    printf("\r\n====== Servo ID=%d Info ======\r\n", id);
    
    if (servo_read_position(id, &pos) == 0) {
        printf("  Position:    %d (%.1f deg)\r\n", pos, (pos - 500) * 0.24f);
    } else {
        printf("  Position:    [READ FAILED]\r\n");
    }
    
    if (servo_read_temp(id, &temp) == 0) {
        printf("  Temperature: %d C\r\n", temp);
    } else {
        printf("  Temperature: [READ FAILED]\r\n");
    }
    
    if (servo_read_vin(id, &vin) == 0) {
        printf("  Voltage:     %d mV (%.2f V)\r\n", vin, vin / 1000.0f);
    } else {
        printf("  Voltage:     [READ FAILED]\r\n");
    }
    
    if (servo_read_deviation(id, &deviation) == 0) {
        printf("  Deviation:   %d\r\n", deviation);
    } else {
        printf("  Deviation:   [READ FAILED]\r\n");
    }
    
    if (servo_read_angle_limit(id, &min_angle, &max_angle) == 0) {
        printf("  Angle Limit: %d ~ %d\r\n", min_angle, max_angle);
    } else {
        printf("  Angle Limit: [READ FAILED]\r\n");
    }
    
    if (servo_read_vin_limit(id, &min_vin, &max_vin) == 0) {
        printf("  Vin Limit:   %d ~ %d mV\r\n", min_vin, max_vin);
    } else {
        printf("  Vin Limit:   [READ FAILED]\r\n");
    }
    
    if (servo_read_temp_limit(id, &max_temp) == 0) {
        printf("  Temp Limit:  %d C\r\n", max_temp);
    } else {
        printf("  Temp Limit:  [READ FAILED]\r\n");
    }
    
    if (servo_read_load(id, &load) == 0) {
        printf("  Load:        %s\r\n", load ? "ENABLED" : "DISABLED");
    } else {
        printf("  Load:        [READ FAILED]\r\n");
    }
    
    printf("==============================\r\n\r\n");
}

/*===========================================================================
 * Main
 *===========================================================================*/

int main(void)
{
    /* 初始化 DWT 用于精确计时 (防止 GDB 调试时 DWT 无法计数) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    __DSB();  /* 确保写入完成 */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();  /* 确保写入完成 */
    __ISB();  /* 指令同步屏障 */
    
    SystemCoreClockUpdate();
    board_init();
    
    printf("\r\n\r\n");
    printf("########################################\r\n");
    printf("#  Bus Servo Complete Test            #\r\n");
    printf("#  UART3: TX=GPIO27, RX=GPIO26        #\r\n");
    printf("#  DIR:   GPIO22 (1=TX, 0=RX)         #\r\n");
    printf("########################################\r\n\r\n");
    
    /* 初始化舵机相关 GPIO 和 UART */
    printf("[Init] Initializing servo pins...\r\n");
    
    /* 开启 UART3 时钟 */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    
    /* 配置引脚 */
    set_gpio_function(GPIOA, 27, FUNCTION_3);  /* UART3 TX */
    set_gpio_function(GPIOA, 26, FUNCTION_3);  /* UART3 RX */
    set_gpio_function(GPIOA, DIR_PIN, FUNCTION_2);  /* GPIO */
    set_gpio_direction(GPIOA, DIR_PIN, 1);  /* Output */
    set_gpio_data(GPIOA, DIR_PIN, 1);  /* 默认发送模式 */
    
    /* 初始化 UART3 波特率 115200 */
    uint32_t apb_clk = rcc_get_clock(RCC_CLOCK_APB1);
    uart_init(UART_IDX3, UARTTYPE_STD_SERIAL, apb_clk, 115200);
    
    printf("[Init] UART3 baudrate=115200, APB_CLK=%lu\r\n", apb_clk);
    printf("[Init] GPIO22=DIR, GPIO27=TX, GPIO26=RX\r\n");
    
    /* 等待舵机上电 */
    printf("[Init] Waiting 1s for servo...\r\n");
    delay_ms(1000);
    
    /* 启用舵机加载 */
    printf("[Init] Enabling servo load...\r\n");
    servo_set_load(6, 1);  /* Yaw */
    delay_ms(10);
    servo_set_load(4, 1);  /* Pitch */
    delay_ms(10);
    printf("[Init] Servos loaded.\r\n");
    
    /* 读取所有舵机信息 */
    printf("\r\n===== Reading Servo Information =====\r\n");
    servo_print_info(6);  /* Yaw */
    servo_print_info(4);  /* Pitch */
    
    /* 测试循环 */
    int loop = 0;
    while (1) {
        loop++;
        printf("\r\n===== Loop %d =====\r\n", loop);
        
        /* 两个舵机同时移动到位置A */
        printf("\r\n[TEST] Moving to Position A (Yaw=500, Pitch=400)\r\n");
        servo_move(6, 500, 500);  /* Yaw 中间 */
        servo_move(4, 400, 500);  /* Pitch 稍低 */
        delay_ms(600);
        
        uint16_t pos6, pos4;
        if (servo_read_position(6, &pos6) == 0) {
            printf("[READ] ID=6 (Yaw),   POS=%d\r\n", pos6);
        }
        if (servo_read_position(4, &pos4) == 0) {
            printf("[READ] ID=4 (Pitch), POS=%d\r\n", pos4);
        }
        
        /* 两个舵机同时移动到位置B */
        printf("\r\n[TEST] Moving to Position B (Yaw=300, Pitch=500)\r\n");
        servo_move(6, 300, 500);  /* Yaw 左转 */
        servo_move(4, 500, 500);  /* Pitch 中间 */
        delay_ms(600);
        
        if (servo_read_position(6, &pos6) == 0) {
            printf("[READ] ID=6 (Yaw),   POS=%d\r\n", pos6);
        }
        if (servo_read_position(4, &pos4) == 0) {
            printf("[READ] ID=4 (Pitch), POS=%d\r\n", pos4);
        }
        
        /* 两个舵机同时移动到位置C */
        printf("\r\n[TEST] Moving to Position C (Yaw=700, Pitch=300)\r\n");
        servo_move(6, 700, 500);  /* Yaw 右转 */
        servo_move(4, 300, 500);  /* Pitch 稍高 */
        delay_ms(600);
        
        if (servo_read_position(6, &pos6) == 0) {
            printf("[READ] ID=6 (Yaw),   POS=%d\r\n", pos6);
        }
        if (servo_read_position(4, &pos4) == 0) {
            printf("[READ] ID=4 (Pitch), POS=%d\r\n", pos4);
        }
        
        /* 每 5 轮打印一次完整信息 */
        if (loop % 5 == 0) {
            printf("\r\n----- Full Status -----\r\n");
            servo_print_info(6);
            servo_print_info(4);
        }
        
        printf("[Loop %d done]\r\n", loop);
        delay_ms(2000);
    }
    
    return 0;
}
