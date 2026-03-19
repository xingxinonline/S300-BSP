#include "subboard_startup_i2c.h"

#include <string.h>

#include "board.h"
#include "gpio.h"
#include "i2c.h"
#include "rcc.h"
#include "subboard_app_identity.h"
#include "subboard_startup_proto.h"

#define SUBBOARD_REG_MAP_SIZE 256u

static volatile uint8_t g_reg_map[SUBBOARD_REG_MAP_SIZE];
static volatile uint8_t g_reg_addr;
static volatile uint8_t g_tx_index;
static volatile uint8_t g_first_byte;

void I2C1_IRQHandler(void)
{
    uint32_t status = I2C_INTR_STAT(EM_I2C1);

    if ((status & EM_I2C_START_DET) != 0u) {
        (void)I2C_CLR_START_DET(EM_I2C1);
        g_first_byte = 1u;
    }

    if ((status & EM_I2C_RX_FULL) != 0u) {
        uint8_t data = i2c_slave_recv_byte(EM_I2C1);
        if (g_first_byte != 0u) {
            g_reg_addr = data;
            g_tx_index = data;
            g_first_byte = 0u;
        } else {
            g_reg_map[g_reg_addr] = data;
            g_reg_addr++;
        }
    }

    if ((status & EM_I2C_RD_REQ) != 0u) {
        (void)I2C_CLR_RD_REQ(EM_I2C1);
        i2c_slave_send_byte(EM_I2C1, g_reg_map[g_tx_index]);
        g_tx_index++;
    }

    if ((status & EM_I2C_RX_DONE) != 0u) {
        (void)I2C_CLR_RX_DONE(EM_I2C1);
    }

    if ((status & EM_I2C_STOP_DET) != 0u) {
        (void)I2C_CLR_STOP_DET(EM_I2C1);
    }

    if ((status & EM_I2C_RESTART_DET) != 0u) {
        (void)I2C_CLR_RESTART_DET(EM_I2C1);
    }
}

int subboard_startup_i2c_init(uint8_t slave_addr)
{
    i2c_slave_config_t cfg;

    memset((void *)g_reg_map, 0, sizeof(g_reg_map));
    g_reg_addr = 0u;
    g_tx_index = 0u;
    g_first_byte = 1u;

    g_reg_map[SUBBOARD_STARTUP_REG_STATUS] = SUBBOARD_STARTUP_STATUS_NONE;
    g_reg_map[SUBBOARD_STARTUP_REG_SYS_STATE] = SUBBOARD_STARTUP_STATE_BOOT;
    g_reg_map[SUBBOARD_STARTUP_REG_ERROR_CODE] = SUBBOARD_STARTUP_ERR_NONE;
    g_reg_map[SUBBOARD_STARTUP_REG_HEARTBEAT] = 0u;
    g_reg_map[SUBBOARD_STARTUP_REG_PROTO_VER] = SUBBOARD_STARTUP_PROTO_VER;
    g_reg_map[SUBBOARD_STARTUP_REG_REQUEST] = SUBBOARD_STARTUP_REQ_NONE;
    g_reg_map[SUBBOARD_STARTUP_REG_REQUEST_ARG] = 0u;
    g_reg_map[SUBBOARD_STARTUP_REG_REQUEST_ACK] = SUBBOARD_STARTUP_REQ_NONE;
    g_reg_map[SUBBOARD_STARTUP_REG_CAPABILITIES] = SUBBOARD_CAPABILITIES;
    g_reg_map[SUBBOARD_STARTUP_REG_CMD] = SUBBOARD_STARTUP_CMD_NONE;
    g_reg_map[SUBBOARD_STARTUP_REG_CMD_ARG] = 0u;
    g_reg_map[SUBBOARD_STARTUP_REG_CMD_ACK] = SUBBOARD_STARTUP_CMD_NONE;
    g_reg_map[SUBBOARD_STARTUP_REG_CMD_RESULT] = SUBBOARD_STARTUP_RESULT_OK;

    rcc_set_cortex_m4_apb1_clock(RCC_CM4_APB1_I2C1, true);
    gpio_set_function(GPIOA, 0u, FUNCTION_3);
    gpio_set_function(GPIOA, 1u, FUNCTION_3);

    cfg.slave_addr = slave_addr;
    cfg.speed = EM_I2C_400K;
    cfg.intr_mask = I2C_SLAVE_DEFAULT_INTR_MASK;
    cfg.callback = NULL;
    cfg.user_data = NULL;

    if (i2c_slave_init(EM_I2C1, &cfg) != 0) {
        return -1;
    }

    i2c_slave_irq_enable(EM_I2C1, true);
    return 0;
}

void subboard_startup_i2c_set_status(uint8_t status)
{
    __disable_irq();
    g_reg_map[SUBBOARD_STARTUP_REG_STATUS] = status;
    __enable_irq();
}

void subboard_startup_i2c_set_public_state(uint8_t state)
{
    __disable_irq();
    g_reg_map[SUBBOARD_STARTUP_REG_SYS_STATE] = state;
    __enable_irq();
}

void subboard_startup_i2c_set_error_code(uint8_t error_code)
{
    __disable_irq();
    g_reg_map[SUBBOARD_STARTUP_REG_ERROR_CODE] = error_code;
    __enable_irq();
}

void subboard_startup_i2c_update_result(const subboard_detection_result_t *result)
{
    if (result == NULL) {
        return;
    }

    __disable_irq();
    g_reg_map[SUBBOARD_STARTUP_REG_STATUS] = result->valid;
    memcpy((void *)&g_reg_map[SUBBOARD_STARTUP_REG_RESULT], result, sizeof(*result));
    __enable_irq();
}

void subboard_startup_i2c_update_tracking(const subboard_tracking_summary_t *summary)
{
    if (summary == NULL) {
        return;
    }

    __disable_irq();
    memcpy((void *)&g_reg_map[SUBBOARD_STARTUP_REG_TRACKING], summary, sizeof(*summary));
    __enable_irq();
}

void subboard_startup_i2c_bump_heartbeat(void)
{
    __disable_irq();
    g_reg_map[SUBBOARD_STARTUP_REG_HEARTBEAT]++;
    __enable_irq();
}

void subboard_startup_i2c_set_request(uint8_t request, uint8_t request_arg)
{
    __disable_irq();
    g_reg_map[SUBBOARD_STARTUP_REG_REQUEST] = request;
    g_reg_map[SUBBOARD_STARTUP_REG_REQUEST_ARG] = request_arg;
    g_reg_map[SUBBOARD_STARTUP_REG_REQUEST_ACK] = SUBBOARD_STARTUP_REQ_NONE;
    __enable_irq();
}

void subboard_startup_i2c_clear_request(void)
{
    __disable_irq();
    g_reg_map[SUBBOARD_STARTUP_REG_REQUEST] = SUBBOARD_STARTUP_REQ_NONE;
    g_reg_map[SUBBOARD_STARTUP_REG_REQUEST_ARG] = 0u;
    __enable_irq();
}

uint8_t subboard_startup_i2c_get_request_ack(void)
{
    uint8_t ack;

    __disable_irq();
    ack = g_reg_map[SUBBOARD_STARTUP_REG_REQUEST_ACK];
    __enable_irq();
    return ack;
}

uint8_t subboard_startup_i2c_get_command(void)
{
    uint8_t cmd;

    __disable_irq();
    cmd = g_reg_map[SUBBOARD_STARTUP_REG_CMD];
    __enable_irq();
    return cmd;
}

uint8_t subboard_startup_i2c_get_command_arg(void)
{
    uint8_t arg;

    __disable_irq();
    arg = g_reg_map[SUBBOARD_STARTUP_REG_CMD_ARG];
    __enable_irq();
    return arg;
}

void subboard_startup_i2c_set_command_result(uint8_t cmd, uint8_t result)
{
    __disable_irq();
    g_reg_map[SUBBOARD_STARTUP_REG_CMD_ACK] = cmd;
    g_reg_map[SUBBOARD_STARTUP_REG_CMD_RESULT] = result;
    __enable_irq();
}

void subboard_startup_i2c_clear_command(void)
{
    __disable_irq();
    g_reg_map[SUBBOARD_STARTUP_REG_CMD] = SUBBOARD_STARTUP_CMD_NONE;
    g_reg_map[SUBBOARD_STARTUP_REG_CMD_ARG] = 0u;
    __enable_irq();
}