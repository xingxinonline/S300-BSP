# Card2 人脸识别 DSP 实现方案

> 目标：给 Card2 的 DSP 数据面提供一套统一实现，使 Demo 与子板工程共用同一套识别行为。
>
> 注：本文已按 DSP 侧最新指导对齐到检测协议 v2.4。凡与早期草案冲突之处，以 DSP 当前协议定义与行为为准。

## 1. 设计目标

本方案解决 4 个问题：

1. Card2 不再依赖主板或子板 CM4 做身份判断。
2. Demo 与 Card2 子板使用同一套 DSP 候选选择和识别逻辑。
3. 不要求 Card1 与 Card2 做逐帧坐标对齐。
4. 主板只消费 Card2 的识别摘要，不消费特征库细节。

本方案明确采用以下产品假设：

1. 跟随开始时，目标人物大概率位于画面中间。
2. Card1 默认选择中间人做人形主跟踪。
3. Card2 默认选择中间脸做人脸识别候选。
4. Card2 的职责是验证当前候选是否为目标人物，而不是承担全局跟踪控制。
5. 跟随能力与人脸找回能力解耦：Card1 只要有人形即可跟随，Card2 只有在成功注册人脸模板后才具备找回能力。

## 1.1 v2.4 权威约束

根据 DSP 当前指导，以下内容属于实现约束，而不是建议项：

1. 检测协议版本当前为 `0x0204`。
2. `DetectionResult_t` 大小当前为 `848` 字节。
3. `feature_dim` 当前实际类型是 `uint32_t`，不是 `uint16_t`。
4. DSP 允许在 `count == 0` 时继续通过 `MAILBOX_MSG_TYPE_MULTI` 上报识别摘要。
5. `verify_state` 已经包含 DSP 侧时间防抖后的稳定结论。
6. `candidate_flags` 是协议语义的一部分，CM4 不应自行复算同名门禁。
7. FR 会话控制命令归属 `CONTROL_CMD_GRP_FR`，并包含 `START_SESSION`、`RESET_SESSION`、`STOP_SESSION`。

## 2. 职责边界

### 2.1 DSP 负责内容

DSP 负责以下逻辑，且 Demo 与子板应完全一致：

1. 人脸检测。
2. 候选脸选择。
3. 候选脸特征提取。
4. 会话模板库管理。
5. 相似度计算。
6. 输出识别结论。

### 2.2 CM4 负责内容

CM4 只负责控制会话和消费结果：

1. 启动识别会话。
2. 重置识别会话。
3. 停止识别会话。
4. 显示或转发识别结果。
5. 在主板场景下，把 Card2 识别结论纳入全局状态机。

### 2.3 不做的事情

本方案明确不做以下内容：

1. Card1 与 Card2 的逐帧框坐标对齐。
2. 跨板多目标身份绑定。
3. 主板上做人脸模板库管理。
4. 对所有检测脸同时提取 embedding。

### 2.4 启动跟随后无人脸的处理原则

如果启动跟随后，当前场景中只有人形、没有可用人脸，本方案明确采用以下策略：

1. 不阻塞跟随启动。
2. Card1 继续按人形结果正常跟随。
3. Card2 进入等待注册状态，不报错、不退出会话。
4. 只要后续某一帧出现合格人脸，即允许延迟注册。
5. 在模板库为空期间，系统应视为“跟随可用，但人脸找回不可用”。
6. 若此期间发生目标丢失，则不能走 Card2 人脸找回分支。

这条规则用于覆盖以下真实场景：

1. 目标背对镜头。
2. 目标低头或大角度侧脸。
3. 人形可见，但当前帧无法稳定提取人脸特征。

## 3. DSP 状态机

建议 DSP 识别状态机采用 4 态。

### 3.1 状态定义

1. `FR_DSP_STATE_IDLE`
   - 未进入识别会话。
   - 不维护模板库。

2. `FR_DSP_STATE_WAIT_ANCHOR`
   - 会话已开始。
   - 当前模板库为空。
   - 等待第一次合格候选脸完成注册。

3. `FR_DSP_STATE_VERIFYING`
   - 已存在至少 1 个模板。
   - 每帧对当前候选脸做验证。
   - 允许在模板库未满时补充模板。

4. `FR_DSP_STATE_STOPPED`
   - 会话主动停止。
   - 模板库被清空。
   - 等待下一次 `START_SESSION`。

其中：

1. `WAIT_ANCHOR` 是正常工作态，不是错误态。
2. `WAIT_ANCHOR` 表示会话已经开始，但当前仍未获得找回能力。
3. 只要模板库为空，DSP 就应保持在可延迟注册的等待模式。

### 3.2 状态转移

1. `IDLE -> WAIT_ANCHOR`
   - 收到 `START_SESSION`。

2. `WAIT_ANCHOR -> VERIFYING`
   - 成功注册第一个模板。

3. `VERIFYING -> WAIT_ANCHOR`
   - 收到 `RESET_SESSION`。

4. `WAIT_ANCHOR -> STOPPED`
   - 收到 `STOP_SESSION`。

5. `VERIFYING -> STOPPED`
   - 收到 `STOP_SESSION`。

6. `STOPPED -> WAIT_ANCHOR`
   - 收到新一次 `START_SESSION`。

### 3.3 外部可见能力语义

为便于 Demo、子板 CM4 和主板统一理解，建议把 Card2 会话能力分成两层：

1. 跟随能力
   - 由 Card1 决定。
   - 与 Card2 是否已注册无关。

2. 找回能力
   - 由 Card2 模板库是否非空决定。
   - 模板库为空时，找回能力为未就绪。

对外语义建议如下：

1. `WAIT_FACE`
   - 会话已启动，但尚未注册到任何模板。

2. `READY`
   - 已注册至少 1 个模板，具备找回能力。

3. `MATCH`
   - 当前候选脸与模板库匹配。

4. `NO_MATCH`
   - 当前候选脸与模板库不匹配。

在协议层如果暂不增加单独能力字段，则可按以下规则解释：

1. `template_count == 0` 等价于 `WAIT_FACE`。
2. `template_count > 0` 且当前没有候选或特征无效，等价于 `READY` 或 `UNCERTAIN`。
3. `template_count > 0` 且有验证结果时，再进入 `MATCH` / `NO_MATCH` / `UNCERTAIN`。

注意：

1. `WAIT_FACE` / `READY` 是业务层能力语义，不要求 DSP 额外输出这两个枚举值。
2. DSP 对 CM4 的权威输出仍应以 `verify_state`、`template_count`、`candidate_flags` 为准。

## 4. 候选脸选择策略

### 4.1 设计原则

候选脸选择应在 DSP 内完成，不下放到 CM4。

理由：

1. Demo 与子板可保持行为一致。
2. 主板与子板 CM4 都不需要重复实现候选策略。
3. `selected_idx` 的语义可以稳定下来。

### 4.2 选择规则

每帧从所有人脸框中选出一个候选脸。

优先级如下：

1. 如果内部 tracker 或模型已经给出合法 `selected_idx`，优先使用该索引。
2. 如果没有合法 `selected_idx`，从所有 `type == FACE` 的框中选距离画面中心最近的脸。
3. 如果两个候选距离中心相同，选择 `score` 更高者。

### 4.3 中心距离定义

以候选脸框中心与画面中心的平方距离为准：

$$
d^2 = (cx - center_x)^2 + (cy - center_y)^2
$$

其中：

1. `cx = (x1 + x2) / 2`
2. `cy = (y1 + y2) / 2`
3. `center_x = frame_width / 2`
4. `center_y = frame_height / 2`

## 5. 特征提取策略

### 5.1 只提候选脸特征

DSP 每帧最多只对 1 张候选脸提特征。

原因：

1. 当前产品不需要多脸并发识别。
2. 节省 DSP 计算量。
3. 避免协议复杂化。

### 5.2 特征有效条件

只有满足以下条件，才认为本帧特征有效：

1. 候选索引合法。
2. 人脸置信度不低于最小阈值。
3. 人脸框尺寸满足下限。
4. `feature_dim == 128`。
5. `feature_flags` 置位 `HAS_FEATURE`。
6. `candidate_flags` 置位 `FEATURE_VALID`。

实现注意：

1. 协议里的 `feature_dim` 当前实际类型为 `uint32_t`。
2. CM4 镜像结构体不能按 `uint16_t feature_dim` 去定义，否则尾部识别摘要字段会错位。

建议最小参数：

1. `MIN_FACE_SCORE = 70`
2. `MIN_FACE_WIDTH = 24`
3. `MIN_FACE_HEIGHT = 24`

## 6. 模板库设计

### 6.1 模板库规模

模板库固定为 3 槽：

1. `template[0]`
2. `template[1]`
3. `template[2]`

不做动态扩容。

### 6.2 设计理由

3 槽足以覆盖最常见的同一人姿态变化：

1. 正脸。
2. 左偏或右偏。
3. 另一侧偏转或距离变化。

### 6.3 模板入库规则

只有满足以下条件时才允许新增模板：

1. 当前处于 `WAIT_ANCHOR` 或 `VERIFYING`。
2. 当前候选脸特征有效。
3. 当前候选脸置信度不低于 `70`。
4. 若模板库非空，则当前特征与已有模板最高相似度低于重复阈值。
5. 模板库尚未满。

建议阈值：

1. `ENROLL_MIN_CONF = 70`
2. `DUPLICATE_SCORE = 88`

### 6.4 模板清空规则

模板库在以下情况下清空：

1. `START_SESSION`
2. `RESET_SESSION`
3. `STOP_SESSION`

### 6.5 延迟注册策略

模板注册不应只限制在会话开始后的极短窗口内，而应允许整个跟随会话期间延迟发生。

原因：

1. 启动跟随时，目标未必正脸面对镜头。
2. 目标可能在跟随进行若干秒后才露出可识别人脸。
3. 如果只允许起始窗口注册，会导致“当前能跟随，但永远无法获得找回能力”的低效状态。

因此建议：

1. `WAIT_ANCHOR` 可长期维持。
2. 在整个会话期间，只要出现合格候选脸，就允许补注册。
3. 一旦注册成功，从该时刻开始系统获得 Card2 找回能力。

## 7. 验证决策策略

### 7.1 相似度计算

沿用当前实现的点积归一方式：

$$
score = clamp\left(\frac{\sum_{i=0}^{127} f_1[i] \cdot f_2[i] \times 100}{16129}, 0, 100\right)
$$

### 7.2 多模板比较

当前候选特征与全部模板逐个比较，取最大相似度：

$$
best\_score = \max(sim(feature\_now, template_i))
$$

### 7.3 识别结论

DSP 对外当前建议输出 5 类识别状态：

1. `NONE`
   - 会话未启动，或会话已停止。

2. `WAIT_ANCHOR`
   - 当前模板库为空，正在等待第一张合格 anchor 人脸。

3. `MATCH`
   - 当前候选脸与模板库匹配。

4. `UNCERTAIN`
   - 当前有模板，但当前帧不足以稳定确认匹配或不匹配。

5. `NO_MATCH`
   - 当前候选存在，但经时间防抖后确认不是模板库目标。

附加约束：

1. `WAIT_ANCHOR` 只能在模板库为空时出现。
2. `MATCH`、`UNCERTAIN`、`NO_MATCH` 只有在模板库非空后才有业务意义。
3. 若当前模板库为空，即使当前场景中存在人脸框，也不能视为可找回状态。
4. `NO_MATCH` 不用于表示“当前没有人脸”，而只用于“当前有候选但确认不是目标”。

建议阈值：

1. `MATCH_THRESHOLD = 62`
2. `UNCERTAIN_THRESHOLD = 52`

### 7.4 时间防抖约束

DSP 当前应把结论层防抖视为协议语义的一部分，而不是可选 UI 逻辑。

当前指导建议：

1. 连续 2 帧达到匹配阈值，才发布 `MATCH`。
2. 连续 2 帧低于不匹配阈值，才发布 `NO_MATCH`。
3. 连续 2 帧没有有效候选或候选失效，才降为 `UNCERTAIN`。
4. 单帧抖动时优先保持上一次稳定结论。

因此：

1. CM4 不应再做一层等价的 2 帧业务防抖。
2. 如果 UI 层希望更稳，只能做显示延迟，不应改写 DSP 输出语义。

## 8. 控制面命令建议

建议对 DSP 暴露 3 个识别会话控制命令。

### 8.1 `START_SESSION`

行为：

1. 清空模板库。
2. 清空上次识别状态。
3. 进入 `WAIT_ANCHOR`。

### 8.2 `RESET_SESSION`

行为：

1. 清空模板库。
2. 保持流运行。
3. 回到 `WAIT_ANCHOR`。

### 8.3 `STOP_SESSION`

行为：

1. 清空模板库。
2. 停止识别会话。
3. 进入 `STOPPED`。

## 9. 数据面协议

### 9.1 当前协议版本

当前 DSP 指导已经明确采用检测协议 `0x0204`。

CM4 侧必须同步：

1. `DETECTION_PROTOCOL_VERSION`
2. `DetectionResult_t`
3. `DetectionVerifyState_e`
4. `DETECTION_CANDIDATE_FLAG_*`
5. `CONTROL_CMD_GRP_FR`
6. `CONTROL_CMD_FR_START_SESSION`
7. `CONTROL_CMD_FR_RESET_SESSION`
8. `CONTROL_CMD_FR_STOP_SESSION`

### 9.2 双版本兼容建议

为便于 CM4 与 DSP 分开升级，建议 CM4 保留双版本兼容：

1. `version <= 0x0203`：按旧协议处理，只解析检测框和可选特征。
2. `version >= 0x0204`：解析识别摘要和会话语义。

### 9.3 v2.4 识别摘要字段

v2.4 在结果尾部新增识别摘要字段：

1. `verify_state`
2. `verify_score`
3. `template_count`
4. `candidate_confidence`
5. `candidate_flags`
6. `reserved[3]`

### 9.4 关键协议注意事项

1. `count == 0` 不等于一定收到 `NO_DETECT`。
2. v2.4 下 `count == 0` 也可能收到 `MAILBOX_MSG_TYPE_MULTI`。
3. 这表示“当前无框，但 DSP 仍在继续发布识别摘要”。
4. mailbox 解析必须先看消息类型和 `version`，不能只靠 `count` 判断路径。

### 9.5 结果结构示意

```c
typedef struct __attribute__((packed)) {
    uint32_t       magic;
    uint32_t       version;
    uint32_t       frame_id;
    uint32_t       timestamp;
    uint32_t       count;
    int32_t        selected_idx;
    DetectionBox_t boxes[MAX_DETECTION_COUNT];
    uint32_t       feature_flags;
   uint32_t       feature_dim;
    int8_t         feature_vector[FACE_FEATURE_DIMENSION];
    uint8_t        verify_state;
    uint8_t        verify_score;
    uint8_t        template_count;
    uint8_t        candidate_confidence;
    uint8_t        candidate_flags;
    uint8_t        reserved[3];
} DetectionResultV204_t;
```

### 9.6 candidate_flags 建议位语义

建议 CM4 按位解析 `candidate_flags`，至少覆盖以下语义：

1. `PRESENT`
2. `ALLOW_EXTRACT`
3. `ALLOW_UPDATE`
4. `FEATURE_VALID`
5. `TEMPLATE_MATCH`
6. `TEMPLATE_ENROLL`
7. `TEMPLATE_FUSE`

这些标志主要用于：

1. UI 调试显示当前帧质量。
2. 业务层判断为什么某帧没有提特征。
3. 日志排查模板入库或融合行为。

## 10. Demo 与子板的统一实现方式

### 10.1 DSP 固件统一

Demo 与 Card2 子板都应加载同一套 DSP 行为：

1. 同一套候选脸选择。
2. 同一套模板库管理。
3. 同一套 verify 阈值。
4. 同一套识别结论输出。

### 10.2 Demo CM4 职责

Demo CM4 只负责：

1. 启动 DSP 会话。
2. 显示候选框。
3. 显示模板数与验证结果。
4. 打印串口日志验证行为。

Demo CM4 不应再次复算候选脸选择或时间防抖。

### 10.3 子板 CM4 职责

子板 CM4 只负责：

1. 启动 DSP 会话。
2. 把 DSP 识别摘要映射到 I2C 结果结构。
3. 响应主板 `START_SESSION`、`RESET_SESSION`、`STOP_SESSION`。

子板 CM4 不应再次维护独立模板库。

### 10.4 主板职责

主板只消费以下摘要：

1. 当前候选脸是否存在。
2. 候选脸位置。
3. `verify_state`。
4. `verify_score`。

主板不维护模板库。

### 10.5 主板行为约束

主板在产品逻辑上应明确遵循以下规则：

1. 跟随启动只依赖 Card1 是否有人形，不依赖 Card2 是否已注册。
2. Card2 在 `WAIT_FACE` / `WAIT_ANCHOR` 状态下，不应阻塞当前跟随。
3. Card2 只有在模板库非空时，才被认为具备找回能力。
4. 若目标丢失时 Card2 仍未注册，则主板必须跳过人脸找回分支。
5. 跟随后若后续补注册成功，主板可从该时刻开始启用 Card2 找回能力。
6. 主板只在长时丢失阶段消费 Card2 的 `MATCH` 结论，不在正常跟随阶段使用 Card2 位置做控制主来源。
7. `WAIT_ANCHOR` 或 `count == 0` 不应被主板解释为 `NO_MATCH`。

## 11. DSP 每帧伪代码

```c
void fr_dsp_process_frame(frame_t *frame)
{
    DetectionResult_t result = {0};
    face_box_t *candidate = NULL;
    int candidate_idx = -1;
    int best_score = -1;

    run_face_detection(frame, &result.boxes, &result.count);

    candidate_idx = select_candidate_face(&result);
    result.selected_idx = candidate_idx;

    if (candidate_idx >= 0) {
        candidate = &result.boxes[candidate_idx];
    }

    if (candidate != NULL && face_feature_is_allowed(candidate)) {
        extract_face_feature(frame, candidate, result.feature_vector);
        result.feature_dim = 128;
        result.feature_flags |= DETECTION_RESULT_FLAG_HAS_FEATURE;
    }

    switch (g_fr_state) {
    case FR_DSP_STATE_IDLE:
        set_verify_state(&result, VERIFY_NONE, 0);
        break;

    case FR_DSP_STATE_WAIT_ANCHOR:
        if (feature_valid(&result)) {
            try_enroll_template(result.feature_vector, candidate->score);
        }

        if (template_count() > 0) {
            g_fr_state = FR_DSP_STATE_VERIFYING;
        }

        set_verify_state(&result, VERIFY_WAIT_ANCHOR, 0);
        break;

    case FR_DSP_STATE_VERIFYING:
        if (feature_valid(&result)) {
            best_score = compare_with_templates(result.feature_vector);
            try_enroll_template(result.feature_vector, candidate->score);
         set_verify_state_by_score_with_debounce(&result, best_score);
        } else {
         set_verify_state_with_candidate_dropout_debounce(&result, VERIFY_UNCERTAIN, 0);
        }
        break;

    case FR_DSP_STATE_STOPPED:
    default:
        set_verify_state(&result, VERIFY_NONE, 0);
        break;
    }

    result.frame_id = next_frame_id();
    result.timestamp = get_timestamp();
   fill_candidate_summary(&result);
    publish_result(&result);
}
```

## 12. 验证建议

建议先用 Face Recognition Demo 单独验证 DSP 行为，再接入子板工程。

验证场景如下：

1. 单人居中注册。
2. 同一人姿态变化。
3. 不同人替换到中心。
4. 双人同时在画面。
5. 无人脸或特征无效。
6. 只有人形、没有可用人脸。

通过标准：

1. 候选选择符合中间优先。
2. 第一个模板可稳定注册。
3. 同一人能稳定输出 `MATCH`。
4. 不同人不会稳定输出 `MATCH`。
5. 模板库数量按预期增长，不会无限污染。
6. 在“只有人形、没有人脸”的场景中，会话保持 `WAIT_ANCHOR`，但不应被视为错误。

### 12.1 只有人形、没有人脸的专项验证

建议增加一个专项验证场景：

1. 启动跟随会话。
2. 让目标人物背对摄像头或保持大侧脸，仅保证人形存在。
3. 观察 DSP 状态是否稳定停留在 `WAIT_ANCHOR`。
4. 观察模板库数量是否保持为 0。
5. 观察系统是否仍然允许 Card1 正常跟随。
6. 随后让目标转正脸。
7. 验证 DSP 是否能在会话中途完成补注册并进入 `VERIFYING`。

该场景通过的判据是：

1. 无人脸时不报错、不退出会话。
2. 跟随不中断。
3. 一旦出现合格人脸，可从 `WAIT_ANCHOR` 平滑切到 `VERIFYING`。

## 13. 实施顺序建议

建议按以下顺序落地：

1. 先把 DSP 内部候选脸选择统一成中间优先。
2. 再把 DSP 内部模板库统一成固定 3 槽。
3. 再输出 `verify_state` 与 `verify_score`。
4. Demo 先验证 DSP 识别行为。
5. 子板 CM4 再改成纯桥接。
6. 最后主板只消费识别摘要。
