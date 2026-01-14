# FreeRTOS + Shell Demo (S300)

This demo brings up FreeRTOS on the S300 and provides a tiny UART shell.

- UART: Debug UART (default UART3, 115200 8N1)
- Commands: `help`, `tasks`, `tick`, `reboot`

## Build

From the BSP build directory:

```bash
cmake ..
cmake --build . --target s300_freertos_shell -j
```

Notes:

- The build uses CMake FetchContent to pull FreeRTOS-Kernel from GitHub. If your environment has no network, vendor the kernel locally and set CMake to use it, or run configure with network once.

## Flash/Debug

A binary and hex are generated next to the target after build. You can also run:

```bash
cmake --build . --target dbg
```

This opens GDB with the SRAM script and connects to :3333.

## Serial

115200 8N1 on UART3. After reset you should see:

```text
S300 FreeRTOS + Shell demo
> 
```
