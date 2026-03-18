# CM4 Face Feature Protocol Upgrade

本文档说明 DSP 检测结果协议从 v2.2 升级到 v2.3 后，CM4 基于既有人脸检测状态机需要做的适配点。

## 协议变化

- 协议版本从 `0x0202` 升级到 `0x0203`。
- `DetectionResult_t` 头部和 `boxes[MAX_DETECTION_COUNT]` 的布局保持不变。
- 在结构体尾部新增：
  - `feature_flags`
  - `feature_dim`
  - `feature_vector[128]`
- `selected_idx` 不再只是兼容字段，当前语义变为：
  - `-1`：本帧没有选中的识别人脸
  - `>= 0`：`feature_vector` 对应 `boxes[selected_idx]`

## DSP 侧当前行为

- DSP 在检测成功后，从有效检测框里选出 1 张主脸。
- 主脸选择规则：优先最高 `score`，分数相同则取面积更大的框。
- DSP 使用恢复到原图坐标的 5 点 landmark 做 112x112 对齐。
- FR 成功时：
  - `feature_flags & DETECTION_RESULT_FLAG_HAS_FEATURE` 为真
  - `feature_dim == 128`
  - `feature_vector` 为有效 embedding
- FR 失败时：
  - `selected_idx` 可能为有效框索引
  - 但 `feature_flags == 0`，CM4 必须以 `feature_flags` 为准判断特征是否有效

## CM4 状态机适配建议

### 1. 更新协议头文件

CM4 侧需要同步 `DetectionResult_t`、`FACE_FEATURE_DIMENSION`、`DETECTION_RESULT_FLAG_HAS_FEATURE` 和 `DETECTION_PROTOCOL_VERSION`。

建议做法：

- 直接同步 DSP 当前版本的 `detection_protocol.h` 到 CM4 工程。
- 如果 CM4 历史代码依赖旧结构体大小，请同步检查：
  - 静态数组大小
  - DMA/共享内存 copy 大小
  - 调试打印中使用的 `sizeof(DetectionResult_t)`

### 2. 保持控制面状态机不扩状态

现阶段不建议为了 FR 再加新的控制面状态。

CM4 仍按原来的检测状态机处理：

- `HELLO`
- `RESOURCE_READY`
- `CONFIG_APPLY`
- `BUFFER_BIND`
- `START_STREAM`

原因：

- DSP 已经在每帧检测路径内部完成“检测 -> 选脸 -> 对齐 -> embedding”。
- 对 CM4 来说，这仍然是一种“检测结果 ready”事件，而不是一条新的独立流水线。

### 3. 升级 mailbox 结果解析逻辑

收到 `MAILBOX_MSG_TYPE_MULTI` 后，CM4 当前应该已经会：

- 取 payload
- 加 `DSP_PTCM_M4_BASE_OFFSET`
- 转成 `DetectionResult_t *`

现在要补的检查：

- 先校验 `magic`
- 再校验 `version >= 0x0203`
- 若版本较老，则按“只有检测框、没有 embedding”路径兼容处理

推荐判定顺序：

1. `msg_type == MAILBOX_MSG_TYPE_MULTI`
2. `result->magic == DETECTION_RESULT_MAGIC`
3. `result->count <= MAX_DETECTION_COUNT`
4. `result->version >= DETECTION_PROTOCOL_VERSION`
5. `result->feature_flags & DETECTION_RESULT_FLAG_HAS_FEATURE`
6. `result->selected_idx >= 0 && result->selected_idx < result->count`
7. `result->feature_dim == FACE_FEATURE_DIMENSION`

### 4. 在原检测结果消费点增加 feature 分支

建议不要单独新增 mailbox 消息类型。

直接在原“处理检测结果”的函数里追加：

- 若本帧 `feature_flags == 0`：按旧逻辑仅处理框
- 若本帧 `feature_flags != 0`：
  - 从 `selected_idx` 找到对应框
  - 读取 `feature_vector[128]`
  - 送入 CM4 本地的人脸库比对、注册或追踪逻辑

### 5. 人脸库比对建议

如果 CM4 侧已经有你之前的人脸识别状态机，建议最小化改法如下：

- 把原来“等待 DSP 返回单独特征”的分支改为“从 `DetectionResult_t` 一并取特征”。
- 每帧最多消费 1 个 embedding，避免一次检测多脸时把旧状态机复杂化。
- 使用 `selected_idx` 把识别结果绑定到当前检测框，而不是重新做框匹配。

### 6. UI / 业务层建议

如果 CM4 侧需要显示识别结果：

- 以 `boxes[selected_idx]` 作为识别标签附着的框
- 如果 `feature_flags == 0`，只显示人脸框，不显示身份
- 如果比对失败，显示“unknown”而不是丢掉框

## 兼容策略建议

为了平滑升级，建议 CM4 做成双版本兼容：

- `version <= 0x0202`：按旧检测协议解析
- `version >= 0x0203`：按新检测+特征协议解析

这样 DSP 和 CM4 可以分开升级，不需要强依赖同一天切换。

## CM4 侧最小改动清单

1. 同步新的 `detection_protocol.h`
2. 检查所有 `sizeof(DetectionResult_t)` 的使用点
3. 在原 `MAILBOX_MSG_TYPE_MULTI` 消费点增加 `feature_flags/feature_dim/selected_idx/feature_vector` 解析
4. 把原“单独等特征返回”的状态机动作并入检测结果处理回调
5. 用 `selected_idx` 绑定识别结果和显示框