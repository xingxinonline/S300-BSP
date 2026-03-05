# App_GimbalMaster (MVP0 → MVP1)

本目录是 `gimbal_master` 板卡的主控应用入口，当前已完成 MVP0，并接入 MVP1 的 KWS 基础链路。

## 架构设计

遵循模块化、高内聚、低耦合原则：

```
┌─────────────────────────────────────────────────────────────┐
│                        main.c                               │
│                    (初始化 + 任务创建)                        │
└─────────────────┬───────────────────────────────────────────┘
                  │
    ┌─────────────┼─────────────┬─────────────────────────────┐
    ▼             ▼             ▼                             ▼
┌────────┐  ┌──────────┐  ┌──────────┐              ┌───────────┐
│command │  │ task_    │  │ heartbeat│              │ app_log   │
│_task   │  │ state    │  │ _task    │              │           │
└───┬────┘  └────┬─────┘  └──────────┘              └───────────┘
    │            │
    │ post()     │ wait()
    ▼            ▼
┌─────────────────────────┐      ┌─────────────────────────┐
│    track_events         │      │    track_state          │
│    (FreeRTOS Queue)     │◄────►│    (纯逻辑状态机)        │
└─────────────────────────┘      └─────────────────────────┘
```

### 模块职责

| 模块           | 职责                       | 依赖                      |
| -------------- | -------------------------- | ------------------------- |
| `track_state`  | 状态机纯逻辑，无 RTOS 依赖 | 无                        |
| `track_events` | 事件队列，任务间解耦       | FreeRTOS                  |
| `task_state`   | 状态机服务任务，消费事件   | track_state, track_events |
| `command_task` | 串口命令解析，生产事件     | track_events              |

### 设计原则

- **纯逻辑状态机**: `track_state.c` 无 RTOS/日志依赖，可独立单元测试
- **事件驱动解耦**: 任务间通过 `track_events` 队列通信
- **单一职责**: 每个模块只负责一个功能领域

## 功能

- FreeRTOS 启动与调度
- 心跳任务 (WS2812 LED + 1s 日志)
- 状态机 (IDLE/TRACKING/LOCK/SEARCH)
- 串口命令事件触发 (start/stop/found/lost/photo)
- KWS 任务 (Mailbox 轮询)
- 音频初始化骨架 (ES7210/ES8311 + I2S 基础配置)

## MVP1 当前进展

- 已打通 `KWS 结果 -> 事件队列 -> 状态机` 链路
- 已完成 ES7210/ES8311 初始化与 I2S 基础启动日志
- 已接入 I2S DMA 音频流处理与 DSP 轮询握手
- 新增 MVP2 最小显示初始化：OV5640 预初始化 + `init_video()`

## 编译

```bash
cmake -B build -G Ninja -DBOARD=gimbal_master .
ninja -C build s300_gimbal_master
```

## 运行验证

串口周期输出：

```text
[Heartbeat] tick=...
```

串口命令测试：

```text
help   - 显示帮助
start  - 启动跟踪
found  - 发现目标
lost   - 丢失目标
stop   - 停止跟踪
photo  - 拍照请求
state  - 查询当前状态
```
