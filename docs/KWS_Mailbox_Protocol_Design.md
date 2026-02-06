# S300 KWS Mailbox 协议设计文档

## 文档版本

| 版本 | 日期       | 作者    | 说明                                     |
| ---- | ---------- | ------- | ---------------------------------------- |
| 1.0  | 2026-02-03 | Copilot | 初始版本，添加 Mailbox 支持              |
| 1.1  | 2026-02-03 | Copilot | 使用偏移地址方式，参考检测 Demo 统一架构 |
| 1.2  | 2026-02-03 | Copilot | 删除共享内存轮询模式，统一使用 Mailbox   |

## 1. 概述

本文档描述了 S300 Audio KWS (语音关键词识别) Demo 的 DSP 通信协议设计。

**当前实现**：统一使用 Mailbox 通信，与检测 Demo 保持一致的架构。

### 1.1 适用范围

- **算法模型**: Keyword_Spotting (语音关键词识别)
- **M4 Demo**: `Projects/Demo/Audio_KWS_Demo`
- **DSP 工程**: `.vscode/dsp_next_kws`
- **协议头文件**: `Algorithm_Models/protocol/kws_proto.h`

### 1.2 参考文档

- [mailbox_proto.h](../Drivers/SoC/MAILBOX/Include/mailbox_proto.h) - 通用邮箱消息框架
- [detection_proto.h](../Algorithm_Models/protocol/detection_proto.h) - 检测算法协议参考
- [kws_proto.h](../Algorithm_Models/protocol/kws_proto.h) - KWS 协议定义

---

## 2. DSP 现有代码分析

### 2.1 代码架构

```
.vscode/dsp_next_kws/
├── src/
│   ├── testbench_dsp.c      # 主程序入口
│   ├── kws/                  # KWS 推理引擎
│   │   ├── Infer.c/h         # 推理流水线
│   │   ├── LayerCNN.c/h      # CNN 卷积层
│   │   ├── LayerLinear.c/h   # 全连接层
│   │   ├── LayerQuant.c/h    # 量化层
│   │   ├── LayerAvePool.c/h  # 平均池化层
│   │   ├── Tensor.c/h        # 张量数据结构
│   │   ├── mfcc_10_float.c/h # MFCC 特征提取
│   │   └── LayerWeight.c/h   # 模型权重
│   └── vad/                  # VAD 语音活动检测
│       └── vad_core.c/h      # WebRTC VAD
├── dsp_csl/                  # CEVA DSP CSL 库
└── dsp_lib/                  # DSP 数学库
```

### 2.2 推理流水线

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  MFCC 特征  │ ──> │  VAD 检测   │ ──> │  KWS 推理   │
│  提取 (10D) │     │  (WebRTC)   │     │  (DS-CNN)   │
└─────────────┘     └─────────────┘     └─────────────┘
                           │
                    kws_state=1 时触发
```

**神经网络结构** (DS-CNN):
```
Input [84x10] → Quant → Conv3x3 → 
  → DSCNN_01 (DW3x3 + PW1x1) →
  → DSCNN_02 (DW3x3 + PW1x1) →
  → DSCNN_03 (DW3x3 + PW1x1) →
  → DSCNN_04 (DW3x3 + PW1x1) →
  → DSCNN_05 (DW3x3 + PW1x1) →
  → GlobalAvgPool → Linear → Output [45]
```

---

## 3. Mailbox 协议设计

### 3.1 设计原则

参考检测 Demo (`face_tracker.c`) 的 Mailbox 通信方式：
- **使用偏移地址**：消息 payload 为相对基址的偏移量
- **无额外共享内存**：结果结构体直接放在 DSP 内存，M4 通过偏移地址访问
- **统一消息格式**：与检测协议保持一致的 32-bit 消息格式

### 3.2 消息格式

```
┌──────────────┬────────────────────────────────┐
│  Bit [31:28] │  消息类型 (MsgType)             │
├──────────────┼────────────────────────────────┤
│  Bit [27:0]  │  结果偏移地址 (Offset)          │
└──────────────┴────────────────────────────────┘
```

### 3.3 DSP → M4 消息

| 类型                         | 值           | Payload 含义                            |
| ---------------------------- | ------------ | --------------------------------------- |
| `MAILBOX_MSG_TYPE_KWS`       | `0x20000000` | KWSResult 相对 `DSP_KWS_BASE_ADDR` 偏移 |
| `MAILBOX_MSG_TYPE_NO_RESULT` | `0xF0000000` | 本帧无识别结果                          |

### 3.4 M4 端读取方式

**参考检测 Demo 的偏移地址读取模式**：

```c
void kws_mailbox_poll(void)
{
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

        switch (msg_type) {
        case MAILBOX_MSG_TYPE_KWS: {
            /* 通过基址+偏移计算实际地址 */
            uintptr_t addr = (uintptr_t)DSP_KWS_BASE_ADDR + (uintptr_t)payload;
            const KWSResult_t *result = (const KWSResult_t*)addr;
            
            /* 校验 magic */
            if (KWS_RESULT_IS_VALID(result) && 
                KWS_IS_VALID_KEYWORD(result->keyword_idx)) {
                printf("[KWS] 识别: %u (置信度=%u)\n",
                       result->keyword_idx, result->confidence);
            }
            break;
        }
        case MAILBOX_MSG_TYPE_NO_RESULT:
            /* 本帧无识别结果 */
            break;
        }
    }
}
```

### 3.5 内存布局

```
DSP_KWS_BASE_ADDR (0x44810000)
│
├── Offset 0x0000: KWSResult_t (48 bytes)
│   ├── magic       (4B)  = 0x4B575352 ("KWSR")
│   ├── version     (4B)  = 0x0100
│   ├── frame_id    (4B)
│   ├── timestamp   (4B)
│   ├── keyword_idx (2B)
│   ├── confidence  (2B)
│   ├── scores[16]  (16B)
│   └── reserved    (12B)
│
├── Offset 0x0030: (可扩展其他结构)
│
└── ...
```

---

## 4. DSP 固件升级优化指南

### 4.1 升级目标

1. **统一通信协议**：使用 Mailbox 模式（与检测 Demo 一致）
2. **使用协议头文件**：引用 `kws_proto.h` 确保 M4/DSP 数据结构一致
3. **偏移地址通信**：通过基址 + 偏移访问结果结构体

### 4.2 需要修改的文件

```
.vscode/dsp_next_kws/src/
├── testbench_dsp.c    # 主要修改：添加 Mailbox 支持
└── kws/
    └── (无需修改推理代码)
```

### 4.3 DSP 端修改指南

#### 4.3.1 添加协议头文件

```c
/* testbench_dsp.c 头部添加 */
#include "kws_proto.h"       /* KWS 协议定义 */
#include "mailbox_proto.h"   /* 通用邮箱协议 */

/* 注意：需要将协议头文件复制到 DSP 工程的 include 路径 */
```

#### 4.3.2 定义结果结构体

替换现有的 `kws_cmd[45]` 为标准结构体：

```c
/* 旧代码 */
volatile uint8_t kws_cmd[COMMAND_NUM] __attribute__((section(".sram1_data")));

/* 新代码：使用标准结构体，放置在 DSP_KWS_BASE_ADDR */
static KWSResult_t kws_result __attribute__((section(".kws_result"), aligned(4)));

/* 初始化 */
void kws_result_init(void) {
    kws_result.magic = KWS_RESULT_MAGIC;
    kws_result.version = KWS_PROTOCOL_VERSION;
    kws_result.frame_id = 0;
}
```

#### 4.3.3 添加 Mailbox 发送函数

```c
/* Mailbox 基地址 (DSP 视角) */
#define DSP_MAILBOX_BASE    0x40019000

/* 发送 KWS 结果到 M4 */
static void send_kws_result(void) 
{
    /* 计算相对基址的偏移 */
    uint32_t offset = (uint32_t)&kws_result - DSP_KWS_BASE_ADDR;
    
    /* 构造消息：类型 + 偏移 */
    uint32_t msg = MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_KWS, offset);
    
    /* 发送邮箱消息 */
    *(volatile uint32_t*)(DSP_MAILBOX_BASE + 0x10) = msg;
}

/* 发送无结果消息 */
static void send_no_result(void)
{
    uint32_t msg = MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_NO_RESULT, 0);
    *(volatile uint32_t*)(DSP_MAILBOX_BASE + 0x10) = msg;
}
```

#### 4.3.4 修改主循环

```c
/* 旧代码：轮询模式 (已废弃) */
// if (input_ready_flag) {
//     input_ready_flag = 0;
//     for (int i = 0; i < COMMAND_NUM; i++) {
//         kws_cmd[i] = data_out[i];
//     }
//     output_ready_flag = 1;
// }

/* 新代码：Mailbox 模式 */
if (input_ready_flag) {
    input_ready_flag = 0;
    // ... 处理 ...
    
    /* 填充 KWSResult 结构体 */
    kws_result.frame_id++;
    kws_result.timestamp = get_timestamp_ms();
    
    /* 解析最大置信度关键词 */
    uint8_t max_val = 0;
    uint16_t max_idx = 0;
    for (int i = 0; i < COMMAND_NUM; i++) {
        kws_result.scores[i] = data_out[i];
        if (data_out[i] > max_val) {
            max_val = data_out[i];
            max_idx = i;
        }
    }
    kws_result.keyword_idx = max_idx;
    kws_result.confidence = max_val;
    
    /* 通过 Mailbox 通知 M4 */
    if (max_idx > 0 && max_val >= 90) {
        send_kws_result();
    } else {
        send_no_result();
    }
}
```

### 4.4 链接脚本修改

在 DSP 链接脚本中添加 KWS 结果段：

```ld
MEMORY
{
    /* ... 其他段 ... */
    KWS_RESULT (rw) : ORIGIN = 0x44810000, LENGTH = 0x1000
}

SECTIONS
{
    .kws_result : {
        *(.kws_result)
    } > KWS_RESULT
}
```

### 4.5 性能优化建议

#### 4.5.1 MFCC 特征提取优化

```c
/* 现有代码使用浮点运算 */
void get_mfcc_feature(float *frame, float *feature);

/* 优化建议：使用 CEVA DSP 库的定点 FFT */
#include "ceva_dsp_lib.h"

/* 定点 MFCC 可减少 ~30% 计算时间 */
void get_mfcc_feature_fixed(int16_t *frame, int16_t *feature);
```

#### 4.5.2 利用 DSP 向量指令

```c
/* 现有代码：标量循环 */
for (int i = 0; i < 256; i++) {
    output_block[i] = inPcm[i];
}

/* 优化：使用向量拷贝 */
vec_copy(output_block, inPcm, 256);
```

#### 4.5.3 VAD 与 KWS 流水线优化

```c
/* 现有代码：VAD 后立即 KWS */
if (kws_state == 1) {
    LayerForward(&tinput, &toutput, &kws);
}

/* 优化建议：跳帧推理 (每 2 帧推理一次，减少 50% 计算量) */
static int frame_skip = 0;
if (kws_state == 1 && (frame_skip++ & 1) == 0) {
    LayerForward(&tinput, &toutput, &kws);
}
```

---

## 5. 关键词映射

| 索引 | DSP 输出含义       | M4 枚举值                     | 中文       |
| ---- | ------------------ | ----------------------------- | ---------- |
| 0    | unknown            | `KWS_KEYWORD_UNKNOWN`         | 未识别     |
| 1    | pai-zhang-zhaopian | `KWS_KEYWORD_TAKE_PHOTO`      | 拍张照片   |
| 2    | kaishi-luxiang     | `KWS_KEYWORD_START_RECORDING` | 开始录像   |
| 3    | tingzhi-luxiang    | `KWS_KEYWORD_STOP_RECORDING`  | 停止录像   |
| 4    | qidong-gensui      | `KWS_KEYWORD_START_TRACKING`  | 启动跟随   |
| 5    | jieshu-gensui      | `KWS_KEYWORD_STOP_TRACKING`   | 结束跟随   |
| 6    | dakai-buguangdeng  | `KWS_KEYWORD_LIGHT_ON`        | 打开补光灯 |
| 7    | guanbi-buguangdeng | `KWS_KEYWORD_LIGHT_OFF`       | 关闭补光灯 |

> **注意**：DSP 模型支持 45 个关键词 (`COMMAND_NUM=45`)，但当前仅使用前 8 个。

---

## 6. 调试与验证

### 6.1 DSP 调试命令

```gdb
# 查看 KWS 结果结构体
x/12wx 0x44810000

# 查看 Mailbox 发送消息
x/wx 0x40019010

# 监控 DSP 处理周期
x/wx &dsp_calc_cycles
```

### 6.2 协议版本校验

```c
/* M4 端验证协议版本 */
if (result->version != KWS_PROTOCOL_VERSION) {
    printf("[KWS] 版本不匹配: DSP=0x%04X, M4=0x%04X\n",
           result->version, KWS_PROTOCOL_VERSION);
}
```

### 6.3 性能指标

| 指标              | 现有值 | 目标值  |
| ----------------- | ------ | ------- |
| MFCC 提取时间     | ~2ms   | < 1.5ms |
| KWS 推理时间      | ~8ms   | < 6ms   |
| 总帧处理时间      | ~12ms  | < 10ms  |
| 识别延迟 (端到端) | ~500ms | < 400ms |

---

## 7. 升级检查清单

### DSP 端

- [ ] 复制 `kws_proto.h` 和 `mailbox_proto.h` 到 DSP 工程
- [ ] 定义 `KWSResult_t` 结构体变量
- [ ] 修改链接脚本添加 `.kws_result` 段
- [ ] 实现 `send_kws_result()` 函数
- [ ] 修改主循环填充结构体并发送 Mailbox
- [ ] 移除 `output_ready_flag` 轮询相关代码
- [ ] 验证 Mailbox 消息格式

### M4 端

- [x] Mailbox 通信实现完成 (统一架构)
- [x] 偏移地址读取方式与检测 Demo 一致
- [ ] 测试关键词识别功能

---

## 8. 附录：检测 Demo Mailbox 通信参考

检测 Demo (`face_tracker.c`) 的 Mailbox 读取模式：

```c
void face_tracker_poll(void)
{
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

        switch (msg_type) {
        case MAILBOX_MSG_TYPE_MULTI: {
            /* 基址 + 偏移 = 实际地址 */
            uintptr_t addr = (uintptr_t)DSP_DETECTION_BASE_ADDR + (uintptr_t)payload;
            const DetectionResult_t *result = (const DetectionResult_t*)addr;
            
            if (DETECTION_RESULT_IS_VALID(result)) {
                /* 处理检测结果 */
            }
            break;
        }
        case MAILBOX_MSG_TYPE_NO_RESULT:
            break;
        }
    }
}
```

KWS Demo 应采用相同模式，使用 `DSP_KWS_BASE_ADDR` 作为基址。
