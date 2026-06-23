# S800 智能互联时钟 — 编译与运行说明

> 学号 524031910772 · 姓名 许浩然 · 版本 v1.0

---

## 一、项目结构

```
Project/
├── mcu/                         # MCU 固件
│   ├── User/main.c              #   单一源文件 (4718 行, 19 模块拼装)
│   ├── driverlib/               #   TivaWare 外设驱动库
│   ├── inc/                     #   硬件寄存器映射头文件
│   ├── Objects/                 #   编译产物
│   ├── RTE/                     #   CMSIS 启动代码 (startup + SystemInit)
│   ├── main.uvprojx             #   Keil MDK 工程文件
│   └── main.uvoptx              #   Keil 工程选项
├── pc_host/                     # PC 上位机
│   ├── main.py                  #   入口 + 信号/槽总协调
│   ├── protocol.py              #   协议解析/构建 (零 Qt 依赖)
│   ├── serial_worker.py         #   后台串口 QThread
│   ├── twin_panel.py            #   数字孪生面板
│   ├── control_panel.py         #   命令控制面板
│   ├── log_panel.py             #   日志终端
│   ├── ntp_helper.py            #   NTP 对时扩展
│   ├── weather_helper.py        #   天气获取扩展
│   ├── auto_daynight.py         #   自动昼夜扩展
│   ├── dashboard.py             #   数据看板扩展
│   ├── theme.qss                #   Obsidian/Mint 暗色主题
│   └── requirements.txt         #   Python 依赖
├── docs/
│    └── 大作业524031910772-许浩然.md        #   项目简介
├── README.md                    #   本文件
```

---

## 二、MCU 固件编译与烧录

### 2.1 编译环境

| 组件       | 版本/说明                                      |
|----------|--------------------------------------------|
| IDE      | Keil MDK-ARM v5                            |
| 编译器      | ARM Compiler 5 (V5.06 update 5 build 528)  |
| 目标芯片     | TM4C1294NCPDT                              |
| 系统时钟     | 20 MHz (PLL from 25 MHz XTAL, VCO 480 MHz) |
| 优化等级     | O1 (必须 O1+, 否则 7SEG 显示异常)                  |
| Flash 占用 | ~38 KB / 1024 KB                           |

### 2.2 命令行编译

```powershell
# 在 mcu/ 目录下执行
UV4.exe -r main.uvprojx -t "Target 1" -o build.log

# 查看编译结果
type build.log
```

### 2.3 Keil IDE 编译

1. 打开 `mcu/main.uvprojx`
2. 选择 Target: `Target 1`
3. 点击 Build (F7) 或 Rebuild
4. 确认 0 Error(s), 0 Warning(s)

### 2.4 烧录到开发板

1. 通过 USB 调试器连接 S800 开发板
2. 在 Keil IDE 中点击 Download (F8)
3. 或使用 J-Link / Stellaris ICDI 命令行工具

### 2.5 串口参数

| 参数  | 值                   |
|-----|---------------------|
| 波特率 | 115200              |
| 数据位 | 8                   |
| 停止位 | 1                   |
| 校验位 | None                |
| 引脚  | PA0 (RX) / PA1 (TX) |

---

## 三、PC 上位机运行

### 3.1 环境要求

| 组件         | 版本                |
|------------|-------------------|
| Python     | 3.11+             |
| PyQt5      | 5.15+             |
| pyserial   | 3.5+              |
| matplotlib | 3.10+ (可选, 数据看板)  |
| ntplib     | 0.4+ (可选, NTP 对时) |
| requests   | 2.31+ (可选, 天气)    |
| astral     | 3.2+ (可选, 自动昼夜)   |

### 3.2 安装与启动

```powershell
cd pc_host
pip install -r requirements.txt
python main.py
```

### 3.3 操作流程

1. **连接设备:** 在上位机工具栏选择 MCU 对应的 COM 口，点击"连接"
2. **验证连接:** 连接后状态栏显示 `ONLINE`，日志中出现 `*PONG` 响应
3. **数字孪生:** 左侧面板实时显示 MCU 的 7 段数码管和 LED 状态
4. **命令下发:** 右侧控制面板提供完整的命令可视化界面
5. **NTP 对时:** 点击工具栏 `NTP 对时` 或按下 USER1 虚拟按键
6. **天气查询:** 点击工具栏 `天气` 或按下 USER2 虚拟按键

### 3.4 上位机面板

| 面板                  | 功能                                          |
|---------------------|---------------------------------------------|
| 数字孪生 (TwinPanel)    | 7SEG 发光渲染 + LED 渐变 + 虚拟按键 K1-K8/USER1/USER2 |
| 控制面板 (ControlPanel) | 日期/时间/闹钟/显示/格式/消息/LED/蜂鸣/电机/系统命令            |
| 日志终端 (LogPanel)     | 实时 TX(蓝)/RX(青)/事件(紫)/错误(红) 彩色日志             |
| NTP 对时              | aliyun + pool 双源, daemon 线程                 |
| 天气显示                | Open-Meteo + wttr.in 双源备用, 2h 缓存, LED5-7 编码 |
| 自动昼夜                | astral 日出日落, 自动 SET:MODE DAY/NIGHT          |
| 数据看板                | CSV 持久化 + matplotlib 图表                     |

---

## 四、UART 协议快速参考

### 响应格式

- 成功: `OK` 或 `OK <value>` (以 `\r\n` 结尾)
- 失败: `ERROR SYNTAX`, `ERROR PARAM`, `ERROR RANGE`, `ERROR BUSY`

### 常用命令示例

```text
*PING                                    → *PONG 1234
*SET:DATE YEAR MONTH DATE 2025 6 15     → OK
*SET:TIME HOUR MINUTE SECOND 12 30 0    → OK
*SET:ALARM HOUR MINUTE SECOND 7 0 0     → OK
*SET:ALARM OFF                           → OK
*SET:DISPLAY ON                          → OK
*SET:FORMAT LEFT                         → OK
*SET:MSG HELLO                           → OK
*SET:LED FF                              → OK
*SET:WEATHER 80                          → OK
*SET:BEEP 500                            → OK
*SET:MODE NIGHT                          → OK
*SET:KEY USER1                           → OK
*GET:DATE                                → OK 2025.06.15
*GET:TIME                                → OK 12.30.45
*GET:ALARM                               → OK 07.00.00
*MOTOR:START                             → OK
*GET:MOTOR                               → OK 1 0
*RST                                     → OK
*NTP SYNC                                → OK
INIT                                     → (系统复位)
HELP                                     → OK + 命令列表
```

### MCU 主动上报事件

```text
*EVT:DISP _12.30.45 0A       (显示变化 + 1Hz 心跳)
*EVT:LED 01                  (LED 变化 + 1Hz 心跳)
*EVT:KEY USER1               (物理按键)
*EVT:MODE FLOWING            (模式切换)
*EVT:ALARM / *EVT:ALARM OFF  (闹钟)
*EVT:EDIT DATE 2025.06.15    (设置保存)
```

### 协议特性

- **大小写不敏感:** `*set:date` = `*SET:DATE`
- **最小匹配缩写:** `MINUTE` 可匹配 `MIN`
- **空格/Tab 容忍**
- **FORMAT RIGHT:** 内容 + DP 镜像

---

## 五、LED 状态

| LED       | 含义                      |
|-----------|-------------------------|
| LED0 (心跳) | 1Hz 闪烁 — MCU 正常运行       |
| LED1      | 常亮=闹钟已设, 闪烁=正在响铃        |
| LED2      | 亮=编辑模式                  |
| LED3      | 闪=UART 有数据              |
| LED4      | 亮=NTP 对时完成              |
| LED5-7    | 天气编码 (晴/多云/雾/雨/雪/雷暴/未知) |

---

## 六、常见问题

**Q: Keil 编译报错?**
> ARM Compiler 5 对 C99 支持有限, 检查嵌套结构体字段是否全部显式初始化，编译器版本v5.06。

**Q: 数码管显示乱码或闪烁?**
> 优化等级必须为 O1+, ARMCC5 O1+ 下栈数组不稳定, 已用 if-else 链规避。

**Q: PC 上位机显示 OFFLINE?**
> 确认 MCU 已烧录最新固件且 COM 口未被占用, 等待 5 秒宽限期。

**Q: NTP 对时失败?**
> 确认网络可达 ntp.aliyun.com, ntplib 已安装, 防火墙未拦截 UDP 123。

**Q: 天气获取失败?**
> 上位机自动代理→直连回退, 检查日志错误链; 2 小时缓存备用。

**Q: USER1/USER2 虚拟按键无效?**
> 确认已连接 — 虚拟按键通过 `*SET:KEY` 下发, MCU 回报 `*EVT:KEY` 后触发。
