#ifndef SUBBOARD_STARTUP_I2C_H
#define SUBBOARD_STARTUP_I2C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t start_det_count;
	uint32_t rx_full_count;
	uint32_t rd_req_count;
	uint32_t rx_done_count;
	uint32_t stop_det_count;
	uint32_t restart_det_count;
	uint8_t current_reg_addr;
	uint8_t current_tx_index;
} subboard_startup_i2c_debug_stats_t;

int subboard_startup_i2c_init(uint8_t slave_addr);
void subboard_startup_i2c_set_status(uint8_t status);
void subboard_startup_i2c_set_public_state(uint8_t state);
void subboard_startup_i2c_set_error_code(uint8_t error_code);
void subboard_startup_i2c_bump_heartbeat(void);
void subboard_startup_i2c_set_request(uint8_t request, uint8_t request_arg);
void subboard_startup_i2c_clear_request(void);
uint8_t subboard_startup_i2c_get_request_ack(void);
uint8_t subboard_startup_i2c_get_command(void);
uint8_t subboard_startup_i2c_get_command_arg(void);
void subboard_startup_i2c_set_command_result(uint8_t cmd, uint8_t result);
void subboard_startup_i2c_clear_command(void);
void subboard_startup_i2c_get_debug_stats(subboard_startup_i2c_debug_stats_t *stats);
uint8_t subboard_startup_i2c_peek_reg(uint8_t reg);

#ifdef __cplusplus
}
#endif

#endif /* SUBBOARD_STARTUP_I2C_H */