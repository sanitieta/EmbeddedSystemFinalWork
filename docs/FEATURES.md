# 自主功能与难点攻克

## 1. 开机旋律 — "See You Again"

### 动机

原项目仅要求在开机动画中显示学号和姓名。为增强用户体验，决定在启动序列结束时播放一段钢琴旋律，利用已有 PWM 蜂鸣器硬件，无需额外电路。

### 设计

- **非阻塞状态机:** 旋律播放不阻塞主循环，与闹钟蜂鸣、显示刷新共存
- **文件作用域静态变量:** `s_active`, `s_note_index`, `s_note_start` 不污染全局结构体 `g`
- **音符结构:** `{freq_hz, duration_ms}`, freq=0 表示休止符 (PWMStop)
- **Staccato 衔接:** 每个旋律音符后插入 23ms 静音间隙
- **频率范围:** 587-1175 Hz (D5-D6), 共 68 个音符 (34 旋律 + 34 休止)

### 实现

```c
// melody.c — 非阻塞更新, 每次主循环迭代调用
void Melody_Update(void) {
    if (!s_active) return;
    const note_t *note = &see_you_again[s_note_index];
    if ((g.timer.tick - s_note_start) < note->duration_ms) return;

    s_note_index++;
    if (s_note_index >= MELODY_NUM_NOTES) { PWMStop(); s_active = false; return; }

    note = &see_you_again[s_note_index];
    s_note_start = g.timer.tick;
    if (note->freq_hz == 0) PWMStop(); else PWMStart(note->freq_hz);
}

// 启动: 开机动画结束后调用 Melody_Start()
void Melody_Start(void) {
    s_active = true; s_note_index = 0;
    s_note_start = g.timer.tick;
    PWMStart(see_you_again[0].freq_hz);
}
```

### 演示

1. MCU 上电 → 启动动画 (全亮→全灭→524031910772→XUHAORAN→v1.0)
2. 动画结束 → 蜂鸣器开始播放 "See You Again" 钢琴副歌
3. 旋律播放期间时钟正常走时，按键/命令正常响应
4. 约 15 秒后旋律自动结束，蜂鸣器静音
5. 不干扰闹钟: 闹钟触发时 `HandleAlarm()` 独立调用 `PWMStart/Stop`

---

## 2. 步进电机 — 28BYJ-48

### 动机

S800 开发板配有 28BYJ-48 步进电机接口 (PF0-PF3)，但课程未要求使用。作为附加硬件展示，添加了恒速旋转驱动和 UART 远程控制功能。

### 设计

- **8 拍半步驱动:** 最大扭矩, 4096 拍/圈
  ```
  stepper_sequence[8] = {0x01,0x03,0x02,0x06,0x04,0x0C,0x08,0x09}
  ```
- **Timer0A 中断驱动:** 非阻塞, 不占用主循环时间
  - 频率计算: `(20MHz × 60) / (4096 × 1) - 1 ≈ 292,967` → ~68 Hz 中断
  - 精确 1 RPM
- **PF0 解锁:** TM4C1294 将 PF0 默认锁定为 NMI, 需 `GPIO_LOCK_KEY` 解锁后配置为 GPIO
- **UART 远程控制:** `*MOTOR:START/STOP/FWD/REV` + `*GET:MOTOR`

### 实现

```c
// stepper.c — Timer0A ISR 驱动一拍
void StepperTimerTick(void) {
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    if (!g.motor.running) return;
    uint8_t output = stepper_sequence[g.motor.position & 0x07];
    GPIOPinWrite(GPIO_PORTF_BASE, STEPPER_PINS, output);
    if (g.motor.direction == 0) {
        g.motor.position++;
        if (g.motor.position >= 4096) g.motor.position = 0;
    } else {
        if (g.motor.position == 0) g.motor.position = 4095;
        else g.motor.position--;
    }
}

// PF0 解锁 (StepperGPIOInit)
HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
HWREG(GPIO_PORTF_BASE + GPIO_O_CR) |= GPIO_PIN_0;
GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, STEPPER_PINS);
```

### 演示

1. MCU 上电 → 步进电机以 1 RPM 反向恒速旋转
2. PC 上位机控制面板 → `START/STOP/FWD/REV` 按钮 → 实时启停和换向
3. `*GET:MOTOR` → `OK 1 0` (运行中, 正转) 或 `OK 0 1` (停止, 反转)
4. 电机旋转时不阻塞显示刷新和命令处理 (Timer0A 中断独立运行)

---

## 3. 难点问题攻克

| # | 问题 | 根因 | 修复 | Commit |
|---|------|------|------|--------|
| 1 | **ARMCC5 嵌套结构体不零初始化** — 开机后 `blinking`/`night_mode`/`shift_speed` 等字段为随机值, 导致显示逆序、闪烁异常 | ARMCC5 对未显式赋值的嵌套 struct 字段不执行 C99 零初始化 | 所有 bool/uint8_t 字段在 `app_state.c` 中显式 `=false/=0`; `FinishBootSequence` 运行时二次确认 | `c370656` `fca650f` `ffe3c38` |
| 2 | **ARMCC5 O1+ 优化破坏栈数组** — `Update7SegmentDisplay()` 中 `uint8_t pat[8]` 被循环展开为寄存器操作, 值损坏导致显示乱码 | ARMCC5 O1+ 对局部数组的寄存器分配策略在大量分支中不稳定 | 用 ~50 个显式 if-else 分支替代循环+数组, 每个 `display_cnt` 位置独立赋值 | — |
| 3 | **多命令 ISR `\0` 覆盖尾字符** — 追加路径用 `++rx_len` 预增, `\r\n` 处理器在 `rx_buf[rx_len]` 处无条件放 `\0`, 覆盖第二条命令最后一字节 | 正常路径 (post-increment) rx_len 指向尾字符之后; 追加路径 (pre-increment) rx_len 指向尾字符本身 | `\r\n` 处理器区分 `cmd_state`: 首命令直接放 `\0`, 追加命令先 `++rx_len` 再放 | `92fba7c` |
| 4 | **`*EVT:DISP` 空格破坏解析** — 消息含空格时 `args.split()` 错误拆分 8 字符 payload, 数字孪生只收到 3 字符 | 协议使用空格分隔 8chars 和 dpHex, 但 chars 内可能含空格 (消息/闹钟显示) | `protocol.py` 改用固定位置提取: `args[:8]` + `args[9:11]` | `1f559e3` `3e8ef90` |
| 5 | **ReverseDpBitmap bit 7 丢失** — FORMAT RIGHT 时 DP 位图镜像 bit 7 被丢弃 | 循环 `for(i=0;i<8;i++)` 中 `1<<i` → `1<<(6-i)` 未覆盖 i=7 的情况 | bit 7 → bit 0 单独处理 | `eb5fae1` |
| 6 | **`OK ERROR PARAM` 畸形响应** — `*GET:DATE YEAR` 等子字段查询先发 `OK ` 前缀再校验字段, 非法字段时出现 `OK ERROR PARAM` | 逻辑顺序错误: 应先确认至少一个合法字段再发前缀 | 预扫描所有字段 Token, 确认合法后才发 `OK ` | `1f0e955` |
| 7 | **PF0 NMI 锁定** — 步进电机初始化时 PF0 无法配置为 GPIO 输出 | TM4C1294 将 PF0 默认锁定为 NMI 功能 | 写入 `GPIO_LOCK_KEY` 解锁 + `GPIO_CR` 提交变更 | `5ee4de2` |
| 8 | **开机动画期间误拦截 `*GET:*`** — PC 连接时下发 `*GET:FORMAT` 被 `ERROR BUSY` 拒绝 | 开机 `init_flag` 期间所有修改类命令被拦截, 但 `*GET:*` 只读且 PC 需要验证在线 | 放行所有 `*GET:*` 前缀命令 + `*PING`/`HELP`/`INIT`/`*RST` | `f1a6a42` |
| 9 | **7 段码 `C` 显示为小写 c** — seg7[12] = 0x58 (段 d,e,g) 呈小写 c 形状 | 0x58 点亮底横杠 + 左下 + 右中, 不包含 a 段 (顶横) 和 f 段 (左上) | seg7[12] 改为 0x39 (段 a,d,e,f), 与 PC 端 `_ASCII_TO_7SEG` 一致 | `cf66fef` |
| 10 | **`*SET:KEY USER1/USER2` 虚拟按键无响应** — MCU 设 `suppress_key_events`, PC 收不到 `*EVT:KEY` → 不触发 NTP/天气 | `suppress_key_events` 对所有虚拟按键生效, 但 USER1/USER2 需要 PC 端 `*EVT:KEY` 事件 | USER1/USER2 处理中移除 `suppress_key_events = true` | `4008613` |

---

## 4. 特色亮点

### 固件设计

- **14 行 `main.c`:** 极简入口, 全部复杂逻辑在模块中
- **无动态内存:** 堆大小 0, 全部缓冲区静态分配
- **非阻塞全链路:** 旋律/闹钟/显示/命令均在主循环中协作, 无阻塞等待
- **编辑-提交-回滚模式:** 进入编辑备份 `original_*` → 修改 `temp_*` → 保存写入 / 超时回滚
- **秒级精确 carry:** 时钟进位在 SysTick ISR 中 inline 完成 (非函数调用), 含闰年感知的二月处理
- **7SEG 字形反向映射:** `CharForSegment()` 将流动显示的 7 段码逆映射为 ASCII, 供 `*EVT:DISP` 上报
- **LED 变化检测:** `current_led`/`last_sent_led` 双缓冲, 仅变化时上报 `*EVT:LED`, 减少 UART 流量
- **`*PING` 手动 itoa:** 避免链接 `sprintf`, 节省代码空间

### 上位机设计

- **数字孪生两遍渲染:** Glow Pass (半透明光晕) + Crisp Pass (实心填充), 模拟真空荧光管效果
- **`protocol.py` 零 Qt 依赖:** 纯 Python, 可命令行独立测试
- **双源天气:** Open-Meteo (结构化 WMO 码) → wttr.in (文本关键词) → 2h 缓存, 代理→直连自动回退
- **智能心跳:** 收到**任何** MCU 数据刷新存活时间戳 (不限于 `*PONG`), 消除误断连
- **命令-响应匹配队列:** `deque` 记录已发命令, `OK`/`ERROR` 到达时出队匹配
- **5 层卡片暗色主题:** 画布→面板→卡片→抬升面→Mint 强调, 语义色区分 TX/RX/事件/错误
- **缩写+大小写演示按钮:** `*SET:DAT YEA MON 2025 6 15` / `*SeT:DaTe ...`

### 协议设计

- **最小匹配缩写:** `CountMandatory()` 计算命令中 `*` 和大写字母数作为最小匹配长度
- **多命令缓冲:** ISR 用 `\0` 分隔连续到达的命令, `ParseUartInput` 遇 `\0` 停止, 尾部处理 memmove 移位
- **FORMAT RIGHT 全链路镜像:** 硬件扫描 (display_cnt 反转) + 协议输出 (Display_FormatBufferForProtocol 反转) + DP 位图 (ReverseDpBitmap) 三重镜像
- **错误分类:** `SYNTAX`/`PARAM`/`RANGE`/`BUSY` 四级错误, 精确区分语法/参数/范围/忙碌

---

## 5. 提交时间线 (代表性)

| Commit | 描述 |
|--------|------|
| `17ae999` | 初始提交 |
| `5ee4de2` | 新增步进电机驱动 |
| `65a8f52` | 模块化重构: 拆分 main.c 为分层模块 |
| `a917cea` | 修复 SegmentForChar 'C' 和 '-' |
| `c370656` | ARMCC5 shift 显式初始化 |
| `f1a6a42` | 开机期间放行 `*GET:*` |
| `2bae2ce` | 加入 NTP 对时功能 |
| `e3a257b` | LED 接管完全控制 8 LED |
| `3ee55e5` | 多命令连续接收修复 |
| `ffe3c38` | FinishBootSequence 显式初始化所有显示状态 |
| `92fba7c` | 修复 ISR `\r\n` 覆盖追加命令尾字符 |
| `21c9a04` | 新增 `*SET:WEATHER` 天气 LED 命令 |
| `4008613` | 允许 USER1/USER2 虚拟按键触发 NTP/天气 |
| `3518c81` | 新增 `*SET:BEEP` 远程蜂鸣 |
| `f96955d` | 新增 `*SET:ALARM OFF` 关闭闹钟 |
| `cf66fef` | 7 段码 'C' 0x58→0x39 (大写) |
| `7aaedd3` | 重构文件结构 (mcu/ / pc_host/ 分离) |
