# S300 Reset Tool

现代化的S300软件复位控制工具，支持多种触发方式和包管理器。

## 🚀 快速安装

### 使用 uv (推荐)

```bash
# 安装 uv (如果还没有)
curl -LsSf https://astral.sh/uv/install.sh | sh

# 安装项目依赖
uv sync

# 运行工具
uv run s300-reset scan
```

### 使用 Windows Scoop

```powershell
# 安装 Scoop (如果还没有)
Set-ExecutionPolicy RemoteSigned -Scope CurrentUser
irm get.scoop.sh | iex

# 安装 Python 和 uv
scoop install python
scoop install uv

# 然后使用 uv 安装依赖
uv sync
```

### 传统方式

```bash
# 创建虚拟环境
python -m venv venv
source venv/bin/activate  # Linux/macOS
# 或
venv\Scripts\activate.bat  # Windows

# 安装依赖
pip install -e .
```

## 📖 使用方法

### 扫描设备

```bash
uv run s300-reset scan
```

### 触发下载模式

```bash
# 串口方式
uv run s300-reset serial /dev/ttyUSB0 --mode download

# 网络方式
uv run s300-reset http 192.168.1.100 --mode download

# 双重启模拟
uv run s300-reset double-reset /dev/ttyUSB0 --interval 1.5
```

### 查看状态

```bash
uv run s300-reset status /dev/ttyUSB0
```

## 🛠️ 开发

### 安装开发依赖

```bash
uv sync --dev
```

### 代码格式化

```bash
uv run black .
```

### 类型检查

```bash
uv run mypy .
```

### 运行测试

```bash
uv run pytest
```

## 📁 项目结构

```text
tools/
├── pyproject.toml          # uv/pip 配置
├── s300_reset_tool.py      # 主要工具脚本
├── s300_ota_tool.py        # OTA 工具脚本
└── README.md               # 本文件
```
