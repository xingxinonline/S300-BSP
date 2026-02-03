# Algorithm Models Protocol

本目录包含 M4 与 DSP 之间的邮箱通信协议定义。

## 协议架构

```
Drivers/SoC/MAILBOX/Include/
└── mailbox_proto.h          # 通用邮箱消息框架

Algorithm_Models/protocol/
├── detection_proto.h         # 检测/追踪算法 payload
├── kws_proto.h               # 语音识别算法 payload
└── recognition_proto.h       # 人脸识别算法 payload (待添加)
```

## 消息格式

32-bit 邮箱消息格式：

```
┌──────────────┬────────────────────────────────┐
│  Bit [31:28] │  消息类型 (MsgType)             │
├──────────────┼────────────────────────────────┤
│  Bit [27:0]  │  消息数据 (Payload/Offset)      │
└──────────────┴────────────────────────────────┘
```

### DSP → M4 消息类型

| 类型        | 值         | 说明                     |
| ----------- | ---------- | ------------------------ |
| SINGLE      | 0x00000000 | 单目标检测（旧协议兼容） |
| MULTI       | 0x10000000 | 多目标检测               |
| KWS         | 0x20000000 | 语音识别结果             |
| RECOGNITION | 0x30000000 | 人脸识别结果             |
| NO_RESULT   | 0xF0000000 | 本帧无结果               |

### M4 → DSP 命令类型

| 命令            | 值         | 说明          |
| --------------- | ---------- | ------------- |
| START           | 0x80000000 | 启动检测/追踪 |
| STOP            | 0x90000000 | 停止检测/追踪 |
| RESET_SELECTION | 0xA0000000 | 重置选中目标  |
| SWITCH_MODEL    | 0xB0000000 | 切换模型      |

## 使用示例

### 检测/追踪

```c
#include "mailbox.h"
#include "mailbox_proto.h"
#include "detection_proto.h"

void process_mailbox(void)
{
    uint32_t msg = read_mailbox(MAILBOX_BASE);
    uint32_t type = MAILBOX_GET_MSG_TYPE(msg);
    uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

    switch (type) {
    case MAILBOX_MSG_TYPE_MULTI: {
        uintptr_t addr = DSP_DETECTION_BASE_ADDR + payload;
        const DetectionResult_t *result = (const DetectionResult_t*)addr;
        
        if (DETECTION_RESULT_IS_VALID(result)) {
            for (uint32_t i = 0; i < result->count; i++) {
                // 处理 result->boxes[i]
            }
        }
        break;
    }
    // ... 其他类型
    }
}
```

### KWS (语音识别)

KWS 支持两种模式：

**轮询模式** (推荐，低延迟)：
```c
#include "kws_proto.h"

void kws_poll(void)
{
    volatile KWSSyncFlags_t *flags = KWS_GET_SYNC_FLAGS_PTR();
    
    // 检查 DSP 是否就绪
    if (!flags->dsp_ready) return;
    
    // 检查是否有新结果
    if (flags->output_ready) {
        volatile KWSResult_t *result = KWS_GET_RESULT_PTR();
        if (KWS_IS_VALID_KEYWORD(result->keyword_idx)) {
            printf("识别到: %d, 置信度: %d\n", 
                   result->keyword_idx, result->confidence);
        }
        flags->output_ready = 0;  // 清除标志
    }
}
```

**Mailbox 模式** (可选)：
```c
case MAILBOX_MSG_TYPE_KWS: {
    uintptr_t addr = DSP_KWS_BASE_ADDR + payload;
    const KWSResult_t *result = (const KWSResult_t*)addr;
    
    if (KWS_RESULT_IS_VALID(result) && 
        KWS_IS_VALID_KEYWORD(result->keyword_idx)) {
        // 处理识别结果
    }
    break;
}
```

## 版本历史

| 版本 | 日期       | 变更                               |
| ---- | ---------- | ---------------------------------- |
| v2.2 | 2026-02-03 | 分离协议架构，添加 Kalman 滤波字段 |
| v2.1 | 2026-01-30 | 添加 selected_idx 字段             |
| v2.0 | 2026-01-26 | 多目标检测支持                     |
