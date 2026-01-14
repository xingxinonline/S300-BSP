# Face Transfer App

PC 端人脸采集和传输工具，配合 Face_Recognition_Demo 固件使用。

## 环境要求

- Python 3.8+
- [uv](https://github.com/astral-sh/uv) 包管理器
- 摄像头
- S300 开发板 (UART 连接，默认 COM7)

## 安装

```bash
cd S300_BSP/Projects/Demo/Face_Recognition_Demo/host_app
uv sync
```

## 使用

```bash
uv run python app.py
```

操作：
- `t` - 采集目标人脸
- `c` - 采集比对人脸
- `s` - 自动模式
- `q` - 退出

## 配置

如需修改串口号，编辑 `app.py` 中的 `SERIAL_PORT` 变量。
