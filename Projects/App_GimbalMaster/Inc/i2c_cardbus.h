#ifndef I2C_CARDBUS_H
#define I2C_CARDBUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARDBUS_CARD1_ADDR   0x10u
#define CARDBUS_CARD2_ADDR   0x11u
#define CARDBUS_CARD3_ADDR   0x12u

#define CARDBUS_REG_STATUS   0x00u
#define CARDBUS_REG_SYS_STATE 0x01u
#define CARDBUS_REG_RESULT   0x10u
#define CARDBUS_REG_CMD      0x80u
#define CARDBUS_REG_CMD_ACK  0x81u

#define CARDBUS_CMD_NONE          0u
#define CARDBUS_CMD_START_MM_DSP  1u
#define CARDBUS_CMD_STOP_MM_DSP   2u
#define CARDBUS_RESULT_LEN   32u

typedef struct __attribute__((packed)) {
    uint8_t  valid;
    uint8_t  count;
    uint8_t  type;
    uint8_t  selected_idx;
    int16_t  cx;
    int16_t  cy;
    int16_t  x1;
    int16_t  y1;
    int16_t  x2;
    int16_t  y2;
    int8_t   vx;
    int8_t   vy;
    uint8_t  gesture_type;
    uint8_t  face_id;
    uint8_t  confidence;
    uint8_t  reserved[11];
} card_detection_t;

typedef struct {
    card_detection_t card1;
    card_detection_t card2;
    card_detection_t card3;
    uint8_t online_mask;
    uint8_t target_present;
    uint16_t _reserved;
    uint32_t timestamp_ms;
    uint32_t cycle_time_us;
} cardbus_snapshot_t;

int i2c_cardbus_init(void);
bool i2c_cardbus_is_ready(void);
int i2c_cardbus_probe(uint8_t addr);
int i2c_cardbus_read_detection(uint8_t addr, card_detection_t *out);
int i2c_cardbus_write_reg8(uint8_t addr, uint8_t reg, uint8_t value);
int i2c_cardbus_read_card1(card_detection_t *out);
int i2c_cardbus_read_card2(card_detection_t *out);
int i2c_cardbus_read_card3(card_detection_t *out);

#ifdef __cplusplus
}
#endif

#endif /* I2C_CARDBUS_H */
