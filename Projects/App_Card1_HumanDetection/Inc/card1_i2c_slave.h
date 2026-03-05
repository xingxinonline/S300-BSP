#ifndef CARD1_I2C_SLAVE_H
#define CARD1_I2C_SLAVE_H

#include <stdint.h>
#include "card1_detection.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARD1_I2C_ADDR      0x10u
#define CARD1_REG_STATUS    0x00u
#define CARD1_REG_RESULT    0x10u

int card1_i2c_slave_init(uint8_t slave_addr);
void card1_i2c_slave_update_result(const card1_detection_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* CARD1_I2C_SLAVE_H */
