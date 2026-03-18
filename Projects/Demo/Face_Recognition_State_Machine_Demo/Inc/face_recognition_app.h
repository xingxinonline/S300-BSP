#ifndef FACE_RECOGNITION_APP_H
#define FACE_RECOGNITION_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void face_recognition_app_init(uint32_t (*get_millis_fn)(void));
void face_recognition_app_tick(void);

#ifdef __cplusplus
}
#endif

#endif