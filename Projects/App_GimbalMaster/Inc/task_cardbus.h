#ifndef TASK_CARDBUS_H
#define TASK_CARDBUS_H

#include <stdbool.h>
#include "i2c_cardbus.h"

#ifdef __cplusplus
extern "C" {
#endif

int task_cardbus_init(void);
int task_cardbus_start(void);
bool task_cardbus_get_snapshot(cardbus_snapshot_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TASK_CARDBUS_H */
