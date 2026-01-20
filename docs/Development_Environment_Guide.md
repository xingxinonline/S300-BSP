# S300 BSP 开发环境搭建与 VS Code 使用指南

本文档详细介绍了如何搭建 PiMCHIP S300 BSP 的开发环境，并重点说明如何在 Visual Studio Code (VS Code) 中进行编译、调试等操作。

## 1. 核心工具链安装

无论使用命令行还是 VS Code，都需要先安装以下基础工具链，并将其添加到系统的 `PATH` 环境变量中。

### 1.1 工具列表

| 工具                  | 说明                             | 推荐版本 | 下载/安装链接                                                                                    |
| :-------------------- | :------------------------------- | :------- | :----------------------------------------------------------------------------------------------- |
| **ARM GCC Toolchain** | 交叉编译器 (`arm-none-eabi-gcc`) | 10.3+    | [Arm GNU Toolchain Downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) |
| **CMake**             | 构建系统生成器                   | >= 3.16  | [CMake Download](https://cmake.org/download/)                                                    |
| **Ninja**             | 快速构建工具                     | 最新版   | [Ninja Releases](https://github.com/ninja-build/ninja/releases)                                  |
| **OpenOCD**           | 调试与烧录工具                   | 0.11+    | [OpenOCD](https://openocd.org/) (Windows 可下载预编译版本如 xPack OpenOCD)                       |
| **Python**            | 辅助脚本运行环境                 | 3.8+     | [Python.org](https://www.python.org/)                                                            |

### 1.2 验证安装
打开终端 (Terminal) 或 PowerShell，输入以下命令检查版本，确保无报错：
```bash
arm-none-eabi-gcc --version
cmake --version
ninja --version
openocd --version
python --version
```

---

## 2. VS Code 开发环境配置

本项目依然保留了对 VS Code 的深度支持，通过预置的配置文件实现开箱即用。

### 2.1 安装 VS Code
前往 [Visual Studio Code 官网](https://code.visualstudio.com/) 下载并安装。

### 2.2 推荐插件
为了获得最佳开发体验，请在 VS Code 扩展市场中安装以下插件：

1.  **C/C++** (Microsoft): 提供 Intellisense 代码补全、导航和调试功能。
2.  **CMake Tools** (Microsoft): 提供 CMake 项目的语法高亮和辅助工具。
3.  **Cortex-Debug** (marus25): 专为 ARM Cortex-M 芯片设计的调试插件，支持 OpenOCD/J-Link。
4.  **Chinese (Simplified)** (Microsoft): (可选) VS Code 中文语言包。

---

## 3. VS Code 操作流程

本项目在 `.vscode/tasks.json` 中定义了常用任务，您可以通过 VS Code 的任务系统完成大部分开发工作，无需手动输入复杂命令。

### 3.1 切换板级配置 (Select Board)
S300 BSP 支持多种板型（如 `generic_evb` 开发板, `app_board` 应用板）。在编译前，必须先选择目标板型并生成构建配置。

1.  按快捷键 `Ctrl + Shift + P` 打开命令面板。
2.  输入 `Tasks: Run Task` 并回车。
3.  根据您的硬件选择以下任务之一：
    *   **`S300: Switch to Generic EVB (开发板)`**
    *   **`S300: Switch to App Board (应用板)`**
4.  任务会自动运行 `cmake -B build -G Ninja -DBOARD=...` 命令。如果在终端看到 "Build files have been written to..." 字样，说明配置成功。

### 3.2 编译代码 (Build)
配置完成后，即可进行编译。

*   **快捷键**: 直接按下 `Ctrl + Shift + B` (默认 Build 任务)。
*   **或者**: 运行任务 **`Build`**。

编译产物（`.elf`, `.hex`, `.bin`）将生成在 `build/Projects/<Project_Name>/` 目录下。

### 3.3 清理构建 (Clean/Rebuild)
如果遇到莫名其妙的编译错误，或者需要彻底重新编译：

*   运行任务 **`Clean`**: 仅清理 `build` 目录下的生成文件。
*   运行任务 **`Rebuild`**: 先执行清理，再执行编译。

---

## 4. 常见问题

**Q: 运行任务时提示 "ninja: command not found"?**  
A: 请检查 Ninja 是否已正确安装，且其所在的文件夹路径已添加到系统的 Path 环境变量中。重启 VS Code 使环境变量生效。

**Q: C/C++ 插件提示找不到头文件?**  
A: 请先运行一次 **3.1 切换板级配置**。CMake 配置生成后，插件会自动读取 `build/compile_commands.json` 来索引头文件路径。
