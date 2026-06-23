# TM4C1294 智能互联时钟 固件架构文档

> 基于代码库 commit: `1c12b6e`, 生成日期: 2026-06-21
> 由 ultracode workflow 自动生成 — 43 个代理读取全部 42 个源文件

---

## 1. 项目总览 (Project Overview)

### 1.1 固件功能

本固件运行于 S800 开发板的 TM4C1294NCPDT 微控制器上，实现一个智能互联时钟系统，主要功能包括：

- **时钟显示**: 7 段数码管通过 I2C 驱动芯片 (TCA6424) 实现 8 位动态扫描显示
- **日期/时间管理**: 支持年月日时分秒的完整日历，含闰年判断
- **闹钟功能**: 可设置闹钟时间，到达后 PWM 蜂鸣器以 200ms 交替节奏响铃，10 秒自动停止
- **启动动画**: 上电全亮 → 全灭 → 学号 524031910772 → 姓名 XUHAORAN → 版本 v1.0
- **开机旋律**: 播放 "See You Again" (Wiz Khalifa ft. Charlie Puth) 钢琴副歌旋律，约 380 个音符
- **按键交互**: 8 个矩阵按键 (K1-K8) + 2 个独立按键 (USER1/USER2)
- **编辑模式**: 日期编辑 → 时间编辑 → 闹钟编辑 → 正常显示，支持字段闪烁和超时回滚
- **UART 命令**: 完整的 ASCII 文本协议，支持 PC 上位机远程控制所有功能
- **步进电机**: 28BYJ-48 以 1 RPM 旋转，通过 Timer0A 中断驱动 8 拍序列
- **休眠 RTC**: 利用 Hibernate 模块在掉电后保持时间
- **夜间模式**: 仅显示 HH.MM，静音蜂鸣器，关闭除心跳外的所有 LED
- **LED 接管**: 上位机可通过 UART 强制控制 LED 输出
- **消息显示**: 支持临时文本消息显示（静止短消息或滚动长消息）

### 1.2 硬件平台

| 组件 | 型号/资源 | 连接方式 |
|------|----------|---------|
| MCU | TM4C1294NCPDT (Cortex-M4, 120MHz max) | - |
| 系统时钟 | 20 MHz (PLL from 25MHz XTAL, VCO 480MHz) | 内部 |
| 数码管驱动 | TCA6424 (I2C 0x22) | I2C0, PB2(SCL)/PB3(SDA) |
| LED 驱动 | PCA9557 (I2C 0x18) | I2C0 |
| 蜂鸣器 | 无源蜂鸣器, PWM0 Gen3 Out7 | PK5 (M0PWM7) |
| 矩阵按键 K1-K8 | TCA6424 PORT0 输入 | I2C0 |
| 独立按键 USER1/USER2 | GPIO PJ0/PJ1 | 直接 GPIO |
| 步进电机 28BYJ-48 | 4 相, PF0-PF3 | Timer0A 中断 |
| UART 调试串口 | UART0, 115200-8-N-1 | PA0(RX)/PA1(TX) |
| 休眠 RTC | Hibernate 模块 | 内部 |

### 1.3 构建系统

- **构建工具**: CMake 3.20 + ARM Compiler 5 (armcc, Keil MDK 工具链)
- **构建配置**: `CMakeLists.txt` 定义所有源文件和包含路径
- **预定义宏**: `__CC_ARM`, `TM4C1294NCPDT`, `PART_TM4C1294NCPDT`, `_RTE_`
- **CLion 兼容**: CMake 配置使 CLion 能解析 TivaWare 头文件
- **当前构建**: 0 errors, 0 warnings. Code=27546 RO-data=10206 RW-data=708 ZI-data=612 (Flash: ~38 KB / 1024 KB)

---

## 2. 目录结构与分层 (Directory Structure & Layering)

### 2.1 完整目录树

```
Project/
├── CMakeLists.txt                    # CMake 构建脚本
├── CLAUDE.md                         # 项目开发指南
├── ARCHITECTURE.md                   # 本文档
│
├── RTE/                              # CMSIS 运行时环境 (Keil MDK 生成)
│   ├── _Target_1/
│   │   └── RTE_Components.h          # CMSIS 设备头文件选择
│   └── Device/TM4C1294NCPDT/
│       ├── startup_TM4C129.s         # 启动汇编 (向量表 + Reset Handler)
│       └── system_TM4C129.c          # 系统初始化 (SystemInit, FPU 使能)
│
├── inc/                              # TivaWare 硬件寄存器定义 (只读)
│   ├── hw_memmap.h                   # 外设基地址映射
│   ├── hw_types.h                    # 硬件访问类型定义
│   ├── hw_gpio.h, hw_i2c.h, ...     # 各外设寄存器位定义
│   └── tm4c1294ncpdt.h              # 芯片中断号枚举 (IRQn_Type)
│
├── driverlib/                        # TivaWare 外设驱动库
│   ├── sysctl.c/h                    # 系统控制 (时钟、复位)
│   ├── gpio.c/h                      # GPIO 驱动
│   ├── uart.c/h                      # UART 驱动
│   ├── i2c.c/h                       # I2C 驱动
│   ├── pwm.c/h                       # PWM 驱动
│   ├── timer.c/h                     # 通用定时器驱动
│   ├── hibernate.c/h                 # 休眠模块驱动
│   ├── systick.c/h                   # SysTick 驱动
│   ├── interrupt.c/h                 # NVIC 中断控制
│   ├── pin_map.h                     # 引脚复用映射
│   └── ...
│
└── User/                             # 用户应用层代码
    ├── main.c                        # 入口: main() (14 行)
    ├── callback.c/h                   # 中断回调函数 (ISR 实现)
    │
    ├── base/                          # 底层硬件抽象驱动
    │   ├── board.c/h                  # 板级初始化 (DevicesInit)
    │   ├── board_config.h             # 全局常量与引脚宏定义
    │   ├── timebase.c/h              # SysTick 1ms 时基与标志生成
    │   ├── delay.c/h                  # 简单软件延时
    │   ├── uart_port.c/h             # UART0 初始化和收发
    │   ├── i2c_bus.c/h               # I2C0 初始化和读写
    │   ├── pwm_buzzer.c/h            # PWM 蜂鸣器驱动 (启停/频率)
    │   ├── stepper.c/h               # 步进电机 GPIO + Timer 驱动
    │   └── hibernate_rtc.c/h         # 休眠 RTC 初始化
    │
    └── app/                           # 应用逻辑层
        ├── app_types.h                # 所有类型与结构体定义
        ├── app_state.h                # 全局状态单例声明
        ├── app_state.c                # 全局状态实例与初始化值
        ├── app.h/c                    # 应用初始化与主循环编排
        ├── datetime.h/c               # 日期/时间验证与进位
        ├── alarm.h/c                  # 闹钟检测与蜂鸣节奏
        ├── display.h/c                # 7 段显示刷新、LED 管理、缓冲区
        ├── boot_sequence.h/c         # 启动动画序列
        ├── button_logic.h/c          # 按键事件处理与编辑流程
        ├── command.h/c               # UART 命令解析与分发
        └── melody.h/c                # 开机旋律播放器
```

### 2.2 分层架构图

```
+====================================================================+
|                        main.c (while(1) 主循环)                      |
|  调用: App_Init() → while(true) { App_RunOnce(); }                  |
+====================================================================+
|                                                                     |
|  User/app/ (应用逻辑层)                                              |
|  +--------------------------------------------------------------+  |
|  | app.c  -- 主循环编排 (Melody/Alarm/Button/Display/Command)    |  |
|  | boot_sequence.c -- 启动动画                                   |  |
|  | button_logic.c -- 按键事件分发与编辑状态机                     |  |
|  | command.c -- UART 协议解析与命令执行                           |  |
|  | display.c -- 7-SEG 刷新, LED 更新, 缓冲区管理                  |  |
|  | alarm.c -- 闹钟触发与蜂鸣节奏                                   |  |
|  | datetime.c -- 日期/时间有效性校验与 Tick                       |  |
|  | melody.c -- 旋律播放器 (非阻塞)                                |  |
|  | app_state.c -- 全局状态 g 实例                                  |  |
|  | app_types.h -- 所有数据结构定义                                |  |
|  +------------------------------+-------------------------------+  |
|                                 | 依赖                              |
|  User/base/ (硬件抽象层)        |                                   |
|  +------------------------------+-------------------------------+  |
|  | board.c -- 外设初始化编排     |                                 |  |
|  | timebase.c -- SysTick 时基   |                                 |  |
|  | uart_port.c -- UART 收发     |                                 |  |
|  | i2c_bus.c -- I2C 读写        |                                 |  |
|  | pwm_buzzer.c -- PWM 蜂鸣器   |                                 |  |
|  | stepper.c -- 步进电机驱动    |                                 |  |
|  | hibernate_rtc.c -- RTC 初始化|                                 |  |
|  | delay.c -- 软件延时          |                                 |  |
|  +------------------------------+-------------------------------+  |
|                                 | 依赖                              |
|  callback.c (中断服务程序)       |                                   |
|  +------------------------------+-------------------------------+  |
|  | UART0_Handler() -- RX FIFO → ring buffer                    |  |
|  | SysTick_Handler() -- tick++, 按键去抖, 时钟进位              |  |
|  | TIMER0A_Handler() -- StepperTimerTick()                      |  |
|  +------------------------------+-------------------------------+  |
|                                 | 调用 driverlib                    |
+====================================================================+
|                                                                     |
|  driverlib/ (TivaWare 外设驱动库)                                   |
|  +--------------------------------------------------------------+  |
|  | sysctl, gpio, uart, i2c, pwm, timer, hibernate, systick, ... |  |
|  +------------------------------+-------------------------------+  |
|                                 | 访问寄存器                        |
|  inc/ (硬件寄存器映射头文件)                                        |
|  +------------------------------+-------------------------------+  |
|  | hw_memmap, hw_gpio, hw_i2c, ... (寄存器地址与位定义)         |  |
|  +------------------------------+-------------------------------+  |
+====================================================================+
|                                                                     |
|  RTE/ (CMSIS 启动)                                                  |
|  +--------------------------------------------------------------+  |
|  | startup_TM4C129.s -- 向量表, Reset_Handler → SystemInit → main|
|  | system_TM4C129.c -- FPU 使能, SystemCoreClock                    |
|  +--------------------------------------------------------------+  |
+====================================================================+
```

### 2.3 依赖规则

```
main.c                → board.h, app.h
User/app/*.c          → app_state.h, User/base/*.h, driverlib/*.h
User/base/*.c         → board_config.h, app_state.h, driverlib/*.h
callback.c            → app_state.h, datetime.h, stepper.h, i2c_bus.h
board.c               → User/base 所有模块 + app_state.h
app_state.c           → app_types.h
```

- **driverlib** 不知道上层存在
- **User/base** 只知道 `app_state.h` (全局状态 g) 和 `board_config.h` (常量)，不知道任何 `User/app` 模块
- **User/app** 调用 `User/base` 的公开接口，但 `User/base` 绝不调用 `User/app`
- **callback.c** 是唯一跨越分层的地方: ISR 直接读写 `g.xxx` 全局状态，不通过函数调用传递

---

## 3. 启动与初始化流程 (Boot & Init Flow)

### 3.1 硬件上电到 main()

```
硬件上电
    |
    v
CPU 从地址 0x00000000 读取 SP (__initial_sp = 0x20000200)
    从地址 0x00000004 读取 PC = Reset_Handler
    |
    v
Reset_Handler (startup_TM4C129.s):
    LDR R0, =SystemInit
    BLX R0                    // 1. 调用 SystemInit()
    LDR R0, =__main
    BX  R0                    // 2. 跳转到 C 运行时初始化 → main()
    |
    v
SystemInit (system_TM4C129.c):
    +-- 如果 __FPU_USED == 1:
    |   SCB->CPACR |= (3 << 20) | (3 << 22)  // 使能 FPU (CP10, CP11 完全访问)
    +-- 如果 UNALIGNED_SUPPORT_DISABLE:
    |   SCB->CCR |= SCB_CCR_UNALIGN_TRP_Msk  // 禁用非对齐访问
    +-- SystemCoreClock = 16,000,000         // 初始值 (稍后重配)
    |
    v
__main (ARM C 运行时库):
    +-- 初始化数据段 (从 Flash 复制 .data 到 RAM)
    +-- 清零 .bss 段
    +-- 调用 main()
```

### 3.2 main() 函数

```c
int main(void)
{
    DevicesInit();   // 板级外设初始化
    App_Init();      // 应用层初始化

    while (true)
    {
        App_RunOnce();  // 主循环每次迭代
    }
}
```

### 3.3 DevicesInit() 初始化顺序

```
DevicesInit() [board.c]:
    1. SysCtlClockFreqSet(..., 20000000)
       +-- 设置系统时钟为 20 MHz
       +-- 返回实际频率存入 g.sys_clock_hz

    2. UARTInit() [uart_port.c]
       +-- 使能 UART0 + GPIOA 时钟
       +-- 配置 PA0→U0RX, PA1→U0TX
       +-- UARTConfigSetExpClk: 115200-8-N-1
       +-- FIFO: TX 1/8, RX 7/8
       +-- 使能 UART0 中断 (RX + RX Timeout)

    3. PWMInit() [pwm_buzzer.c]
       +-- 使能 PWM0 + GPIOK 时钟
       +-- 配置 PK5→M0PWM7
       +-- PWM 发生器 3: 向下计数模式，无同步
       +-- 输出位 7 使能

    4. S800_I2C0_Init() [i2c_bus.c]
       +-- 使能 I2C0 + GPIOB 时钟
       +-- 配置 PB2→SCL, PB3→SDA
       +-- I2CMasterInitExpClk: 快速模式
       +-- 配置 TCA6424: PORT0 输入, PORT1/2 输出
       +-- 配置 PCA9557: 全部输出, 初始高电平

    5. HibernateInit() [hibernate_rtc.c]
       +-- 使能 Hibernate 模块时钟
       +-- HibernateEnableExpClk: 用系统时钟初始化
       +-- HibernateClockConfig: 低功耗振荡器
       +-- HibernateRTCEnable: 使能 RTC

    6. UserKeyGPIOInit() [board.c]
       +-- 使能 GPIOJ 时钟
       +-- PJ0/PJ1 配置为输入，上拉
       +-- 标记 g.in.user_gpio_ready = true

    7. StepperGPIOInit() [stepper.c]
       +-- 使能 GPIOF 时钟
       +-- 解锁 PF0 (NMI 锁定)
       +-- PF0-PF3 配置为推挽输出

    8. StepperTimerInit() [stepper.c]
       +-- 使能 Timer0 时钟
       +-- Timer0A 配置为周期定时器
       +-- 定时器装载值 = 20MHz×60 / (4096×1) - 1
       +-- 使能 Timer0A 超时中断
       +-- 启动 Timer0A

    9. SysTickInit() [timebase.c]
       +-- SysTickPeriodSet: 周期 = 20MHz / 1000 = 20000
       +-- SysTickEnable: 启动计数
       +-- SysTickIntEnable: 使能中断

   10. IntMasterEnable()
       +-- 开启全局中断 (CPSIE i)
```

### 3.4 启动动画序列 (Boot Sequence)

`g.disp.init_flag = true` 时主循环跳过正常逻辑，执行 `RunInitializationSequence()`:

| Step | 显示内容 | LED | 定时 | 描述 |
|------|---------|-----|------|------|
| (前置) | - | - | - | RestoreRtcOrDefaultTime() 恢复时间 |
| 0 | 8 位全亮 (0xFF) | 全亮 (0xFF) | 900ms | 测试所有数码管和 LED |
| 1 | 8 位全灭 | 全灭 | 900ms | 确认所有段可关闭 |
| 2 | "52403191" | 灭 | 900ms | 学号前半 |
| 3 | "31910772" | 灭 | 900ms | 学号后半 |
| 4 | "XUHAORAN" | 灭 | 900ms | 学生姓名 |
| 5 | "v1.0" | 灭 | 900ms | 固件版本 |
| 6 | FinishBootSequence() | - | - | 切换到正常显示 |

RTC 时间恢复：读取 HibernateRTCGet() 和 HibernateDataGet()，计算经过秒数，逐秒回放 DateTime_TickOneSecond() 恢复时间。

---

## 4. 全局状态管理 (Global State Management)

### 4.1 AppState 结构体

整个固件使用**单一全局结构体** `app_state_t g`，定义在 `app_types.h`，实例化在 `app_state.c`。所有模块通过包含 `app_state.h` 获取 `extern app_state_t g` 声明。

```
app_state_t g:
|
+-- uint32_t sys_clock_hz          // 系统时钟频率 (Hz), board.c 初始化后写入
|
+-- clock_data_t clock             // ---- 时钟数据 ----
|   +-- hh, mm, ss                 // 当前时间 (0-23, 0-59, 0-59)
|   +-- year, month, day           // 当前日期 (2000-2099, 1-12, 1-31)
|   +-- alm_hh, alm_mm, alm_ss     // 闹钟设定 (25 表示未设置)
|   +-- alarm_enabled              // 闹钟是否启用 (alm_hh != 25)
|   +-- temp_*                     // 编辑模式临时值 (9 个字段)
|   +-- original_*                 // 进入编辑前的原始值 (9 个字段)
|   +-- unsaved_changes_active     // 存在未保存的更改标志
|
+-- display_ctx_t disp             // ---- 显示上下文 ----
|   +-- time_buf[9], alarm_buf[9], date_buf[11]  // 协议传输缓冲区
|   +-- seg7[18]                   // 7 段字形表 (0-F, -, 空白)
|   +-- master_buf[18]             // 流动显示主缓冲区 (日期+时间)
|   +-- cnt (0-7)                  // 当前数码管扫描索引
|   +-- shift (0-17)               // 流动显示移位量
|   +-- rightshift (0x01-0x80)     // 位选控制
|   +-- shift_mode                 // false=左移, true=右移 (FORMAT)
|   +-- shift_speed                // false=慢(500ms), true=快(300ms)
|   +-- shifting                   // 是否正在流动
|   +-- reversed                   // 显示是否反向 (对应 FORMAT RIGHT)
|   +-- on                         // 7 段数码管是否开启
|   +-- blinking                   // 编辑字段闪烁标志
|   +-- mode                       // 系统模式枚举
|   +-- field                      // 当前编辑字段枚举
|   +-- main_disp                  // 正常显示内容枚举
|   +-- prev_shifting/prev_shift_mode/prev_shift_speed  // 进入编辑前的状态快照
|   +-- night_mode                 // 夜间模式标志
|   +-- led_takeover               // LED 上位机接管标志
|   +-- led_pattern                // LED 接管强制输出值
|   +-- uart_activity_until        // UART 活动 LED 截止 tick
|   +-- alarm_ringing / alarm_silenced / alarm_ring_start / alarm_beep_phase / alarm_beep_on
|   +-- msg_active, msg_scroll, msg_buf[33], msg_len, msg_shift, msg_start, msg_last_shift
|   +-- save_blink_active, save_blink_timer, long_press_saving
|   +-- init_flag, init_step, i2c_result
|
+-- sys_timer_t timer              // ---- 系统时基 ----
|   +-- tick (uint32_t)            // 1ms 自增计数器 (约 49.7 天回绕)
|   +-- flag_2ms, flag_500ms, flag_300ms, flag_900ms, flag_1s  // 时间片标志
|   +-- mode_timeout               // 编辑模式超时 tick
|   +-- mode_timeout_flag          // 超时事件标志
|
+-- uart_ctx_t uart                // ---- UART 上下文 ----
|   +-- rx_buf[100]                // 接收环形缓冲区
|   +-- rx_len                     // 当前接收长度
|   +-- cmd_state (volatile)       // 0=空闲, 1=有新命令
|   +-- tokens[10] (每个 15 字节)   // 解析出的 Token
|   +-- num_tokens                 // Token 数量
|
+-- input_state_t in               // ---- 按键输入状态 ----
|   +-- debounce[8], press_start[8], long_press[8]  // TCA6424 K1-K8
|   +-- state[8], short_evt[8], long_start_evt[8], repeat_evt[8]
|   +-- user_debounce[2], user_press_start[2]       // USER1/USER2
|   +-- user_state[2], user_short_evt[2]
|   +-- user_gpio_ready                             // GPIO 就绪标志
|
+-- motor_state_t motor            // ---- 步进电机 ----
|   +-- position (0-4095)          // 当前步进位置
|   +-- direction (0=正转, 1=反转) // 旋转方向
|   +-- running (0=停止, 1=运行)   // 运行状态
|
+-- rtc_data_t rtc                 // ---- 休眠 RTC ----
    +-- stored[4]                  // 待存储数据 [hh, mm, ss, RTC_counter]
    +-- current                    // 当前 RTC 计数值
    +-- fetched[4]                 // 从 Hibernate 读取的数据
    +-- days_in_month[13]          // 每月天数表
```

### 4.2 状态读写规则

| 状态字段 | 写入者 (Writer) | 读取者 (Reader) |
|---------|----------------|----------------|
| clock.hh/mm/ss | SysTick_Handler (每秒+1), command.c, button_logic.c | display.c, alarm.c, command.c |
| clock.alm_* | command.c, button_logic.c | alarm.c, display.c, command.c |
| clock.temp_* | button_logic.c (递增/编辑) | display.c (闪烁显示), button_logic.c |
| disp.mode/field | button_logic.c, command.c, boot_sequence.c | display.c, button_logic.c, alarm.c |
| disp.on | command.c, boot_sequence.c, button_logic.c | display.c |
| disp.night_mode | command.c | display.c, alarm.c |
| disp.shift_mode/reversed | button_logic.c, command.c | display.c, command.c |
| disp.msg_* | command.c (SET:MSG), display.c (stop) | display.c |
| disp.led_takeover/pattern | command.c | display.c |
| disp.alarm_* | alarm.c | alarm.c, display.c |
| disp.init_flag/step | boot_sequence.c | boot_sequence.c, app.c |
| timer.tick | SysTick_Handler | 几乎所有模块 |
| timer.flag_* | SysTick_Handler | app.c, display.c, boot_sequence.c |
| uart.rx_* | UART0_Handler | command.c |
| uart.cmd_state | UART0_Handler (置1), app.c (清零处理) | command.c |
| in.* (按键) | SysTick_Handler | button_logic.c, board.c |
| motor.* | command.c, stepper.c | stepper.c, command.c |
| rtc.stored[] | display.c (UpdateTimeAndDisplayBuffers) | boot_sequence.c |
| rtc.current/fetched[] | boot_sequence.c (RestoreRtc) | boot_sequence.c |

---

## 5. 模块详解 (Module Details)

### 5.1 board.c/h + board_config.h — 硬件抽象

**文件**: `User/base/board.c`, `User/base/board.h`, `User/base/board_config.h`

**职责**: `DevicesInit()` 按顺序初始化所有外设；`UserKeyGPIOInit()` 初始化 USER1/USER2 按键 GPIO；`board_config.h` 集中定义所有硬件常量。

**board_config.h 关键常量**:

| 宏 | 值 | 含义 |
|----|----|------|
| SYSTICK_FREQUENCY | 1000 | SysTick 频率 1kHz |
| V_T2ms | 2 | 2ms tick 分频 |
| V_T500ms | 500 | 500ms tick 分频 |
| V_T300ms | 300 | 300ms tick 分频 |
| V_T900ms | 900 | 900ms tick 分频 |
| V_T1s | 1000 | 1 秒 tick 分频 |
| BLINK_ON_TIME_MS | 500 | 编辑闪烁亮 500ms |
| BLINK_OFF_TIME_MS | 500 | 编辑闪烁灭 500ms |
| MODE_TIMEOUT_MS | 5000 | 编辑模式 5s 超时 |
| SAVE_BLINK_DURATION_MS | 2000 | 保存成功闪烁 2s |
| DEBOUNCE_TIME_MS | 20 | 按键去抖 20ms |
| LONG_PRESS_TIME_MS | 800 | 长按阈值 800ms |
| REPEAT_PRESS_TIME_MS | 200 | 长按连发间隔 200ms |
| UART_ACTIVITY_FLASH_MS | 100 | UART 活动 LED 亮 100ms |
| MESSAGE_STATIC_MS | 2500 | 短消息显示 2.5s |
| ALARM_BEEP_ON_MS | 200 | 蜂鸣开 200ms |
| ALARM_BEEP_OFF_MS | 200 | 蜂鸣关 200ms |
| ALARM_RING_MAX_MS | 10000 | 响铃最长 10s |
| TCA6424_I2CADDR | 0x22 | TCA6424 I2C 地址 |
| PCA9557_I2CADDR | 0x18 | PCA9557 I2C 地址 |
| MAX_COMMAND_TOKENS | 10 | 最大 Token 数 |
| MAX_TOKEN_LENGTH | 15 | Token 最大长度 |

**公开 API**: `void DevicesInit(void)`, `void UserKeyGPIOInit(void)`

### 5.2 timebase.c/h — SysTick 1ms 时基

**公开 API**: `void SysTickInit(void)`, `void SysTickUpdateTimebaseFlags(void)`

**注**: `SysTickUpdateTimebaseFlags()` 的逻辑已完全复制到 `callback.c` 的 `SysTick_Handler()` ISR 中。该函数保留但未被调用。

### 5.3 delay.c/h — 阻塞延时

**公开 API**: `void Delay(uint32_t value)` — CPU 忙等延时，仅在 I2C 读取间隔使用。

### 5.4 uart_port.c/h — UART 驱动

**公开 API**:
```c
void UARTInit(void);
void UARTStringPutNOBlocking(uint32_t ui32Base, uint8_t *cMessage);
void UARTStringPutReversedNOBlocking(uint32_t ui32Base, uint8_t *cMessage);
void UARTCharPutBlocking(uint32_t ui32Base, uint8_t ucData);
```

FIFO: TX 1/8, RX 7/8。中断: INT_UART0 + UART_INT_RX | UART_INT_RT。

### 5.5 i2c_bus.c/h — I2C 驱动

**公开 API**:
```c
void S800_I2C0_Init(void);
uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData);
uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr);
```

I/O 扩展器配置: TCA6424 (0x22): PORT0=输入(按键), PORT1=输出(段码), PORT2=输出(位选)。PCA9557 (0x18): 全部输出(LED)。

### 5.6 pwm_buzzer.c/h — PWM 蜂鸣器

**公开 API**:
```c
void PWMInit(void);
void PWMStart(uint32_t ui32Freq_Hz);    // 启动指定频率 (50% 占空比)
void PWMStop(void);                     // 停止并切 GPIO 拉低 (消除丝丝声)
```

**关键细节**: 引脚 PK5, M0PWM7, PWM 发生器 3, 输出位 7。`PWMStop()` 将引脚切回 GPIO 并输出低电平以消除无源蜂鸣器的背景噪声。

### 5.7 stepper.c/h — 步进电机驱动

**公开 API**:
```c
void StepperGPIOInit(void);
void StepperTimerInit(void);
void StepperTimerTick(void);    // Timer0A ISR 回调
```

**8 拍序列** (半步驱动): 0x01, 0x03, 0x02, 0x06, 0x04, 0x0C, 0x08, 0x09。参数: 4096 拍/圈, 1 RPM。

### 5.8 hibernate_rtc.c/h — 休眠 RTC

**公开 API**: `void HibernateInit(void)`

每秒调用 `HibernateDataSet(g.rtc.stored, 4)` 存储 [hh, mm, ss, RTC_counter]。启动时读取恢复。

### 5.9 app_state.c/h — 全局应用状态

定义并初始化全局状态单例 `app_state_t g`。默认值: 2025-06-03 00:00:00, 闹钟未设置 (alm_hh=25), init_flag=true。

### 5.10 app_types.h — 共享类型定义

**枚举**: `system_mode_t` (FLOWING/DATE_SET/TIME_SET/ALARM_SET/ALARM_DISPLAY), `setting_field_t` (NONE/YEAR/MONTH/DAY/HOUR/MINUTE/SECOND/ALARM_HOUR/ALARM_MINUTE/ALARM_SECOND), `main_display_t` (FLOW/TIME/DATE/YEAR)。

**结构体**: `command_token_t`, `clock_data_t`, `display_ctx_t`, `sys_timer_t`, `uart_ctx_t`, `input_state_t`, `motor_state_t`, `rtc_data_t`, `app_state_t`。

### 5.11 datetime.c/h — 日期/时间管理

**公开 API**:
```c
bool is_leap_year(uint16_t year_val);
bool is_valid_date(uint16_t y, uint8_t m, uint8_t d);
bool is_valid_time(uint8_t h, uint8_t m, uint8_t s);
void DateTime_TickOneSecond(void);
```

进位逻辑: ss≥60 → mm++ → mm≥60 → hh++ → hh≥24 → day++ → day>当月天数 → month++ → month>12 → year++。

### 5.12 alarm.c/h — 闹钟逻辑

**公开 API**:
```c
void HandleAlarm(void);
void StopAlarmRinging(bool silence_current_match);
```

**状态机**:
```
[正常] --(时间==闹钟设定且未静音)--> [响铃中]
  |                                     |
  | alarm_beep_on = true                | 每 200ms 翻转
  | PWMStart(500Hz)                     | PWMStart/PWMStop
  | alarm_ring_start = tick             |
  |                                     |
  | [持续 10s 后] 或 [FUNC 短按]       |
  +<--- StopAlarmRinging() -------------+
```

蜂鸣参数: 频率 500 Hz, 节奏 200ms ON / 200ms OFF, 最长 10 秒, 夜间模式完全静音。

### 5.13 display.c/h — 7 段显示 + LED 管理

**公开 API**:
```c
void Update7SegmentDisplay(void);                             // 2ms 调用: 扫描一个数码管位置
void UpdateDisplayShift(void);                                // 300/500ms 调用: 流动移位
void UpdateTimeAndDisplayBuffers(void);                       // 1s 调用: 更新所有缓冲区
void Display_StartMessage(const uint8_t *text, uint8_t len);  // 启动临时消息
void Display_StopMessage(void);                               // 停止临时消息
void Display_SendEvent(void);                                 // 发送 *EVT:DISP
void Display_SetLedOutput(uint8_t led_pattern);               // 设置 LED 输出
void Display_UpdateStatusLeds(void);                          // 更新 LED 状态
void Display_FormatBufferForProtocol(const uint8_t *src, uint8_t len, uint8_t *dst);
```

**7 段字形表** (seg7[18]): 0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07(0-7), 0x7F,0x6F,0x77,0x7C,0x58,0x5E,0x79,0x71(8-9,A-F), 0x5C('-'), 0x000(空白)。

**显示模式内容布局**:
```
FLOWING+TIME:       "_HH.MM.SS_"  (dp=0x0A)
FLOWING+DATE:       "_YY.MM.DD_"  (dp=0x0A)
FLOWING+YEAR:       "YYYY.MMDD"   (dp=0x28)
FLOWING+FLOW:       master_buf[18] 滚动
DATE_SET:           "YYYY.MMDD"   (dp=0x28, 字段闪烁)
TIME_SET:           "  HH.MM.SS"  (dp=0x0A, 字段闪烁)
ALARM_SET:          "  HH.MM.SS"  (dp=0x0A, 字段闪烁)
ALARM_DISPLAY:      "ALHH:MM:SS"  或 "ALxx:xx"
NIGHT:              "HH.MM____"   (dp=0x02)
```

**LED 状态**:
- LED0: 1Hz 心跳 (500ms ON/OFF)
- LED1: 闹钟指示 (响铃时 400ms 闪烁, 闹钟设置时 常亮)
- LED2: 编辑模式 (亮)
- LED3: UART 活动 (100ms 闪烁)
- LED4-7: 默认灭 (LED takeover 时由上位机控制)
- 夜间模式: 仅 LED0 亮

### 5.14 button_logic.c/h — 按键逻辑

**公开 API**: `void ProcessButtonEvents(void)`, `void HandleModeTimeout(void)`

**按键映射**:

| 物理按键 | I/O | 短按功能 | 长按功能 |
|---------|-----|---------|---------|
| K1 (FUNC) | TCA6424 P0.0 | 响铃时停止; 否则循环编辑模式 | 保存并退出 |
| K2 (SHIFT) | P0.1 | 切换编辑字段 | - |
| K3 (ADD) | P0.2 | 递增当前字段 | 连发递增(200ms) |
| K4 (SAVE) | P0.3 | 保存当前设置并退出 | - |
| K5 (DISP) | P0.4 | 循环显示内容(TIME/DATE/YEAR/FLOW) | - |
| K6 (SPEED) | P0.5 | 切换移速(慢/快) | - |
| K7 (FORMAT) | P0.6 | 切换方向(LEFT/RIGHT) | - |
| K8 (EXT) | P0.7 | 发送 *EVT:KEY EXT | - |
| USER1 | PJ0 | 发送 *EVT:KEY USER1 | - |
| USER2 | PJ1 | 发送 *EVT:KEY USER2 | - |

**去抖算法** (在 SysTick_Handler 中): 双向计数器，按下时 debounce++，释放时 debounce--。debounce==20 确认按下，debounce==0 确认释放。

**编辑状态机**:
```
MODE_FLOWING → [K1 short] → MODE_DATE_SET → [K1 short] → MODE_TIME_SET
                                  |                              |
                                  v                              v
                           [K1 long] SAVE                [K1 long] SAVE
                                  |                              |
                                  v                              v
MODE_ALARM_SET → [K1 short] → MODE_FLOWING (退出)
     |
     v
[K1 long] SAVE

所有编辑模式: 5s 超时 → 回滚 → MODE_FLOWING
```

### 5.15 command.c/h — UART 命令解析器

**公开 API**: `void ProcessUartCommand(void)`, `bool compareTokens(...)`

**解析流程**:
```
cmd_state == 1 ?
    |
    v
ParseUartInput(): 空格分隔 Token 化, 最多 10 个 Token, 每个最多 15 字符
    |
    v
matchCommand(): 不区分大小写, 最小匹配长度支持
    |
    v
执行命令 → UART 输出响应 → 清空 rx_buf
```

### 5.16 boot_sequence.c/h — 启动动画

**公开 API**: `void RunInitializationSequence(void)`

6 步启动动画，每步 900ms，由 flag_900ms 驱动。

### 5.17 melody.c/h — 旋律播放器

**公开 API**: `void Melody_Start(void)`, `void Melody_Update(void)`, `bool Melody_IsPlaying(void)`

非阻塞播放 "See You Again"，约 380 个音符，文件作用域静态变量保存播放状态。

### 5.18 app.c/h — 应用编排

**公开 API**: `void App_Init(void)`, `void App_RunOnce(void)`

**主循环流程**:
```
App_RunOnce():
    |
    +-- 1. Melody_Update()                    // 旋律推进 (非阻塞)
    +-- 2. HandleAlarm()                      // 闹钟检测与蜂鸣
    |
    +-- 3. if (init_flag)                     // 正在启动动画?
    |       RunInitializationSequence()        //   执行动画步骤
    |
    +-- 4. else:                              // 正常运行时:
    |       ProcessButtonEvents()              //   消费按键事件
    |       if (mode_timeout_flag)             //
    |           HandleModeTimeout()            //   回滚未保存更改
    |       if (flag_1s)                       //   每秒:
    |           UpdateTimeAndDisplayBuffers()  //     更新缓冲区 + RTC 存储
    |       if (flag_2ms)                      //   每 2ms:
    |           Update7SegmentDisplay()        //     扫描一个数码管位置
    |       if (shifting/msg_active)           //   如果需要在 300/500ms:
    |           UpdateDisplayShift()           //     更新流动移位/消息滚动
    |       Display_UpdateStatusLeds()         //   更新 LED 状态
    |
    +-- 5. ProcessUartCommand()               // 处理 UART 命令 (每次迭代)
```

**调用频率**: 主循环无阻塞；数码管扫描 500Hz (每 2ms 一位, 62.5Hz 全屏刷新)；显示缓冲区更新 1Hz。

### 5.19 callback.c/h — 中断回调

**公开 API** (由启动代码 [WEAK] 符号覆盖):
```c
void UART0_Handler(void);      // UART0 RX 中断
void SysTick_Handler(void);    // SysTick 1ms 定时中断
void TIMER0A_Handler(void);    // Timer0A 步进电机中断
```

**SysTick_Handler** (约 200 行, 最重 ISR):
1. tick++ (1ms 自增)
2. 更新 flag_2ms, flag_500ms, flag_300ms, flag_900ms
3. 每秒: flag_1s=true, ss++, 进位逻辑
4. 保存闪烁倒计时
5. 按键去抖检测 (8 矩阵+2 独立)
6. 模式超时检测: 5s 无操作 → mode_timeout_flag

### 5.20 main.c — 程序入口

```c
int main(void)
{
    DevicesInit();
    App_Init();
    while (true) { App_RunOnce(); }
}
```

极简设计: 14 行。

---

## 6. 中断体系 (Interrupt Architecture)

### 6.1 使用的 ISR

| 中断 | 向量号 | 处理函数 | 触发频率 | 工作内容 |
|------|--------|---------|---------|---------|
| SysTick | 15 | SysTick_Handler | 1000 Hz | tick++, 时基标志, 时钟进位, 按键去抖, 超时检测 |
| UART0 | 21 | UART0_Handler | 按需 (RX FIFO ≥ 7/8) | 字符接收, 命令检测 |
| Timer0A | 35 | TIMER0A_Handler | ~293 Hz | 步进电机拍序列输出 |

NVIC 硬件优先级: SysTick(15) > UART0(21) > Timer0A(35)。

### 6.2 设计原则

ISR 只做最少的工作 (标志、缓冲)，重量级处理 (命令解析、显示刷新) 在主循环完成。tick 为 uint32_t，约 49.7 天回绕，代码使用 `(current - past) < threshold` 安全处理。

---

## 7. UART 协议 (UART Protocol)

### 7.1 物理层

UART0, 115200 baud, 8-N-1, `\r` 或 `\n` 行结束。ASCII 文本协议。大小写不敏感，空白容忍，支持最小匹配缩写。

### 7.2 命令总表

| 命令 | 参数 | 说明 |
|------|------|------|
| `*RST` | (无) | 复位协议状态 |
| `*SET:DATE` | `YEAR MONTH DATE yyyy mm dd` 或子集 | 设置日期 |
| `*SET:TIME` | `HOUR MINUTE SECOND hh mm ss` 或子集 | 设置时间 |
| `*SET:ALARM` | 同上格式 | 设置闹钟 |
| `*SET:DISPLAY` | `ON` / `OFF` | 数码管开关 |
| `*SET:FORMAT` | `LEFT` / `RIGHT` | 显示方向 |
| `*SET:MSG` | `<text>` (max 32 字节) | 临时消息 |
| `*SET:LED` | `<hex2>` (00=恢复) | LED 接管 |
| `*SET:MODE` | `NIGHT` / `NORMAL` | 夜间模式 |
| `*GET:DATE` | `[YEAR] [MONTH] [DATE]` | 查询日期 |
| `*GET:TIME` | `[...]` | 查询时间 |
| `*GET:ALARM` | (无) | 查询闹钟 |
| `*GET:DISPLAY` | (无) | 查询显示状态 |
| `*GET:FORMAT` | (无) | 查询格式 |
| `*MOTOR:START/STOP/FWD/REV` | (无) | 电机控制 |
| `*GET:MOTOR` | (无) | 查询电机状态 |
| `HELP` | (无) | 显示帮助 |
| `INIT` | (无) | 系统复位 |

### 7.3 事件发射

| 事件 | 格式 | 时机 |
|------|------|------|
| 按键 | `*EVT:KEY USER1/USER2/EXT` | 按键短按 |
| 显示 | `*EVT:DISP <8chars> <dpHex>` | 每秒+格式/开关切换 |

FORMAT RIGHT 时: 8 字符顺序反转，DP 位图在 ReverseDpBitmap() 中反转 (位N → 位6-N)。

---

## 8. 数据流图 (Data Flow Diagrams)

### 8.1 时间更新流

```
Hibernate RTC (32.768kHz)
  |
  v
[启动] HibernateRTCGet() → RestoreRtcOrDefaultTime()
  past_sec = current - fetched[3] → 逐秒回放
  |
  v
clock.hh/mm/ss
  |
  +-- [SysTick 每秒] ss++ → 进位 (callback.c)
  |
  +-- [flag_1s 主循环] UpdateTimeAndDisplayBuffers() (display.c):
  |     time_buf, alarm_buf, date_buf, master_buf 格式化
  |     HibernateDataSet(stored)  → 保存到 RTC
  |     Display_SendEvent()      → *EVT:DISP
  |
  +-- [flag_2ms 主循环] Update7SegmentDisplay() (display.c):
        I2C → TCA6424 段码+位选
```

### 8.2 UART 命令流

```
PC → UART0 RX → UART0_Handler (ISR): rx_buf[] + cmd_state=1
  → ProcessUartCommand() (主循环):
     ParseUartInput() → tokens[]
     matchCommand() → 匹配命令
     execute → 修改 g 状态 → UART TX 响应
```

### 8.3 按键事件流

```
TCA6424/PJ0/PJ1 → SysTick_Handler (每 1ms 去抖):
  state/short_evt/long_start_evt/repeat_evt 标志
  → ProcessButtonEvents() (主循环):
     HandleButtonShortPress/LongPress → 状态变更
     → display 更新 + UART 响应
```

### 8.4 闹钟触发流

```
SysTick_Handler: ss++ (每秒)
  → HandleAlarm() (主循环每次迭代):
     时间匹配? → PWMStart(500Hz) → alarm_ringing=true
     蜂鸣节奏: 200ms ON / 200ms OFF
     10s 或 FUNC → StopAlarmRinging()
```

---

## 9. 关键设计模式与约束 (Design Patterns & Constraints)

### 9.1 全局结构体模式

所有运行时状态集中在 `app_state_t g`。使用子结构体分组。优点: 聚合便于初始化和调试。

### 9.2 模块分层设计

app → base → driverlib，严格单向依赖。callback.c 是唯一跨越分层的地方。

### 9.3 无动态内存分配

不使用 malloc/free。堆大小 = 0。所有缓冲区为静态分配。

### 9.4 中断-主循环通信

ISR 只设标志/填缓冲区。主循环消费标志并执行处理。

### 9.5 非阻塞发送

UARTStringPutNOBlocking() 逐字符非阻塞。TX FIFO 满时丢弃本次，下次迭代继续。

### 9.6 编辑-提交-回滚模式

进入编辑: 当前值 → original_* (备份), → temp_* (工作副本)。保存: temp_* → 实际字段。超时/退出: original_* → 实际字段 (回滚)。

### 9.7 [WEAK] ISR 覆盖

启动代码所有 ISR 为 [WEAK]。用户在 callback.c 定义同名函数即可覆盖，无需注册。

### 9.8 安全注意事项

- PF0 默认 NMI 锁定，需 GPIO_LOCK_KEY 解锁
- 日期变更自动调整非法日期 (如 1月31→2月 自动变 2月28/29)
- 闹钟使用哨兵值 25 表示未设置 (int8_t)
- volatile uint32_t 用于 ISR/主循环共享，`(int32_t)(until-tick)>0` 安全处理回绕
