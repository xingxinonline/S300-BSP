# S300 DMA LLI 使用指南

本文说明在 S300 平台使用 DMA 的链表（LLI）方式进行大数据搬运时的要点与限制，配合 `Drivers/SoC/DMA` 驱动。

## 核心概念

- BLOCK_TS：每块传输的“传输次数”，单位为传输位宽对应的数据项（8/16/32 位）。驱动按 12 位编程（0xFFF），因此单块最大传输次数为 4095。
- 单块最大字节数：`0xFFF × bytes_per_transfer`，其中 bytes_per_transfer 为 1/2/4（对应 8/16/32 位）。
- LLI（链表项）：包含 SAR、DAR、LLP、CTL_L、CTL_H 等字段。每完成一块，通道会从 LLI 中“重载” CTL 等字段。

## 关键注意事项

1. 对齐要求

- 源地址、目的地址、长度都需要按传输位宽对齐（32 位宽 -> 4 字节对齐，16 位 -> 2 字节）。
- LLP 地址通常需要对齐（常见为 8 字节对齐），建议把 LLI 数组按 8 字节对齐分配。

2. LLP 续链位必须在每个 LLI 条目的 CTL_L 中置位

- 因为每块结束后 CTL 会从 LLI 重新加载，如果 LLI 自身未设置 `LLP_SRC_EN` 与 `LLP_DST_EN`，续链会在第一块后停止。
- 驱动提供的 `dma_set_link_unit()` 生成 CTL 的基础字段，随后请对 LLI 的 `CTL_L` OR 上 LLP 使能位，或使用 `dma_memcpy_lli()` 统一处理。

3. 分块与自动链传

- 当长度超过单块上限时，需要切分为多块，并通过 LLI 的 `LLP` 字段将下一块的地址链接起来，最后一块的 LLP 写 0。
- 驱动新增了 `dma_memcpy_lli()` 辅助函数，可自动分块并构建链表，设置通道并启动传输。

## 新增 API（在 dma.h）

```c
uint32_t dma_calc_lli_count(uint32_t len, dma_width_t width);
int dma_memcpy_lli(dma_idx_t d, uint8_t ch, uint32_t src, uint32_t dst, uint32_t len,
                   dma_width_t width, dma_lli_t *llis, uint32_t lli_capacity);
int dma_memcpy_lli_blocking(dma_idx_t d, uint8_t ch, uint32_t src, uint32_t dst, uint32_t len,
                            dma_width_t width, dma_lli_t *llis, uint32_t lli_capacity);
```

- 返回值：0 成功；<0 为错误码
  - -1：无效通道
  - -2：对齐错误（src/dst/len 未按位宽对齐）
  - -3：LLI 容量不足
  - -4：LLI 基地址未满足对齐（例如非 8 字节）

使用建议：

```
/* 计算需要的 LLI 个数并准备内存（8 字节对齐） */
uint32_t need = dma_calc_lli_count(len, DMA_WIDTH_32);
static dma_lli_t llis[MAX_NEED] __attribute__((aligned(8))); // 示例

/* 一行调用，非阻塞 */
dma_memcpy_lli(DMA_IDX0, 0, (uint32_t)src, (uint32_t)dst, len, DMA_WIDTH_32, llis, need);
while (dma_is_busy(DMA_IDX0, 0)) {}

/* 或阻塞版本 */
dma_memcpy_lli_blocking(DMA_IDX0, 0, (uint32_t)src, (uint32_t)dst, len, DMA_WIDTH_32, llis, need);
```

## 典型场景

- 连续地址大内存拷贝（如 RGB565 帧 320×240 = 153600 字节）：使用 32 位位宽（若 4 字节对齐），单块 16380 字节，需要约 10 个 LLI。
- 内存到外设（M2P，16 位 FIFO 的 LCD）：使用 16 位位宽，`SINC=INC, DINC=KEEP`，配置目的握手，单块 8190 字节，按需分块与链传。

## 实战参考

- 示例工程：`Projects/Demo/DMA_LLI_Big` 展示了 153600 字节的 LLI 连续内存搬运，以及在每个 LLI 中置 LLP 续链位的正确方法。
