#include "s300.h"
#include "board.h"
#include "uart.h"
#include "uart_s300.h"
#include "gpio.h"
#include "rcc.h"

#include <stdio.h>
#include <string.h>

#define DIAG_SERVO_ID 6u
#define DIR_PIN 22u

#define DIAG_MAX_SCAN_ROUNDS 3u

#define RX_FIRST_TIMEOUT_MS 100u
#define RX_POLL_INTERVAL_US 100u

typedef struct {
    uint16_t pre_switch_delay_us;
    uint16_t post_switch_delay_us;
    uint8_t tx_level;
    uint8_t rx_level;
} tx_rx_delay_profile_t;

static const tx_rx_delay_profile_t s_delay_profiles[] = {
    { 0u, 0u, 1u, 0u },
    { 0u, 10u, 1u, 0u },
    { 0u, 30u, 1u, 0u },
    { 0u, 50u, 1u, 0u },
    { 0u, 100u, 1u, 0u },
    { 0u, 200u, 1u, 0u },
    { 5u, 25u, 1u, 0u },
    { 10u, 20u, 1u, 0u },
    { 20u, 10u, 1u, 0u },
    { 30u, 0u, 1u, 0u },
    { 50u, 0u, 1u, 0u },
    { 100u, 0u, 1u, 0u },
    { 200u, 0u, 1u, 0u },
    { 25u, 25u, 1u, 0u },
    { 50u, 50u, 1u, 0u },
    { 100u, 100u, 1u, 0u },
    { 0u, 0u, 0u, 1u },
    { 0u, 10u, 0u, 1u },
    { 0u, 30u, 0u, 1u },
    { 0u, 50u, 0u, 1u },
    { 0u, 100u, 0u, 1u },
    { 0u, 200u, 0u, 1u },
    { 5u, 25u, 0u, 1u },
    { 10u, 20u, 0u, 1u },
    { 20u, 10u, 0u, 1u },
    { 30u, 0u, 0u, 1u },
    { 50u, 0u, 0u, 1u },
    { 100u, 0u, 0u, 1u },
    { 200u, 0u, 0u, 1u },
    { 25u, 25u, 0u, 1u },
    { 50u, 50u, 0u, 1u },
    { 100u, 100u, 0u, 1u },
};

static void delay_ms(uint32_t ms)
{
    uint32_t reload = SystemCoreClock / 1000u - 1u;

    if (reload > 0xFFFFFFu) {
        reload = 0xFFFFFFu;
    }

    SysTick->LOAD = reload;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    for (uint32_t index = 0u; index < ms; index++) {
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

static void print_hex_line(const char *prefix, const uint8_t *data, uint32_t len)
{
    printf("%s", prefix);
    for (uint32_t index = 0u; index < len; index++) {
        printf("%02X ", data[index]);
    }
    printf("\r\n");
}

static void uart3_send_byte(uint8_t byte)
{
    while ((UART3->USR & 0x02u) == 0u) {
    }
    UART3->RBR_THR_DLL = byte;
}

static void uart3_wait_tx_done(void)
{
    while ((UART3->USR & 0x04u) == 0u) {
    }
    while ((UART3->USR & 0x01u) != 0u) {
    }
}

static void uart3_flush_rx_fifo(void)
{
    while ((UART3->USR & 0x08u) != 0u) {
        (void)UART3->RBR_THR_DLL;
    }
}

static uint32_t uart3_collect_rx_bytes(uint8_t *buffer, uint32_t capacity)
{
    uint32_t count = 0u;
    uint32_t polls = (RX_FIRST_TIMEOUT_MS * 1000u) / RX_POLL_INTERVAL_US;

    while (polls-- > 0u) {
        if ((UART3->USR & 0x08u) != 0u) {
            while (((UART3->USR & 0x08u) != 0u) && (count < capacity)) {
                buffer[count++] = (uint8_t)(UART3->RBR_THR_DLL & 0xFFu);
                delay_us(RX_POLL_INTERVAL_US);
            }
            break;
        }
        delay_us(RX_POLL_INTERVAL_US);
    }

    return count;
}

static void servo_diag_send_pos_read(uint8_t servo_id,
                                     const tx_rx_delay_profile_t *profile,
                                     uint8_t *rx_buffer,
                                     uint32_t rx_capacity)
{
    uint8_t tx_packet[6];
    uint8_t checksum = 0u;
    uint32_t rx_len;

    if (profile == NULL) {
        return;
    }

    tx_packet[0] = 0x55u;
    tx_packet[1] = 0x55u;
    tx_packet[2] = servo_id;
    tx_packet[3] = 0x03u;
    tx_packet[4] = 0x1Cu;

    for (uint32_t index = 2u; index < 5u; index++) {
        checksum = (uint8_t)(checksum + tx_packet[index]);
    }
    tx_packet[5] = (uint8_t)(~checksum);

    uart3_flush_rx_fifo();
    printf("[DIAG] RX FIFO flushed, USR=0x%08lX\r\n", (unsigned long)UART3->USR);

        gpio_set_data(GPIOA, DIR_PIN, profile->tx_level);
        printf("[DIAG] BUSEN -> TX(level=%u), USR=0x%08lX\r\n",
            (unsigned)profile->tx_level,
            (unsigned long)UART3->USR);
    print_hex_line("[DIAG][TX] ", tx_packet, 6u);

    for (uint32_t index = 0u; index < 6u; index++) {
        uart3_send_byte(tx_packet[index]);
    }

    uart3_wait_tx_done();
    printf("[DIAG] UART3 TX complete, USR=0x%08lX\r\n", (unsigned long)UART3->USR);

        delay_us(profile->pre_switch_delay_us);
        gpio_set_data(GPIOA, DIR_PIN, profile->rx_level);
        printf("[DIAG] BUSEN -> RX(level=%u) after %u us, USR=0x%08lX\r\n",
            (unsigned)profile->rx_level,
            (unsigned)profile->pre_switch_delay_us,
           (unsigned long)UART3->USR);
        delay_us(profile->post_switch_delay_us);
    printf("[DIAG] RX settle done after %u us, USR=0x%08lX\r\n",
            (unsigned)profile->post_switch_delay_us,
           (unsigned long)UART3->USR);

    memset(rx_buffer, 0, rx_capacity);
    rx_len = uart3_collect_rx_bytes(rx_buffer, rx_capacity);
    if (rx_len > 0u) {
        printf("[DIAG] UART3 RX got %lu byte(s), USR=0x%08lX\r\n",
               (unsigned long)rx_len,
               (unsigned long)UART3->USR);
        print_hex_line("[DIAG][RX] ", rx_buffer, rx_len);
    } else {
        printf("[DIAG] UART3 RX got 0 byte, timeout=%u ms, USR=0x%08lX\r\n",
               (unsigned)RX_FIRST_TIMEOUT_MS,
               (unsigned long)UART3->USR);
    }
}

int main(void)
{
    uint8_t rx_buffer[32];
    uint32_t profile_index = 0u;
    uint32_t scan_round = 1u;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    __DSB();
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();

    SystemCoreClockUpdate();
    board_init();

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  Bus Servo POS_READ Minimal Diagnostic\r\n");
    printf("  Servo ID=%u\r\n", (unsigned)DIAG_SERVO_ID);
    printf("  UART3 TX=GPIO27 RX=GPIO26 BUSEN=GPIO22\r\n");
    printf("========================================\r\n\r\n");

    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    set_gpio_function(GPIOA, 27u, FUNCTION_3);
    set_gpio_function(GPIOA, 26u, FUNCTION_3);
    set_gpio_function(GPIOA, DIR_PIN, FUNCTION_2);
    set_gpio_direction(GPIOA, DIR_PIN, 1u);
    set_gpio_data(GPIOA, DIR_PIN, 1u);
    uart_init(UART_IDX3, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), 115200u);

    printf("[DIAG] UART3 initialized, APB1=%lu\r\n", (unsigned long)rcc_get_clock(RCC_CLOCK_APB1));
    printf("[DIAG] Waiting 1000 ms for servo power-up\r\n");
    delay_ms(1000u);

        while (scan_round <= DIAG_MAX_SCAN_ROUNDS) {
        const tx_rx_delay_profile_t *profile = &s_delay_profiles[profile_index];

                 printf("[DIAG] round=%lu/%lu profile=%lu pre=%u us post=%u us tx=%u rx=%u\r\n",
                             (unsigned long)scan_round,
                             (unsigned long)DIAG_MAX_SCAN_ROUNDS,
               (unsigned long)profile_index,
               (unsigned)profile->pre_switch_delay_us,
             (unsigned)profile->post_switch_delay_us,
             (unsigned)profile->tx_level,
             (unsigned)profile->rx_level);
        servo_diag_send_pos_read(DIAG_SERVO_ID, profile, rx_buffer, sizeof(rx_buffer));
        printf("[DIAG] ----\r\n");
        profile_index++;
        if (profile_index >= (sizeof(s_delay_profiles) / sizeof(s_delay_profiles[0]))) {
            profile_index = 0u;
            printf("[DIAG] profile scan wrapped (round %lu/%lu complete)\r\n",
                   (unsigned long)scan_round,
                   (unsigned long)DIAG_MAX_SCAN_ROUNDS);
            scan_round++;
        }
        delay_ms(1000u);
    }

    printf("[DIAG] profile scan finished after %lu round(s)\r\n",
           (unsigned long)DIAG_MAX_SCAN_ROUNDS);

    while (1) {
        delay_ms(1000u);
    }
}