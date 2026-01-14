# Hello World 示例应用

这是 S300 BSP 的最小示例程序，演示基本的系统初始化和串口输出。

## 功能

- 板级初始化 (board_init)
- SysTick 配置 (1ms 定时)
- UART printf 输出
- 简单的计数循环

## 构建

```bash
cd S300_BSP/build
cmake --build . --target s300_hello_world
```

## 运行

1. 连接 S300 开发板
2. 使用调试器下载程序到 SRAM
3. 打开串口终端 (115200 8N1)
4. 观察 "Hello, World!" 输出

## 输出示例

```
========================================
  S300 Hello World Demo
========================================
System Clock: 192000000 Hz

[       0 ms] Hello, World! Count: 0
[    1000 ms] Hello, World! Count: 1
[    2000 ms] Hello, World! Count: 2
...
```

## 作为模板

此项目可作为新应用的起点模板。复制整个目录并修改：

1. 重命名目录和 CMakeLists.txt 中的 TARGET
2. 根据需要添加源文件和驱动依赖
3. 在 `Projects/CMakeLists.txt` 中添加 `add_subdirectory()`
