#include "card1_i2c_slave.h"

#include "rcc.h"
#include "gpio.h"
#include "i2c.h"

#include <string.h>

#define CARD1_REG_MAP_SIZE 256u

static volatile uint8_t g_reg_map[CARD1_REG_MAP_SIZE];
static volatile uint8_t g_reg_addr;
static volatile uint8_t g_tx_index;
static volatile uint8_t g_first_byte;

void I2C1_IRQHandler(void)
{
    uint32_t status = I2C_INTR_STAT(EM_I2C1);

    if (status & EM_I2C_START_DET) {
        (void)I2C_CLR_START_DET(EM_I2C1);
        g_first_byte = 1u;
    }

    if (status & EM_I2C_RX_FULL) {
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

    if (status & EM_I2C_RD_REQ) {
        (void)I2C_CLR_RD_REQ(EM_I2C1);
        i2c_slave_send_byte(EM_I2C1, g_reg_map[g_tx_index]);
        g_tx_index++;
    }

    if (status & EM_I2C_RX_DONE) {
        (void)I2C_CLR_RX_DONE(EM_I2C1);
    }

    if (status & EM_I2C_STOP_DET) {
        (void)I2C_CLR_STOP_DET(EM_I2C1);
    }

    if (status & EM_I2C_RESTART_DET) {
        (void)I2C_CLR_RESTART_DET(EM_I2C1);
    }
}

int card1_i2c_slave_init(uint8_t slave_addr)
{
    i2c_slave_config_t cfg;

    memset((void *)g_reg_map, 0, sizeof(g_reg_map));
    g_reg_addr = 0u;
    g_tx_index = 0u;
    g_first_byte = 1u;
    g_reg_map[CARD1_REG_STATUS] = 0u;
    g_reg_map[CARD1_REG_SYS_STATE] = CARD1_SYS_STATE_BOOT;
    g_reg_map[CARD1_REG_CMD] = CARD1_CMD_NONE;
    g_reg_map[CARD1_REG_CMD_ACK] = CARD1_CMD_NONE;

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

void card1_i2c_slave_update_result(const card1_detection_result_t *result)
{
    if (result == NULL) {
        return;
    }

    __disable_irq();
    g_reg_map[CARD1_REG_STATUS] = result->valid;
    memcpy((void *)&g_reg_map[CARD1_REG_RESULT], result, sizeof(*result));
    __enable_irq();
}

void card1_i2c_slave_set_system_state(uint8_t state_flags)
{
    __disable_irq();
    g_reg_map[CARD1_REG_SYS_STATE] = state_flags;
    __enable_irq();
}

uint8_t card1_i2c_slave_get_command(void)
{
    uint8_t cmd;

    __disable_irq();
    cmd = g_reg_map[CARD1_REG_CMD];
    __enable_irq();

    return cmd;
}

void card1_i2c_slave_clear_command(void)
{
    __disable_irq();
    g_reg_map[CARD1_REG_CMD] = CARD1_CMD_NONE;
    __enable_irq();
}

void card1_i2c_slave_set_command_ack(uint8_t cmd)
{
    __disable_irq();
    g_reg_map[CARD1_REG_CMD_ACK] = cmd;
    __enable_irq();
}
