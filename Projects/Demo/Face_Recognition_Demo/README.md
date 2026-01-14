# Face Recognition Demo

人脸识别演示程序，支持摄像头实时采集、DSP 特征提取和比对。

## 功能特性

- OV5640 摄像头图像采集
- 128x160 显示屏实时预览
- DSP 人脸特征提取 (112x112 RGB888)
- 支持存储最多 10 个目标特征向量
- 串口命令行和 PC 上位机两种交互方式

## 目录结构

```
Face_Recognition_Demo/
├── CMakeLists.txt          # CMake 构建配置
├── Inc/                    # 头文件
├── Src/                    # 源代码
├── ld/                     # 链接脚本
└── host_app/               # PC 上位机 (Python)
    ├── app.py              # 主程序
    ├── pyproject.toml      # 依赖配置
    └── README.md           # 上位机说明
```

## 依赖

- `s300_board` - 板级支持
- `s300_video` - 视频/显示驱动
- `s300_mailbox` - Mailbox 通信库
- `s300_psram` - PSRAM 驱动
- OV5640 / I2C 驱动

## 构建

```bash
cd S300_BSP/build
cmake ..
make s300_face_recognition_demo
```

调试 (加载模型)：
```bash
make dbg_face_recognition_demo_model
```

## PC 上位机使用

上位机位于 `host_app/` 目录，使用 Python + OpenCV + MediaPipe 实现人脸检测和图像传输。

```bash
cd host_app
uv sync           # 安装依赖
uv run python app.py
```

操作说明：
- `t` - 采集当前人脸作为目标，发送到 S300
- `c` - 采集当前人脸进行比对
- `s` - 自动模式
- `q` - 退出

详见 [host_app/README.md](host_app/README.md)。

## 串口命令

| 命令 | 描述 |
|------|------|
| `help` | 显示帮助 |
| `dsp start/stop` | DSP 复位控制 |
| `face ping` | 检查 DSP 状态 |
| `load` | 进入文件传输模式 |
| `status` | 显示当前状态 |

## 内存布局

| 区域 | 地址 | 用途 |
|------|------|------|
| DSP SRAM0 | 0x44000000 | 图像数据 |
| DSP DTCM | 0x44800000 | DSP 紧耦合内存 |
| PSRAM | 0x80000000 | 模型和显示缓冲 |
| 共享缓冲 | 0x80500000 | M4/DSP 通信 |
