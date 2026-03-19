#ifndef MASTER_DEMO_APP_H
#define MASTER_DEMO_APP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint8_t public_state;
	bool online;
	bool running;
	bool faulted;
} master_demo_subboard_state_t;

typedef struct {
	master_demo_subboard_state_t card1;
	master_demo_subboard_state_t card2;
	master_demo_subboard_state_t card3;
	bool any_running;
	bool any_error;
} master_demo_subboard_snapshot_t;

typedef enum {
	MASTER_DEMO_POLL_CARD1 = 0,
	MASTER_DEMO_POLL_CARD2 = 1,
	MASTER_DEMO_POLL_CARD3 = 2,
	MASTER_DEMO_POLL_ALL = 3,
} master_demo_poll_target_t;

int master_demo_app_init(uint32_t (*get_millis_fn)(void));
void master_demo_app_tick(void);
void master_demo_app_tick_target(master_demo_poll_target_t target);
bool master_demo_app_get_subboard_snapshot(master_demo_subboard_snapshot_t *snapshot);
int master_demo_app_finalize_subboard_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MASTER_DEMO_APP_H */