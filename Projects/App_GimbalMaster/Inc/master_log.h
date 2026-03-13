#ifndef MASTER_LOG_H
#define MASTER_LOG_H

#include <stdio.h>

#define MASTER_LOG_LEVEL_WARN  1
#define MASTER_LOG_LEVEL_INFO  2
#define MASTER_LOG_LEVEL_DEBUG 3

#ifndef MASTER_LOG_LEVEL
#define MASTER_LOG_LEVEL MASTER_LOG_LEVEL_INFO
#endif

#if MASTER_LOG_LEVEL >= MASTER_LOG_LEVEL_WARN
#define MASTER_LOG_WARN(fmt, ...) printf("[WARN] " fmt, ##__VA_ARGS__)
#else
#define MASTER_LOG_WARN(...) ((void)0)
#endif

#if MASTER_LOG_LEVEL >= MASTER_LOG_LEVEL_INFO
#define MASTER_LOG_INFO(fmt, ...) printf("[INFO] " fmt, ##__VA_ARGS__)
#else
#define MASTER_LOG_INFO(...) ((void)0)
#endif

#if MASTER_LOG_LEVEL >= MASTER_LOG_LEVEL_DEBUG
#define MASTER_LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define MASTER_LOG_DEBUG(...) ((void)0)
#endif

#endif /* MASTER_LOG_H */