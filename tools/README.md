# S300 开发工具

## 工具列表

| 工具                   | 用途                  |
| ---------------------- | --------------------- |
| `s300_image.py`        | ROMBOOT 镜像生成/提取 |
| `s300_download.py`     | 固件下载              |
| `serial_monitor.py`    | 串口监控              |
| `get_model_version.py` | 获取模型版本          |

## 安装

```bash
cd tools
uv sync
```

## 使用

### 镜像生成

```bash
# 仅 M4 核心
uv run python s300_image.py generate -o firmware.bin --m4 build/App_HelloWorld

# M4 + DSP
uv run python s300_image.py generate -o firmware.bin --m4 build/App --dsp build/DSP
```

### 镜像提取

```bash
# 从镜像提取各核心 bin 文件
uv run python s300_image.py extract firmware.bin -o extracted/
```

### 下载与监控

```bash
# 下载固件
uv run python s300_download.py --port COM7 --file firmware.bin

# 串口监控
uv run s300-monitor read COM7 --baud 921600
```
