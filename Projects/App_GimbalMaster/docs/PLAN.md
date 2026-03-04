# Plan: Gimbal Master 云台主控应用实现

> **实现策略**: 采用渐进式 MVP 迭代，从最小闭环开始，逐步增加功能。每个阶段都有明确的验证标准，确保可测试、可验证。

---

## 设计原则

> **核心要求**: 模块化设计，高内聚、低耦合。所有迭代必须遵循以下原则。

### 1. 模块化 (Modular)

- **单一职责**: 每个模块/文件只负责一个功能领域
- **独立编译**: 模块可单独编译测试，不依赖其他模块实现
- **清晰边界**: 模块对外暴露最小接口（头文件），隐藏内部实现

```
✅ 正确示例:
   track_state.h  - 只暴露状态枚举和事件处理接口
   track_state.c  - 状态机内部实现，不暴露

❌ 错误示例:
   main.c 中直接写状态机 switch-case 逻辑
```

### 2. 高内聚 (High Cohesion)

- **功能聚合**: 相关功能放在同一模块，如所有 I2C 子板通信放在 `i2c_cardbus.c`
- **数据聚合**: 相关数据结构定义在同一头文件
- **生命周期一致**: 模块内资源（任务、队列、信号量）统一初始化和销毁

```c
/* 高内聚示例: i2c_cardbus 模块 */
// i2c_cardbus.h
void cardbus_init(void);           /* 初始化 */
int  cardbus_read_card1(card_detection_t *out);
int  cardbus_read_card2(card_detection_t *out);
int  cardbus_read_card3(card_detection_t *out);

// i2c_cardbus.c - 内部实现
static I2C_HandleTypeDef hi2c1;    /* 私有，不暴露 */
static uint8_t i2c_buffer[64];     /* 私有缓冲区 */
```

### 3. 低耦合 (Low Coupling)

- **接口依赖**: 模块间通过接口（头文件函数声明）交互，不直接访问内部变量
- **事件驱动**: 使用 FreeRTOS EventGroup/Queue 解耦任务，避免直接函数调用
- **依赖注入**: 回调函数或配置结构体传递，而非硬编码依赖

```c
/* 低耦合示例: KWS 通过事件通知状态机 */

// task_kws.c
void handle_kws_result(KWSResult_t *result) {
    // ❌ 错误: 直接调用状态机内部函数
    // g_track_state = TRACK_STATE_TRACKING;
    
    // ✅ 正确: 通过事件接口
    track_state_handle_event(TRACK_EVT_START);
}

// track_state.c
void track_state_handle_event(TrackEvent_t evt) {
    // 状态机内部处理，对外隐藏
}
```

### 4. 模块依赖规则

```
┌─────────────────────────────────────────────────────────────┐
│                        main.c                               │
│                    (初始化 + 任务创建)                        │
└─────────────────┬───────────────────────────────────────────┘
                  │ 初始化调用
    ┌─────────────┼─────────────┬─────────────┬───────────────┐
    ▼             ▼             ▼             ▼               ▼
┌────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   ┌───────────┐
│track   │  │ task_kws │  │ task_    │  │ i2c_     │   │ display_  │
│_state  │  │          │  │ tracking │  │ cardbus  │   │ overlay   │
└───┬────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘   └─────┬─────┘
    │            │             │             │               │
    │◄───────────┤ EventGroup  │             │               │
    │            │             │◄────────────┤ 共享结构体    │
    │◄───────────┼─────────────┤             │               │
    │            │             │─────────────┼───────────────►│
    ▼            ▼             ▼             ▼               ▼
┌─────────────────────────────────────────────────────────────┐
│                   硬件抽象层 (Drivers/)                      │
│   uart, gpio, i2c, spi, mailbox, video, bus_servo, ...      │
└─────────────────────────────────────────────────────────────┘
```

**依赖方向**: 上层依赖下层，禁止反向依赖

### 5. 代码审查检查清单

每次提交前检查：

- [ ] 新模块是否有独立的 `.h` 和 `.c` 文件？
- [ ] 头文件是否只暴露必要接口？（无 `static` 函数声明）
- [ ] 模块间是否通过接口通信？（无 `extern` 全局变量跨模块访问）
- [ ] 任务间是否通过 FreeRTOS 原语通信？（EventGroup/Queue/Semaphore）
- [ ] 新增依赖是否符合依赖方向？

---

## 系统架构说明

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              硬件架构                                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│    OV5640 ──DVP──► CPLD (XC2C128) ──┬──DVP0──► 主板 (LCD显示+录像)          │
│                    (1入4出分发)     ├──DVP1──► Card1 (人形检测)             │
│                                     ├──DVP2──► Card2 (人脸检测+识别)        │
│                                     └──DVP3──► Card3 (手势检测)             │
│                                                                             │
│    主板 ◄──I2C1──► Card1/Card2/Card3 (轮询检测结果)                         │
│                                                                             │
│    主板 DSP: 仅运行 KWS 语音识别 (Mailbox 通信)                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

**关键点**：
- **视觉检测在子板**: Card1(人形)、Card2(人脸)、Card3(手势) 各自独立推理
- **主板通过 I2C1 轮询子板**: 获取 `detection_result_t` 结构体
- **主板 DSP 仅运行 KWS**: 通过 Mailbox 返回 `KWSResult_t`

---

## 迭代路线图

```
MVP0 (框架+状态机)   MVP1 (KWS语音)   MVP2 (视频显示)   MVP3 (子板I2C)   MVP4 (舵机完整)
       │                 │                 │                │                │
       ▼                 ▼                 ▼                ▼                ▼
  ┌─────────┐      ┌──────────┐      ┌──────────┐     ┌──────────┐     ┌──────────┐
  │ FreeRTOS│      │ DSP KWS  │      │ 摄像头   │     │ I2C轮询  │     │ 舵机PID  │
  │ +状态机 │  →   │ +Mailbox │  →   │ +CPLD    │  →  │ +检测框  │  →  │ +蓝牙HID │
  │ (串口)  │      │ +语音状态│      │ +LCD     │     │ +状态触发│     │ +UWB+IMU │
  └─────────┘      └──────────┘      └──────────┘     └──────────┘     └──────────┘
     2天               2天               2天              2天              3天
```

**设计理由**:
1. **MVP0 先建状态机** - 串口命令模拟触发，建立状态流转框架
2. **MVP1 KWS 优先** - 语音是主要控制入口，验证 DSP Mailbox 通信
3. **MVP2 视频显示** - 独立于检测，LCD 显示摄像头画面
4. **MVP3 子板通信** - I2C 轮询检测结果，叠加检测框，完善状态触发
5. **MVP4 舵机完整** - 有了状态机和检测数据，舵机跟踪是执行层

---

## MVP0: 基础框架 + 状态机 (Day 1-2)

**目标**: 建立项目骨架，FreeRTOS 运行正常，状态机框架可用

### Step 0.1: 项目结构创建

**新建文件**:
```
Projects/App_GimbalMaster/
├── CMakeLists.txt
├── README.md
├── Inc/
│   ├── app_config.h
│   └── track_state.h
└── Src/
    ├── main.c
    └── track_state.c
```

**CMakeLists.txt** (最小配置):
```cmake
cmake_minimum_required(VERSION 3.16)
project(App_GimbalMaster)

s300_add_executable(
    TARGET s300_gimbal_master
    SOURCES Src/main.c Src/track_state.c
    DRIVERS rcc gpio uart
)

# FreeRTOS 依赖
target_link_libraries(s300_gimbal_master PRIVATE freertos_kernel)
```

**验证方法**:
```bash
cd build && ninja s300_gimbal_master
# 预期: 编译成功，生成 s300_gimbal_master.elf
```

### Step 0.2: FreeRTOS 最小系统

**main.c** 实现:
- `board_init()` 初始化
- 创建 1 个心跳任务 (LED 闪烁 + 串口打印)
- `vTaskStartScheduler()` 启动调度器

**验证方法**:
```
1. 烧录到开发板
2. 串口终端观察输出: "[Heartbeat] tick=1000, 2000, 3000..."
3. 观察 LED 以 1Hz 闪烁
```

### Step 0.3: 状态机骨架

**track_state.h** 定义:
```c
typedef enum {
    TRACK_STATE_IDLE,       /* 待机 - 不跟踪 */
    TRACK_STATE_TRACKING,   /* 跟踪中 - 等待检测 */
    TRACK_STATE_LOCK,       /* 锁定 - 检测到目标 */
    TRACK_STATE_SEARCH,     /* 搜索 - 目标丢失 */
} TrackState_t;

typedef enum {
    TRACK_EVT_START,        /* 启动跟踪 (语音/手势) */
    TRACK_EVT_STOP,         /* 停止跟踪 (语音/手势) */
    TRACK_EVT_TARGET_FOUND, /* 检测到目标 */
    TRACK_EVT_TARGET_LOST,  /* 目标丢失 */
    TRACK_EVT_PHOTO,        /* 拍照请求 */
} TrackEvent_t;

void track_state_init(void);
TrackState_t track_state_get(void);
void track_state_handle_event(TrackEvent_t evt);
```

**track_state.c** 实现:
- 状态机转换逻辑
- 串口打印状态变化

**串口命令模拟** (用于测试):
```
cmd: start   → TRACK_EVT_START   → IDLE→TRACKING
cmd: stop    → TRACK_EVT_STOP    → *→IDLE
cmd: found   → TRACK_EVT_TARGET_FOUND → TRACKING→LOCK
cmd: lost    → TRACK_EVT_TARGET_LOST  → LOCK→SEARCH
cmd: photo   → TRACK_EVT_PHOTO
```

**验证方法**:
```
1. 串口输入 "start" → 打印 "[STATE] IDLE → TRACKING"
2. 串口输入 "found" → 打印 "[STATE] TRACKING → LOCK"
3. 串口输入 "lost"  → 打印 "[STATE] LOCK → SEARCH"
4. 串口输入 "stop"  → 打印 "[STATE] SEARCH → IDLE"
```

**✅ MVP0 完成标准**:
- [ ] 编译通过，无警告
- [ ] FreeRTOS 调度正常
- [ ] 串口打印心跳信息
- [ ] LED 闪烁正常
- [ ] 状态机串口命令测试通过

---

## MVP1: KWS 语音识别 (Day 3-4)

**目标**: DSP 运行 KWS，Mailbox 通信，语音指令驱动状态机

### Step 1.1: 音频采集初始化

**新增文件**:
```
Inc/task_kws.h
Src/task_kws.c
```

**CMakeLists.txt 更新**:
```cmake
SOURCES ... Src/task_kws.c
DRIVERS ... mailbox i2s dma es7210 es8311
DEFINES ... BOARD_AUDIO_ENABLE=1
```

**实现要点**:
- 初始化 ES7210 麦克风 (`es7210_init()`)
- 初始化 ES8311 扬声器 (`es8311_init()`) - 用于播放提示音
- 配置 I2S + DMA 音频采集

**验证方法**:
```
1. 串口打印: "[AUDIO] ES7210 init OK"
2. 串口打印: "[AUDIO] I2S DMA running"
```

### Step 1.2: DSP 初始化 + Mailbox

**实现要点**:
- 初始化 DSP PLL (`rcc_init_dsp_pll()`)
- 复位 DSP (`set_dsp_warm_reset()`)
- 初始化 Mailbox (`init_mailbox()`)
- 创建 KWSTask (优先级 3)

**Mailbox 消息轮询**:
```c
/* 轮询 Mailbox 接收 KWS 结果 */
while (1) {
    if (mailbox_receive(&msg) == MAILBOX_MSG_TYPE_KWS) {
        KWSResult_t *result = (KWSResult_t *)msg.data;
        handle_kws_result(result);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
}
```

**验证方法**:
```
1. 串口打印: "[DSP] init OK, mailbox ready"
2. DSP 固件加载成功
```

### Step 1.3: KWS 结果处理

**关键字映射** (参考 kws_proto.h):
```c
typedef enum {
    KWS_KEYWORD_NONE = 0,
    KWS_KEYWORD_WAKEUP = 1,      /* "小信小信" */
    KWS_KEYWORD_START = 4,       /* "启动跟随" */
    KWS_KEYWORD_STOP = 5,        /* "结束跟随" */
    KWS_KEYWORD_PHOTO = 6,       /* "拍张照片" */
} KWSKeyword_t;
```

**实现要点**:
```c
void handle_kws_result(KWSResult_t *result) {
    switch (result->keyword) {
        case KWS_KEYWORD_START:
            track_state_handle_event(TRACK_EVT_START);
            break;
        case KWS_KEYWORD_STOP:
            track_state_handle_event(TRACK_EVT_STOP);
            break;
        case KWS_KEYWORD_PHOTO:
            track_state_handle_event(TRACK_EVT_PHOTO);
            break;
    }
}
```

**验证方法**:
```
1. 说 "启动跟随" → 打印 "[KWS] keyword=4" → "[STATE] IDLE → TRACKING"
2. 说 "结束跟随" → 打印 "[KWS] keyword=5" → "[STATE] * → IDLE"
3. 说 "拍张照片" → 打印 "[KWS] keyword=6" → "[STATE] PHOTO event"
4. 识别率 > 90%
```

**✅ MVP1 完成标准**:
- [ ] ES7210/ES8311 初始化成功
- [ ] DSP 启动，Mailbox 通信正常
- [ ] 语音 "启动跟随" 触发状态变为 TRACKING
- [ ] 语音 "结束跟随" 触发状态变为 IDLE
- [ ] 语音 "拍张照片" 触发 PHOTO 事件

---

## MVP2: 视频显示 (Day 5-6)

**目标**: 摄像头 → CPLD 分发 → 主板 LCD 显示 (此时无检测框)

### Step 2.1: 摄像头 + CPLD + LCD

**新增文件**:
```
Inc/display_overlay.h
Src/display_overlay.c
```

**CMakeLists.txt 更新**:
```cmake
SOURCES Src/main.c Src/display_overlay.c
DRIVERS rcc gpio uart video psram ov5640
DEFINES BOARD_MM_ENABLE=1 BOARD_CAMERA_ENABLE=1
```

**实现要点**:
- 主板 I2C3 初始化 OV5640 (`camera_ov5640_preinit()`)
- CPLD 自动将视频分发到 DVP0/1/2/3
- 初始化视频子系统 (`init_video()`)
- 初始化双缓冲帧缓冲 (DISP_RFRAME0/1_ADDR)

**验证方法**:
```
1. 烧录到主板
2. LCD 显示摄像头实时画面
3. 画面流畅无撕裂
4. 确认子板也能收到视频 (串口打印帧率)
```

**✅ MVP2 完成标准**:
- [ ] OV5640 初始化成功
- [ ] CPLD 视频分发正常
- [ ] 主板 LCD 显示实时画面
- [ ] 双缓冲无撕裂

---

## MVP3: 子板 I2C 通信 + 检测框显示 (Day 7-8)

**目标**: 主板通过 I2C1 轮询子板，获取检测结果，LCD 叠加检测框

> **关键点**: 这是主板获取视觉检测数据的唯一途径！

### Step 3.1: I2C1 初始化

**新增文件**:
```
Inc/i2c_cardbus.h
Src/i2c_cardbus.c
```

**CMakeLists.txt 更新**:
```cmake
SOURCES ... Src/i2c_cardbus.c
DRIVERS ... i2c_soft
DEFINES ... BOARD_I2C1_ENABLE=1
```

**I2C1 配置** (参考 board.h):
- SCL: PA2, SDA: PA1 (软件 I2C)
- 频率: 100kHz
- 子板地址: Card1=0x10, Card2=0x11, Card3=0x12

**实现要点**:
```c
/* 子板地址定义 */
#define CARD1_ADDR  0x10  /* 人形检测 */
#define CARD2_ADDR  0x11  /* 人脸检测+识别 */
#define CARD3_ADDR  0x12  /* 手势检测 */

/* 寄存器定义 */
#define REG_STATUS  0x00  /* 状态寄存器 (1字节) */
#define REG_RESULT  0x10  /* 检测结果 (32字节) */
```

**验证方法**:
```
1. I2C 扫描: 发现 0x10, 0x11, 0x12 三个设备
2. 读取 REG_STATUS 返回有效值
3. 使用逻辑分析仪验证 I2C 时序
```

### Step 3.2: 子板数据读取

**实现要点**:
```c
/* 检测结果结构体 (与子板共享) */
typedef struct __attribute__((packed)) {
    uint8_t  valid;         /* 结果有效标志 */
    uint8_t  count;         /* 检测目标数量 */
    uint8_t  type;          /* 检测类型: 1=人脸, 2=人形, 3=手势 */
    uint8_t  selected_idx;  /* 选中目标索引 */
    int16_t  cx, cy;        /* 目标中心坐标 */
    int16_t  x1, y1, x2, y2;/* 边界框坐标 */
    int8_t   vx, vy;        /* 速度向量 */
    uint8_t  gesture_type;  /* 手势类型: 5=Palm, 6=Peace */
    uint8_t  face_id;       /* 人脸ID (用于找回) */
    uint8_t  confidence;    /* 置信度 [0-100] */
    uint8_t  reserved[14];  /* 预留，总计32字节 */
} card_detection_t;

/* 读取子板检测结果 */
int card_read_detection(uint8_t addr, card_detection_t *result);
```

**验证方法**:
```
1. 对摄像头展示人脸 → Card2 返回 valid=1, type=1
2. 对摄像头展示人形 → Card1 返回 valid=1, type=2
3. 对摄像头做手势 → Card3 返回 valid=1, gesture_type=5或6
4. 串口打印: "[Card1] valid=1 cx=160 cy=120"
```

### Step 3.3: 轮询任务

**实现要点**:
- 创建 CardPollTask (50Hz)
- 依次轮询 Card1 → Card2 → Card3
- 将结果存入共享结构体供 TrackingTask 使用

**轮询时序**:
```
时间线 (20ms 周期):
 0ms  ├── 读取 Card1 (人形检测) ~3ms
 3ms  ├── 读取 Card2 (人脸检测) ~3ms
 6ms  ├── 读取 Card3 (手势检测) ~3ms
 9ms  ├── 数据处理
20ms  └── 下一周期
```

**验证方法**:
```
1. 串口打印每个子板的读取耗时
2. 总轮询时间 < 15ms
3. 无 I2C 通信错误
```

**✅ MVP3 完成标准**:
- [ ] I2C1 初始化成功，扫描到 3 个子板
- [ ] 能正确读取 Card1 人形检测结果
- [ ] 能正确读取 Card2 人脸检测结果
- [ ] 能正确读取 Card3 手势检测结果
- [ ] 轮询周期稳定 < 15ms
- [ ] 检测框正确叠加在视频上
- [ ] 检测到目标时状态转为 LOCK
- [ ] 目标丢失时状态转为 SEARCH

---

## MVP4: 舵机跟踪 + 蓝牙 + UWB (Day 9-11)

**目标**: 舵机跟随目标 + 蓝牙 HID 拍照 + UWB 辅助定位

### Step 4.1: 检测框绘制

**新增文件**:
```
Inc/tracking_visualizer.h
Src/tracking_visualizer.c
```

**实现要点**:
- 从 CardPollTask 获取检测结果
- 移植 `draw_color_rect_border()` 绘制边界框
- 移植 `draw_velocity_arrow()` 绘制速度箭头
- 实现双缓冲交换 (`swap_buffers()`)

**颜色映射**:
| 子板  | 检测类型 | 边界框颜色  |
| ----- | -------- | ----------- |
| Card1 | 人形     | 绿色 (选中) |
| Card2 | 人脸     | 蓝色        |
| Card3 | 手势     | 青色图标    |

**验证方法**:
```
1. 人形出现 → LCD 显示绿色边界框
2. 人脸出现 → LCD 显示蓝色边界框
3. 做手势 → LCD 显示手势图标
4. 边界框跟随目标移动，无撕裂
```

### Step 4.2: 舵机基础控制

**新增文件**:
```
Inc/task_tracking.h
Src/task_tracking.c
```

**CMakeLists.txt 更新**:
```cmake
SOURCES ... Src/task_tracking.c
DRIVERS ... bus_servo
DEFINES ... BOARD_SERVO_ENABLE=1
```

**实现要点**:
- 创建 TrackingTask (50Hz, 优先级 5)
- 初始化舵机控制器 (`board_servo_init()`)
- 实现角度控制接口 (`servo_set_angle(pan, tilt)`)

**验证方法**:
```
1. 串口命令: "servo pan 90" → 舵机转到 90°
2. 串口命令: "servo tilt 45" → 舵机转到 45°
3. 舵机运动平滑无抖动
```

### Step 4.3: 视觉跟踪 PID

**实现要点**:
- 从 Card1 (人形检测) 获取目标中心坐标
- 计算目标与画面中心的偏差
- 实现 PID 控制器 (参考 Gyroscope_Demo)
- 输出控制量到舵机

**PID 参数初值**:
```c
#define PID_KP  0.5f
#define PID_KI  0.01f
#define PID_KD  0.1f
```

**验证方法**:
```
1. 目标在画面中心时，舵机静止
2. 目标向左移动，舵机向左转
3. 目标向右移动，舵机向右转
4. 跟踪延迟 < 200ms
```

### Step 4.4: IMU 防抖集成

**CMakeLists.txt 更新**:
```cmake
DRIVERS ... qmi8658a imu
DEFINES ... BOARD_IMU_ENABLE=1
```

**实现要点**:
- 初始化 QMI8658A (`qmi8658a_init()`)
- TrackingTask 中读取 IMU 数据
- 计算姿态补偿角度
- 融合视觉 + IMU 输出最终控制量

**验证方法**:
```
1. 启用 IMU 防抖，晃动云台底座
2. 舵机自动补偿抖动，画面稳定
3. 比较开启/关闭防抖的画面稳定性
```

**✅ MVP4 完成标准**:
- [ ] 检测框正确叠加在视频上
- [ ] 速度箭头方向正确
- [ ] 舵机响应串口命令
- [ ] 视觉跟踪目标，舵机跟随移动
- [ ] PID 参数调优完成，无振荡
- [ ] IMU 防抖有效，画面稳定
- [ ] 跟踪任务周期稳定 20ms ± 2ms
- [ ] 蓝牙配对并控制手机拍照
- [ ] UWB 辅助定位有效 (可选)
- [ ] 手势交互响应正常

### Step 4.5: 蓝牙 HID 控制

**新增文件**:
```
Inc/ble_xm04.h
Src/ble_xm04.c
Inc/task_comm.h
Src/task_comm.c
```

**实现要点**:
- 移植 xm04.c 蓝牙驱动
- 创建 CommTask (优先级 2)
- 实现 `ble_send_volume_up()` 发送拍照键

**验证方法**:
```
1. 蓝牙与手机配对成功
2. 说 "拍张照片" → KWSTask 通知 CommTask → 手机相机拍照
3. 手机相机 App 中验证拍照功能
```

### Step 4.6: UWB 辅助定位 (可选)

**新增文件**:
```
Inc/uwb_ulm3.h
Src/uwb_ulm3.c
```

**实现要点**:
- 移植 uwb_ulm3_pdoa.c 驱动
- UART2_RX 中断接收 AOA 数据
- SEARCH 状态时使用 UWB 角度引导云台

**验证方法**:
```
1. 目标佩戴 UWB 标签
2. 视觉丢失后，状态变为 SEARCH
3. 云台根据 UWB AOA 角度转向
4. 目标重新出现在画面中
```

### Step 4.7: 手势交互

> 注：i2c_cardbus.h/c 已在 MVP3 中创建，此步骤复用该模块

**实现要点**:
- 复用 I2C1 子板通信模块 (MVP3)
- 轮询 Card3 (0x12) 手势检测结果中的 `gesture_type`
- Palm 手势 (gesture_type=5) → 切换跟踪状态
- Peace 手势 (gesture_type=6) → 触发蓝牙拍照

**验证方法**:
```
1. 对摄像头做 Palm 手势 → 跟踪启动/停止
2. 对摄像头做 Peace 手势 → 手机拍照
3. 响应延迟 < 500ms
```

---

## 项目最终结构

```
Projects/App_GimbalMaster/
├── CMakeLists.txt
├── README.md
├── PLAN.md                  # 本文档
├── Inc/
│   ├── app_config.h         # 应用配置
│   ├── track_state.h        # 状态机定义
│   ├── task_tracking.h      # 跟踪任务
│   ├── task_kws.h           # KWS 任务
│   ├── task_comm.h          # 通信任务
│   ├── i2c_cardbus.h        # 子板通信
│   ├── uwb_ulm3.h           # UWB 驱动
│   ├── ble_xm04.h           # 蓝牙驱动
│   ├── display_overlay.h    # 显示 Overlay
│   └── tracking_visualizer.h# 跟踪可视化
└── Src/
    ├── main.c               # 主入口
    ├── track_state.c        # 状态机实现
    ├── task_tracking.c      # 跟踪任务
    ├── task_kws.c           # KWS 任务
    ├── task_comm.c          # 通信任务
    ├── i2c_cardbus.c        # 子板通信
    ├── uwb_ulm3.c           # UWB 驱动
    ├── ble_xm04.c           # 蓝牙驱动
    ├── display_overlay.c    # 显示 Overlay
    └── tracking_visualizer.c# 跟踪可视化
```

---

## 参考资源

| 功能模块    | 参考代码路径                                                                                                                                             |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 显示+检测框 | [Projects/Demo/Algorithm_Tracking_Demo/Src/face_tracker.c](Projects/Demo/Algorithm_Tracking_Demo/Src/face_tracker.c)                                     |
| 舵机控制    | [Projects/Demo/BusServo_Demo/](Projects/Demo/BusServo_Demo/)                                                                                             |
| IMU 防抖    | [Projects/Demo/Gyroscope_Demo/](Projects/Demo/Gyroscope_Demo/)                                                                                           |
| KWS 语音    | [Projects/Demo/Audio_KWS_Demo/](Projects/Demo/Audio_KWS_Demo/)                                                                                           |
| 蓝牙 HID    | [.vscode/cortex-m4-null/app/xm04.c](.vscode/cortex-m4-null/app/xm04.c)                                                                                   |
| UWB 定位    | [.vscode/gimbal_demo_20260122_161807/cortex-m4/externdevice/uwb_ulm3_pdoa.c](.vscode/gimbal_demo_20260122_161807/cortex-m4/externdevice/uwb_ulm3_pdoa.c) |
| 协议定义    | [Algorithm_Models/protocol/](Algorithm_Models/protocol/)                                                                                                 |

---

## 技术规格

### FreeRTOS 任务配置

| 任务名       | 优先级 | 栈大小    | 周期  | 职责                         |
| ------------ | ------ | --------- | ----- | ---------------------------- |
| TrackingTask | 5      | 512 words | 20ms  | 视觉跟踪 + IMU + 舵机 + 显示 |
| CardPollTask | 4      | 256 words | 20ms  | I2C 轮询子板检测结果        |
| KWSTask      | 3      | 256 words | 事件  | 语音识别结果处理             |
| CommTask     | 2      | 256 words | 100ms | UWB + 蓝牙 HID               |

### 颜色定义 (RGB565)

| 颜色         | 值     | 用途             |
| ------------ | ------ | ---------------- |
| COLOR_GREEN  | 0x07E0 | 选中目标边界框   |
| COLOR_BLUE   | 0x001F | 非选中目标边界框 |
| COLOR_CYAN   | 0x07FF | 速度箭头         |
| COLOR_RED    | 0xF800 | 丢失/警告        |
| COLOR_YELLOW | 0xFFE0 | 搜索状态         |

### 速度箭头配置

```c
#define ARROW_SHOW_THRESHOLD  2     /* 显示阈值 */
#define ARROW_HIDE_THRESHOLD  1     /* 隐藏阈值（滞后防抖）*/
#define ARROW_SCALE           8     /* 速度到箭头长度比例 */
```

### 子板 I2C 通信

| 子板  | I2C 地址 | 检测类型 | 边界框样式 |
| ----- | -------- | -------- | ---------- |
| Card1 | 0x10     | 人形检测 | 绿色实线框 |
| Card2 | 0x11     | 人脸检测 | 蓝色虚线框 |
| Card3 | 0x12     | 手势检测 | 青色图标   |

### 任务间通信

| 通信方向                    | 机制       | 用途          |
| --------------------------- | ---------- | ------------- |
| KWSTask → TrackingTask      | EventGroup | 跟踪启停指令  |
| KWSTask → CommTask          | Queue      | 蓝牙 HID 请求 |
| CardPollTask → TrackingTask | 共享结构体 | 子板检测结果  |
| CommTask → TrackingTask     | Queue      | UWB 角度数据  |

---

## 风险与缓解

| 风险                | 缓解措施                                   |
| ------------------- | ------------------------------------------ |
| 子板 I2C 通信不稳定 | 先用 I2C 分析仪验证时序，软件加重试机制    |
| 舵机 PID 调参困难   | 参考 Gyroscope_Demo 已调好的参数           |
| DSP KWS 固件不兼容  | MVP1 先用 Audio_KWS_Demo 验证 Mailbox 协议 |
| 蓝牙配对失败        | 使用 AT 指令查询状态，确保 HID 模式        |
| 子板未响应          | 增加心跳检测，超时后重新初始化对应子板     |

