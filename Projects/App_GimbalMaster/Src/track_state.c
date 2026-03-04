/**
 * @file track_state.c
 * @brief 跟踪状态机 - 纯逻辑实现
 *
 * 设计原则:
 * - 纯逻辑: 无 RTOS/硬件/日志依赖
 * - 可测试: 所有函数可独立单元测试
 * - 线程安全由调用层保证
 */

#include "track_state.h"

/* 私有状态 - 调用层需保证线程安全 */
static TrackState_t g_track_state = TRACK_STATE_IDLE;

/* 状态名称表 */
static const char *const STATE_NAMES[] = {
    [TRACK_STATE_IDLE]     = "IDLE",
    [TRACK_STATE_TRACKING] = "TRACKING",
    [TRACK_STATE_LOCK]     = "LOCK",
    [TRACK_STATE_SEARCH]   = "SEARCH",
};

/* 事件名称表 */
static const char *const EVENT_NAMES[] = {
    [TRACK_EVT_START]        = "START",
    [TRACK_EVT_STOP]         = "STOP",
    [TRACK_EVT_TARGET_FOUND] = "TARGET_FOUND",
    [TRACK_EVT_TARGET_LOST]  = "TARGET_LOST",
    [TRACK_EVT_PHOTO]        = "PHOTO",
    [TRACK_EVT_RECORD_START] = "RECORD_START",
    [TRACK_EVT_RECORD_STOP]  = "RECORD_STOP",
};

const char *track_state_to_string(TrackState_t state)
{
    if (state < TRACK_STATE_COUNT) {
        return STATE_NAMES[state];
    }
    return "UNKNOWN";
}

const char *track_event_to_string(TrackEvent_t evt)
{
    if (evt < TRACK_EVT_COUNT) {
        return EVENT_NAMES[evt];
    }
    return "UNKNOWN";
}

void track_state_reset(void)
{
    g_track_state = TRACK_STATE_IDLE;
}

TrackState_t track_state_get(void)
{
    return g_track_state;
}

/**
 * @brief 计算下一状态 (纯函数)
 */
static TrackState_t compute_next_state(TrackState_t current, TrackEvent_t evt)
{
    switch (current) {
    case TRACK_STATE_IDLE:
        if (evt == TRACK_EVT_START) {
            return TRACK_STATE_TRACKING;
        }
        break;

    case TRACK_STATE_TRACKING:
        if (evt == TRACK_EVT_STOP) {
            return TRACK_STATE_IDLE;
        }
        if (evt == TRACK_EVT_TARGET_FOUND) {
            return TRACK_STATE_LOCK;
        }
        break;

    case TRACK_STATE_LOCK:
        if (evt == TRACK_EVT_STOP) {
            return TRACK_STATE_IDLE;
        }
        if (evt == TRACK_EVT_TARGET_LOST) {
            return TRACK_STATE_SEARCH;
        }
        break;

    case TRACK_STATE_SEARCH:
        if (evt == TRACK_EVT_STOP) {
            return TRACK_STATE_IDLE;
        }
        if (evt == TRACK_EVT_TARGET_FOUND) {
            return TRACK_STATE_LOCK;
        }
        break;

    default:
        return TRACK_STATE_IDLE;
    }

    return current; /* 无效事件，状态不变 */
}

TrackTransition_t track_state_process(TrackEvent_t evt)
{
    TrackTransition_t result;

    result.prev_state = g_track_state;
    result.event = evt;
    result.is_photo = (evt == TRACK_EVT_PHOTO);
    result.is_record_start = (evt == TRACK_EVT_RECORD_START);
    result.is_record_stop = (evt == TRACK_EVT_RECORD_STOP);

    if (result.is_photo || result.is_record_start || result.is_record_stop) {
        /* PHOTO/RECORD 业务事件不改变状态 */
        result.next_state = g_track_state;
        result.changed = false;
    } else {
        result.next_state = compute_next_state(g_track_state, evt);
        result.changed = (result.prev_state != result.next_state);
        g_track_state = result.next_state;
    }

    return result;
}
