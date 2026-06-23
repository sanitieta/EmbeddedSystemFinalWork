# 系统架构

## 1. 硬件架构

```
┌────────────────────────────────────────────────────────────┐
│                    TM4C1294NCPDT (Cortex-M4)                │
│                                                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌───────────┐  │
│  │  I2C0    │  │  UART0   │  │  PWM0    │  │  Timer0A  │  │
│  │ PB2/PB3  │  │ PA0/PA1  │  │ PK5      │  │ PF0-PF3   │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └─────┬─────┘  │
│       │             │             │              │         │
│  ┌────┴────┐   ┌────┴────┐   ┌───┴───┐    ┌─────┴─────┐   │
│  │TCA6424  │   │ USB-UART│   │Buzzer │    │ 28BYJ-48  │   │
│  │ 0x22    │   │ 115200  │   │ 无源   │    │ Stepper   │   │
│  └────┬────┘   └─────────┘   └───────┘    └───────────┘   │
│       │                                                    │
│  ┌────┴────┐  ┌─────────┐  ┌──────────┐                  │
│  │ 8位7段  │  │PCA9557  │  │Hibernate │                  │
│  │ 数码管  │  │ 0x18    │  │   RTC    │                  │
│  └─────────┘  │ 8 LEDs  │  └──────────┘                  │
│               └─────────┘                                 │
│  ┌──────────────────────┐  ┌────────────┐                 │
│  │  K1-K8 矩阵按键      │  │ USER1/USER2│                 │
│  │  (TCA6424 PORT0)     │  │ (PJ0/PJ1)  │                 │
│  └──────────────────────┘  └────────────┘                 │
│                                                            │
│  SysTick 1kHz · 3 ISRs · ~38 KB Flash · 20 MHz PLL        │
└────────────────────────────────────────────────────────────┘
```

## 2. 固件分层

```
┌─────────────────────────────────────────────┐
│  main.c (14 行)                              │
│  DevicesInit() → App_Init() → while(1)       │
│              App_RunOnce()                   │
├─────────────────────────────────────────────┤
│  User/app/  — 应用逻辑层 (10 模块)            │
│  ┌──────────────────────────────────────┐   │
│  │ app.c — 主循环编排                    │   │
│  │ alarm.c — 闹钟状态机                  │   │
│  │ boot_sequence.c — 启动动画 FSM        │   │
│  │ button_logic.c — 按键事件分发          │   │
│  │ command.c — UART 命令解析 (20+ 命令)   │   │
│  │ datetime.c — 日期验证 (启动 RTC 追赶)  │   │
│  │ display.c — 7SEG/LED/*EVT 发射         │   │
│  │ melody.c — 非阻塞旋律播放器            │   │
│  │ app_state.c — 全局状态单例 g           │   │
│  │ app_types.h — 所有类型定义             │   │
│  └──────────────────────────────────────┘   │
│                    ↓ 依赖                    │
│  User/base/  — 硬件抽象层 (8 模块)           │
│  ┌──────────────────────────────────────┐   │
│  │ board.c — DevicesInit() 编排          │   │
│  │ uart_port.c — UART 非阻塞收发         │   │
│  │ i2c_bus.c — I2C 读写                 │   │
│  │ pwm_buzzer.c — PWMStart/PWMStop      │   │
│  │ stepper.c — 步进电机 8 拍驱动         │   │
│  │ hibernate_rtc.c — 休眠 RTC 初始化     │   │
│  │ timebase.c — SysTick 配置             │   │
│  │ board_config.h — 集中常量             │   │
│  └──────────────────────────────────────┘   │
│                    ↓ 依赖                    │
│  driverlib/ — TivaWare 外设驱动 (~70 模块)    │
├─────────────────────────────────────────────┤
│  User/callback.c — ISR 桥接层               │
│  SysTick_Handler / UART0_Handler /          │
│  TIMER0A_Handler                            │
│  直接读写 g.xxx 全局状态                     │
└─────────────────────────────────────────────┘
```

**依赖规则:** driverlib 不知道上层 · User/base 只知道 `app_state.h` · User/app 调用 base · callback.c 直接读写全局状态

## 3. 全局状态管理

所有运行时状态集中在单一全局结构体 `app_state_t g`：

```
app_state_t g
├── clock_data_t clock     — 时间/日期/闹钟 (含 temp_* 编辑临时值, original_* 回滚值)
├── display_ctx_t disp     — 显示上下文 (~50 字段: 缓冲区、扫描、模式、LED、消息、闹钟运行态)
├── sys_timer_t timer      — SysTick 1ms 时基 + 分频标志
├── uart_ctx_t uart        — 接收缓冲区 + Token 数组 + cmd_state
├── input_state_t in       — 按键去抖/事件标志 (K1-K8 + USER1/USER2)
├── motor_state_t motor    — 步进电机位置/方向/运行
└── rtc_data_t rtc         — Hibernate RTC 存储缓冲区
```

关键设计: cmd_state 支持多命令缓冲 (ISR 用 `\0` 分隔连续命令) · 编译期 ARMCC5 兼容 — 所有 bool/uint8_t 字段显式初始化

## 4. 主循环与中断

### 主循环 (App_RunOnce, 约 100 行)

```
每次迭代:
  1. Melody_Update()           — 旋律推进 (非阻塞)
  2. HandleAlarm()             — 闹钟检测/蜂鸣节奏

  if (init_flag):
     RunInitializationSequence()  — 6 步启动动画
  else:
     3. ProcessButtonEvents()  — 消费按键事件标志
     4. if timeout: HandleModeTimeout()
     5. if flag_1s:   UpdateTimeAndDisplayBuffers()
     6. if flag_2ms:  Update7SegmentDisplay()
     7. if shifting:  UpdateDisplayShift()
     8. Display_UpdateStatusLeds()

  9. if beep expired: PWMStop()
  10. ProcessUartCommand()
```

调用频率: 数码管扫描 500Hz · 缓冲区更新 1Hz · 流动移位 2Hz/3.3Hz · 主循环无阻塞

### 中断体系

| 中断 | 频率 | 处理函数 | 职责 |
|------|------|---------|------|
| SysTick | 1000 Hz | `SysTick_Handler` | tick++, 时基标志, **时钟进位**, 按键去抖, 超时检测 |
| UART0 | 按需 | `UART0_Handler` | 字符接收 → rx_buf, `\r\n` 触发 cmd_state=1, 多命令 `\0` 分隔 |
| Timer0A | ~68 Hz | `TIMER0A_Handler` | 步进电机 8 拍序列输出 |

**设计原则:** ISR 只设标志/填缓冲 · 主循环消费标志 · tick uint32_t 约 49.7 天回绕, 使用 `(int32_t)(current-past)>0` 安全比较

**时钟进位 (在 SysTick ISR 中):** 每秒 ss++ → mm++ → hh++ → day++ → 月进位(闰年感知) → 年进位

## 5. UART 协议

### 物理层

UART0, 115200 baud, 8-N-1, `\r` 或 `\n` 行结束。ASCII 文本。大小写不敏感 · 空格/Tab 容忍 · 最小匹配缩写 (如 `HOU`→`HOUR`, `*PIN`→`*PING`)

### PC→MCU 命令表

| 命令 | 格式 | 说明 |
|------|------|------|
| `*SET:DATE` | `YEAR/MONTH/DATE <值>` | 设置日期, 10 种部分字段组合, 自动调整非法日期 |
| `*SET:TIME` | `HOUR/MINUTE/SECOND <值>` | 设置时间, 同上 |
| `*SET:ALARM` | `...` 或 `OFF` | 设置闹钟; OFF 设 alm_hh=25 关闭 |
| `*SET:DISPLAY` | `ON`/`OFF` | 数码管开关 |
| `*SET:FORMAT` | `LEFT`/`RIGHT` | 设置方向 (同时影响 shift_mode 和 reversed) |
| `*SET:MSG` | `<文本>` (≤32 字节) | 临时消息; ≤8 字符静态 2.5s, >8 滚动一次 |
| `*SET:LED` | `<hex2>` | LED 接管; 00 退出, 非零强制模式 |
| `*SET:WEATHER` | `<hex2>` | 天气 LED5-7 指示 (掩码 0xE0, 不进入接管) |
| `*SET:BEEP` | `<ms>` (10-5000) | 远程蜂鸣, 2400 Hz, 自动停止 |
| `*SET:MODE` | `NIGHT`/`DAY` | 夜间模式 |
| `*SET:KEY` | `FUNC`/`SHIFT`/.../`USER1`/`USER2` | 虚拟按键注入 |
| `*GET:*` | 见完整协议 | DATE/TIME/ALARM/DISPLAY/FORMAT/MOTOR |
| `*MOTOR:*` | `START`/`STOP`/`FWD`/`REV` | 步进电机控制 |
| `*RST` | — | 复位协议状态 (DISP=ON, LEFT, 退出接管, MODE=FLOWING) |
| `*NTP SYNC` | — | 标记 NTP 对时完成, 亮 LED4 |
| `*PING` | — | 回应 `*PONG <uptime_s>` |
| `HELP` | — | 打印完整命令参考 |
| `INIT` | — | 软件系统复位 |

所有成功响应: `OK` / `OK <value>` · 所有失败: `ERROR SYNTAX`/`PARAM`/`RANGE`/`BUSY`

### MCU→PC 事件

| 事件 | 格式 | 触发时机 |
|------|------|---------|
| `*EVT:DISP` | `<8chars> <dpHex>` | 显示变化 + 1Hz 心跳 |
| `*EVT:LED` | `<hex2>` | LED 变化 + 1Hz 心跳 |
| `*EVT:KEY` | `USER1`/`USER2`/`EXT` | 物理按键短按 |
| `*EVT:MODE` | `FLOWING`/`DATE`/`TIME`/`ALARM`/`NIGHT`/`DAY` | 模式切换 |
| `*EVT:ALARM` / `*EVT:ALARM OFF` | — | 闹钟开始/停止 |
| `*EVT:EDIT` | `DATE/TIME/ALARM <值>` | 设置保存 (按键或 UART) |

FORMAT RIGHT 时: 8 字符反转 + DP 位图镜像 (bit N→bit 6-N, bit 7→bit 0). 6 字符内容 (TIME/DATE) 通过 `CenterContent()` 居中, 两侧补 `_`

## 6. LED 系统

### 默认状态 LED

| LED | 位 | 行为 |
|-----|---|------|
| LED0 | 0x01 | 1Hz 心跳 (500ms ON/OFF) |
| LED1 | 0x02 | 闹钟启用: 常亮; 响铃中: 200ms 闪烁 |
| LED2 | 0x04 | 编辑模式激活 |
| LED3 | 0x08 | UART 活动 (收到数据后亮 100ms) |
| LED4 | 0x10 | NTP 对时完成 (受 `*NTP SYNC` 设置, `*RST` 清除) |
| LED5-7 | 0xE0 | 天气指示 (`*SET:WEATHER` 下发, 不进入接管) |

### 天气 LED 编码 (3-bit)

| 天气 | Hex | LED7 LED6 LED5 |
|------|-----|----------------|
| 晴 SUNNY | 0x80 | 1 0 0 |
| 多云 CLOUDY | 0x40 | 0 1 0 |
| 雾 FOG | 0x60 | 0 1 1 |
| 雨 RAIN | 0x20 | 0 0 1 |
| 雪 SNOW | 0xA0 | 1 0 1 |
| 雷暴 STORM | 0xC0 | 1 1 0 |
| 未知 WEATHER | 0xE0 | 1 1 1 |

### LED 接管模式

`*SET:LED <非零>` 进入接管, 强制输出指定模式; `*SET:LED 00` 退出, 恢复固件自主控制。`*SET:WEATHER` 不触发接管, 仅 OR 入天气位。

## 7. PC 上位机架构

```
┌──────────────────────────────────────────────────────────┐
│               SerialWorker (QThread)                     │
│  20 Hz 循环: 非阻塞读取 → 拆行 → 发射 line_received     │
│  1 Hz *PING + 3s 超时 + 5s 宽限期 + 断连恢复            │
├──────────────────────────────────────────────────────────┤
│                  MainWindow                              │
│  _on_line_received → Protocol.parse_event → dispatch     │
│                                                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────┐ │
│  │TwinPanel │ │Control   │ │LogPanel  │ │Extensions │ │
│  │          │ │Panel     │ │          │ │           │ │
│  │7SEG 发光 │ │6 组命令  │ │TX 蓝     │ │ NtpHelper │ │
│  │LED 渐变  │ │参数预设  │ │RX 青     │ │ Weather   │ │
│  │虚拟按键  │ │缩写演示  │ │事件紫    │ │ DayNight  │ │
│  └──────────┘ └──────────┘ └──────────┘ │ Dashboard │ │
│                                         └───────────┘ │
└──────────────────────────────────────────────────────────┘
```

**关键设计:**
- 数字孪生 7SEG 两遍渲染: Glow Pass (半透明宽描边, 仅激活段) → Crisp Pass (实心填充, 所有段)
- `protocol.py` 零 Qt 依赖, 可命令行单测
- 收到**任何** MCU 数据都刷新心跳 (不限于 `*PONG`) — 根因修复 "收到数据但显示断连"
- NTP/天气 在 daemon 线程中执行, Qt 信号回主线程
- 天气双源备用: Open-Meteo (WMO 码) → wttr.in (关键词) → 2h 缓存
- QSS 暗色主题: 5 层卡片层级, 语义色 (TX 蓝 / RX 青 / 事件紫 / 错误红)
