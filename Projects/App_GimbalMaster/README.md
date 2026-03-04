# App_GimbalMaster (MVP0)

本目录是 `gimbal_master` 板卡的主控应用入口，当前实现为 MVP0 最小闭环：

- FreeRTOS 启动与调度
- 心跳任务每 1 秒打印一次 tick
- 预留可选 GPIO 心跳输出（默认关闭）

## 编译

```bash
cmake -B build -G Ninja -DBOARD=gimbal_master .
ninja -C build s300_gimbal_master
```

## 运行验证

串口应周期输出：

```text
[Heartbeat] tick=...
```

如需启用 GPIO 心跳输出，可在 `Inc/app_config.h` 中将 `APP_HEARTBEAT_GPIO_ENABLE` 置为 `1` 并按硬件修改引脚定义。
