# DSP↔CM4 Mailbox 通信协议规范 v3.1

| 版本 | 日期       | 作者     | 变更说明                         |
| ---- | ---------- | -------- | -------------------------------- |
| v3.1 | 2026-02-13 | AI Agent | 修正命令编码冲突，同步文档与代码 |
| v3.0 | 2026-02-13 | AI Agent | 初版，支持双向命令控制           |

---

## 目录

1. [概述](#1-概述)
2. [消息格式](#2-消息格式)
3. [CM4→DSP 命令协议](#3-cm4dsp-命令协议)
4. [DSP→CM4 结果协议](#4-dspcm4-结果协议)
5. [数据结构定义](#5-数据结构定义)
6. [系统握手协议](#6-系统握手协议)
7. [流控与可靠性](#7-流控与可靠性)
8. [时序要求](#8-时序要求)
9. [错误处理](#9-错误处理)
10. [版本兼容性](#10-版本兼容性)
11. [CM4 实现参考](#11-cm4-实现参考)
12. [附录](#12-附录)

---

## 1. 概述

### 1.1 通信架构

```
┌────────────────┐                          ┌────────────────┐
│     CM4        │                          │     DSP        │
│   (RT-Thread)  │                          │  (CEVA Bare)   │
├────────────────┤                          ├────────────────┤
│  KWS 命令处理   │──── Mailbox CMD ──────▶│  tracker_cmd   │
│  跟踪器状态机   │                          │  SOT Tracker   │
│  云台 PID 控制  │◀─── Mailbox RESULT ────│  检测结果发送   │
│  IMU 读取       │──── Mailbox VEL ──────▶│  运动补偿       │
└────────────────┘                          └────────────────┘
```

### 1.2 Mailbox 地址映射

| 方向      | CM4 地址        | DSP 地址        | 说明     |
| --------- | --------------- | --------------- | -------- |
| CM4 → DSP | 写 `0x40019000` | 读 `0x44080408` | 命令通道 |
| DSP → CM4 | 读 `0x40019808` | 写 `0x44080400` | 结果通道 |

### 1.3 DSP 内存地址映射

| 视角     | 地址范围                    | 说明         |
| -------- | --------------------------- | ------------ |
| DSP 本地 | `0x00000000` ~ `0x0003FFFF` | PTCM 256KB   |
| CM4 访问 | `0x44800000` ~ `0x4483FFFF` | 需加偏移访问 |

**CM4 地址转换**: `m4_addr = dsp_addr + 0x44800000`

---

## 2. 消息格式

### 2.1 32-bit 消息通用结构

```
┌────────┬────────────────────────────┐
│ 31..28 │           27..0            │
│  Type  │          Payload           │
└────────┴────────────────────────────┘
```

| 字段    | 位      | 说明                             |
| ------- | ------- | -------------------------------- |
| Type    | [31:28] | 消息类型标识 (0-15)              |
| Payload | [27:0]  | 消息载荷（268MB 地址空间，足够） |

### 2.2 消息类型分配

| Type | 值    | 方向    | 名称      | 说明                 |
| ---- | ----- | ------- | --------- | -------------------- |
| 0x0  | `0x0` | DSP→CM4 | SINGLE    | 单目标结果（旧协议） |
| 0x1  | `0x1` | DSP→CM4 | MULTI     | 多目标结果指针       |
| 0x5  | `0x5` | 双向    | HANDSHAKE | 握手消息 (特殊处理)  |
| 0x8  | `0x8` | CM4→DSP | CMD       | 控制命令             |
| 0x9  | `0x9` | DSP→CM4 | ACK       | 命令确认 (v3.1 预留) |
| 0xA  | `0xA` | DSP→CM4 | NACK      | 命令拒绝 (v3.1 预留) |
| 0xE  | `0xE` | DSP→CM4 | STATUS    | 状态通知 (v3.1 预留) |
| 0xF  | `0xF` | DSP→CM4 | NO_DETECT | 本帧无检测           |

---

## 3. CM4→DSP 命令协议

### 3.1 命令消息格式 (Type = 0x8)

```
┌────────┬────────┬────────────────────┐
│ 31..28 │ 27..24 │       23..0        │
│  0x8   │ CmdGrp │      Payload       │
└────────┴────────┴────────────────────┘

CmdGrp: 命令组 (0-15)
Payload: 24-bit 参数空间
```

### 3.2 命令组分配

| CmdGrp | 用途     | 命令范围               |
| ------ | -------- | ---------------------- |
| 0x0    | 基础控制 | START/STOP/RESET_TRACK |
| 0x1    | 目标选择 | SELECT_TARGET          |
| 0x2    | 模式设置 | SET_SELECT_MODE        |
| 0x3    | 云台控制 | SET_GIMBAL_VEL         |
| 0x4    | 配置参数 | SET_CONFIG             |
| 0xF    | 系统命令 | PING                   |

### 3.3 命令定义表

| 命令            | 完整值       | Payload       | 说明                            |
| --------------- | ------------ | ------------- | ------------------------------- |
| **START_TRACK** | `0x80000001` | -             | 启用 SOT 跟踪器                 |
| **STOP_TRACK**  | `0x80000002` | -             | 停止跟踪（保持检测）            |
| **RESET_TRACK** | `0x80000003` | -             | 重置当前目标                    |
| SELECT_TARGET   | `0x81XXXXXX` | `X` = det_idx | 手动选择第 X 个检测框           |
| SET_SELECT_MODE | `0x82XXXXXX` | `X` = mode    | 选择模式 (0=中心,1=最大,2=手动) |
| SET_GIMBAL_VEL  | `0x83XXYYZZ` | 见 §3.4       | 设置云台角速度                  |
| SET_CONFIG      | `0x84XXPPVV` | 见 §3.5       | 设置配置参数                    |
| PING            | `0x8F000000` | -             | 心跳测试                        |

### 3.4 SET_GIMBAL_VEL 格式

```
命令: 0x83000000 | (pan_q8 << 8) | tilt_q8

┌────────────┬──────────┬──────────┬──────────┐
│   31..24   │  23..16  │  15..8   │   7..0   │
│    0x83    │ reserved │  pan_q8  │ tilt_q8  │
└────────────┴──────────┴──────────┴──────────┘

pan_q8:  有符号 Q8 定点数 (°/frame × 128), int8_t
tilt_q8: 有符号 Q8 定点数 (°/frame × 128), int8_t
范围: ±1.0°/frame
```

**编码示例**:
```c
// CM4: pan=0.5°/frame, tilt=-0.25°/frame
int8_t pan_q8  = (int8_t)(0.5f * 128);    // = 64 = 0x40
int8_t tilt_q8 = (int8_t)(-0.25f * 128);  // = -32 = 0xE0
uint32_t cmd = 0x83000000 | ((uint8_t)pan_q8 << 8) | (uint8_t)tilt_q8;
// cmd = 0x830040E0 ✓
```

**解码示例**:
```c
// DSP:
float pan  = (float)((int8_t)((cmd >> 8) & 0xFF)) / 128.0f;  // = 0.5
float tilt = (float)((int8_t)(cmd & 0xFF)) / 128.0f;         // = -0.25
```

### 3.5 SET_CONFIG 格式

```
命令: 0x84000000 | (param_id << 8) | value

┌────────────┬──────────┬──────────┬──────────┐
│   31..24   │  23..16  │  15..8   │   7..0   │
│    0x84    │ reserved │ param_id │  value   │
└────────────┴──────────┴──────────┴──────────┘
```

| param_id | 参数名          | 值范围 | 默认 | 说明               |
| -------- | --------------- | ------ | ---- | ------------------ |
| 0x01     | CONF_THRESHOLD  | 0-100  | 60   | 检测置信度阈值 (%) |
| 0x02     | IOU_THRESH      | 0-100  | 20   | IOU 匹配阈值 (%)   |
| 0x03     | APPEAR_THRESH   | 0-100  | 50   | 外观相似度阈值 (%) |
| 0x04     | MAX_LOST_FRAMES | 0-255  | 15   | 最大丢失搜索帧数   |
| 0x05     | SELECT_MODE     | 0-2    | 0    | 目标选择模式       |

**注**: SET_CONFIG 为 v3.1 预留，当前使用编译时常量。

---

## 4. DSP→CM4 结果协议

### 4.1 多目标消息 (Type = 0x1)

```
消息: 0x1XXXXXXX
       ├─ Type = 0x1
       └─ Payload = DetectionResult_t 的 DSP 本地地址 (27-bit)

CM4 访问: m4_ptr = (DetectionResult_t*)(0x44800000 + payload)
```

### 4.2 无检测消息 (Type = 0xF)

```
消息: 0xF0000000

表示本帧无任何检测结果。CM4 可跳过读取共享内存。
```

---

## 5. 数据结构定义

### 5.1 DetectionResult_t (704 bytes)

```c
typedef struct __attribute__((packed)) {
    uint32_t       magic;         // = 0x44455446 ("DETF")
    uint32_t       version;       // = 0x0301 (v3.1)
    uint32_t       frame_id;      // 帧序号（递增）
    uint16_t       timestamp_ms;  // 时间戳低16位（毫秒）
    uint8_t        tracker_state; // TrackerState_e
    uint8_t        tracker_flags; // TRACKER_FLAG_*
    uint32_t       count;         // 检测数量 [0, 10]
    int32_t        selected_idx;  // 锁定目标索引 (-1=无)
    DetectionBox_t boxes[10];     // 检测框数组 (68×10=680)
} DetectionResult_t;              // 24 + 680 = 704 bytes
```

### 5.2 DetectionBox_t (68 bytes)

```c
typedef struct __attribute__((packed)) {
    float    score;           // 置信度 [0.0, 1.0]           4B
    int32_t  x1, y1;          // 左上角坐标                  8B
    int32_t  x2, y2;          // 右下角坐标                  8B
    float    lm[10];          // 5个关键点 (x0,y0...x4,y4)  40B
    uint8_t  type;            // DetectionType_e             1B
    uint8_t  track_id;        // 跟踪ID                      1B
    int8_t   vx;              // X方向速度 (px/frame)        1B
    int8_t   vy;              // Y方向速度 (px/frame)        1B
    uint8_t  speed;           // 速度大小 (0-255)            1B
    uint8_t  kf_confidence;   // 卡尔曼置信度 (0-100)        1B
    uint8_t  miss_count;      // 连续漏检帧数                1B
    uint8_t  reserved;        // 保留                        1B
} DetectionBox_t;             // 总计 68 bytes
```

### 5.3 枚举类型

```c
// 检测类型
typedef enum {
    DETECTION_TYPE_UNKNOWN = 0,
    DETECTION_TYPE_FACE    = 1,
    DETECTION_TYPE_PERSON  = 2,
    DETECTION_TYPE_GESTURE = 3,
    DETECTION_TYPE_OBJECT  = 4,
} DetectionType_e;

// 跟踪器状态
typedef enum {
    TRACKER_STATE_DISABLED  = 0,  // 跟踪器禁用
    TRACKER_STATE_IDLE      = 1,  // 等待目标
    TRACKER_STATE_TENTATIVE = 2,  // 试探中
    TRACKER_STATE_TRACKING  = 3,  // 稳定跟踪
    TRACKER_STATE_LOST      = 4,  // 目标丢失
} TrackerState_e;
```

### 5.4 跟踪器标志位

```c
#define TRACKER_FLAG_ENABLED      0x01  // SOT 已启用
#define TRACKER_FLAG_REID_ACTIVE  0x02  // ReID 搜索中
#define TRACKER_FLAG_COASTING     0x04  // 预测滑行中
#define TRACKER_FLAG_APPEAR_VALID 0x08  // 外观特征有效
```

---

## 6. 系统握手协议

### 6.1 握手流程

```
                         power on
DSP: [WAIT_HANDSHAKE] ─────────────────
          │
CM4: ─────┼── 0x5A5A5A5A ──▶ DSP
          │
DSP: ─────┼── 0xA5A5A5A5 ──▶ CM4 (可选)
          │
DSP: [RUNNING] ◀───────────────────────
          │
DSP: ─────┴── 开始发送检测结果 ──▶ CM4
```

### 6.2 握手消息

| 消息           | 值           | 方向    | 说明     |
| -------------- | ------------ | ------- | -------- |
| HANDSHAKE_INIT | `0x5A5A5A5A` | CM4→DSP | 启动握手 |
| HANDSHAKE_ACK  | `0xA5A5A5A5` | DSP→CM4 | 握手确认 |

**注**: 握手消息为完整 32-bit 特殊值，不遵循 Type|Payload 格式。

### 6.3 超时处理

| 参数         | 值  | 说明               |
| ------------ | --- | ------------------ |
| 握手超时     | 3s  | CM4 超时后重发握手 |
| 最大重试次数 | 5   | 失败后报告硬件错误 |

---

## 7. 流控与可靠性

### 7.1 Mailbox FIFO 规格

| 参数      | 值                         |
| --------- | -------------------------- |
| FIFO 深度 | 16 条消息                  |
| 溢出行为  | 丢弃最新（DSP 非阻塞发送） |
| 下溢行为  | 返回 false（非阻塞读取）   |

### 7.2 流控策略

```c
// DSP 端：非阻塞发送
if (!mailbox_write_data_nb(msg)) {
    g_drop_count++;  // 统计丢包
}

// CM4 端：轮询读取，避免积压
while (!mailbox_is_empty()) {
    process_message(mailbox_read());
}
```

### 7.3 数据完整性校验

| 机制     | 实现         | 说明                 |
| -------- | ------------ | -------------------- |
| 魔数校验 | `0x44455446` | "DETF", 检测数据损坏 |
| 版本校验 | `0x0301`     | 协议兼容性           |
| 帧号递增 | `frame_id++` | 检测丢帧/乱序        |
| CRC      | ❌ 未实现     | 可选扩展             |

---

## 8. 时序要求

| 参数          | 值     | 说明                     |
| ------------- | ------ | ------------------------ |
| 命令响应延迟  | < 50ms | DSP 在下一帧处理命令     |
| 结果发送间隔  | ~50ms  | 约 20 FPS                |
| FIFO 排队深度 | 16     | 命令可排队               |
| CM4 读取超时  | 500ms  | 超时未收到结果应检查 DSP |
| 握手超时      | 3s     | 握手失败重试             |

---

## 9. 错误处理

### 9.1 DSP 端

```c
void tracker_cmd_process(uint32_t cmd) {
    uint32_t type = cmd & 0xF0000000;
    
    if (type != 0x80000000) {
        return;  // 忽略非命令消息
    }
    
    uint32_t grp = (cmd >> 24) & 0x0F;
    switch (grp) {
        case 0x0:  // 基础控制
            handle_basic_cmd(cmd);
            break;
        case 0x1:  // 目标选择
            handle_select_cmd(cmd & 0x00FFFFFF);
            break;
        case 0x3:  // 云台控制
            handle_gimbal_cmd(cmd);
            break;
        default:
            // 未知命令组，记录日志
            break;
    }
}
```

### 9.2 CM4 端

```c
bool verify_result(DetectionResult_t *r) {
    if (r->magic != 0x44455446) {
        return false;  // 数据损坏
    }
    if ((r->version >> 8) != 0x03) {
        return false;  // 主版本不兼容
    }
    return true;
}
```

---

## 10. 版本兼容性

| 协议版本 | 值       | 支持命令             | 兼容性       |
| -------- | -------- | -------------------- | ------------ |
| v1.0     | `0x0100` | 仅单目标结果         | 不推荐       |
| v2.x     | `0x02XX` | 多目标结果           | 向后兼容     |
| v3.0     | `0x0300` | 双向命令（旧格式）   | 需迁移       |
| **v3.1** | `0x0301` | **双向命令（修正）** | **当前版本** |

### 版本检查

```c
// 主版本兼容性检查
if ((result->version >> 8) != (DETECTION_PROTOCOL_VERSION >> 8)) {
    // 主版本不匹配，不兼容
}
```

---

## 11. CM4 实现参考

### 11.1 Mailbox 发送

```c
#define CM4_MAILBOX_BASE     0x40019000
#define CM4_MAILBOX_WRDATA   (*(volatile uint32_t*)(CM4_MAILBOX_BASE + 0x00))
#define CM4_MAILBOX_STATUS   (*(volatile uint32_t*)(CM4_MAILBOX_BASE + 0x10))
#define MAILBOX_FULL_FLAG    (1 << 1)

void mailbox_send_to_dsp(uint32_t cmd) {
    while (CM4_MAILBOX_STATUS & MAILBOX_FULL_FLAG) { }
    CM4_MAILBOX_WRDATA = cmd;
}
```

### 11.2 命令发送示例

```c
// 启用跟踪
mailbox_send_to_dsp(0x80000001);  // START_TRACK

// 停止跟踪
mailbox_send_to_dsp(0x80000002);  // STOP_TRACK

// 选择第 2 个检测框
mailbox_send_to_dsp(0x81000002);  // SELECT_TARGET, idx=2

// 设置云台角速度 (pan=0.5°/frame, tilt=-0.25°/frame)
int8_t pan_q8  = (int8_t)(0.5f * 128);    // = 64
int8_t tilt_q8 = (int8_t)(-0.25f * 128);  // = -32
uint32_t cmd = 0x83000000 | ((uint8_t)pan_q8 << 8) | (uint8_t)tilt_q8;
mailbox_send_to_dsp(cmd);  // = 0x830040E0

// 设置检测阈值为 70%
mailbox_send_to_dsp(0x84000146);  // SET_CONFIG: param=0x01, value=70=0x46

// 心跳测试
mailbox_send_to_dsp(0x8F000000);  // PING
```

### 11.3 KWS 命令映射

```c
void handle_kws_command(const char *cmd) {
    if (strncmp(cmd, "qidonggensui", 12) == 0) {
        mailbox_send_to_dsp(0x80000001);
    } else if (strncmp(cmd, "jieshugensui", 12) == 0) {
        mailbox_send_to_dsp(0x80000002);
    }
}
```

---

## 12. 附录

### 12.1 协议常量速查表

```c
// 消息类型
#define MSG_TYPE_SINGLE     0x00000000
#define MSG_TYPE_MULTI      0x10000000
#define MSG_TYPE_CMD        0x80000000
#define MSG_TYPE_ACK        0x90000000  // v3.1 预留
#define MSG_TYPE_NACK       0xA0000000  // v3.1 预留
#define MSG_TYPE_NO_DETECT  0xF0000000

// 基础控制命令 (CmdGrp=0x0)
#define CMD_START_TRACK     0x80000001
#define CMD_STOP_TRACK      0x80000002
#define CMD_RESET_TRACK     0x80000003

// 目标选择命令 (CmdGrp=0x1)
#define CMD_SELECT_TARGET   0x81000000  // | idx

// 模式设置命令 (CmdGrp=0x2)
#define CMD_SET_SELECT_MODE 0x82000000  // | mode

// 云台控制命令 (CmdGrp=0x3)
#define CMD_SET_GIMBAL_VEL  0x83000000  // | (pan<<8) | tilt

// 配置命令 (CmdGrp=0x4)
#define CMD_SET_CONFIG      0x84000000  // | (param<<8) | value

// 系统命令 (CmdGrp=0xF)
#define CMD_PING            0x8F000000

// 握手
#define HANDSHAKE_INIT      0x5A5A5A5A
#define HANDSHAKE_ACK       0xA5A5A5A5

// 校验
#define MAGIC_NUMBER        0x44455446  // "DETF"
#define PROTOCOL_VERSION    0x0301
```

### 12.2 相关文件

| 文件                                       | 说明             |
| ------------------------------------------ | ---------------- |
| `src/common/protocol.h`                    | 协议定义头文件   |
| `src/app/tracker_cmd.c`                    | DSP 命令处理实现 |
| `src/hal/mailbox.c`                        | Mailbox 驱动     |
| `docs/Gimbal_Tracking_System_Design_v3.md` | 系统设计文档     |

### 12.3 变更日志

- **v3.1** (2026-02-13):
  - 修正命令编码冲突：使用 CmdGrp [27:24] 隔离命令类型
  - SELECT_TARGET: `0x80000010` → `0x81000000`
  - SET_SELECT_MODE: `0x80000020` → `0x82000000`
  - SET_GIMBAL_VEL: `0x80000030` → `0x83000000`
  - SET_CONFIG: `0x80000040` → `0x84000000`
  - PING: `0x800000FF` → `0x8F000000`
  - 同步文档与代码结构定义

- **v3.0** (2026-02-13): 初版双向命令协议

- **v2.3**: 多目标协议，track_id, vx, vy

- **v2.0**: 多目标检测协议

- **v1.0**: 单目标检测协议（弃用）
