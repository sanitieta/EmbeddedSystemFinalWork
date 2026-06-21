#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>
#include "board_config.h"

typedef enum // 系统模式枚举
{
    MODE_FLOWING,      // 流动显示模式
    MODE_DATE_SET,     // 日期设置模式
    MODE_TIME_SET,     // 时间设置模式
    MODE_ALARM_SET,    // 闹钟设置模式
    MODE_ALARM_DISPLAY // 闹钟显示模式
} system_mode_t;

typedef enum // 设置字段枚举
{
    FIELD_NONE,         // 无字段
    FIELD_YEAR,         // 年份字段
    FIELD_MONTH,        // 月份字段
    FIELD_DAY,          // 日期字段
    FIELD_HOUR,         // 小时字段
    FIELD_MINUTE,       // 分钟字段
    FIELD_SECOND,       // 秒字段
    FIELD_ALARM_HOUR,   // 闹钟小时字段
    FIELD_ALARM_MINUTE, // 闹钟分钟字段
    FIELD_ALARM_SECOND  // 闹钟秒字段
} setting_field_t;

typedef enum // 正常显示内容枚举
{
    MAIN_DISPLAY_FLOW,  // 流动显示 (日期+时间交替滚动)
    MAIN_DISPLAY_TIME,  // 仅显示时间 HH.MM.SS
    MAIN_DISPLAY_DATE,  // 仅显示日期 YY.MM.DD
    MAIN_DISPLAY_YEAR   // 显示完整年份 YYYY.MMDD
} main_display_t;

typedef struct // 命令Token结构体
{
    uint8_t token_str[MAX_TOKEN_LENGTH]; // Token字符串
    uint8_t token_len;                   // Token长度
} command_token_t;

/* ── 时钟数据：时间、日期、闹钟的当前值 / 临时编辑值 / 原始回滚值 ── */
typedef struct {
    /* 当前时间 */
    int8_t   hh;           // 当前小时 (0-23)
    int8_t   mm;           // 当前分钟 (0-59)
    int8_t   ss;           // 当前秒   (0-59)
    uint16_t year;         // 当前年份 (2000-2099)
    uint8_t  month;        // 当前月份 (1-12)
    uint8_t  day;          // 当前日期 (1-31)

    /* 闹钟设定 */
    int8_t   alm_hh;       // 闹钟小时，25 表示未设置
    int8_t   alm_mm;       // 闹钟分钟
    int8_t   alm_ss;       // 闹钟秒
    bool     alarm_enabled; // alm_hh != 25 时有效 (方便快速判断)

    /* 编辑模式临时值 —— 用户在编辑模式下修改，保存前不覆盖实际值 */
    uint16_t temp_year;    // 临时年份
    uint8_t  temp_month;   // 临时月份
    uint8_t  temp_day;     // 临时日期
    uint8_t  temp_hh;      // 临时小时
    uint8_t  temp_mm;      // 临时分钟
    uint8_t  temp_ss;      // 临时秒
    uint8_t  temp_alm_hh;  // 临时闹钟小时
    uint8_t  temp_alm_mm;  // 临时闹钟分钟
    uint8_t  temp_alm_ss;  // 临时闹钟秒

    /* 进入编辑模式前的原始值 —— 用于放弃修改或超时回滚 */
    int8_t   original_hh;           // 原始小时
    int8_t   original_mm;           // 原始分钟
    int8_t   original_ss;           // 原始秒
    uint16_t original_year;         // 原始年份
    uint8_t  original_month;        // 原始月份
    uint8_t  original_day;          // 原始日期
    int8_t   original_alm_hh;       // 原始闹钟小时
    int8_t   original_alm_mm;       // 原始闹钟分钟
    int8_t   original_alm_ss;       // 原始闹钟秒
    bool     unsaved_changes_active; // 标志是否存在未保存的更改
} clock_data_t;

/* ── 显示上下文：7-SEG 缓冲区、模式、标志、消息、LED ── */
typedef struct {
    /* 协议传输缓冲区 —— 通过 UART 发送给上位机 */
    uint8_t  time_buf[9];     // 时间显示缓冲区 "HH:MM:SS"
    uint8_t  alarm_buf[9];    // 闹钟显示缓冲区 "HH:MM:SS" 或 "xx:xx:xx"
    uint8_t  date_buf[11];    // 日期显示缓冲区 "YYYY-MM-DD"
    uint8_t  seg7[18];        // 7段数码管字形码 (0-9, A-F, -, 空白)
    uint8_t  master_buf[18];  // 主显示缓冲区，18字节流动显示用

    /* 数码管动态扫描状态 */
    uint8_t  cnt;             // 当前刷新的数码管索引 (0-7)
    int8_t   shift;           // 流动显示移位量 (0-17)
    uint8_t  rightshift;      // 位选控制移位寄存器

    /* 显示模式与方向 */
    bool     shift_mode;      // 移位方向: false=左移, true=右移 (FORMAT)
    bool     shift_speed;     // 移位速度: false=慢(500ms), true=快(300ms)
    bool     shifting;        // 是否正在流动移位
    bool     reversed;        // 显示是否反向 (FORMAT RIGHT)
    bool     on;              // 7段数码管是否开启 (DISPLAY ON/OFF)
    bool     blinking;        // 编辑模式字段闪烁标志

    /* 当前系统模式与编辑状态 */
    system_mode_t   mode;      // 当前系统模式 (流动/日期设置/时间设置/闹钟设置/闹钟显示)
    setting_field_t field;     // 当前编辑字段 (年/月/日/时/分/秒/闹钟字段)
    main_display_t  main_disp; // 正常模式下的显示内容 (流动/时间/日期/完整年份)
    bool     prev_shifting;    // 进入编辑前的移位状态 (用于退出恢复)
    bool     prev_shift_mode;  // 进入编辑前的移位方向 (用于退出恢复)
    bool     prev_shift_speed; // 进入编辑前的移位速度 (用于退出恢复)

    /* 夜间模式与 LED 接管 */
    bool     night_mode;      // 夜间模式激活 (仅显示 HH.MM，静音，关LED)
    bool     led_takeover;    // LED 上位机接管模式
    uint8_t  led_pattern;     // LED 接管时的强制输出模式 (hex2)
    uint8_t  current_led;     // 当前实际输出的 LED 状态 (用于变化检测)
    uint8_t  last_sent_led;   // 上次通过 *EVT:LED 上报的 LED 状态
    volatile uint32_t uart_activity_until; // UART 活动指示 LED 截止 tick (100ms 闪烁)

    /* 闹钟运行时状态 */
    bool     alarm_ringing;   // 闹钟是否正在响铃
    bool     alarm_silenced;  // 当前闹钟匹配秒是否已被手动静音
    uint32_t alarm_ring_start;// 闹钟响铃开始时刻 (tick)
    uint32_t alarm_beep_phase;// 闹钟蜂鸣相位起点 (tick)
    bool     alarm_beep_on;   // 闹钟蜂鸣当前是否开启 (200ms ON/OFF 循环)

    /* 临时消息显示 */
    bool     msg_active;      // 临时消息显示激活
    bool     msg_scroll;      // 临时消息是否需要滚动 (长度 > 8)
    uint8_t  msg_buf[33];     // 临时消息文本缓冲区 (最多 32 字节 + \0)
    uint8_t  msg_len;         // 临时消息长度
    int8_t   msg_shift;       // 临时消息滚动偏移
    uint32_t msg_start;       // 临时消息开始显示时刻 (tick)
    uint32_t msg_last_shift;  // 临时消息上次滚动时刻 (tick)

    /* 保存闪烁 */
    bool     save_blink_active;  // 保存成功闪烁激活标志
    uint32_t save_blink_timer;   // 保存闪烁定时器 (tick)
    bool     long_press_saving;  // 长按保存进行中标志

    /* 启动初始化 */
    bool     init_flag;       // 初始化流程标志 (true=正在启动动画)
    uint8_t  init_step;       // 初始化步骤 (0=全亮, 1=全暗, 2=学号前半...)

    /* I2C 操作结果 */
    uint8_t  i2c_result;      // 最近一次 I2C 操作返回值 (调试用)
} display_ctx_t;

/* ── 系统时基：SysTick 1ms 定时器与分频标志 ── */
typedef struct {
    volatile uint32_t tick;              // 系统滴答计数器 (1ms/tick, 约 49.7 天回绕)
    volatile bool flag_2ms;             // 2ms 时间片标志 (数码管扫描)
    volatile bool flag_500ms;           // 500ms 时间片标志 (慢速流动 + LED 心跳)
    volatile bool flag_300ms;           // 300ms 时间片标志 (快速流动 + 消息滚动)
    volatile bool flag_900ms;           // 900ms 时间片标志 (启动动画帧切换)
    volatile bool flag_1s;              // 1s 时间片标志 (时钟计时 + 缓冲区更新)
    volatile uint32_t mode_timeout;     // 编辑模式超时定时器 (5s 无操作自动退出)
    volatile bool mode_timeout_flag;    // 编辑模式超时标志
} sys_timer_t;

/* ── UART 上下文：接收缓冲区与命令解析状态 ── */
typedef struct {
    uint8_t  rx_buf[100];         // UART 接收缓冲区
    uint8_t  rx_len;              // 当前接收长度
    volatile uint8_t cmd_state;   // 命令状态: 0=空闲, 1=有新命令待处理
    command_token_t tokens[MAX_COMMAND_TOKENS]; // 解析出的 Token 数组
    uint8_t  num_tokens;          // 解析出的 Token 数量
} uart_ctx_t;

/* ── 按键输入状态：8 个矩阵按键 (K1-K8) + 2 个用户按键 (USER1/USER2) ── */
typedef struct {
    /* 矩阵按键 K1-K8 (TCA6424 PORT0 低电平有效) */
    volatile uint32_t debounce[8];       // 按键去抖计数器 (20ms)
    volatile uint32_t press_start[8];    // 按键按下起始时刻 (tick)
    volatile uint32_t long_press[8];     // 按键长按计数器 (800ms 触发)
    volatile bool     state[8];          // 按键当前稳定状态 (去抖后)
    volatile bool     short_evt[8];      // 按键短按事件标志 (主循环消费)
    volatile bool     long_start_evt[8]; // 按键长按开始事件标志
    volatile bool     repeat_evt[8];     // 按键连按事件标志 (K3 ADD 长按>=800ms 后每 200ms)

    /* USER1/USER2 独立按键 (GPIO PJ0/PJ1 低电平有效) */
    volatile uint32_t user_debounce[2];    // USER 按键去抖计数器
    volatile uint32_t user_press_start[2]; // USER 按键按下起始时刻
    volatile bool     user_state[2];       // USER 按键当前稳定状态
    volatile bool     user_short_evt[2];   // USER 按键短按事件标志
    volatile bool     user_gpio_ready;     // USER 按键 GPIO 是否已初始化
} input_state_t;

/* ── 步进电机状态 ── */
typedef struct {
    volatile uint16_t position;  // 当前步进位置 (0-4095, 对应 8 拍 × 512 循环)
    volatile uint8_t  direction; // 旋转方向: 0=正转(forward), 1=反转(reverse)
    volatile uint8_t  running;   // 运行状态: 0=停止, 1=运行
} motor_state_t;

/* ── 休眠 RTC 数据：关机不掉电的时间保持 ── */
typedef struct {
    uint32_t stored[4];       // 存储到 Hibernate 的数据: [hh, mm, ss, RTC_counter]
    uint32_t current;         // 当前 RTC 计数值 (秒)
    uint32_t fetched[4];      // 从 Hibernate 读取的数据
    uint8_t  days_in_month[13]; // 每月天数表: [0]=占位, [1]=31, [2]=28, ..., [12]=31
} rtc_data_t;

/* ── 顶层应用状态 ── */
typedef struct {
    uint32_t       sys_clock_hz; // 系统时钟频率 (Hz), 由 SysCtlClockFreqSet 返回
    clock_data_t   clock;        // 时钟数据 (时间/日期/闹钟)
    display_ctx_t  disp;         // 显示上下文
    sys_timer_t    timer;        // 系统时基
    uart_ctx_t     uart;         // UART 串口状态
    input_state_t  in;           // 按键输入状态
    motor_state_t  motor;        // 步进电机状态
    rtc_data_t     rtc;          // 休眠 RTC 数据
} app_state_t;

#endif
