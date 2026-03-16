#include "xm04_ble.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "gpio.h"
#include "rcc.h"
#include "s300.h"
#include "uart.h"
#include "xm04_ring_buffer.h"

#ifndef XM04_AT_PIN
#define XM04_AT_PIN 21u
#endif

#ifndef XM04_UART_BAUDRATE
#define XM04_UART_BAUDRATE 9600u
#endif

#ifndef XM04_ENABLE_AT_MODE
#define XM04_ENABLE_AT_MODE 0
#endif

#define XM04_UART_IDX UART_IDX1
#define XM04_UART_TYPE UARTTYPE_STD_SERIAL
#define XM04_AT_TIMEOUT_MS 1000u

static volatile uint32_t s_tick_ms = 0;
static xm04_ring_buffer_t s_rx_ring;

static void xm04_set_at_mode(bool enable)
{
#if XM04_ENABLE_AT_MODE
    set_gpio_data(GPIOA, XM04_AT_PIN, enable ? 1u : 0u);
#else
    (void)enable;
#endif
}

static void xm04_at_pin_init(void)
{
#if XM04_ENABLE_AT_MODE
    set_gpio_function(GPIOA, XM04_AT_PIN, FUNCTION_2);
    set_gpio_mode(GPIOA, XM04_AT_PIN, GPIO_UP);
    set_gpio_direction(GPIOA, XM04_AT_PIN, 1u);
    xm04_set_at_mode(false);
#endif
}

static bool xm04_response_contains_terminal(const char *response)
{
    return (strstr(response, "OK") != 0) || (strstr(response, "ERROR") != 0);
}

static const char *xm04_report_name(uint8_t report_id)
{
    switch (report_id) {
    case XM04_HID_REPORT_ID_KEYBOARD:
        return "keyboard";
    case XM04_HID_REPORT_ID_CONSUMER:
        return "consumer";
    case XM04_HID_REPORT_ID_SYSTEM:
        return "system";
    default:
        return "unknown";
    }
}

static void xm04_log_tx_packet(uint8_t report_id, const uint8_t *packet, uint8_t packet_len)
{
    uint8_t index;

    printf("[XM04] tx %-8s:", xm04_report_name(report_id));
    for (index = 0; index < packet_len; index++) {
        printf(" %02X", packet[index]);
    }
    printf("\r\n");
}

static bool xm04_send_raw_hid_packet(uint8_t report_id, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t packet[16] = {0};
    uint8_t packet_len = (uint8_t)(4u + payload_len);

    if (packet_len > sizeof(packet)) {
        return false;
    }

    packet[0] = packet_len;
    packet[1] = 0x00u;
    packet[2] = 0xA1u;
    packet[3] = report_id;

    if ((payload != 0) && (payload_len > 0u)) {
        memcpy(&packet[4], payload, payload_len);
    }

    xm04_log_tx_packet(report_id, packet, packet_len);
    xm04_set_at_mode(false);
    return xm04_ble_send_data(packet, packet_len);
}

void xm04_ble_tick_1ms(void)
{
    s_tick_ms++;
}

uint32_t xm04_ble_now_ms(void)
{
    return s_tick_ms;
}

void xm04_ble_delay_ms(uint32_t timeout_ms)
{
    uint32_t start = xm04_ble_now_ms();
    while ((xm04_ble_now_ms() - start) < timeout_ms) {
        __NOP();
    }
}

bool xm04_ble_at_mode_enabled(void)
{
#if XM04_ENABLE_AT_MODE
    return true;
#else
    return false;
#endif
}

bool xm04_ble_init(void)
{
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART1, true);
    set_gpio_function(BOARD_UART1_PORT, BOARD_UART1_TX_PIN, BOARD_UART1_FUNCTION);
    set_gpio_function(BOARD_UART1_PORT, BOARD_UART1_RX_PIN, BOARD_UART1_FUNCTION);
    init_uart(XM04_UART_IDX, XM04_UART_TYPE, rcc_get_clock(RCC_CLOCK_APB1), XM04_UART_BAUDRATE);
    xm04_at_pin_init();
    xm04_ring_buffer_init(&s_rx_ring);
    set_uart_interrupt(XM04_UART_IDX, false, true);
    NVIC_ClearPendingIRQ(UART1_IRQn);
    NVIC_SetPriority(UART1_IRQn, 3);
    NVIC_EnableIRQ(UART1_IRQn);
    return true;
}

bool xm04_ble_send_at_command(const char *cmd, char *response, uint16_t response_len, uint32_t timeout_ms)
{
    char tx_buffer[96];
    uint32_t start_ms;
    uint16_t response_idx = 0;
    uint8_t rx_byte;

    if ((response == 0) || (response_len == 0u)) {
        return false;
    }

    if (!xm04_ble_at_mode_enabled()) {
        response[0] = '\0';
        return false;
    }

    memset(response, 0, response_len);

    if ((cmd == 0) || (cmd[0] == '\0') || ((cmd[0] == 'A') && (cmd[1] == 'T') && (cmd[2] == '\0'))) {
        (void)snprintf(tx_buffer, sizeof(tx_buffer), "AT\r\n");
    } else {
        (void)snprintf(tx_buffer, sizeof(tx_buffer), "AT+%s\r\n", cmd);
    }

    xm04_ring_buffer_reset(&s_rx_ring);
    xm04_set_at_mode(true);
    xm04_ble_delay_ms(5u);

    if (!xm04_ble_send_data((const uint8_t *)tx_buffer, (uint16_t)strlen(tx_buffer))) {
        xm04_set_at_mode(false);
        return false;
    }

    start_ms = xm04_ble_now_ms();
    while ((xm04_ble_now_ms() - start_ms) < timeout_ms) {
        if (xm04_ring_buffer_pop(&s_rx_ring, &rx_byte)) {
            if (response_idx < (uint16_t)(response_len - 1u)) {
                response[response_idx++] = (char)rx_byte;
                response[response_idx] = '\0';
            }

            if (xm04_response_contains_terminal(response)) {
                xm04_set_at_mode(false);
                return strstr(response, "OK") != 0;
            }
        }
    }

    xm04_set_at_mode(false);
    return strstr(response, "OK") != 0;
}

bool xm04_ble_get_version(char *version, uint16_t len)
{
    return xm04_ble_send_at_command("VERSION?", version, len, XM04_AT_TIMEOUT_MS);
}

bool xm04_ble_get_state(char *state, uint16_t len)
{
    return xm04_ble_send_at_command("STATE?", state, len, XM04_AT_TIMEOUT_MS);
}

bool xm04_ble_get_name(char *name, uint16_t len)
{
    return xm04_ble_send_at_command("NAME?", name, len, XM04_AT_TIMEOUT_MS);
}

bool xm04_ble_set_name(const char *name)
{
    char cmd[64];
    char response[64];

    if (name == 0) {
        return false;
    }

    (void)snprintf(cmd, sizeof(cmd), "NAME=%s", name);
    return xm04_ble_send_at_command(cmd, response, sizeof(response), XM04_AT_TIMEOUT_MS);
}

bool xm04_ble_get_mode(char *mode, uint16_t len)
{
    return xm04_ble_send_at_command("DEVTYPE?", mode, len, XM04_AT_TIMEOUT_MS);
}

bool xm04_ble_set_mode(xm04_ble_mode_t mode)
{
    char cmd[32];
    char response[64];

    (void)snprintf(cmd, sizeof(cmd), "DEVTYPE=%u", (unsigned int)mode);
    return xm04_ble_send_at_command(cmd, response, sizeof(response), XM04_AT_TIMEOUT_MS);
}

bool xm04_ble_reset_module(void)
{
    char response[64];
    return xm04_ble_send_at_command("RESET", response, sizeof(response), XM04_AT_TIMEOUT_MS);
}

bool xm04_ble_set_auth(uint8_t auth)
{
    char cmd[32];
    char response[64];

    (void)snprintf(cmd, sizeof(cmd), "AUTH=%u", (unsigned int)auth);
    return xm04_ble_send_at_command(cmd, response, sizeof(response), XM04_AT_TIMEOUT_MS);
}

bool xm04_ble_get_auth(char *auth, uint16_t len)
{
    return xm04_ble_send_at_command("AUTH?", auth, len, XM04_AT_TIMEOUT_MS);
}

uint16_t xm04_ble_receive_data(uint8_t *buffer, uint16_t buffer_len, uint32_t timeout_ms)
{
    uint32_t start_ms;
    uint16_t received = 0;

    if ((buffer == 0) || (buffer_len == 0u)) {
        return 0;
    }

    start_ms = xm04_ble_now_ms();
    while (((xm04_ble_now_ms() - start_ms) < timeout_ms) && (received < buffer_len)) {
        if (!xm04_ring_buffer_pop(&s_rx_ring, &buffer[received])) {
            continue;
        }
        received++;
    }

    return received;
}

uint16_t xm04_ble_rx_available(void)
{
    return xm04_ring_buffer_available(&s_rx_ring);
}

uint32_t xm04_ble_rx_dropped(void)
{
    return s_rx_ring.dropped;
}

bool xm04_ble_send_data(const uint8_t *data, uint16_t len)
{
    uint16_t index;

    if ((data == 0) || (len == 0u)) {
        return false;
    }

    for (index = 0; index < len; index++) {
        if (uart_write(XM04_UART_IDX, XM04_UART_TYPE, data[index]) != 0) {
            return false;
        }
    }

    return true;
}

bool xm04_send_keyboard_report(xm04_modifier_key_t modifiers, const uint8_t *key_codes, uint8_t key_count)
{
    uint8_t payload[8] = {0};

    payload[0] = (uint8_t)modifiers;
    if ((key_codes != 0) && (key_count > 0u)) {
        if (key_count > 6u) {
            key_count = 6u;
        }
        memcpy(&payload[2], key_codes, key_count);
    }

    return xm04_send_raw_hid_packet(XM04_HID_REPORT_ID_KEYBOARD, payload, sizeof(payload));
}

bool xm04_send_consumer_control_key(uint32_t key_mask)
{
    uint8_t payload[4] = {0};

    payload[0] = (uint8_t)(key_mask & 0xFFu);
    payload[1] = (uint8_t)((key_mask >> 8) & 0xFFu);
    payload[2] = (uint8_t)((key_mask >> 16) & 0xFFu);
    payload[3] = (uint8_t)((key_mask >> 24) & 0xFFu);

    return xm04_send_raw_hid_packet(XM04_HID_REPORT_ID_CONSUMER, payload, sizeof(payload));
}

bool xm04_send_system_control_key(uint8_t key_code)
{
    uint8_t payload[1] = {key_code};
    return xm04_send_raw_hid_packet(XM04_HID_REPORT_ID_SYSTEM, payload, sizeof(payload));
}

bool xm04_release_keyboard_keys(void)
{
    return xm04_send_keyboard_report(XM04_MOD_NONE, 0, 0u);
}

bool xm04_release_consumer_control(void)
{
    return xm04_send_consumer_control_key(0u);
}

bool xm04_release_system_control(void)
{
    return xm04_send_system_control_key(0u);
}

bool xm04_release_all_keys(void)
{
    bool ok = true;

    ok = xm04_release_keyboard_keys() && ok;
    ok = xm04_release_consumer_control() && ok;
    ok = xm04_release_system_control() && ok;
    return ok;
}

xm04_ble_status_t xm04_ble_parse_status(const char *response)
{
    const char *state_start;
    int state_value = 0;

    if (response == 0) {
        return XM04_BLE_STATUS_UNKNOWN;
    }

    state_start = strstr(response, "+STATE:");
    if (state_start == 0) {
        return XM04_BLE_STATUS_UNKNOWN;
    }

    state_start += 7;
    if (sscanf(state_start, "%d", &state_value) != 1) {
        return XM04_BLE_STATUS_UNKNOWN;
    }

    switch (state_value) {
    case 1: return XM04_BLE_STATUS_DISCOVERABLE_STANDBY;
    case 2: return XM04_BLE_STATUS_CONNECTABLE_STANDBY;
    case 3: return XM04_BLE_STATUS_CONNECTING;
    case 4: return XM04_BLE_STATUS_CONNECTED;
    case 5: return XM04_BLE_STATUS_DISCONNECTING;
    default: return XM04_BLE_STATUS_UNKNOWN;
    }
}

void UART1_IRQHandler(void)
{
    uint32_t int_id = UART1->IIR_FCR & 0x0Fu;

    if ((int_id == 0x04u) || (int_id == 0x0Cu)) {
        while ((UART1->LSR & 0x01u) != 0u) {
            (void)xm04_ring_buffer_push(&s_rx_ring, (uint8_t)(UART1->RBR_THR_DLL & 0xFFu));
        }
        return;
    }

    if (int_id == 0x06u) {
        volatile uint32_t lsr = UART1->LSR;
        (void)lsr;
    }
}