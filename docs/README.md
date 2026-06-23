# S800 智能互联时钟 — 项目文档

> TM4C1294NCPDT 固件 + PyQt5 PC 上位机  
> 学号 524031910772 · 姓名 XUHAORAN · 版本 v1.0

---

## 项目简介

S800 智能互联时钟是一个嵌入式系统项目，基于 **TM4C1294NCPDT** (Cortex-M4, 20MHz) 微控制器。固件使用 **ARM Compiler 5 / Keil MDK** 构建 (~38 KB Flash)，PC 上位机使用 **Python 3.11 / PyQt5 / pyserial** 开发 (~3000 行 Python + ~620 行 QSS)。两者通过 **UART 115200-8-N-1** 以 ASCII 文本协议通信。

核心功能：7 段数码管动态扫描时钟、闹钟、按键编辑状态机、UART 远程命令、临时消息、LED 状态指示、NTP 网络对时、天气显示。两项自主扩展：**开机旋律** (See You Again, 非阻塞 PWM) 和**步进电机** (28BYJ-48, 8 拍半步, 1 RPM)。

---

## 硬件平台

| 组件 | 型号/资源 | 连接 |
|------|----------|------|
| MCU | TM4C1294NCPDT, 20MHz | — |
| 数码管驱动 | TCA6424 (I2C 0x22) | I2C0: PB2(SCL)/PB3(SDA) |
| LED 驱动 | PCA9557 (I2C 0x18) | I2C0 |
| 矩阵按键 K1-K8 | TCA6424 PORT0 输入 | I2C0 |
| 独立按键 USER1/USER2 | PJ0/PJ1 | GPIO 上拉 |
| 蜂鸣器 | 无源, PK5 (M0PWM7) | PWM0 Gen3 |
| 步进电机 | 28BYJ-48, 4 相, PF0-PF3 | Timer0A |
| 休眠 RTC | Hibernate 模块 | 内部 32kHz 振荡器 |
| UART | PA0(RX)/PA1(TX) | UART0 |

---

## 快速导航

| 文档 | 内容 |
|------|------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | 系统架构 — 分层设计、主循环/中断体系、UART 协议、LED 系统、PC 上位机 |
| [FEATURES.md](FEATURES.md) | 自主功能与难点 — 开机旋律、步进电机、难点攻克表、特色亮点 |

---

## 构建与运行

```powershell
# MCU 固件 (Keil MDK-ARM v5)
UV4.exe -r main.uvprojx -t "Target 1" -o build.log

# PC 上位机
cd pc_host && pip install -r requirements.txt && python main.py
```

---

## 功能一览

**基础功能:** 7 段数码管 8 位动态扫描 (500Hz) · 日期/时间/闹钟 · 流动显示 (快/慢, LEFT/RIGHT) · 编辑模式 (5s 超时回滚) · 按键去抖 (20ms) / 长按 (800ms) · 启动动画 · 夜间模式

**扩展功能:** NTP 网络对时 (aliyun/pool 双源) · 天气显示 (Open-Meteo + wttr.in, 2h 缓存) · 自动昼夜 (astral 日出日落) · 天气 LED 编码 (3-bit, 7 种天气) · 远程蜂鸣 (`*SET:BEEP`) · 数据看板 (CSV + matplotlib)

**自主扩展:** 开机旋律 "See You Again" (68 音符, 非阻塞 PWM) · 步进电机 28BYJ-48 (8 拍半步, Timer0A, 1 RPM, UART 控制)

**PC 上位机:** 数字孪生 (7SEG 发光渲染 + LED 渐变 + 虚拟按键) · 控制面板 · 实时日志终端 · Obsidian/Mint 暗色主题

---

## 仓库结构

```
Project/
├── mcu/                         # MCU 固件
│   ├── User/                    #   用户代码
│   │   ├── main.c               #     入口 (14 行)
│   │   ├── callback.c           #     ISR (SysTick/UART0/Timer0A)
│   │   ├── app/                 #     应用层 (10 模块)
│   │   └── base/                #     硬件抽象层 (8 模块)
│   ├── driverlib/ / inc/ / RTE/ # TivaWare + CMSIS
│   └── main.uvprojx             # Keil MDK 工程
├── pc_host/                     # PC 上位机
│   ├── main.py                  #   入口 + 信号协调
│   ├── protocol.py / serial_worker.py
│   ├── twin_panel.py / control_panel.py
│   ├── log_panel.py / theme.qss
│   ├── ntp_helper.py / weather_helper.py
│   └── auto_daynight.py / dashboard.py
└── docs/                        # 项目文档
```
