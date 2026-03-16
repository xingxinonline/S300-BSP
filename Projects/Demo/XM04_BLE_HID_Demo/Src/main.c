#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "s300.h"
#include "uart.h"
#include "board.h"
#include "xm04_ble.h"

#define XM04_DEMO_KEY_A 0x04u

static int debug_uart_try_read(uint8_t *ch)
{
    if (ch == 0) {
        return 0;
    }

    if ((BOARD_DEBUG_UART->USR & 0x08u) == 0u) {
        return 0;
    }

    *ch = (uint8_t)(BOARD_DEBUG_UART->RBR_THR_DLL & 0xFFu);
    return 1;
}

static void print_help(void)
{
    printf("\r\n=== XM04 BLE HID Demo ===\r\n");
    printf("debug uart=%d, baud=%d\r\n", BOARD_DEBUG_UART_IDX, BOARD_DEBUG_UART_BAUDRATE);
    printf("xm04 uart=1, baud=%d, hid_only=%d\r\n", XM04_UART_BAUDRATE, xm04_ble_at_mode_enabled() ? 0 : 1);
    printf("commands:\r\n");
    printf("  h/? : help\r\n");
    printf("  p   : send play/pause\r\n");
    printf("  ]   : send next-track\r\n");
    printf("  [   : send prev-track\r\n");
    printf("  +   : send volume-up\r\n");
    printf("  -   : send volume-down\r\n");
    printf("  1   : send Android Home\r\n");
    printf("  2   : send Power\r\n");
    printf("  3   : send iOS soft-keyboard switch\r\n");
    printf("  k   : send keyboard key 'A'\r\n");
    printf("  r   : release all keys\r\n");
    printf("  d   : dump UART1 rx buffer\r\n");
    if (xm04_ble_at_mode_enabled()) {
        printf("  a   : send plain AT\r\n");
        printf("  v   : query version\r\n");
        printf("  s   : query state\r\n");
        printf("  g   : query name\r\n");
        printf("  n   : set name to S300-XM04\r\n");
        printf("  m   : set mode to HID\r\n");
        printf("  u   : query mode\r\n");
        printf("  t   : query auth mode\r\n");
        printf("  x   : reset module\r\n");
    }
}

static void print_at_response(const char *label, bool ok, const char *response)
{
    printf("[XM04] %s: %s\r\n", label, ok ? "OK" : "FAIL");
    if ((response != 0) && (response[0] != '\0')) {
        printf("%s\r\n", response);
    }
}

static void send_consumer_click(uint32_t key_code, const char *label)
{
    bool ok = xm04_send_consumer_control_key(key_code);
    xm04_ble_delay_ms(40u);
    ok = xm04_release_consumer_control() && ok;
    printf("[XM04] %s: %s\r\n", label, ok ? "sent" : "failed");
}

static void send_keyboard_a(void)
{
    uint8_t key_code = XM04_DEMO_KEY_A;
    bool ok = xm04_send_keyboard_report(XM04_MOD_NONE, &key_code, 1u);
    xm04_ble_delay_ms(40u);
    ok = xm04_release_keyboard_keys() && ok;
    printf("[XM04] keyboard A: %s\r\n", ok ? "sent" : "failed");
}

static void dump_rx_buffer(void)
{
    uint8_t buffer[64];
    uint16_t received = xm04_ble_receive_data(buffer, sizeof(buffer), 10u);
    uint16_t i;

    printf("[XM04] rx available=%u dropped=%lu\r\n",
           (unsigned int)xm04_ble_rx_available(),
           (unsigned long)xm04_ble_rx_dropped());

    if (received == 0u) {
        printf("[XM04] rx buffer empty\r\n");
        return;
    }

    printf("[XM04] rx bytes (%u): ", (unsigned int)received);
    for (i = 0; i < received; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\r\n");
}

static void run_startup_probe(void)
{
    char response[96];
    bool ok;

    if (!xm04_ble_at_mode_enabled()) {
        printf("[XM04] startup probe skipped (HID-only mode)\r\n");
        return;
    }

    ok = xm04_ble_send_at_command("AT", response, sizeof(response), 1000u);
    print_at_response("AT", ok, response);

    ok = xm04_ble_get_version(response, sizeof(response));
    print_at_response("VERSION?", ok, response);

    ok = xm04_ble_get_mode(response, sizeof(response));
    print_at_response("DEVTYPE?", ok, response);

    ok = xm04_ble_get_state(response, sizeof(response));
    print_at_response("STATE?", ok, response);
}

static void handle_command(uint8_t cmd)
{
    char response[96];
    bool ok;

    switch (cmd) {
    case 'h':
    case '?':
        print_help();
        break;

    case 'a':
        if (!xm04_ble_at_mode_enabled()) {
            printf("[XM04] AT disabled in HID-only mode\r\n");
            break;
        }
        ok = xm04_ble_send_at_command("AT", response, sizeof(response), 1000u);
        print_at_response("AT", ok, response);
        break;

    case 'v':
        if (!xm04_ble_at_mode_enabled()) {
            printf("[XM04] AT disabled in HID-only mode\r\n");
            break;
        }
        ok = xm04_ble_get_version(response, sizeof(response));
        print_at_response("VERSION?", ok, response);
        break;

    case 's':
        if (!xm04_ble_at_mode_enabled()) {
            printf("[XM04] AT disabled in HID-only mode\r\n");
            break;
        }
        ok = xm04_ble_get_state(response, sizeof(response));
        print_at_response("STATE?", ok, response);
        printf("[XM04] parsed state=%d\r\n", (int)xm04_ble_parse_status(response));
        break;

    case 'g':
        if (!xm04_ble_at_mode_enabled()) {
            printf("[XM04] AT disabled in HID-only mode\r\n");
            break;
        }
        ok = xm04_ble_get_name(response, sizeof(response));
        print_at_response("NAME?", ok, response);
        break;

    case 'n':
        if (!xm04_ble_at_mode_enabled()) {
            printf("[XM04] AT disabled in HID-only mode\r\n");
            break;
        }
        ok = xm04_ble_set_name("S300-XM04");
        printf("[XM04] NAME=S300-XM04: %s\r\n", ok ? "OK" : "FAIL");
        break;

    case 'm':
        if (!xm04_ble_at_mode_enabled()) {
            printf("[XM04] AT disabled in HID-only mode\r\n");
            break;
        }
        ok = xm04_ble_set_mode(XM04_BLE_MODE_HID);
        printf("[XM04] DEVTYPE=HID: %s\r\n", ok ? "OK" : "FAIL");
        break;

    case 'u':
        if (!xm04_ble_at_mode_enabled()) {
            printf("[XM04] AT disabled in HID-only mode\r\n");
            break;
        }
        ok = xm04_ble_get_mode(response, sizeof(response));
        print_at_response("DEVTYPE?", ok, response);
        break;

    case 't':
        if (!xm04_ble_at_mode_enabled()) {
            printf("[XM04] AT disabled in HID-only mode\r\n");
            break;
        }
        ok = xm04_ble_get_auth(response, sizeof(response));
        print_at_response("AUTH?", ok, response);
        break;

    case 'p':
        send_consumer_click(XM04_CONSUMER_KEY_PLAY_PAUSE, "play/pause");
        break;

    case ']':
        send_consumer_click(XM04_CONSUMER_KEY_NEXT_TRACK, "next-track");
        break;

    case '[':
        send_consumer_click(XM04_CONSUMER_KEY_PREV_TRACK, "prev-track");
        break;

    case '+':
        send_consumer_click(XM04_CONSUMER_KEY_VOLUME_UP, "volume-up");
        break;

    case '-':
        send_consumer_click(XM04_CONSUMER_KEY_VOLUME_DOWN, "volume-down");
        break;

    case '1':
        send_consumer_click(XM04_CONSUMER_KEY_AC_HOME, "android home");
        break;

    case '2':
        send_consumer_click(XM04_CONSUMER_KEY_POWER, "power");
        break;

    case '3':
        send_consumer_click(XM04_CONSUMER_KEY_IOS_SWITCH_KEYBOARD, "ios soft-keyboard switch");
        break;

    case 'k':
        send_keyboard_a();
        break;

    case 'r':
        printf("[XM04] release: %s\r\n", xm04_release_all_keys() ? "OK" : "FAIL");
        break;

    case 'x':
        if (!xm04_ble_at_mode_enabled()) {
            printf("[XM04] AT disabled in HID-only mode\r\n");
            break;
        }
        printf("[XM04] RESET: %s\r\n", xm04_ble_reset_module() ? "OK" : "FAIL");
        break;

    case 'd':
        dump_rx_buffer();
        break;

    case '\r':
    case '\n':
        break;

    default:
        printf("[XM04] unknown command '%c'\r\n", cmd);
        break;
    }
}

void SysTick_Handler(void)
{
    xm04_ble_tick_1ms();
}

int main(void)
{
    uint8_t cmd;

    board_init();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0) {
        while (1) {
        }
    }

    printf("\r\n[XM04] demo boot\r\n");
    printf("[XM04] tx packet log enabled\r\n");
    xm04_ble_init();
    print_help();
    run_startup_probe();

    while (1) {
        if (!debug_uart_try_read(&cmd)) {
            continue;
        }

        handle_command(cmd);
    }
}