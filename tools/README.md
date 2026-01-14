# S300 开发工具

## 工具列表

| 工具                   | 用途         |
| ---------------------- | ------------ |
| `s300_download.py`     | 固件下载     |
| `serial_monitor.py`    | 串口监控     |
| `get_model_version.py` | 获取模型版本 |

## 安装

```bash
cd tools
uv sync
```

## 使用

```bash
# 下载固件
uv run python s300_download.py --port COM7 --file firmware.bin

# 串口监控
uv run s300-monitor read COM7 --baud 115200

# 扫描串口
uv run s300-monitor scan
```
