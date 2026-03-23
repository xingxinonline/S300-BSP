/**
 * @file    main_sdk_readonly.c
 * @brief   SDK 风格机械臂舵机只读测试
 * @details 参考 .vscode/gimbal_demo_20260122_161807/cortex-m4/app/main.c
 *          与 robot_arm.c 的初始化方式：
 *          - UART3: GPIO27(TX), GPIO26(RX)
 *          - BUSEN: GPIO22
 *          - SDK 默认双轴: X=ID6, Y=ID3
 *          本程序不发送任何运动命令，只做状态读取。
 */

#include "s300.h"
#include "board.h"
#include "uart.h"
#include "uart_s300.h"
#include "gpio.h"
#include "rcc.h"

#include <stdio.h>
#include <string.h>

#define SDK_ARM_X_SERVO_ID            6u
#define SDK_ARM_Y_SERVO_ID            3u
#define DIR_PIN                       22u
#define SDK_SERVO_UART                UART3
#define SDK_SCAN_INTERVAL_MS          1000u

#define WRITE_COMMAND_PIN             1u
#define READ_COMMAND_PIN              0u

#define SERVO_ANGLE_OFFSET_READ       19u
#define SERVO_TEMP_READ               26u
#define SERVO_VIN_READ                27u
#define SERVO_POS_READ                28u
#define SERVO_LOAD_UNLOAD_WRITE       31u
#define SERVO_LOAD_UNLOAD_READ        32u

static void delay_ms(uint32_t ms)
{
    uint32_t reload = SystemCoreClock / 1000u - 1u;

    if (reload > 0xFFFFFFu) {
        reload = 0xFFFFFFu;
    }

    SysTick->LOAD = reload;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    for (uint32_t i = 0u; i < ms; i++) {
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0u) {
        }
    }

    SysTick->CTRL = 0u;
}

static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;

    __NOP();
    __NOP();
    __NOP();
    __NOP();

    if (DWT->CYCCNT != start) {
        uint32_t cycles = us * (SystemCoreClock / 1000000u);

        while ((DWT->CYCCNT - start) < cycles) {
        }
        return;
    }

    volatile uint32_t count = us * (SystemCoreClock / 5000000u);
    while (count-- > 0u) {
        __NOP();
    }
}

static int uart_read_with_timeout_us(uint32_t timeout_us)
{
    while (timeout_us-- > 0u) {
        if ((SDK_SERVO_UART->USR & 0x08u) != 0u) {
            return (int)(SDK_SERVO_UART->RBR_THR_DLL & 0xFFu);
        }
        delay_us(1u);
    }

    return -1;
}

static uint8_t sdk_calc_checksum_and_length(uint8_t *data_buffer)
{
    uint8_t cmd_len;
    uint8_t index;
    uint8_t checksum = 0u;

    if (data_buffer[0] != 0x55u || data_buffer[1] != 0x55u) {
        return 0u;
    }

    cmd_len = (uint8_t)(data_buffer[3] + 3u);
    for (index = 2u; index < cmd_len - 1u; index++) {
        checksum = (uint8_t)(checksum + data_buffer[index]);
    }
    data_buffer[index] = (uint8_t)(~checksum);
    return cmd_len;
}

static void sdk_set_write_mode(void)
{
    set_gpio_data(GPIOA, DIR_PIN, WRITE_COMMAND_PIN);
}

static void sdk_set_read_mode(void)
{
    while ((SDK_SERVO_UART->USR & 0x01u) != 0u) {
    }

    /* 与 SDK servo_hiwonder.c 保持一致，切读前等待 120us。 */
    delay_us(120u);
    set_gpio_data(GPIOA, DIR_PIN, READ_COMMAND_PIN);
}

static void sdk_write_command(uint8_t *command_buffer)
{
    uint32_t length = sdk_calc_checksum_and_length(command_buffer);

    sdk_set_write_mode();
    for (uint32_t index = 0u; index < length; index++) {
        (void)write_uart(UART_IDX3, UARTTYPE_STD_SERIAL, command_buffer[index]);
    }
}

static int sdk_read_command(uint8_t *command_buffer, uint8_t *out_buffer, int out_capacity)
{
    uint32_t length;
    int value;

    if (out_buffer == NULL || out_capacity <= 0) {
        return 0;
    }

    length = sdk_calc_checksum_and_length(command_buffer);
    sdk_set_write_mode();
    for (uint32_t index = 0u; index < length; index++) {
        (void)write_uart(UART_IDX3, UARTTYPE_STD_SERIAL, command_buffer[index]);
    }
    sdk_set_read_mode();

    value = uart_read_with_timeout_us(10000u);
    if (value < 0) {
        return 0;
    }
    out_buffer[0] = (uint8_t)value;

    value = uart_read_with_timeout_us(1000u);
    if (value < 0) {
        return 0;
    }
    out_buffer[1] = (uint8_t)value;

    value = uart_read_with_timeout_us(1000u);
    if (value < 0) {
        return 0;
    }
    out_buffer[2] = (uint8_t)value;

    value = uart_read_with_timeout_us(1000u);
    if (value < 0) {
        return 0;
    }
    out_buffer[3] = (uint8_t)value;

    length = (uint32_t)out_buffer[3] + 3u;
    if ((int)length >= out_capacity) {
        return 0;
    }

    for (uint32_t index = 4u; index < length; index++) {
        value = uart_read_with_timeout_us(1000u);
        if (value < 0) {
            return 0;
        }
        out_buffer[index] = (uint8_t)value;
    }

    return (int)length;
}

static int servo_cmd(uint8_t id,
                     uint8_t cmd,
                     const uint8_t *params,
                     int param_len,
                     uint8_t *rx_buf,
                     int rx_max)
{
    uint8_t tx_buf[16];
    int tx_len = 0;

    tx_buf[tx_len++] = 0x55u;
    tx_buf[tx_len++] = 0x55u;
    tx_buf[tx_len++] = id;
    tx_buf[tx_len++] = (uint8_t)(param_len + 3);
    tx_buf[tx_len++] = cmd;

    for (int index = 0; index < param_len; index++) {
        tx_buf[tx_len++] = params[index];
    }

    if (rx_buf == NULL || rx_max == 0) {
        sdk_write_command(tx_buf);
        return 0;
    }

    return sdk_read_command(tx_buf, rx_buf, rx_max);
}

static void servo_set_load(uint8_t id, uint8_t load)
{
    uint8_t params[1] = { load };
    (void)servo_cmd(id, SERVO_LOAD_UNLOAD_WRITE, params, 1, NULL, 0);
}

static int servo_read_position(uint8_t id, uint16_t *position)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_POS_READ, NULL, 0, rx_buf, (int)sizeof(rx_buf));

    if (len >= 7 && rx_buf[4] == SERVO_POS_READ) {
        *position = (uint16_t)(rx_buf[5] | (rx_buf[6] << 8));
        return 0;
    }
    return -1;
}

static int servo_read_temp(uint8_t id, uint8_t *temp)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_TEMP_READ, NULL, 0, rx_buf, (int)sizeof(rx_buf));

    if (len >= 6 && rx_buf[4] == SERVO_TEMP_READ) {
        *temp = rx_buf[5];
        return 0;
    }
    return -1;
}

static int servo_read_vin(uint8_t id, uint16_t *vin)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_VIN_READ, NULL, 0, rx_buf, (int)sizeof(rx_buf));

    if (len >= 7 && rx_buf[4] == SERVO_VIN_READ) {
        *vin = (uint16_t)(rx_buf[5] | (rx_buf[6] << 8));
        return 0;
    }
    return -1;
}

static int servo_read_deviation(uint8_t id, int8_t *deviation)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_ANGLE_OFFSET_READ, NULL, 0, rx_buf, (int)sizeof(rx_buf));

    if (len >= 6 && rx_buf[4] == SERVO_ANGLE_OFFSET_READ) {
        *deviation = (int8_t)rx_buf[5];
        return 0;
    }
    return -1;
}

static int servo_read_load(uint8_t id, uint8_t *load)
{
    uint8_t rx_buf[16];
    int len = servo_cmd(id, SERVO_LOAD_UNLOAD_READ, NULL, 0, rx_buf, (int)sizeof(rx_buf));

    if (len >= 6 && rx_buf[4] == SERVO_LOAD_UNLOAD_READ) {
        *load = rx_buf[5];
        return 0;
    }
    return -1;
}

static void servo_print_info(uint8_t id)
{
    uint16_t position = 0u;
    uint16_t vin_mv = 0u;
    uint8_t temp = 0u;
    uint8_t load = 0u;
    int8_t deviation = 0;

    printf("\r\n====== SDK Servo ID=%u Info ======\r\n", (unsigned)id);

    if (servo_read_position(id, &position) == 0) {
        printf("  Position:    %u\r\n", (unsigned)position);
    } else {
        printf("  Position:    [READ FAILED]\r\n");
    }

    if (servo_read_temp(id, &temp) == 0) {
        printf("  Temperature: %u C\r\n", (unsigned)temp);
    } else {
        printf("  Temperature: [READ FAILED]\r\n");
    }

    if (servo_read_vin(id, &vin_mv) == 0) {
        printf("  Voltage:     %u mV (%.2f V)\r\n", (unsigned)vin_mv, vin_mv / 1000.0f);
    } else {
        printf("  Voltage:     [READ FAILED]\r\n");
    }

    if (servo_read_deviation(id, &deviation) == 0) {
        printf("  Deviation:   %d\r\n", (int)deviation);
    } else {
        printf("  Deviation:   [READ FAILED]\r\n");
    }

    if (servo_read_load(id, &load) == 0) {
        printf("  Load:        %s\r\n", load ? "ENABLED" : "DISABLED");
    } else {
        printf("  Load:        [READ FAILED]\r\n");
    }

    printf("==================================\r\n");
}

int main(void)
{
    uint16_t position_x = 0u;
    uint16_t position_y = 0u;
    uint32_t loop = 0u;
    uint32_t apb_clk;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    __DSB();
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();

    SystemCoreClockUpdate();
    board_init();

    printf("\r\n\r\n");
    printf("########################################\r\n");
    printf("#  SDK Style Servo Read-Only Test     #\r\n");
    printf("#  UART3: TX=GPIO27, RX=GPIO26        #\r\n");
    printf("#  DIR:   GPIO22 (1=TX, 0=RX)         #\r\n");
    printf("#  Servo: X=ID6, Y=ID3                #\r\n");
    printf("########################################\r\n\r\n");

    printf("[Init] Initializing SDK-style servo pins...\r\n");
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    set_gpio_function(GPIOA, 27u, FUNCTION_3);
    set_gpio_function(GPIOA, 26u, FUNCTION_3);
    set_gpio_function(GPIOA, DIR_PIN, FUNCTION_2);
    set_gpio_direction(GPIOA, DIR_PIN, 1u);
    set_gpio_data(GPIOA, DIR_PIN, WRITE_COMMAND_PIN);

    apb_clk = rcc_get_clock(RCC_CLOCK_APB1);
    uart_init(UART_IDX3, UARTTYPE_STD_SERIAL, apb_clk, 115200u);

    printf("[Init] UART3 baudrate=115200, APB_CLK=%lu\r\n", (unsigned long)apb_clk);
    printf("[Init] GPIO22=DIR, GPIO27=TX, GPIO26=RX\r\n");
    printf("[Init] Waiting 1s for servo...\r\n");
    delay_ms(1000u);

    printf("[Init] Enabling servo load...\r\n");
    servo_set_load(SDK_ARM_X_SERVO_ID, 1u);
    delay_ms(10u);
    servo_set_load(SDK_ARM_Y_SERVO_ID, 1u);
    delay_ms(10u);
    printf("[Init] SDK servos loaded.\r\n");

    printf("\r\n===== Initial Read =====\r\n");
    servo_print_info(SDK_ARM_X_SERVO_ID);
    servo_print_info(SDK_ARM_Y_SERVO_ID);

    while (1) {
        loop++;
        printf("\r\n===== SDK Loop %lu =====\r\n", (unsigned long)loop);
        printf("[TEST] Read-only polling (no motion)\r\n");

        if (servo_read_position(SDK_ARM_X_SERVO_ID, &position_x) == 0) {
            printf("[READ] ID=%u (X), POS=%u\r\n",
                   (unsigned)SDK_ARM_X_SERVO_ID,
                   (unsigned)position_x);
        } else {
            printf("[READ] ID=%u (X), POS=[READ FAILED]\r\n",
                   (unsigned)SDK_ARM_X_SERVO_ID);
        }

        delay_ms(10u);

        if (servo_read_position(SDK_ARM_Y_SERVO_ID, &position_y) == 0) {
            printf("[READ] ID=%u (Y), POS=%u\r\n",
                   (unsigned)SDK_ARM_Y_SERVO_ID,
                   (unsigned)position_y);
        } else {
            printf("[READ] ID=%u (Y), POS=[READ FAILED]\r\n",
                   (unsigned)SDK_ARM_Y_SERVO_ID);
        }

        if ((loop % 5u) == 0u) {
            printf("\r\n----- SDK Full Status -----\r\n");
            servo_print_info(SDK_ARM_X_SERVO_ID);
            servo_print_info(SDK_ARM_Y_SERVO_ID);
        }

        printf("[Loop %lu done]\r\n", (unsigned long)loop);
        delay_ms(SDK_SCAN_INTERVAL_MS);
    }
}