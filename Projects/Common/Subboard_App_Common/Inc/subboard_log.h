#ifndef SUBBOARD_LOG_H
#define SUBBOARD_LOG_H

#include <stdio.h>

#define SUBBOARD_LOG_LEVEL_WARN  1
#define SUBBOARD_LOG_LEVEL_INFO  2
#define SUBBOARD_LOG_LEVEL_DEBUG 3

#ifndef SUBBOARD_LOG_LEVEL
#define SUBBOARD_LOG_LEVEL SUBBOARD_LOG_LEVEL_INFO
#endif

#if SUBBOARD_LOG_LEVEL >= SUBBOARD_LOG_LEVEL_WARN
#define SUB_LOG_WARN(fmt, ...) printf("[WARN] " fmt, ##__VA_ARGS__)
#else
#define SUB_LOG_WARN(...) ((void)0)
#endif

#if SUBBOARD_LOG_LEVEL >= SUBBOARD_LOG_LEVEL_INFO
#define SUB_LOG_INFO(fmt, ...) printf("[INFO] " fmt, ##__VA_ARGS__)
#else
#define SUB_LOG_INFO(...) ((void)0)
#endif

#if SUBBOARD_LOG_LEVEL >= SUBBOARD_LOG_LEVEL_DEBUG
#define SUB_LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define SUB_LOG_DEBUG(...) ((void)0)
#endif

#endif /* SUBBOARD_LOG_H */