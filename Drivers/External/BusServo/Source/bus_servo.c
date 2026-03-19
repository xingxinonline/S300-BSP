/**
 * @file    bus_servo.c
 * @brief   总线舵机驱动实现 (Hiwonder/幻尔科技 协议)
 * @details 实现半双工 UART 通信的总线舵机控制
 */

#include "bus_servo.h"
#include "uart.h"
#include "uart_s300.h"
#include "gpio.h"
#include "rcc.h"
#include <string.h>
#include <stdio.h>

/*===========================================================================
 * Debug Configuration
 *===========================================================================*/

/** @brief 启用调试输出 (可通过 CMake 编译定义覆盖) */
#ifndef BUS_SERVO_DEBUG
#define BUS_SERVO_DEBUG         0
#endif

#if BUS_SERVO_DEBUG
#define DBG_PRINT(...)          printf(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

/*===========================================================================
 * Private Definitions
 *===========================================================================*/

/** @brief 发送模式 (MOTO_BUSEN = HIGH) */
#define BUS_SERVO_DIR_TX        1

/** @brief 接收模式 (MOTO_BUSEN = LOW) */
#define BUS_SERVO_DIR_RX        0

/** @brief 帧各部分偏移 */
#define FRAME_OFF_HEAD0         0
#define FRAME_OFF_HEAD1         1
#define FRAME_OFF_ID            2
#define FRAME_OFF_LEN           3
#define FRAME_OFF_CMD           4
#define FRAME_OFF_PARAM         5

/** @brief 发完最后一位后，切到 RX 前的保护时间 (微秒) */
#define TX_RX_PRE_SWITCH_DELAY_US   5

/** @brief 切到 RX 后，等待模拟开关和总线稳定的时间 (微秒) */
#define TX_RX_POST_SWITCH_DELAY_US  25

/** @brief 接收首字节超时 (毫秒) */
#define RX_FIRST_BYTE_TIMEOUT_MS  100

/** @brief 接收后续字节超时 (毫秒) */
#define RX_BYTE_TIMEOUT_MS        5

/*===========================================================================
 * Private Functions
 *===========================================================================*/

/**
 * @brief  微秒延时 (使用 DWT 或循环后备)
 * @note   DWT 在 GDB 调试时可能被禁用
 */
static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    __NOP(); __NOP(); __NOP(); __NOP();
    if (DWT->CYCCNT != start) {
        /* DWT 正常工作 */
        uint32_t cycles = us * (SystemCoreClock / 1000000);
        while ((DWT->CYCCNT - start) < cycles);
    } else {
        /* DWT 被禁用，使用循环延时 */
        volatile uint32_t cnt = us * (SystemCoreClock / 5000000);
        while (cnt--) {
            __NOP();
        }
    }
}

/**
 * @brief  计算校验和并填充到数据包
 * @param  packet   数据包缓冲区
 * @return 数据包总长度
 */
static uint8_t calc_checksum(uint8_t *packet)
{
    uint8_t len, i;
    uint8_t sum = 0;
    
    if (packet[FRAME_OFF_HEAD0] != BUS_SERVO_FRAME_HEADER ||
        packet[FRAME_OFF_HEAD1] != BUS_SERVO_FRAME_HEADER) {
        return 0;
    }
    
    len = packet[FRAME_OFF_LEN]; /* 数据长度 (包含自身) */
    uint8_t total_len = len + 3; /* 帧头(2) + ID(1) + 数据部分 */
    
    /* 校验和计算: ID + Length + Cmd + Params */
    for (i = FRAME_OFF_ID; i < total_len - 1; i++) {
        sum += packet[i];
    }
    
    packet[total_len - 1] = ~sum; /* 取反 */
    return total_len;
}

/**
 * @brief  验证接收到的数据包校验和
 * @param  packet   数据包缓冲区
 * @param  len      数据包长度
 * @return true=校验正确, false=校验错误
 */
static bool verify_checksum(const uint8_t *packet, uint8_t len)
{
    uint8_t sum = 0;
    
    if (len < 4) return false;
    if (packet[FRAME_OFF_HEAD0] != BUS_SERVO_FRAME_HEADER ||
        packet[FRAME_OFF_HEAD1] != BUS_SERVO_FRAME_HEADER) {
        return false;
    }
    
    /* 计算校验和 */
    for (uint8_t i = FRAME_OFF_ID; i < len - 1; i++) {
        sum += packet[i];
    }
    
    return (packet[len - 1] == (uint8_t)(~sum));
}

/**
 * @brief  获取 UART 寄存器指针
 */
static S300_UART_TypeDef* get_uart_dev(uint8_t uart_idx)
{
    switch (uart_idx) {
        case 0: return UART0;
        case 1: return UART1;
        case 2: return UART2;
        default: return UART3;
    }
}

/**
 * @brief  设置为发送模式
 */
static void set_tx_mode(bus_servo_t *servo)
{
    gpio_set_data(GPIOA, servo->dir_pin, BUS_SERVO_DIR_TX);
}

/**
 * @brief  等待 UART 发送完成 (FIFO空 + 移位寄存器空闲)
 */
static void wait_tx_complete(bus_servo_t *servo)
{
    S300_UART_TypeDef *U = get_uart_dev(servo->uart_idx);
    
    /* USR bit2: TFE (TX FIFO Empty) */
    while (!(U->USR & 0x04u));
    
    /* USR bit0: BUSY (UART忙, 移位寄存器正在发送) */
    while (U->USR & 0x01u);
}

/**
 * @brief  清空 RX FIFO (丢弃半双工回响)
 */
static void flush_rx_fifo(bus_servo_t *servo)
{
    S300_UART_TypeDef *U = get_uart_dev(servo->uart_idx);
    while (U->USR & 0x08u) {
        (void)U->RBR_THR_DLL;
    }
}

/**
 * @brief  设置为接收模式
 * @note   参考测试程序验证的时序
 */
static void set_rx_mode(bus_servo_t *servo)
{
    /* 等待发送完成 */
    wait_tx_complete(servo);

    /* 给最后一位和线端留一个很小的保护时间，避免切换过早 */
    delay_us(TX_RX_PRE_SWITCH_DELAY_US);

    /* 尽快切换到接收模式，避免错过舵机紧跟写包后的快速响应 */
    gpio_set_data(GPIOA, servo->dir_pin, BUS_SERVO_DIR_RX);

    /* 给模拟开关和总线方向一点稳定时间 */
    delay_us(TX_RX_POST_SWITCH_DELAY_US);
}

/**
 * @brief  发送数据包 (仅发送，不等待响应)
 */
static int send_packet(bus_servo_t *servo, uint8_t *packet)
{
    uint8_t len = calc_checksum(packet);
    if (len == 0) return -1;
    
    set_tx_mode(servo);
    
    for (uint8_t i = 0; i < len; i++) {
        uart_write((uart_idx_t)servo->uart_idx, UARTTYPE_STD_SERIAL, packet[i]);
    }
    
    /* 等待发送完成 */
    wait_tx_complete(servo);
    
    return 0;
}

/**
 * @brief  带超时的读取单个字节
 * @param  timeout_ms  超时时间(毫秒)
 * @return 读取的字节，-1 表示超时
 */
static int read_byte_timeout(bus_servo_t *servo, uint32_t timeout_ms)
{
    S300_UART_TypeDef *U = get_uart_dev(servo->uart_idx);
    
    /* 使用毫秒级超时，精度更高 */
    for (uint32_t i = 0; i < timeout_ms; i++) {
        for (uint32_t j = 0; j < 10; j++) { /* 每毫秒检查10次 */
            if (U->USR & 0x8u) { /* RX FIFO 非空 (RFNE) */
                return (int)(U->RBR_THR_DLL & 0xFF);
            }
            delay_us(100);
        }
    }
    
    return -1; /* 超时 */
}

/**
 * @brief  发送数据包并接收响应 (使用同步头搜索)
 * @note   经测试验证，使用同步头搜索可处理半双工回响和噪音
 */
static int send_recv_packet(bus_servo_t *servo, uint8_t *tx_packet, 
                             uint8_t *rx_packet, uint8_t *rx_len)
{
    uint8_t tx_len = calc_checksum(tx_packet);
    if (tx_len == 0) return -1;
    
    /* 调试: 打印发送数据 */
    DBG_PRINT("[TX] ");
    for (uint8_t i = 0; i < tx_len; i++) {
        DBG_PRINT("%02X ", tx_packet[i]);
    }
    DBG_PRINT("\r\n");
    
    /* 清空 RX FIFO (丢弃可能的半双工回响) */
    flush_rx_fifo(servo);
    
    /* 发送 */
    set_tx_mode(servo);
    for (uint8_t i = 0; i < tx_len; i++) {
        uart_write((uart_idx_t)servo->uart_idx, UARTTYPE_STD_SERIAL, tx_packet[i]);
    }
    
    /* 切换到接收模式 */
    set_rx_mode(servo);
    
    /* 搜索同步头 0x55 0x55 */
    int b0 = -1, b1 = -1;
    for (int retry = 0; retry < 32; retry++) {
        /* 第一次使用较长超时 */
        int b = read_byte_timeout(servo, (retry == 0) ? RX_FIRST_BYTE_TIMEOUT_MS : RX_BYTE_TIMEOUT_MS);
        if (b < 0) {
            DBG_PRINT("[RX] Timeout searching sync header\r\n");
            return -2;
        }
        
        if (b == 0x55) {
            if (b0 == 0x55) {
                /* 找到 0x55 0x55 */
                b1 = b;
                break;
            }
            b0 = b;
        } else {
            b0 = -1;
        }
    }
    
    if (b0 != 0x55 || b1 != 0x55) {
        DBG_PRINT("[RX] Sync header not found\r\n");
        return -2;
    }
    
    rx_packet[0] = 0x55;
    rx_packet[1] = 0x55;
    
    /* 接收 ID */
    int id_byte = read_byte_timeout(servo, RX_BYTE_TIMEOUT_MS);
    if (id_byte < 0) {
        DBG_PRINT("[RX] Timeout at ID\r\n");
        return -2;
    }
    rx_packet[2] = (uint8_t)id_byte;
    
    /* 接收长度 */
    int len_byte = read_byte_timeout(servo, RX_BYTE_TIMEOUT_MS);
    if (len_byte < 0) {
        DBG_PRINT("[RX] Timeout at LEN\r\n");
        return -2;
    }
    rx_packet[3] = (uint8_t)len_byte;
    
    uint8_t data_len = (uint8_t)len_byte;
    uint8_t total_len = data_len + 3;
    
    if (total_len >= 16) {
        DBG_PRINT("[RX] Bad LEN=%d\r\n", data_len);
        return -3; /* 长度异常 */
    }
    
    /* 接收剩余数据 */
    for (uint8_t i = 4; i < total_len; i++) {
        int b = read_byte_timeout(servo, RX_BYTE_TIMEOUT_MS);
        if (b < 0) {
            DBG_PRINT("[RX] Timeout at byte %d\r\n", i);
            return -2;
        }
        rx_packet[i] = (uint8_t)b;
    }
    
    /* 调试: 打印接收数据 */
    DBG_PRINT("[RX] ");
    for (uint8_t i = 0; i < total_len; i++) {
        DBG_PRINT("%02X ", rx_packet[i]);
    }
    DBG_PRINT("\r\n");
    
    /* 验证校验和 */
    if (!verify_checksum(rx_packet, total_len)) {
        DBG_PRINT("[RX] Checksum failed!\r\n");
        return -4;
    }
    
    *rx_len = total_len;
    return 0;
}

/**
 * @brief  构建基本命令包
 * @note   长度字段 = cmd(1) + params + checksum(1) + 自身(1) = param_len + 3
 */
static void build_packet(uint8_t *packet, uint8_t id, uint8_t cmd, uint8_t param_len)
{
    packet[FRAME_OFF_HEAD0] = BUS_SERVO_FRAME_HEADER;
    packet[FRAME_OFF_HEAD1] = BUS_SERVO_FRAME_HEADER;
    packet[FRAME_OFF_ID]    = id;
    packet[FRAME_OFF_LEN]   = param_len + 3; /* cmd(1) + params + checksum(1) + 自身(1) */
    packet[FRAME_OFF_CMD]   = cmd;
}

/*===========================================================================
 * Public Functions
 *===========================================================================*/

int bus_servo_init(bus_servo_t *servo, uint8_t uart_idx, uint8_t dir_pin, uint32_t sysclk_hz)
{
    if (servo == NULL) return -1;
    
    servo->uart_idx = uart_idx;
    servo->dir_pin = dir_pin;
    servo->dir_port = GPIOA;
    servo->rx_timeout_us = RX_FIRST_BYTE_TIMEOUT_MS * 1000; /* 兼容旧代码 */
    
    /* 方向控制引脚由 board 层初始化，这里只确保处于发送模式 */
    gpio_set_data(GPIOA, dir_pin, BUS_SERVO_DIR_TX);
    
    /* 初始化 UART */
    uart_init((uart_idx_t)uart_idx, UARTTYPE_STD_SERIAL, sysclk_hz, BUS_SERVO_BAUDRATE);
    
    return 0;
}

void bus_servo_deinit(bus_servo_t *servo)
{
    if (servo == NULL) return;
    gpio_set_data(GPIOA, servo->dir_pin, BUS_SERVO_DIR_RX);
}

/*---------------------------------------------------------------------------
 * 运动控制
 *---------------------------------------------------------------------------*/

int bus_servo_move(bus_servo_t *servo, uint8_t id, float angle, uint16_t time_ms)
{
    uint16_t pos = bus_servo_angle_to_pos(angle);
    return bus_servo_move_raw(servo, id, pos, time_ms);
}

int bus_servo_move_raw(bus_servo_t *servo, uint8_t id, uint16_t position, uint16_t time_ms)
{
    uint8_t packet[10];
    
    if (position > BUS_SERVO_ANGLE_MAX) position = BUS_SERVO_ANGLE_MAX;
    if (time_ms > BUS_SERVO_TIME_MAX) time_ms = BUS_SERVO_TIME_MAX;
    
    build_packet(packet, id, BUS_SERVO_CMD_MOVE_TIME_WRITE, 4);
    packet[FRAME_OFF_PARAM + 0] = position & 0xFF;
    packet[FRAME_OFF_PARAM + 1] = (position >> 8) & 0xFF;
    packet[FRAME_OFF_PARAM + 2] = time_ms & 0xFF;
    packet[FRAME_OFF_PARAM + 3] = (time_ms >> 8) & 0xFF;
    
    return send_packet(servo, packet);
}

int bus_servo_move_prepare(bus_servo_t *servo, uint8_t id, float angle, uint16_t time_ms)
{
    uint8_t packet[10];
    uint16_t pos = bus_servo_angle_to_pos(angle);
    
    if (time_ms > BUS_SERVO_TIME_MAX) time_ms = BUS_SERVO_TIME_MAX;
    
    build_packet(packet, id, BUS_SERVO_CMD_MOVE_TIME_WAIT_WRITE, 4);
    packet[FRAME_OFF_PARAM + 0] = pos & 0xFF;
    packet[FRAME_OFF_PARAM + 1] = (pos >> 8) & 0xFF;
    packet[FRAME_OFF_PARAM + 2] = time_ms & 0xFF;
    packet[FRAME_OFF_PARAM + 3] = (time_ms >> 8) & 0xFF;
    
    return send_packet(servo, packet);
}

int bus_servo_move_start(bus_servo_t *servo, uint8_t id)
{
    uint8_t packet[6];
    build_packet(packet, id, BUS_SERVO_CMD_MOVE_START, 0);
    return send_packet(servo, packet);
}

int bus_servo_stop(bus_servo_t *servo, uint8_t id)
{
    uint8_t packet[6];
    build_packet(packet, id, BUS_SERVO_CMD_MOVE_STOP, 0);
    return send_packet(servo, packet);
}

/*---------------------------------------------------------------------------
 * 状态查询
 *---------------------------------------------------------------------------*/

int bus_servo_read_angle(bus_servo_t *servo, uint8_t id, float *angle)
{
    int16_t pos;
    int ret = bus_servo_read_position(servo, id, &pos);
    if (ret == 0 && angle != NULL) {
        *angle = bus_servo_pos_to_angle(pos);
    }
    return ret;
}

int bus_servo_read_position(bus_servo_t *servo, uint8_t id, int16_t *position)
{
    uint8_t tx_packet[6];
    uint8_t rx_packet[BUS_SERVO_MAX_PACKET_LEN];
    uint8_t rx_len;
    
    build_packet(tx_packet, id, BUS_SERVO_CMD_POS_READ, 0);
    
    int ret = send_recv_packet(servo, tx_packet, rx_packet, &rx_len);
    if (ret != 0) return ret;
    
    if (rx_packet[FRAME_OFF_LEN] != 5) return -5;
    
    *position = (int16_t)(rx_packet[FRAME_OFF_PARAM] | 
                          (rx_packet[FRAME_OFF_PARAM + 1] << 8));
    return 0;
}

int bus_servo_read_vin(bus_servo_t *servo, uint8_t id, uint16_t *vin_mv)
{
    uint8_t tx_packet[6];
    uint8_t rx_packet[BUS_SERVO_MAX_PACKET_LEN];
    uint8_t rx_len;
    
    build_packet(tx_packet, id, BUS_SERVO_CMD_VIN_READ, 0);
    
    int ret = send_recv_packet(servo, tx_packet, rx_packet, &rx_len);
    if (ret != 0) return ret;
    
    if (rx_packet[FRAME_OFF_LEN] != 5) return -5;
    
    *vin_mv = rx_packet[FRAME_OFF_PARAM] | 
              (rx_packet[FRAME_OFF_PARAM + 1] << 8);
    return 0;
}

int bus_servo_read_temp(bus_servo_t *servo, uint8_t id, uint8_t *temp)
{
    uint8_t tx_packet[6];
    uint8_t rx_packet[BUS_SERVO_MAX_PACKET_LEN];
    uint8_t rx_len;
    
    build_packet(tx_packet, id, BUS_SERVO_CMD_TEMP_READ, 0);
    
    int ret = send_recv_packet(servo, tx_packet, rx_packet, &rx_len);
    if (ret != 0) return ret;
    
    if (rx_packet[FRAME_OFF_LEN] != 4) return -5;
    
    *temp = rx_packet[FRAME_OFF_PARAM];
    return 0;
}

int bus_servo_read_load(bus_servo_t *servo, uint8_t id, uint8_t *load)
{
    uint8_t tx_packet[6];
    uint8_t rx_packet[BUS_SERVO_MAX_PACKET_LEN];
    uint8_t rx_len;
    
    build_packet(tx_packet, id, BUS_SERVO_CMD_LOAD_READ, 0);
    
    int ret = send_recv_packet(servo, tx_packet, rx_packet, &rx_len);
    if (ret != 0) return ret;
    
    if (rx_packet[FRAME_OFF_LEN] != 4) return -5;
    
    *load = rx_packet[FRAME_OFF_PARAM];
    return 0;
}

int bus_servo_read_id(bus_servo_t *servo, uint8_t *id)
{
    uint8_t tx_packet[6];
    uint8_t rx_packet[BUS_SERVO_MAX_PACKET_LEN];
    uint8_t rx_len;
    
    /* 使用广播 ID 查询 */
    build_packet(tx_packet, BUS_SERVO_BROADCAST_ID, BUS_SERVO_CMD_ID_READ, 0);
    
    int ret = send_recv_packet(servo, tx_packet, rx_packet, &rx_len);
    if (ret != 0) return ret;
    
    if (rx_packet[FRAME_OFF_LEN] != 4) return -5;
    
    *id = rx_packet[FRAME_OFF_PARAM];
    return 0;
}

int bus_servo_read_status(bus_servo_t *servo, uint8_t id, bus_servo_status_t *status)
{
    int ret;
    
    if (status == NULL) return -1;
    memset(status, 0, sizeof(bus_servo_status_t));
    status->id = id;
    
    /* 读取位置 */
    ret = bus_servo_read_position(servo, id, &status->position);
    if (ret != 0) return ret;
    status->angle = bus_servo_pos_to_angle(status->position);
    
    /* 读取电压 */
    ret = bus_servo_read_vin(servo, id, &status->vin_mv);
    if (ret != 0) return ret;
    
    /* 读取温度 */
    ret = bus_servo_read_temp(servo, id, &status->temperature);
    if (ret != 0) return ret;
    
    /* 读取加载状态 */
    uint8_t tx_packet[6];
    uint8_t rx_packet[BUS_SERVO_MAX_PACKET_LEN];
    uint8_t rx_len;
    
    build_packet(tx_packet, id, BUS_SERVO_CMD_LOAD_READ, 0);
    ret = send_recv_packet(servo, tx_packet, rx_packet, &rx_len);
    if (ret == 0 && rx_packet[FRAME_OFF_LEN] == 4) {
        status->loaded = (rx_packet[FRAME_OFF_PARAM] == 1);
    }
    
    /* 读取模式 */
    build_packet(tx_packet, id, BUS_SERVO_CMD_MODE_READ, 0);
    ret = send_recv_packet(servo, tx_packet, rx_packet, &rx_len);
    if (ret == 0 && rx_packet[FRAME_OFF_LEN] == 7) {
        status->mode = rx_packet[FRAME_OFF_PARAM];
    }
    
    return 0;
}

/*---------------------------------------------------------------------------
 * 配置设置
 *---------------------------------------------------------------------------*/

int bus_servo_set_id(bus_servo_t *servo, uint8_t old_id, uint8_t new_id)
{
    uint8_t packet[7];
    
    if (new_id > BUS_SERVO_MAX_ID) return -1;
    
    build_packet(packet, old_id, BUS_SERVO_CMD_ID_WRITE, 1);
    packet[FRAME_OFF_PARAM] = new_id;
    
    return send_packet(servo, packet);
}

int bus_servo_set_load(bus_servo_t *servo, uint8_t id, bool load)
{
    uint8_t packet[7];
    
    build_packet(packet, id, BUS_SERVO_CMD_LOAD_WRITE, 1);
    packet[FRAME_OFF_PARAM] = load ? 1 : 0;
    
    return send_packet(servo, packet);
}

int bus_servo_set_mode(bus_servo_t *servo, uint8_t id, bus_servo_mode_t mode,
                       bus_servo_motor_mode_t motor_mode, int16_t speed)
{
    uint8_t packet[10];
    
    build_packet(packet, id, BUS_SERVO_CMD_MODE_WRITE, 4);
    packet[FRAME_OFF_PARAM + 0] = (uint8_t)mode;
    packet[FRAME_OFF_PARAM + 1] = (uint8_t)motor_mode;
    packet[FRAME_OFF_PARAM + 2] = (uint8_t)(speed & 0xFF);
    packet[FRAME_OFF_PARAM + 3] = (uint8_t)((speed >> 8) & 0xFF);
    
    return send_packet(servo, packet);
}

int bus_servo_adjust_offset(bus_servo_t *servo, uint8_t id, int8_t offset)
{
    uint8_t packet[7];
    
    if (offset < BUS_SERVO_OFFSET_MIN) offset = BUS_SERVO_OFFSET_MIN;
    if (offset > BUS_SERVO_OFFSET_MAX) offset = BUS_SERVO_OFFSET_MAX;
    
    build_packet(packet, id, BUS_SERVO_CMD_ANGLE_OFFSET_ADJUST, 1);
    packet[FRAME_OFF_PARAM] = (uint8_t)offset;
    
    return send_packet(servo, packet);
}

int bus_servo_save_offset(bus_servo_t *servo, uint8_t id)
{
    uint8_t packet[6];
    build_packet(packet, id, BUS_SERVO_CMD_ANGLE_OFFSET_WRITE, 0);
    return send_packet(servo, packet);
}

int bus_servo_set_angle_limit(bus_servo_t *servo, uint8_t id, float min_angle, float max_angle)
{
    uint8_t packet[10];
    uint16_t min_pos = bus_servo_angle_to_pos(min_angle);
    uint16_t max_pos = bus_servo_angle_to_pos(max_angle);
    
    if (min_pos >= max_pos) return -1;
    
    build_packet(packet, id, BUS_SERVO_CMD_ANGLE_LIMIT_WRITE, 4);
    packet[FRAME_OFF_PARAM + 0] = min_pos & 0xFF;
    packet[FRAME_OFF_PARAM + 1] = (min_pos >> 8) & 0xFF;
    packet[FRAME_OFF_PARAM + 2] = max_pos & 0xFF;
    packet[FRAME_OFF_PARAM + 3] = (max_pos >> 8) & 0xFF;
    
    return send_packet(servo, packet);
}

int bus_servo_set_led(bus_servo_t *servo, uint8_t id, bool off)
{
    uint8_t packet[7];
    
    build_packet(packet, id, BUS_SERVO_CMD_LED_CTRL_WRITE, 1);
    packet[FRAME_OFF_PARAM] = off ? 1 : 0;
    
    return send_packet(servo, packet);
}

int bus_servo_set_led_error(bus_servo_t *servo, uint8_t id, uint8_t error_mask)
{
    uint8_t packet[7];
    
    if (error_mask > 7) error_mask = 7;
    
    build_packet(packet, id, BUS_SERVO_CMD_LED_ERROR_WRITE, 1);
    packet[FRAME_OFF_PARAM] = error_mask;
    
    return send_packet(servo, packet);
}
