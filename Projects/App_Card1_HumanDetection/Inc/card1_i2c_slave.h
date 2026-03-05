#ifndef CARD1_I2C_SLAVE_H
#define CARD1_I2C_SLAVE_H

#include <stdint.h>
#include "card1_detection.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARD1_I2C_ADDR      0x10u
#define CARD1_REG_STATUS    0x00u
#define CARD1_REG_SYS_STATE 0x01u
#define CARD1_REG_RESULT    0x10u
#define CARD1_REG_CMD       0x80u
#define CARD1_REG_CMD_ACK   0x81u

#define CARD1_SYS_STATE_BOOT       (1u << 0)
#define CARD1_SYS_STATE_I2C_READY  (1u << 1)
#define CARD1_SYS_STATE_MM_READY   (1u << 2)
#define CARD1_SYS_STATE_DSP_READY  (1u << 3)
#define CARD1_SYS_STATE_RUNNING    (1u << 4)

#define CARD1_CMD_NONE          0u
#define CARD1_CMD_START_MM_DSP  1u
#define CARD1_CMD_STOP_MM_DSP   2u

int card1_i2c_slave_init(uint8_t slave_addr);
void card1_i2c_slave_update_result(const card1_detection_result_t *result);
void card1_i2c_slave_set_system_state(uint8_t state_flags);
uint8_t card1_i2c_slave_get_command(void);
void card1_i2c_slave_clear_command(void);
void card1_i2c_slave_set_command_ack(uint8_t cmd);

#ifdef __cplusplus
}
#endif

#endif /* CARD1_I2C_SLAVE_H */
