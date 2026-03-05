#include "i2c_cardbus.h"

#include "app_log.h"

#include "board.h"
#include "gpio.h"
#include "i2c_soft.h"
#include "rcc.h"

#include <string.h>

#define CARDBUS_I2C_BUS_HZ   100000u
#define CARDBUS_MAX_RETRY     2u

static i2c_soft_t g_cardbus_i2c;
static uint8_t g_cardbus_ready;

static int cardbus_i2c_init(void)
{
    i2c_soft_cfg_t cfg;
    int ret;

    cfg.port = GPIOA;
    cfg.pin_scl = 0u;
    cfg.pin_sda = 1u;
    cfg.func_scl = FUNCTION_2;
    cfg.func_sda = FUNCTION_2;
    cfg.pull_mode = GPIO_UP;
    cfg.bus_hz = CARDBUS_I2C_BUS_HZ;

    set_cortex_m4_apb1_clock(RCC_CM4_APB1_GPIO, true);
    ret = i2c_soft_init(&g_cardbus_i2c, &cfg, SystemCoreClock);
    if (ret != 0) {
        app_log_printf("[CARDBUS] i2c init failed=%d\r\n", ret);
        return -1;
    }

    i2c_soft_bus_recover(&g_cardbus_i2c);
    g_cardbus_ready = 1u;
    return 0;
}

int i2c_cardbus_init(void)
{
    if (g_cardbus_ready != 0u) {
        return 0;
    }

#if !BOARD_I2C1_ENABLE
    app_log_puts("[CARDBUS] BOARD_I2C1_ENABLE=0\r\n");
    return -1;
#else
    return cardbus_i2c_init();
#endif
}

bool i2c_cardbus_is_ready(void)
{
    return (g_cardbus_ready != 0u);
}

int i2c_cardbus_probe(uint8_t addr)
{
    if (i2c_cardbus_init() != 0) {
        return -1;
    }

    return i2c_soft_probe(&g_cardbus_i2c, addr);
}

int i2c_cardbus_read_detection(uint8_t addr, card_detection_t *out)
{
    int ret = -1;

    if (out == NULL) {
        return -1;
    }

    if (i2c_cardbus_init() != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < CARDBUS_MAX_RETRY; i++) {
        ret = i2c_soft_mem_read(&g_cardbus_i2c,
                                addr,
                                CARDBUS_REG_RESULT,
                                false,
                                (uint8_t *)out,
                                (uint16_t)sizeof(*out));
        if (ret == 0) {
            return 0;
        }

        i2c_soft_bus_recover(&g_cardbus_i2c);
    }

    memset(out, 0, sizeof(*out));
    return -1;
}

int i2c_cardbus_write_reg8(uint8_t addr, uint8_t reg, uint8_t value)
{
    int ret;

    if (i2c_cardbus_init() != 0) {
        return -1;
    }

    ret = i2c_soft_mem_write(&g_cardbus_i2c,
                             addr,
                             reg,
                             false,
                             &value,
                             1u);
    if (ret == 0) {
        return 0;
    }

    i2c_soft_bus_recover(&g_cardbus_i2c);
    ret = i2c_soft_mem_write(&g_cardbus_i2c,
                             addr,
                             reg,
                             false,
                             &value,
                             1u);
    return (ret == 0) ? 0 : -1;
}

int i2c_cardbus_read_card1(card_detection_t *out)
{
    return i2c_cardbus_read_detection(CARDBUS_CARD1_ADDR, out);
}

int i2c_cardbus_read_card2(card_detection_t *out)
{
    return i2c_cardbus_read_detection(CARDBUS_CARD2_ADDR, out);
}

int i2c_cardbus_read_card3(card_detection_t *out)
{
    return i2c_cardbus_read_detection(CARDBUS_CARD3_ADDR, out);
}
