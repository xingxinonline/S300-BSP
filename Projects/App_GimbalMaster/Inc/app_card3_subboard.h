#ifndef APP_GIMBAL_MASTER_CARD3_SUBBOARD_H
#define APP_GIMBAL_MASTER_CARD3_SUBBOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*app_card3_subboard_millis_fn_t)(void);
typedef int (*app_card3_subboard_read_regs_fn_t)(uint8_t slave_addr, uint8_t reg, uint8_t *buffer, uint32_t length);

typedef struct {
    app_card3_subboard_millis_fn_t millis_fn;
    app_card3_subboard_read_regs_fn_t read_regs_at;
} app_card3_subboard_ops_t;

int app_card3_subboard_init(const app_card3_subboard_ops_t *ops);
void app_card3_subboard_reset(void);
void app_card3_subboard_tick(void);

#ifdef __cplusplus
}
#endif

#endif