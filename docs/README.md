# S300 BSP 文档

本目录包含 S300 BSP 的技术文档。

## 文档列表

| 文档 | 说明 |
|------|------|
| [AI_Model_Demo_Guide.md](AI_Model_Demo_Guide.md) | **推荐** 人脸/手势/人形检测及识别 Demo 运行指南 |
| [S300_BSP_Architecture.md](S300_BSP_Architecture.md) | BSP 架构设计，包含目录结构、分层设计、驱动接口规范 |
| [S300_DMA_LLI_Guide.md](S300_DMA_LLI_Guide.md) | DMA 链表传输使用指南，大数据搬运的实现方法 |
| [coding_style_cn.md](coding_style_cn.md) | 编码规范（中文） |
| [coding_style_en.md](coding_style_en.md) | 编码规范（英文） |

## 快速入门

1. 参考 `Projects/App_HelloWorld` 作为新项目模板
2. 查看 `Boards/generic_evb/board.h` 了解板级配置
3. 各外设驱动位于 `Drivers/SoC/` 目录

