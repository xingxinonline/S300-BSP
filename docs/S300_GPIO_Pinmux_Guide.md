# S300 GPIO 与 Pinmux 配置指南

## 1. 概述

本文档说明 S300 的 GPIO 驱动使用方法和引脚复用 (Pinmux) 配置。

### 1.1 关键硬件模块

| 模块          | 基地址       | 说明                               |
| ------------- | ------------ | ---------------------------------- |
| **GPIO**      | `0x40010000` | GPIO 控制器 (DW_apb_gpio 兼容)     |
| **IO_MATRIX** | `0x40008000` | 引脚功能选择矩阵 (Function 0~3)    |
| **IO_MUX**    | `0x40009000` | 引脚电气特性配置 (上下拉/驱动强度) |

### 1.2 IO_MATRIX vs IO_MUX 的区别

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   引脚 (Pin)    │───>│   IO_MATRIX     │───>│   IO_MUX        │───> 外设/GPIO
│                 │    │ (功能选择)      │    │ (电气特性)      │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                         选择 Function       配置上下拉/驱动
                           0 / 1 / 2 / 3     强度等参数
```

| 特性           | IO_MATRIX                     | IO_MUX                          |
| -------------- | ----------------------------- | ------------------------------- |
| **作用**       | 选择引脚功能 (Function 0~3)   | 配置电气属性 (上下拉、驱动强度) |
| **典型用法**   | 切换 UART/SPI/I2C/GPIO 等功能 | 启用内部上拉/下拉电阻           |
| **API**        | `gpio_set_function()`         | `gpio_set_mode()`               |
| **寄存器布局** | 每引脚 2 bits                 | 每引脚/信号组 2 bits            |

---

## 2. API 参考

### 2.1 核心函数

```c
#include "gpio.h"

/* 设置引脚功能 (通过 IO_MATRIX) */
int gpio_set_function(gpio_port_t port, uint8_t pin, gpio_func_t func);

/* 设置引脚电气模式 (通过 IO_MUX) */
int gpio_set_mode(gpio_port_t port, uint8_t pin, uint32_t mode_raw);

/* 设置引脚方向 (输入/输出) */
int gpio_set_direction(gpio_port_t port, uint8_t pin, uint32_t is_output);

/* 设置引脚输出值 */
int gpio_set_data(gpio_port_t port, uint8_t pin, uint32_t value);

/* 读取引脚输入值 */
bool gpio_get_value(gpio_port_t port, uint32_t pin);

/* 配置引脚中断 */
void gpio_set_interrupt(gpio_port_t port, uint8_t pin, gpio_int_type_t type, bool en);

/* 清除引脚中断标志 */
void gpio_interrupt_clear(gpio_port_t port, uint8_t pin);
```

### 2.2 兼容旧代码的别名

```c
/* 与新 API 功能相同，参数使用 int 类型 */
int set_gpio_function(int port, uint8_t pin, int func);
int set_gpio_mode(int port, uint8_t pin, uint32_t mode);
int set_gpio_direction(int port, uint8_t pin, uint32_t is_output);
int set_gpio_data(int port, uint8_t pin, uint32_t value);
bool get_gpio_value(int port, uint32_t pin);
```

---

## 3. 引脚功能映射

### 3.1 Function 枚举

```c
typedef enum {
    FUNCTION_0,  /* 功能 0 - 通常为 GPIO */
    FUNCTION_1,  /* 功能 1 */
    FUNCTION_2,  /* 功能 2 */
    FUNCTION_3   /* 功能 3 */
} gpio_func_t;
```

### 3.2 完整引脚功能复用表

S300 有 32 个引脚 (PAD0-PAD31)，每个支持 4 种功能。GPIO 一般在 Function 2。

| PAD   | Function 0 | Function 1         | Function 2 | Function 3 |
| ----- | ---------- | ------------------ | ---------- | ---------- |
| PAD0  | U0_DE      | CLKOUT1            | **GPIO0**  | I2C1_SCK   |
| PAD1  | U0_TXD     | CLKOUT3            | **GPIO1**  | I2C1_SDA   |
| PAD2  | GPIO2      | SPI2_WP            | **GPIO2**  | I2C2_SCK   |
| PAD3  | -          | CLKOUT2 (I2S_MCLK) | **GPIO3**  | U0_RXD     |
| PAD4  | GPIO4      | SPI2_CLK           | **GPIO4**  | I2C3_SCK   |
| PAD5  | GPIO5      | SPI2_CS0           | **GPIO5**  | I2C3_SDA   |
| PAD6  | SPI2D7     | SPI3CLK            | **GPIO6**  | I2S1_CK    |
| PAD7  | SPI2D1     | GPIO7              | I2S1_WS    | SPI2D1     |
| PAD8  | SPI2D2     | GPIO8              | I2S1_DATA  | SPI2D2     |
| PAD9  | SPI2D3     | GPIO9              | GPIO9      | SPI2D3     |
| PAD10 | SPI2D4     | SPI3WP             | GPIO10     | SPI2D4     |
| PAD11 | SPI2D5     | SPI3CS0            | GPIO11     | SPI2D5     |
| PAD12 | SPI2D6     | SPI3D0             | GPIO12     | SPI2D6     |
| PAD13 | SPI2D0     | MTCK               | **GPIO13** | SPI2D0     |
| PAD14 | I2C0_SCK   | MTMS               | GPIO14     | I2C0_SCK   |
| PAD15 | I2C0_SDA   | MTD0               | **GPIO15** | I2C0_SDA   |
| PAD16 | GPIO16     | SPI2_MODE0         | GPIO16     | GPIO16     |
| PAD17 | GPIO17     | SPI2_MODE1         | **U1TXD**  | GPIO17     |
| PAD18 | GPIO18     | SPI3_MODE0         | **GPIO18** | U1_DE      |
| PAD19 | GPIO19     | SPI3_MODE1         | GPIO19     | GPIO19     |
| PAD20 | GPIO20     | PWM_D0             | GPIO20     | I2S0_CK    |
| PAD21 | GPIO21     | PWM_D1             | GPIO21     | I2S0_WS    |
| PAD22 | GPIO22     | PWM_D2             | GPIO22     | I2S0_DATA  |
| PAD23 | GPIO23     | SPI3D1             | GPIO23     | GPIO23     |
| PAD24 | GPIO24     | SPI3D2             | **U2TXD**  | GPIO24     |
| PAD25 | GPIO25     | SPI3D3             | U2_DE      | GPIO25     |
| PAD26 | GPIO26     | SPI3D4             | GPIO26     | DSP_TCK    |
| PAD27 | GPIO27     | SPI3D5             | **U3TXD**  | DSP_TDI    |
| PAD28 | -          | SPI3D6             | GPIO28     | DSP_TMS    |
| PAD29 | DSP_TDO    | SPI3D7             | GPIO29     | -          |
| PAD30 | -          | GPIO30             | GPIO30     | PDM1_L     |
| PAD31 | GPIO31     | GPIO31             | U3_DE      | PDM1_R     |

### 3.3 常用外设引脚速查

#### UART 引脚

| 外设  | TX Pin | TX Function | RX Pin | RX Function | 备注                 |
| ----- | ------ | ----------- | ------ | ----------- | -------------------- |
| UART0 | PAD1   | FUNCTION_0  | PAD3   | FUNCTION_3  | -                    |
| UART1 | PAD17  | FUNCTION_2  | PAD16  | FUNCTION_3  | App Board 调试串口   |
| UART2 | PAD24  | FUNCTION_2  | PAD23  | FUNCTION_3  | -                    |
| UART3 | PAD27  | FUNCTION_2  | PAD26  | FUNCTION_3  | Generic EVB 调试串口 |

#### I2C 引脚

| 外设 | SCL Pin | SCL Function | SDA Pin | SDA Function | 备注 |
| ---- | ------- | ------------ | ------- | ------------ | ---- |
| I2C0 | PAD14   | FUNCTION_0/3 | PAD15   | FUNCTION_0/3 | -    |
| I2C1 | PAD0    | FUNCTION_3   | PAD1    | FUNCTION_3   | -    |
| I2C2 | PAD2    | FUNCTION_3   | PAD3    | FUNCTION_3   | -    |
| I2C3 | PAD4    | FUNCTION_3   | PAD5    | FUNCTION_3   | -    |

#### SPI 引脚

| 外设 | CLK Pin | CS0 Pin | Function   | 备注              |
| ---- | ------- | ------- | ---------- | ----------------- |
| SPI2 | PAD4    | PAD5    | FUNCTION_1 | 支持 QSPI (D0-D7) |
| SPI3 | PAD6    | PAD11   | FUNCTION_1 | -                 |

#### I2S 引脚

| 外设 | CK Pin | WS Pin | DATA Pin | Function   | 备注 |
| ---- | ------ | ------ | -------- | ---------- | ---- |
| I2S0 | PAD20  | PAD21  | PAD22    | FUNCTION_3 | -    |
| I2S1 | PAD6   | PAD7   | PAD8     | FUNCTION_3 | -    |

#### PWM 引脚

| 信号   | Pin   | Function   | 备注 |
| ------ | ----- | ---------- | ---- |
| PWM_D0 | PAD20 | FUNCTION_1 | -    |
| PWM_D1 | PAD21 | FUNCTION_1 | -    |
| PWM_D2 | PAD22 | FUNCTION_1 | -    |

---

## 4. 配置示例

### 4.1 配置 UART3 引脚

```c
#include "gpio.h"

void uart3_pins_init(void)
{
    /* PAD27 = UART3_TX (F2), PAD26 = UART3_RX (F3) */
    gpio_set_function(GPIOA, 27, FUNCTION_2);  /* TX */
    gpio_set_function(GPIOA, 26, FUNCTION_3);  /* RX */
    
    /* 可选: 设置内部上拉 */
    gpio_set_mode(GPIOA, 26, GPIO_UP);
    gpio_set_mode(GPIOA, 27, GPIO_UP);
}
```

### 4.2 配置 GPIO 输出 (LED)

```c
#include "gpio.h"

void led_gpio_init(uint8_t pin)
{
    /* 设置为 GPIO 功能 (Function 2) */
    gpio_set_function(GPIOA, pin, FUNCTION_2);
    
    /* 设置为输出模式 */
    gpio_set_direction(GPIOA, pin, 1);
    
    /* 初始输出低电平 */
    gpio_set_data(GPIOA, pin, 0);
}

void led_toggle(uint8_t pin)
{
    bool current = gpio_get_value(GPIOA, pin);
    gpio_set_data(GPIOA, pin, current ? 0 : 1);
}
```

### 4.3 配置 GPIO 中断 (按键)

```c
#include "gpio.h"

void button_init(uint8_t pin)
{
    /* 设置为 GPIO 功能 */
    gpio_set_function(GPIOA, pin, FUNCTION_2);
    
    /* 设置为输入模式 */
    gpio_set_direction(GPIOA, pin, 0);
    
    /* 启用内部上拉 */
    gpio_set_mode(GPIOA, pin, GPIO_UP);
    
    /* 配置下降沿中断 (按下触发) */
    gpio_set_interrupt(GPIOA, pin, INTERRUPT_EDGE_FALLING, true);
}

/* 中断处理函数 */
void GPIO_IRQHandler(void)
{
    if (GPIO->INTSTATUS & (1 << BUTTON_PIN)) {
        gpio_interrupt_clear(GPIOA, BUTTON_PIN);
        /* 处理按键事件 */
    }
}
```

### 4.4 摄像头引脚初始化 (完整示例)

```c
#include "gpio.h"
#include "board.h"

void camera_pins_init(void)
{
    /* I2C 控制引脚 */
    gpio_set_function(BOARD_CAMERA_I2C_PORT, BOARD_CAMERA_I2C_SCL_PIN, 
                      BOARD_CAMERA_I2C_FUNCTION);
    gpio_set_function(BOARD_CAMERA_I2C_PORT, BOARD_CAMERA_I2C_SDA_PIN, 
                      BOARD_CAMERA_I2C_FUNCTION);
    
    /* 控制引脚: RST, PWDN */
    gpio_set_function(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, BOARD_CAM_CTRL_FUNCTION);
    gpio_set_function(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, BOARD_CAM_CTRL_FUNCTION);
    
    /* 设置控制引脚为输出 */
    gpio_set_direction(BOARD_CAM_PORT, BOARD_CAM_RST_PIN, 1);
    gpio_set_direction(BOARD_CAM_PORT, BOARD_CAM_PWDN_PIN, 1);
}
```

---

## 5. 中断配置

### 5.1 中断类型枚举

```c
typedef enum {
    INTERRUPT_LEVEL_LOW    = 0x00,  /* 低电平触发 */
    INTERRUPT_LEVEL_HIGH   = 0x01,  /* 高电平触发 */
    INTERRUPT_EDGE_FALLING = 0x10,  /* 下降沿触发 */
    INTERRUPT_EDGE_RISING  = 0x11   /* 上升沿触发 */
} gpio_int_type_t;
```

### 5.2 中断寄存器

| 寄存器          | 偏移 | 说明                        |
| --------------- | ---- | --------------------------- |
| `INTEN`         | 0x30 | 中断使能                    |
| `INTMASK`       | 0x34 | 中断屏蔽                    |
| `INTTYPE_LEVEL` | 0x38 | 中断类型 (电平/边沿)        |
| `INT_POLARITY`  | 0x3C | 中断极性                    |
| `INTSTATUS`     | 0x40 | 中断状态                    |
| `RAW_INTSTATUS` | 0x44 | 原始中断状态                |
| `PORTA_EOI`     | 0x4C | 中断清除 (End of Interrupt) |

---

## 6. 电气特性配置 (IO_MUX)

### 6.1 通用 GPIO 模式

```c
typedef enum {
    GPIO_DOWN = 0,  /* 内部下拉 */
    GPIO_UP   = 1,  /* 内部上拉 */
} gpio_mode_t;
```

### 6.2 特殊信号组

IO_MUX 也可以配置专用接口的电气特性：

| 信号组                | 说明            | 配置项           |
| --------------------- | --------------- | ---------------- |
| `DVP_*`               | DVP 摄像头接口  | 上下拉           |
| `PSRAM_*`             | PSRAM 存储接口  | 驱动强度、压摆率 |
| `FLASH_*`             | QSPI Flash 接口 | 驱动强度、压摆率 |
| `SDIO0_*` / `SDIO1_*` | SD 卡接口       | 驱动强度、压摆率 |
| `GMII_*`              | 以太网接口      | 驱动强度、压摆率 |

---

## 7. 板级配置

### 7.1 在 board.h 中定义引脚

引脚定义放在 `Boards/<board_name>/board.h`：

```c
/* board.h */
#define BOARD_DEBUG_UART_PORT       GPIOA
#define BOARD_DEBUG_UART_TX_PIN     26
#define BOARD_DEBUG_UART_RX_PIN     27
#define BOARD_DEBUG_UART_FUNCTION   FUNCTION_3
```

### 7.2 初始化函数模式

```c
/* board.c */
void board_debug_uart_pins_init(void)
{
    gpio_set_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_TX_PIN, 
                      BOARD_DEBUG_UART_FUNCTION);
    gpio_set_function(BOARD_DEBUG_UART_PORT, BOARD_DEBUG_UART_RX_PIN, 
                      BOARD_DEBUG_UART_FUNCTION);
}
```

### 7.3 条件编译保护

```c
#if BOARD_CAMERA_ENABLE
void board_camera_pins_init(void)
{
    /* 摄像头引脚初始化 */
}
#endif
```

---

## 8. 故障排查

### 8.1 常见问题

| 问题             | 可能原因                 | 解决方案                                          |
| ---------------- | ------------------------ | ------------------------------------------------- |
| 引脚功能不生效   | Function 配置错误        | 检查 `gpio_set_function()` 参数                   |
| 输入读取不稳定   | 缺少上下拉配置           | 调用 `gpio_set_mode()` 设置上下拉                 |
| 中断不触发       | 中断使能或 NVIC 配置遗漏 | 检查 `gpio_set_interrupt()` 和 `NVIC_EnableIRQ()` |
| 输出驱动能力不足 | 驱动强度配置不当         | 检查 IO_MUX 驱动强度配置                          |

### 8.2 调试建议

1. **使用寄存器读取验证配置**：
   ```c
   printf("IO_MATRIX[0] = 0x%08X\n", IO_MATRIX->CFG[0]);
   printf("IO_MUX[0] = 0x%08X\n", IO_MUX->CFG[0]);
   printf("GPIO DDR = 0x%08X\n", GPIO->SWPORTA_DDR);
   ```

2. **检查时钟使能**：
   确保 GPIO 模块时钟已使能（通常在 `board_init()` 中完成）。

3. **确认引脚未被其他功能占用**：
   查看完整引脚复用表，避免功能冲突。

---

## 9. 附录

### 9.1 GPIO 寄存器映射

| 寄存器        | 偏移 | 说明                       |
| ------------- | ---- | -------------------------- |
| `SWPORTA_DR`  | 0x00 | Port A 数据寄存器          |
| `SWPORTA_DDR` | 0x04 | Port A 方向寄存器 (1=输出) |
| `SWPORTA_CTL` | 0x08 | Port A 控制寄存器          |
| `EXT_PORTA`   | 0x50 | Port A 外部输入            |
| `DEBOUNCE`    | 0x48 | 去抖动配置                 |
| `LS_SYNC`     | 0x60 | 同步配置                   |
| `ID_CODE`     | 0x64 | IP ID 代码                 |
| `VER_ID_CODE` | 0x6C | IP 版本代码                |
| `CONFIG_REG1` | 0x74 | 配置寄存器 1               |
| `CONFIG_REG2` | 0x70 | 配置寄存器 2               |

### 9.2 参考文档

**硬件原理图：**
- [Generic EVB 开发板原理图](../Boards/generic_evb/doc/NE005_F_V2.0_Schematic.pdf)
- [App Board 应用板原理图](../Boards/app_board/doc/NE005_S_V2.02_Schematic.pdf)

**BSP 文档：**
- [S300_BSP_Architecture.md](S300_BSP_Architecture.md) - BSP 架构设计
- [Development_Environment_Guide.md](Development_Environment_Guide.md) - 开发环境配置

---

## 10. 更新记录

| 版本 | 日期       | 说明                             |
| ---- | ---------- | -------------------------------- |
| 1.0  | 2026-01-21 | 初版，包含完整 32 引脚功能复用表 |

## 11. TODO

- [x] 32 引脚功能复用表
- [x] 外设引脚速查表
- [ ] IO_MUX 驱动强度参数
- [ ] PSRAM/FLASH/SDIO 接口配置
- [ ] GPIO 电气特性
