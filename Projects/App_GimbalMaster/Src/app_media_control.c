#include "app_media_control.h"

#include <stdint.h>

#include "board.h"
#include "gpio.h"
#include "master_log.h"
#include "rcc.h"
#include "uart.h"

#define XM04_PACKET_LEN 8u
#define XM04_UART_IDX UART_IDX1
#define XM04_UART_TYPE UARTTYPE_STD_SERIAL
#define XM04_UART_BAUDRATE 9600u

#define XM04_PACKET_BYTE0_LEN 0x08u
#define XM04_PACKET_BYTE1_FIXED 0x00u
#define XM04_PACKET_BYTE2_FIXED 0xA1u
#define XM04_PACKET_BYTE3_CONSUMER 0x03u

#define XM04_CONSUMER_KEY_VOLUME_UP 0x00000400u

static bool s_xm04_ready = false;

static bool s_recording = false;
static uint32_t s_photo_count = 0u;

static void media_log_tx_packet(const uint8_t *packet)
{
    MASTER_LOG_INFO("[MASTER][MEDIA][XM04] tx: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                    packet[0], packet[1], packet[2], packet[3],
                    packet[4], packet[5], packet[6], packet[7]);
}

static bool xm04_send_packet(const uint8_t *packet, uint32_t len)
{
    uint32_t index;

    if ((packet == NULL) || (len == 0u)) {
        return false;
    }

    media_log_tx_packet(packet);
    for (index = 0; index < len; index++) {
        if (uart_write(XM04_UART_IDX, XM04_UART_TYPE, packet[index]) != 0) {
            MASTER_LOG_WARN("[MASTER][MEDIA][XM04] uart write failed at byte=%lu\r\n",
                            (unsigned long)index);
            return false;
        }
    }

    return true;
}

static bool xm04_send_consumer_pulse(uint32_t key_mask)
{
    uint8_t press_packet[XM04_PACKET_LEN] = {
        XM04_PACKET_BYTE0_LEN,
        XM04_PACKET_BYTE1_FIXED,
        XM04_PACKET_BYTE2_FIXED,
        XM04_PACKET_BYTE3_CONSUMER,
        (uint8_t)(key_mask & 0xFFu),
        (uint8_t)((key_mask >> 8) & 0xFFu),
        (uint8_t)((key_mask >> 16) & 0xFFu),
        (uint8_t)((key_mask >> 24) & 0xFFu),
    };
    uint8_t release_packet[XM04_PACKET_LEN] = {
        XM04_PACKET_BYTE0_LEN,
        XM04_PACKET_BYTE1_FIXED,
        XM04_PACKET_BYTE2_FIXED,
        XM04_PACKET_BYTE3_CONSUMER,
        0u, 0u, 0u, 0u,
    };

    if (!xm04_send_packet(press_packet, XM04_PACKET_LEN)) {
        return false;
    }

    if (!xm04_send_packet(release_packet, XM04_PACKET_LEN)) {
        return false;
    }

    return true;
}

static bool xm04_init_transport(void)
{
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART1, true);
    set_gpio_function(BOARD_UART1_PORT, BOARD_UART1_TX_PIN, BOARD_UART1_FUNCTION);
    set_gpio_function(BOARD_UART1_PORT, BOARD_UART1_RX_PIN, BOARD_UART1_FUNCTION);
    init_uart(XM04_UART_IDX, XM04_UART_TYPE, rcc_get_clock(RCC_CLOCK_APB1), XM04_UART_BAUDRATE);
    MASTER_LOG_INFO("[MASTER][MEDIA][XM04] uart1 initialized, baud=%u\r\n", XM04_UART_BAUDRATE);
    return true;
}

void app_media_control_init(void)
{
    s_recording = false;
    s_photo_count = 0u;
    s_xm04_ready = xm04_init_transport();
}

app_media_control_result_t app_media_control_trigger_photo(void)
{
    if (!s_xm04_ready) {
        MASTER_LOG_WARN("[MASTER][MEDIA][XM04] photo trigger rejected, transport unavailable\r\n");
        return APP_MEDIA_CONTROL_FAILED;
    }

    if (!xm04_send_consumer_pulse(XM04_CONSUMER_KEY_VOLUME_UP)) {
        MASTER_LOG_WARN("[MASTER][MEDIA][XM04] photo trigger send failed\r\n");
        return APP_MEDIA_CONTROL_FAILED;
    }

    s_photo_count++;
    MASTER_LOG_INFO("[MASTER][MEDIA] photo trigger accepted via XM04, count=%lu\r\n",
                    (unsigned long)s_photo_count);
    return APP_MEDIA_CONTROL_ACCEPTED;
}

app_media_control_result_t app_media_control_set_recording(bool enabled)
{
    if (s_recording == enabled) {
        MASTER_LOG_DEBUG("[MASTER][MEDIA] recording already %u\r\n", enabled ? 1u : 0u);
        return APP_MEDIA_CONTROL_NO_CHANGE;
    }

    if (!s_xm04_ready) {
        MASTER_LOG_WARN("[MASTER][MEDIA][XM04] recording trigger rejected, transport unavailable\r\n");
        return APP_MEDIA_CONTROL_FAILED;
    }

    if (!xm04_send_consumer_pulse(XM04_CONSUMER_KEY_VOLUME_UP)) {
        MASTER_LOG_WARN("[MASTER][MEDIA][XM04] recording %s send failed\r\n",
                        enabled ? "START" : "STOP");
        return APP_MEDIA_CONTROL_FAILED;
    }

    s_recording = enabled;
    MASTER_LOG_INFO("[MASTER][MEDIA] recording %s via XM04 volume-up toggle\r\n",
                    enabled ? "START" : "STOP");
    return APP_MEDIA_CONTROL_ACCEPTED;
}

bool app_media_control_is_recording(void)
{
    return s_recording;
}

uint32_t app_media_control_get_photo_count(void)
{
    return s_photo_count;
}