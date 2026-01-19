#ifndef _APP_MAILBOX_H_
#define _APP_MAILBOX_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init app mailbox logic
 */
void app_mailbox_init(void);

/**
 * @brief Provide a function to get current milliseconds (for timeout logic)
 * @param get_ms Function pointer returning uint32_t ms
 */
void app_mailbox_set_time_callback(uint32_t (*get_ms)(void));

/**
 * @brief Poll mailbox for new face/gesture results. Should be called in main loop.
 */
void app_mailbox_poll(void);

/**
 * @brief Check if face/gesture was detected recently (within timeout).
 * @return true if detected, false if timeout.
 */
bool app_mailbox_is_face_present(void);

#ifdef __cplusplus
}
#endif

#endif
