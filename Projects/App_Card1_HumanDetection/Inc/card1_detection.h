#ifndef CARD1_DETECTION_H
#define CARD1_DETECTION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((packed)) {
    uint8_t  valid;
    uint8_t  count;
    uint8_t  type;
    uint8_t  selected_idx;
    int16_t  cx;
    int16_t  cy;
    int16_t  x1;
    int16_t  y1;
    int16_t  x2;
    int16_t  y2;
    int8_t   vx;
    int8_t   vy;
    uint8_t  gesture_type;
    uint8_t  face_id;
    uint8_t  confidence;
    uint8_t  reserved[11];
} card1_detection_result_t;

void card1_detection_init(void);
void card1_detection_poll(void);
bool card1_detection_get_latest(card1_detection_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CARD1_DETECTION_H */
