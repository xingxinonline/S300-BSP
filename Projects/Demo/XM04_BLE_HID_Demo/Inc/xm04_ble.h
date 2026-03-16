#ifndef XM04_BLE_H
#define XM04_BLE_H

#include <stdbool.h>
#include <stdint.h>

#define XM04_HID_REPORT_ID_KEYBOARD 0x01u
#define XM04_HID_REPORT_ID_CONSUMER 0x03u
#define XM04_HID_REPORT_ID_SYSTEM   0x04u

/*
 * XM04 consumer control payload uses a 4-byte bitfield in bytes 5..8,
 * not standard HID usage IDs like 0xE9/0xEA.
 */
#define XM04_CONSUMER_KEY_AC_HOME            0x00000040u
#define XM04_CONSUMER_KEY_MUTE               0x00000100u
#define XM04_CONSUMER_KEY_VOLUME_DOWN        0x00000200u
#define XM04_CONSUMER_KEY_VOLUME_UP          0x00000400u
#define XM04_CONSUMER_KEY_PLAY_PAUSE         0x00000800u
#define XM04_CONSUMER_KEY_STOP               0x00001000u
#define XM04_CONSUMER_KEY_PREV_TRACK         0x00002000u
#define XM04_CONSUMER_KEY_NEXT_TRACK         0x00004000u
#define XM04_CONSUMER_KEY_RECORD             0x00080000u
#define XM04_CONSUMER_KEY_POWER              0x01000000u
#define XM04_CONSUMER_KEY_IOS_SWITCH_KEYBOARD 0x20000000u

typedef enum {
    XM04_MOD_NONE    = 0x00,
    XM04_MOD_L_CTRL  = (1u << 0),
    XM04_MOD_L_SHIFT = (1u << 1),
    XM04_MOD_L_ALT   = (1u << 2),
    XM04_MOD_L_GUI   = (1u << 3),
    XM04_MOD_R_CTRL  = (1u << 4),
    XM04_MOD_R_SHIFT = (1u << 5),
    XM04_MOD_R_ALT   = (1u << 6),
    XM04_MOD_R_GUI   = (1u << 7),
} xm04_modifier_key_t;

typedef enum {
    XM04_BLE_MODE_HID = 0,
    XM04_BLE_MODE_SPP = 1,
} xm04_ble_mode_t;

typedef enum {
    XM04_BLE_STATUS_UNKNOWN = 0,
    XM04_BLE_STATUS_DISCOVERABLE_STANDBY = 1,
    XM04_BLE_STATUS_CONNECTABLE_STANDBY = 2,
    XM04_BLE_STATUS_CONNECTING = 3,
    XM04_BLE_STATUS_CONNECTED = 4,
    XM04_BLE_STATUS_DISCONNECTING = 5,
} xm04_ble_status_t;

void xm04_ble_tick_1ms(void);
uint32_t xm04_ble_now_ms(void);
void xm04_ble_delay_ms(uint32_t timeout_ms);
bool xm04_ble_at_mode_enabled(void);

bool xm04_ble_init(void);
bool xm04_ble_send_at_command(const char *cmd, char *response, uint16_t response_len, uint32_t timeout_ms);
bool xm04_ble_get_version(char *version, uint16_t len);
bool xm04_ble_get_state(char *state, uint16_t len);
bool xm04_ble_get_name(char *name, uint16_t len);
bool xm04_ble_set_name(const char *name);
bool xm04_ble_get_mode(char *mode, uint16_t len);
bool xm04_ble_set_mode(xm04_ble_mode_t mode);
bool xm04_ble_reset_module(void);
bool xm04_ble_set_auth(uint8_t auth);
bool xm04_ble_get_auth(char *auth, uint16_t len);
uint16_t xm04_ble_receive_data(uint8_t *buffer, uint16_t buffer_len, uint32_t timeout_ms);
uint16_t xm04_ble_rx_available(void);
uint32_t xm04_ble_rx_dropped(void);
bool xm04_ble_send_data(const uint8_t *data, uint16_t len);
bool xm04_send_keyboard_report(xm04_modifier_key_t modifiers, const uint8_t *key_codes, uint8_t key_count);
bool xm04_send_consumer_control_key(uint32_t key_mask);
bool xm04_send_system_control_key(uint8_t key_code);
bool xm04_release_keyboard_keys(void);
bool xm04_release_consumer_control(void);
bool xm04_release_system_control(void);
bool xm04_release_all_keys(void);
xm04_ble_status_t xm04_ble_parse_status(const char *response);

void UART1_IRQHandler(void);

#endif