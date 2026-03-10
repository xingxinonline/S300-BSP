# DSP Binary Files

请将 DSP 工程编译生成的 bin 文件放置在此目录下。

## 期望的文件

| 文件名 | 说明 | 加载地址 |
|--------|------|----------|
| `model_ptcm_boot.bin` | DSP 程序代码 (PTCM) | 0x44A00000 |
| `model_dtcm_boot.bin` | DSP 数据段 (DTCM) | 0x44800000 |

## 如何生成这些文件

在 CEVA DSP 工程中，编译完成后通常会生成以下格式的文件：
- `.out` 或 `.elf` - 可执行文件
- `.bin` - 二进制镜像

如果只有 `.out/.elf` 文件，可以使用以下命令转换：

```bash
# 提取 PTCM 段 (程序代码)
<ceva-objcopy> -O binary --only-section=.text <output>.out model_ptcm_boot.bin

# 提取 DTCM 段 (数据)
<ceva-objcopy> -O binary --only-section=.data <output>.out model_dtcm_boot.bin
```

## 文件命名

文件名需要与 CMakeLists.txt 中的配置一致：
- `model_ptcm_boot.bin` - PTCM (Program TCM, 0x44A00000)
- `model_dtcm_boot.bin` - DTCM (Data TCM, 0x44800000)

## 调试命令

放置好 bin 文件后，重新配置 CMake，然后使用：

```bash
# 编译
ninja -C build s300_handshake_heartbeat

# 带 DSP 加载的调试
ninja -C build dbg_handshake_hb
```

调试脚本会自动加载 DSP bin 文件到对应的内存地址。
