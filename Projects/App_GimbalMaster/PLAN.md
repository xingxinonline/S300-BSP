# Plan: Gimbal Master 云台主控应用实现

> **实现策略**: 采用渐进式 MVP 迭代，从最小闭环开始，逐步增加功能。每个阶段都有明确的验证标准，确保可测试、可验证。

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
MVP0 (基础框架)   MVP1 (视频显示)   MVP1.5 (子板通信)   MVP2 (舵机跟踪)   MVP3 (完整系统)
     │                │                  │                  │                │
     ▼                ▼                  ▼                  ▼                ▼
┌─────────┐     ┌──────────┐      ┌─────────────┐    ┌─────────────┐   ┌──────────┐
│ FreeRTOS │     │ 摄像头   │      │ I2C1通信   │    │ 检测框显示  │   │ KWS语音  │
│ +调试串口│  →  │ +CPLD    │  →   │ +子板轮询  │ →  │ +舵机跟踪  │ → │ +蓝牙HID │
│ +心跳LED │     │ +LCD显示 │      │ +数据解析  │    │ +IMU防抖   │   │ +UWB     │
└─────────┘     └──────────┘      └─────────────┘    └─────────────┘   └──────────┘
   2天              2天                2天               3天              3天
```

---

## MVP0: 基础框架 (Day 1-2)

**目标**: 建立项目骨架，FreeRTOS 运行正常，调试串口可用

### Step 0.1: 项目结构创建

**新建文件**:
```
Projects/App_GimbalMaster/
├── CMakeLists.txt
├── README.md
├── Inc/
│   └── app_config.h
└── Src/
    └── main.c
```

**CMakeLists.txt** (最小配置):
```cmake
cmake_minimum_required(VERSION 3.16)
project(App_GimbalMaster)

s300_add_executable(
    TARGET s300_gimbal_master
    SOURCES Src/main.c
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

**✅ MVP0 完成标准**:
- [ ] 编译通过，无警告
- [ ] FreeRTOS 调度正常
- [ ] 串口打印心跳信息
- [ ] LED 闪烁正常

---

## MVP1: 视频显示 (Day 3-4)

**目标**: 摄像头 → CPLD 分发 → 主板 LCD 显示 (此时无检测框)

### Step 1.1: 摄像头 + CPLD + LCD

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

**✅ MVP1 完成标准**:
- [ ] OV5640 初始化成功
- [ ] CPLD 视频分发正常
- [ ] 主板 LCD 显示实时画面
- [ ] 双缓冲无撕裂

---

## MVP1.5: 子板 I2C 通信 (Day 5-6)

**目标**: 主板通过 I2C1 轮询子板，获取检测结果

> **关键点**: 这是主板获取视觉检测数据的唯一途径！

### Step 1.5.1: I2C1 初始化

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

### Step 1.5.2: 子板数据读取

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

### Step 1.5.3: 轮询任务

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

**✅ MVP1.5 完成标准**:
- [ ] I2C1 初始化成功，扫描到 3 个子板
- [ ] 能正确读取 Card1 人形检测结果
- [ ] 能正确读取 Card2 人脸检测结果
- [ ] 能正确读取 Card3 手势检测结果
- [ ] 轮询周期稳定 < 15ms

---

## MVP2: 检测框显示 + 舵机跟踪 (Day 7-9)

**目标**: 子板检测结果 → LCD 检测框 → 舵机跟随

### Step 2.1: 检测框绘制

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

### Step 2.2: 舵机基础控制

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

### Step 2.3: 视觉跟踪 PID

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

### Step 2.3: IMU 防抖集成

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

**✅ MVP2 完成标准**:
- [ ] 舵机响应串口命令
- [ ] 视觉跟踪目标，舵机跟随移动
- [ ] PID 参数调优完成，无振荡
- [ ] IMU 防抖有效，画面稳定
- [ ] 跟踪任务周期稳定 20ms ± 2ms

---

## MVP3: 完整系统集成 (Day 9-11)

**目标**: KWS 语音控制 + 蓝牙 HID + UWB 辅助 + 手势交互

### Step 3.1: KWS 语音识别

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
- 创建 KWSTask (优先级 3)
- 音频采集 (DMA + I2S)
- Mailbox 接收 `MAILBOX_MSG_TYPE_KWS`
- 解析 `KWSResult_t`

**验证方法**:
```
1. 说 "启动跟随" → 串口打印 "[KWS] keyword=4 (START_TRACKING)"
2. 说 "结束跟随" → 串口打印 "[KWS] keyword=5 (STOP_TRACKING)"
3. 识别率 > 90%
```

### Step 3.2: 蓝牙 HID 控制

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

### Step 3.3: 跟踪状态机

**实现要点**:
```c
typedef enum {
    TRACK_STATE_IDLE,       // 待机
    TRACK_STATE_TRACKING,   // 跟踪中
    TRACK_STATE_LOCK,       // 锁定
    TRACK_STATE_SEARCH,     // 搜索
} TrackState_t;
```

- KWS "启动跟随" → IDLE → TRACKING
- 检测到目标 → TRACKING → LOCK
- 目标丢失 → LOCK → SEARCH
- KWS "结束跟随" → * → IDLE

**验证方法**:
```
1. 说 "启动跟随" → 状态变为 TRACKING
2. 人脸出现 → 状态变为 LOCK，舵机跟随
3. 人脸离开画面 → 状态变为 SEARCH
4. 说 "结束跟随" → 状态变为 IDLE，舵机停止
```

### Step 3.4: UWB 辅助定位 (可选)

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

### Step 3.5: 手势交互 (子板)

> 注：i2c_cardbus.h/c 已在 MVP1.5 中创建，此步骤复用该模块

**实现要点**:
- 复用 I2C1 子板通信模块 (MVP1.5)
- 轮询 Card3 (0x12) 手势检测结果中的 `gesture_type`
- Palm 手势 (gesture_type=5) → 切换跟踪状态
- Peace 手势 (gesture_type=6) → 触发蓝牙拍照

**验证方法**:
```
1. 对摄像头做 Palm 手势 → 跟踪启动/停止
2. 对摄像头做 Peace 手势 → 手机拍照
3. 响应延迟 < 500ms
```

**✅ MVP3 完成标准**:
- [ ] 语音指令识别正常
- [ ] 蓝牙配对并控制手机拍照
- [ ] 跟踪状态机转换正确
- [ ] UWB 辅助定位有效 (可选)
- [ ] 手势交互响应正常

---

## 项目最终结构

```
Projects/App_GimbalMaster/
├── CMakeLists.txt
├── README.md
├── PLAN.md                  # 本文档
├── Inc/
│   ├── app_config.h         # 应用配置
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

| 通信方向                | 机制       | 用途          |
| ----------------------- | ---------- | ------------- |
| KWSTask → TrackingTask  | EventGroup | 跟踪启停指令  |
| KWSTask → CommTask      | Queue      | 蓝牙 HID 请求 |
| CommTask → TrackingTask | Queue      | UWB 角度数据  |

---

## 风险与缓解

| 风险                | 缓解措施                                   |
| ------------------- | ------------------------------------------ |
| 子板 I2C 通信不稳定 | 先用 I2C 分析仪验证时序，软件加重试机制    |
| 舵机 PID 调参困难   | 参考 Gyroscope_Demo 已调好的参数           |
| DSP KWS 固件不兼容  | MVP3 先用 Audio_KWS_Demo 验证 Mailbox 协议 |
| 蓝牙配对失败        | 使用 AT 指令查询状态，确保 HID 模式        |
| 子板未响应          | 增加心跳检测，超时后重新初始化对应子板     |

