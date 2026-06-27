/* ===== SUPER-MERGE main.c — 所有 MCU 代码合并 ===== */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
/* 生成日期: Tue Jun 23 13:26:29     2026 */

#include "driverlib/debug.h"
#include "driverlib/gpio.h"
#include "driverlib/hibernate.h"
#include "driverlib/i2c.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/pwm.h"
#include "driverlib/sysctl.h"
#include "driverlib/systick.h"
#include "driverlib/timer.h"
#include "driverlib/uart.h"
#include "inc/hw_gpio.h"
#include "inc/hw_i2c.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"

/* ===== board_config.h ===== */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H


#define SYSTICK_FREQUENCY 1000

#define V_T1ms 1
#define V_T2ms 2
#define V_T20ms 20
#define V_T100ms 100
#define V_T300ms 300
#define V_T500ms 500
#define V_T900ms 900
#define V_T1s 1000
#define V_T2s 2000
#define V_T3s 3000
#define V_T5s 5000
#define V_T10s 10000

#define BLINK_ON_TIME_MS 500
#define BLINK_OFF_TIME_MS 500
#define MODE_TIMEOUT_MS 5000
#define SAVE_BLINK_DURATION_MS 2000

#define TCA6424_I2CADDR 0x22
#define PCA9557_I2CADDR 0x18

#define PCA9557_INPUT 0x00
#define PCA9557_OUTPUT 0x01
#define PCA9557_POLINVERT 0x02
#define PCA9557_CONFIG 0x03

#define TCA6424_CONFIG_PORT0 0x0c
#define TCA6424_CONFIG_PORT1 0x0d
#define TCA6424_CONFIG_PORT2 0x0e
#define TCA6424_INPUT_PORT0 0x00
#define TCA6424_INPUT_PORT1 0x01
#define TCA6424_INPUT_PORT2 0x02
#define TCA6424_OUTPUT_PORT0 0x04
#define TCA6424_OUTPUT_PORT1 0x05
#define TCA6424_OUTPUT_PORT2 0x06

#define MAX_COMMAND_TOKENS 10
#define MAX_TOKEN_LENGTH 15

#define DEBOUNCE_TIME_MS 20
#define LONG_PRESS_TIME_MS 800
#define REPEAT_PRESS_TIME_MS 200

#define UART_ACTIVITY_FLASH_MS 100
#define MESSAGE_STATIC_MS 2500
#define ALARM_BEEP_ON_MS 200
#define ALARM_BEEP_OFF_MS 200
#define ALARM_RING_MAX_MS 10000

#define STEPPER_SYSTEM_CLOCK_HZ      20000000UL
#define STEPPER_BEATS_PER_REVOLUTION 4096UL
#define STEPPER_TARGET_RPM           1UL
#define STEPPER_TIMER_LOAD           ((STEPPER_SYSTEM_CLOCK_HZ * 60UL) / (STEPPER_BEATS_PER_REVOLUTION * STEPPER_TARGET_RPM) - 1)

#define STEPPER_PINS (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3)

#endif

/* ===== app_types.h ===== */


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
    MAIN_DISPLAY_FLOW, // 流动显示 (日期+时间交替滚动)
    MAIN_DISPLAY_TIME, // 仅显示时间 HH.MM.SS
    MAIN_DISPLAY_DATE, // 仅显示日期 YY.MM.DD
    MAIN_DISPLAY_YEAR  // 显示完整年份 YYYY.MMDD
} main_display_t;

typedef struct // 命令Token结构体
{
    uint8_t token_str[MAX_TOKEN_LENGTH]; // Token字符串
    uint8_t token_len;                   // Token长度
} command_token_t;

/* ── 时钟数据：时间、日期、闹钟的当前值 / 临时编辑值 / 原始回滚值 ── */
typedef struct {
    /* 当前时间 */
    int8_t hh;     // 当前小时 (0-23)
    int8_t mm;     // 当前分钟 (0-59)
    int8_t ss;     // 当前秒   (0-59)
    uint16_t year; // 当前年份 (2000-2099)
    uint8_t month; // 当前月份 (1-12)
    uint8_t day;   // 当前日期 (1-31)

    /* 闹钟设定 */
    int8_t alm_hh;      // 闹钟小时，25 表示未设置
    int8_t alm_mm;      // 闹钟分钟
    int8_t alm_ss;      // 闹钟秒
    bool alarm_enabled; // alm_hh != 25 时有效 (方便快速判断)

    /* 编辑模式临时值 —— 用户在编辑模式下修改，保存前不覆盖实际值 */
    uint16_t temp_year;  // 临时年份
    uint8_t temp_month;  // 临时月份
    uint8_t temp_day;    // 临时日期
    uint8_t temp_hh;     // 临时小时
    uint8_t temp_mm;     // 临时分钟
    uint8_t temp_ss;     // 临时秒
    uint8_t temp_alm_hh; // 临时闹钟小时
    uint8_t temp_alm_mm; // 临时闹钟分钟
    uint8_t temp_alm_ss; // 临时闹钟秒

    /* 进入编辑模式前的原始值 —— 用于放弃修改或超时回滚 */
    int8_t original_hh;          // 原始小时
    int8_t original_mm;          // 原始分钟
    int8_t original_ss;          // 原始秒
    uint16_t original_year;      // 原始年份
    uint8_t original_month;      // 原始月份
    uint8_t original_day;        // 原始日期
    int8_t original_alm_hh;      // 原始闹钟小时
    int8_t original_alm_mm;      // 原始闹钟分钟
    int8_t original_alm_ss;      // 原始闹钟秒
    bool unsaved_changes_active; // 标志是否存在未保存的更改
} clock_data_t;

/* ── 显示上下文：7-SEG 缓冲区、模式、标志、消息、LED ── */
typedef struct {
    /* 协议传输缓冲区 —— 通过 UART 发送给上位机 */
    uint8_t time_buf[9];    // 时间显示缓冲区 "HH:MM:SS"
    uint8_t alarm_buf[9];   // 闹钟显示缓冲区 "HH:MM:SS" 或 "xx:xx:xx"
    uint8_t date_buf[11];   // 日期显示缓冲区 "YYYY-MM-DD"
    uint8_t seg7[18];       // 7段数码管字形码 (0-9, A-F, -, 空白)
    uint8_t master_buf[18]; // 主显示缓冲区，18字节流动显示用

    /* 数码管动态扫描状态 */
    uint8_t cnt;        // 当前刷新的数码管索引 (0-7)
    int8_t shift;       // 流动显示移位量 (0-17)
    uint8_t rightshift; // 位选控制移位寄存器

    /* 显示模式与方向 */
    bool shift_mode;  // 移位方向: false=左移, true=右移 (FORMAT)
    bool shift_speed; // 移位速度: false=慢(500ms), true=快(300ms)
    bool shifting;    // 是否正在流动移位
    bool reversed;    // 显示是否反向 (FORMAT RIGHT)
    bool on;          // 7段数码管是否开启 (DISPLAY ON/OFF)
    bool blinking;    // 编辑模式字段闪烁标志

    /* 当前系统模式与编辑状态 */
    system_mode_t mode;       // 当前系统模式 (流动/日期设置/时间设置/闹钟设置/闹钟显示)
    setting_field_t field;    // 当前编辑字段 (年/月/日/时/分/秒/闹钟字段)
    main_display_t main_disp; // 正常模式下的显示内容 (流动/时间/日期/完整年份)
    bool prev_shifting;       // 进入编辑前的移位状态 (用于退出恢复)
    bool prev_shift_mode;     // 进入编辑前的移位方向 (用于退出恢复)
    bool prev_shift_speed;    // 进入编辑前的移位速度 (用于退出恢复)

    /* 夜间模式与 LED 接管 */
    bool night_mode;                       // 夜间模式激活 (仅显示 HH.MM，静音，关LED)
    bool led_takeover;                     // LED 上位机接管模式
    uint8_t led_pattern;                   // LED 接管时的强制输出模式 (hex2)
    uint8_t weather_code;                  // 天气指示 LED5-LED7 编码 (*SET:WEATHER 下发, ARMCC5 兼容)
    uint8_t current_led;                   // 当前实际输出的 LED 状态 (用于变化检测)
    uint8_t last_sent_led;                 // 上次通过 *EVT:LED 上报的 LED 状态
    bool ntp_synced;                       // 已完成 PC/NTP 对时，驱动 LED4 常亮
    volatile uint32_t uart_activity_until; // UART 活动指示 LED 截止 tick (100ms 闪烁)

    /* 闹钟运行时状态 */
    bool alarm_ringing;        // 闹钟是否正在响铃
    bool alarm_silenced;       // 当前闹钟匹配秒是否已被手动静音
    uint32_t alarm_ring_start; // 闹钟响铃开始时刻 (tick)
    uint32_t alarm_beep_phase; // 闹钟蜂鸣相位起点 (tick)
    bool alarm_beep_on;        // 闹钟蜂鸣当前是否开启 (200ms ON/OFF 循环)

    /* 临时消息显示 */
    bool msg_active;         // 临时消息显示激活
    bool msg_scroll;         // 临时消息是否需要滚动 (长度 > 8)
    uint8_t msg_buf[33];     // 临时消息文本缓冲区 (最多 32 字节 + \0)
    uint8_t msg_len;         // 临时消息长度
    int8_t msg_shift;        // 临时消息滚动偏移
    uint32_t msg_start;      // 临时消息开始显示时刻 (tick)
    uint32_t msg_last_shift; // 临时消息上次滚动时刻 (tick)

    /* 保存闪烁 */
    bool save_blink_active;    // 保存成功闪烁激活标志
    uint32_t save_blink_timer; // 保存闪烁定时器 (tick)
    bool long_press_saving;    // 长按保存进行中标志

    /* 远程蜂鸣 */
    uint32_t beep_until; // 蜂鸣截止 tick, 0 = 不蜂鸣 (ARMCC5 兼容)

    /* 启动初始化 */
    bool init_flag;    // 初始化流程标志 (true=正在启动动画)
    uint8_t init_step; // 初始化步骤 (0=全亮, 1=全暗, 2=学号前半...)

    /* I2C 操作结果 */
    uint8_t i2c_result; // 最近一次 I2C 操作返回值 (调试用)
} display_ctx_t;

/* ── 系统时基：SysTick 1ms 定时器与分频标志 ── */
typedef struct {
    volatile uint32_t tick;          // 系统滴答计数器 (1ms/tick, 约 49.7 天回绕)
    volatile bool flag_2ms;          // 2ms 时间片标志 (数码管扫描)
    volatile bool flag_500ms;        // 500ms 时间片标志 (慢速流动 + LED 心跳)
    volatile bool flag_300ms;        // 300ms 时间片标志 (快速流动 + 消息滚动)
    volatile bool flag_900ms;        // 900ms 时间片标志 (启动动画帧切换)
    volatile bool flag_1s;           // 1s 时间片标志 (时钟计时 + 缓冲区更新)
    volatile uint32_t mode_timeout;  // 编辑模式超时定时器 (5s 无操作自动退出)
    volatile bool mode_timeout_flag; // 编辑模式超时标志
} sys_timer_t;

/* ── UART 上下文：接收缓冲区与命令解析状态 ── */
typedef struct {
    uint8_t rx_buf[100];                        // UART 接收缓冲区
    uint8_t rx_len;                             // 当前接收长度
    volatile uint8_t cmd_state;                 // 命令状态: 0=空闲, 1=有新命令待处理
    command_token_t tokens[MAX_COMMAND_TOKENS]; // 解析出的 Token 数组
    uint8_t num_tokens;                         // 解析出的 Token 数量
} uart_ctx_t;

/* ── 按键输入状态：8 个矩阵按键 (K1-K8) + 2 个用户按键 (USER1/USER2) ── */
typedef struct {
    /* 矩阵按键 K1-K8 (TCA6424 PORT0 低电平有效) */
    volatile uint32_t debounce[8];    // 按键去抖计数器 (20ms)
    volatile uint32_t press_start[8]; // 按键按下起始时刻 (tick)
    volatile uint32_t long_press[8];  // 按键长按计数器 (800ms 触发)
    volatile bool state[8];           // 按键当前稳定状态 (去抖后)
    volatile bool short_evt[8];       // 按键短按事件标志 (主循环消费)
    volatile bool long_start_evt[8];  // 按键长按开始事件标志
    volatile bool repeat_evt[8];      // 按键连按事件标志 (K3 ADD 长按>=800ms 后每 200ms)

    /* USER1/USER2 独立按键 (GPIO PJ0/PJ1 低电平有效) */
    volatile uint32_t user_debounce[2];    // USER 按键去抖计数器
    volatile uint32_t user_press_start[2]; // USER 按键按下起始时刻
    volatile bool user_state[2];           // USER 按键当前稳定状态
    volatile bool user_short_evt[2];       // USER 按键短按事件标志
    volatile bool suppress_key_events;     // *SET:KEY 虚拟注入时抑制 *EVT:KEY 回显
    volatile bool user_gpio_ready;         // USER 按键 GPIO 是否已初始化
} input_state_t;

/* ── 步进电机状态 ── */
typedef struct {
    volatile uint16_t position; // 当前步进位置 (0-4095, 对应 8 拍 × 512 循环)
    volatile uint8_t direction; // 旋转方向: 0=正转(forward), 1=反转(reverse)
    volatile uint8_t running;   // 运行状态: 0=停止, 1=运行
} motor_state_t;

/* ── 休眠 RTC 数据：关机不掉电的时间保持 ── */
typedef struct {
    uint32_t stored[4];        // 存储到 Hibernate 的数据: [hh, mm, ss, RTC_counter]
    uint32_t current;          // 当前 RTC 计数值 (秒)
    uint32_t fetched[4];       // 从 Hibernate 读取的数据
    uint8_t days_in_month[13]; // 每月天数表: [0]=占位, [1]=31, [2]=28, ..., [12]=31
} rtc_data_t;

/* ── 顶层应用状态 ── */
typedef struct {
    uint32_t sys_clock_hz; // 系统时钟频率 (Hz), 由 SysCtlClockFreqSet 返回
    clock_data_t clock;    // 时钟数据 (时间/日期/闹钟)
    display_ctx_t disp;    // 显示上下文
    sys_timer_t timer;     // 系统时基
    uart_ctx_t uart;       // UART 串口状态
    input_state_t in;      // 按键输入状态
    motor_state_t motor;   // 步进电机状态
    rtc_data_t rtc;        // 休眠 RTC 数据
} app_state_t;


/* ===== app_state.h ===== */


/* 全局应用状态实例 —— 所有模块通过 g.xxx 访问 */
extern app_state_t g;

/* ===== board.h ===== */
void DevicesInit(void);
void UserKeyGPIOInit(void);

/* ===== delay.h ===== */
void Delay(uint32_t value);

/* ===== hibernate_rtc.h ===== */
void HibernateInit(void);

/* ===== i2c_bus.h ===== */
void S800_I2C0_Init(void);
uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData);
uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr);

/* ===== pwm_buzzer.h ===== */
void PWMInit(void);
void PWMStart(uint32_t ui32Freq_Hz);
void PWMStop(void);

/* ===== stepper.h ===== */
void StepperGPIOInit(void);
void StepperTimerInit(void);
void StepperTimerTick(void);

/* ===== timebase.h ===== */
void SysTickInit(void);
void SysTickUpdateTimebaseFlags(void);

/* ===== uart_port.h ===== */
void UARTInit(void);
void UARTStringPutNOBlocking(uint32_t ui32Base, uint8_t* cMessage);
void UARTStringPutReversedNOBlocking(uint32_t ui32Base, uint8_t* cMessage);
void UARTCharPutBlocking(uint32_t ui32Base, uint8_t ucData);

/* ===== alarm.h ===== */
void HandleAlarm(void);
void StopAlarmRinging(bool silence_current_match);

/* ===== app.h ===== */
void App_Init(void);
void App_RunOnce(void);

/* ===== boot_sequence.h ===== */
void RunInitializationSequence(void);

/* ===== button_logic.h ===== */
void ProcessButtonEvents(void);
void HandleModeTimeout(void);

/* ===== command.h ===== */
void ProcessUartCommand(void);
bool compareTokens(const command_token_t* token, const char* str_literal, uint8_t min_match_len);

/* ===== datetime.h ===== */
bool is_leap_year(uint16_t year_val);
bool is_valid_date(uint16_t y, uint8_t m, uint8_t d);
bool is_valid_time(uint8_t h, uint8_t m, uint8_t s);
void DateTime_TickOneSecond(void);

/* ===== display.h ===== */
void Update7SegmentDisplay(void);
void UpdateDisplayShift(void);
void UpdateTimeAndDisplayBuffers(void);
void Display_StartMessage(const uint8_t* text, uint8_t len);
void Display_StopMessage(void);
void Display_SendEvent(void);
void Display_SendLedEvent(void);
void Display_SendModeEvent(const char* state);
void Display_SendEditEvent(const char* type, const uint8_t* value);
void Display_SetLedOutput(uint8_t led_pattern);
void Display_UpdateStatusLeds(void);
void Display_FormatBufferForProtocol(const uint8_t* src, uint8_t len, uint8_t* dst);

/* ===== melody.h ===== */


void Melody_Start(void);     // 启动开机旋律播放
void Melody_Update(void);    // 每主循环迭代调用一次，推进音符
bool Melody_IsPlaying(void); // 返回 true 表示旋律正在播放


/* ===== callback.h ===== */
void UART0_Handler(void);
void SysTick_Handler(void);
void TIMER0A_Handler(void);

/* ===== app_state.c ===== */

/* ── 全局应用状态实例
   ARMCC5 (ARM Compiler 5) 存在嵌套结构体零初始化 bug:
   C99 designated initializers 不会对未显式赋值的嵌套字段清零。
   因此每个 bool/uint8_t 字段都必须显式初始化，标记 "ARMCC5 兼容"。 ── */
app_state_t g = {
    /* ── 时钟初始值: 2025-06-03 00:00:00, 闹钟未设置 ── */
    .clock = {
        .hh = 0, .mm = 0, .ss = 0,              // 当前时间 00:00:00
        .year = 2025, .month = 6, .day = 3,     // 当前日期 2025-06-03
        .alm_hh = 25, .alm_mm = 0, .alm_ss = 0, // 闹钟未设置 (25 为哨兵值)
        .alarm_enabled = false,                 // 闹钟默认关闭
    },

    /* ── 显示初始值: 7-SEG 字形表 + 扫描/模式默认值 ── */
    .disp = {
        /* 7段数码管字形码: 0 1 2 3 4 5 6 7 8 9 A B C D E F - 空白 */
        .seg7 = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
                  0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71,
                  0x5c, 0x00 },
        .cnt = 0,                       // 数码管扫描索引从 0 开始 (ARMCC5 兼容)
        .rightshift = 0x01,             // 位选从最低位开始扫描
        .on = true,                     // 数码管默认开启
        .shifting = true,               // 流动显示默认启动
        .shift = 0,                     // 流动偏移量从 0 开始 (ARMCC5 兼容)
        .shift_mode = false,            // 默认左移 (FORMAT LEFT)
        .shift_speed = false,           // 默认慢速 500ms (ARMCC5 兼容)
        .reversed = false,              // 默认正序显示
        .blinking = false,              // 无编辑闪烁 (ARMCC5 兼容)
        .mode = MODE_FLOWING,           // 初始为流动显示模式
        .main_disp = MAIN_DISPLAY_FLOW, // 默认显示日期+时间交替流动
        .field = FIELD_NONE,            // 无编辑字段选中
        .prev_shifting = true,          // 进入编辑前移位状态默认值
        .prev_shift_mode = false,       // 进入编辑前格式默认值
        .prev_shift_speed = false,      // 进入编辑前速度默认值 (ARMCC5 兼容)
        .night_mode = false,            // 非夜间模式 (ARMCC5 兼容)
        .led_takeover = false,          // 非 LED 接管模式 (ARMCC5 兼容)
        .led_pattern = 0x00,            // LED 接管输出模式 (ARMCC5 兼容)
        .weather_code = 0x00,           // 天气 LED5-7 初始关闭 (ARMCC5 兼容)
        .alarm_ringing = false,         // 未在响铃 (ARMCC5 兼容)
        .alarm_beep_on = false,         // 蜂鸣器关闭 (ARMCC5 兼容)
        .msg_active = false,            // 无临时消息 (ARMCC5 兼容)
        .msg_scroll = false,            // 无滚动消息 (ARMCC5 兼容)
        .save_blink_active = false,     // 无保存闪烁 (ARMCC5 兼容)
        .long_press_saving = false,     // 无长按保存 (ARMCC5 兼容)
        .beep_until = 0,                // 无远程蜂鸣 (ARMCC5 兼容)
        .ntp_synced = false,            // LED4: 尚未完成 NTP 对时
        .init_flag = true,              // 启动时进入初始化动画流程
        .init_step = 0,                 // 初始化从步骤 0 开始 (ARMCC5 兼容)
    },

    /* ── RTC 初始值: 每月天数查找表 ── */
    .rtc = {
        .days_in_month = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
    },

    /* ── 步进电机初始值: 反向运行，1 RPM ── */
    .motor = {
        .direction = 1, // 反转
        .running = 1,   // 启动运行
    },
};

/* ===== delay.c ===== */

// 简单的软件延时
void Delay(uint32_t value) {
    uint32_t ui32Loop;
    for (ui32Loop = 0; ui32Loop < value; ui32Loop++);
}

/* ===== hibernate_rtc.c ===== */

// 初始化休眠模块
void HibernateInit(void) {
    SysCtlPeripheralEnable(SYSCTL_PERIPH_HIBERNATE);        // 使能休眠模块时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_HIBERNATE)) // 等待模块就绪
        ;
    HibernateEnableExpClk(g.sys_clock_hz);        // 使用系统时钟初始化休眠模块
    HibernateClockConfig(HIBERNATE_OSC_LOWDRIVE); // 配置低功耗振荡器
    HibernateRTCEnable();                         // 使能RTC
}

/* ===== timebase.c ===== */

// 初始化SysTick定时器，用于生成系统滴答
void SysTickInit(void) {
    SysTickPeriodSet(g.sys_clock_hz / SYSTICK_FREQUENCY); // 设置SysTick周期
    SysTickEnable();                                      // 启动SysTick
    SysTickIntEnable();                                   // 使能SysTick中断
}

void SysTickUpdateTimebaseFlags(void) {
    g.timer.tick++;

    if (g.timer.tick % V_T2ms == 0) {
        g.timer.flag_2ms = true;
    }
    if (g.timer.tick % V_T500ms == 0) {
        g.timer.flag_500ms = true;
    }
    if (g.timer.tick % V_T300ms == 0) {
        g.timer.flag_300ms = true;
    }
    if (g.timer.tick % V_T900ms == 0) {
        g.timer.flag_900ms = true;
    }
    if (g.timer.tick % V_T1s == 0) {
        g.timer.flag_1s = true;
    }
}

/* ===== i2c_bus.c ===== */

// 初始化I2C0模块和I/O扩展器
void S800_I2C0_Init(void) {
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);        // 使能I2C0时钟
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);       // 使能GPIOB时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_I2C0)) // 等待I2C0就绪
        ;
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) // 等待GPIOB就绪
        ;

    GPIOPinConfigure(GPIO_PB2_I2C0SCL);             // 配置PB2为I2C0 SCL
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);             // 配置PB3为I2C0 SDA
    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2); // 设置PB2为I2C SCL引脚类型
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);    // 设置PB3为I2C SDA引脚类型

    I2CMasterInitExpClk(I2C0_BASE, g.sys_clock_hz, true); // 初始化I2C0为主机，使用快速模式
    I2CMasterEnable(I2C0_BASE);                           // 使能I2C0

    // 配置TCA6424 I/O扩展器
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT0, 0x0ff); // PORT0配置为输入
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT1, 0x0);   // PORT1配置为输出
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT2, 0x0);   // PORT2配置为输出

    // 配置PCA9557 I/O扩展器
    g.disp.i2c_result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_CONFIG, 0x00);  // 配置为输出
    g.disp.i2c_result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x0ff); // 设置所有输出为高电平
}

// 通过I2C向指定设备地址的寄存器写入一个字节
uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData) {
    uint8_t rop; // 操作结果

    while (I2CMasterBusy(I2C0_BASE)) // 等待I2C总线空闲
        ;
    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);             // 设置从设备地址，写入模式
    I2CMasterDataPut(I2C0_BASE, RegAddr);                         // 放置寄存器地址
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START); // 发送启动信号和寄存器地址
    while (I2CMasterBusy(I2C0_BASE))                              // 等待发送完成
        ;
    rop = (uint8_t)I2CMasterErr(I2C0_BASE);                        // 获取错误状态
    if (rop != I2C_MASTER_ERR_NONE)                                // 如果有错误
        return rop;                                                // 返回错误码
    I2CMasterDataPut(I2C0_BASE, WriteData);                        // 放置要写入的数据
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH); // 发送数据和停止信号
    while (I2CMasterBusy(I2C0_BASE))                               // 等待发送完成
        ;
    rop = (uint8_t)I2CMasterErr(I2C0_BASE); // 获取最终错误状态
    return rop;                             // 返回操作结果
}

// 通过I2C从指定设备地址的寄存器读取一个字节
uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr) {
    uint8_t value; // 读取到的数据

    while (I2CMasterBusy(I2C0_BASE)) // 等待I2C总线空闲
        ;
    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);        // 设置从设备地址，写入模式
    I2CMasterDataPut(I2C0_BASE, RegAddr);                    // 放置要读取的寄存器地址
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_SEND); // 发送启动信号、设备地址和寄存器地址
    while (I2CMasterBusBusy(I2C0_BASE))                      // 等待总线操作完成
        ;

    if (I2CMasterErr(I2C0_BASE) != I2C_MASTER_ERR_NONE) // 如果有错误
    {
        return 0xFF; // 返回错误码
    }
    Delay(5); // 延时等待

    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, true);            // 设置从设备地址，读取模式
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_RECEIVE); // 发送启动信号、设备地址和读取命令
    while (I2CMasterBusBusy(I2C0_BASE))                         // 等待总线操作完成
        ;

    if (I2CMasterErr(I2C0_BASE) != I2C_MASTER_ERR_NONE) // 如果有错误
    {
        return 0xFF; // 返回错误码
    }
    value = I2CMasterDataGet(I2C0_BASE); // 获取读取到的数据
    Delay(5);                            // 延时等待
    return value;                        // 返回读取到的数据
}

/* ===== pwm_buzzer.c ===== */

static void PwmPinToGpioLow(void) {
    GPIOPinTypeGPIOOutput(GPIO_PORTK_BASE, GPIO_PIN_5);
    GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_5, 0); /* 拉低, 蜂鸣器彻底静音 */
}

static void PwmPinRestore(void) {
    GPIOPinConfigure(GPIO_PK5_M0PWM7);
    GPIOPinTypePWM(GPIO_PORTK_BASE, GPIO_PIN_5);
}

// 初始化PWM模块
void PWMInit(void) {
    SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0);        // 使能PWM0模块时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_PWM0)) // 等待PWM0模块就绪
        ;

    PWMOutputState(PWM0_BASE, PWM_OUT_7_BIT, true); // 使能PWM0模块的PWM7输出位

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOK);        // 使能GPIOK时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOK)) // 等待GPIOK就绪
        ;

    GPIOPinConfigure(GPIO_PK5_M0PWM7);           // 配置PK5引脚为M0PWM7功能
    GPIOPinTypePWM(GPIO_PORTK_BASE, GPIO_PIN_5); // 设置PK5为PWM引脚类型

    // 配置PWM发生器3，为向下计数模式，无同步
    PWMGenConfigure(PWM0_BASE, PWM_GEN_3, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC);
}

// 启动PWM输出，设置指定频率
void PWMStart(uint32_t ui32Freq_Hz) {
    PWMGenDisable(PWM0_BASE, PWM_GEN_3); // 先禁用PWM发生器

    PwmPinRestore(); // 恢复 PK5 为 PWM 功能 (PWMStop 可能已切为 GPIO)

    // 设置PWM周期，决定频率
    PWMGenPeriodSet(PWM0_BASE, PWM_GEN_3, g.sys_clock_hz / ui32Freq_Hz);
    // 设置PWM脉冲宽度为周期的一半 (50%占空比)
    PWMPulseWidthSet(PWM0_BASE, PWM_OUT_7, (PWMGenPeriodGet(PWM0_BASE, PWM_GEN_3) / 2));

    PWMGenEnable(PWM0_BASE, PWM_GEN_3); // 启用PWM发生器
}

// 停止PWM输出
void PWMStop(void) {
    PWMGenDisable(PWM0_BASE, PWM_GEN_3); // 禁用PWM发生器3
    PwmPinToGpioLow();                   // 切为 GPIO 并拉低, 消除丝丝声
    g.disp.alarm_ringing = false;
}

/* ===== stepper.c ===== */

static const uint8_t stepper_sequence[8] = {
    0x01, // Beat 1: PF0
    0x03, // Beat 2: PF0+PF1
    0x02, // Beat 3: PF1
    0x06, // Beat 4: PF1+PF2
    0x04, // Beat 5: PF2
    0x0C, // Beat 6: PF2+PF3
    0x08, // Beat 7: PF3
    0x09, // Beat 8: PF3+PF0
};

// 初始化步进电机GPIO (PF0-PF3)
void StepperGPIOInit(void) {
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));

    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE + GPIO_O_CR) |= GPIO_PIN_0;

    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, STEPPER_PINS);
}

// 初始化步进电机Timer0
void StepperTimerInit(void) {
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0));

    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(TIMER0_BASE, TIMER_A, STEPPER_TIMER_LOAD);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);
    TimerEnable(TIMER0_BASE, TIMER_A);
}

// Timer0A中断处理 — 步进电机驱动
void StepperTimerTick(void) {
    uint8_t output = 0;
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

    if (!g.motor.running)
        return;

    output = stepper_sequence[g.motor.position & 0x07];
    GPIOPinWrite(GPIO_PORTF_BASE, STEPPER_PINS, output);

    if (g.motor.direction == 0) {
        g.motor.position++;
        if (g.motor.position >= STEPPER_BEATS_PER_REVOLUTION)
            g.motor.position = 0;
    } else {
        if (g.motor.position == 0)
            g.motor.position = STEPPER_BEATS_PER_REVOLUTION - 1;
        else
            g.motor.position--;
    }
}

/* ===== uart_port.c ===== */

// 初始化UART0
void UARTInit(void) {
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);        // 使能UART0时钟
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);        // 使能GPIOA时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) // 等待GPIOA就绪
        ;

    GPIOPinConfigure(GPIO_PA0_U0RX);                           // 配置PA0为UART0 RX
    GPIOPinConfigure(GPIO_PA1_U0TX);                           // 配置PA1为UART0 TX
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1); // 设置PA0和PA1为UART功能

    // 配置UART0，波特率115200，8位数据，1位停止位，无奇偶校验
    UARTConfigSetExpClk(UART0_BASE,
                        g.sys_clock_hz,
                        115200,
                        (UART_CONFIG_WLEN_8 |
                            UART_CONFIG_STOP_ONE |
                            UART_CONFIG_PAR_NONE));

    UARTFIFOLevelSet(UART0_BASE, UART_FIFO_TX1_8, UART_FIFO_RX7_8); // 设置FIFO触发级别
    IntEnable(INT_UART0);                                           // 使能UART0中断
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);           // 使能接收和接收超时中断
}

// 非阻塞方式发送字符串
void UARTStringPutNOBlocking(uint32_t ui32Base, uint8_t* cMessage) {
    bool TXFIFO_free;

    while (*cMessage != '\0') // 遍历字符串
    {
        TXFIFO_free = UARTCharPutNonBlocking(ui32Base, *(cMessage)); // 尝试非阻塞发送字符
        if (TXFIFO_free)                                             // 如果发送成功
        {
            cMessage++; // 移动到下一个字符
        }
        // 如果发送失败 (FIFO满)，则等待下一个SysTick周期再尝试
    }
}

// 非阻塞方式反向发送字符串
void UARTStringPutReversedNOBlocking(uint32_t ui32Base, uint8_t* cMessage) {
    int len;
    int i;
    bool TXFIFO_free;

    // 计算字符串长度
    len = 0;
    while (cMessage[len] != '\0') {
        len++;
    }
    // 从字符串末尾开始向前遍历发送
    for (i = len - 1; i >= 0; --i) {
        TXFIFO_free = UARTCharPutNonBlocking(ui32Base, cMessage[i]); // 尝试非阻塞发送字符
        while (!TXFIFO_free)                                         // 如果发送失败，则阻塞直到发送成功
        {
            TXFIFO_free = UARTCharPutNonBlocking(ui32Base, cMessage[i]);
        }
    }
}

// 阻塞方式发送单个字符
void UARTCharPutBlocking(uint32_t ui32Base, uint8_t ucData) {
    while (!UARTCharPutNonBlocking(ui32Base, ucData)) // 循环直到字符成功放入FIFO
    {}
}

/* ===== board.c ===== */

// 初始化所有外设
void DevicesInit(void) {
    // 设置系统时钟频率为20MHz
    g.sys_clock_hz = SysCtlClockFreqSet((SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN |
                                            SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480),
                                        20000000);

    UARTInit();         // 初始化UART
    PWMInit();          // 初始化PWM
    S800_I2C0_Init();   // 初始化I2C0
    HibernateInit();    // 初始化休眠模块
    UserKeyGPIOInit();  // 初始化USER1/USER2按键
    StepperGPIOInit();  // 初始化步进电机GPIO
    StepperTimerInit(); // 初始化步进电机Timer
    SysTickInit();      // 初始化SysTick定时器
    IntMasterEnable();  // 开启总中断
}

void UserKeyGPIOInit(void) {
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOJ);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOJ));

    GPIOPinTypeGPIOInput(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    GPIOPadConfigSet(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    g.in.user_gpio_ready = true;
}

// 处理模式超时，恢复到原始显示状态并放弃未保存的更改

/* ===== datetime.c ===== */

// 判断给定年份是否为闰年
bool is_leap_year(uint16_t year_val) {
    return ((year_val % 4 == 0 && year_val % 100 != 0) || year_val % 400 == 0);
}

// 检查给定的年、月、日组合是否为有效日期
bool is_valid_date(uint16_t y, uint8_t m, uint8_t d) {
    uint8_t max_days;

    // 检查年份、月份、日期范围
    if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1) {
        return false;
    }

    max_days = g.rtc.days_in_month[m]; // 获取当前月份的最大天数
    if (m == 2 && is_leap_year(y))     // 如果是闰年的2月
    {
        max_days = 29; // 2月有29天
    }
    if (d > max_days) // 检查日期是否超出当月最大天数
    {
        return false;
    }
    return true;
}

// 检查给定的时、分、秒组合是否为有效时间
bool is_valid_time(uint8_t h, uint8_t m, uint8_t s) {
    return (h < 24 && m < 60 && s < 60); // 时:0-23, 分:0-59, 秒:0-59
}

// 初始化所有外设

void DateTime_TickOneSecond(void) {
    uint8_t max_days_for_current_month;

    g.clock.ss++;
    if (g.clock.ss >= 60) {
        g.clock.mm++;
        g.clock.ss = 0;
    }
    if (g.clock.mm >= 60) {
        g.clock.hh++;
        g.clock.mm = 0;
    }
    if (g.clock.hh >= 24) {
        g.clock.day++;
        g.clock.hh = 0;
        max_days_for_current_month = g.rtc.days_in_month[g.clock.month];
        if (g.clock.month == 2 && is_leap_year(g.clock.year)) {
            max_days_for_current_month = 29;
        }
        if (g.clock.day > max_days_for_current_month) {
            g.clock.day = 1;
            g.clock.month++;
            if (g.clock.month > 12) {
                g.clock.month = 1;
                g.clock.year++;
            }
        }
    }
}

/* ===== melody.c ===== */

/* ── 音符定义 ── */
typedef struct {
    uint16_t freq_hz;     // 频率 (Hz), 0 = 休止符
    uint16_t duration_ms; // 时值 (ms)
} note_t;

/* MAN! WHAT CAN I SAY? */
static const note_t see_you_again[] = {
    { 698, 355 }, { 0, 23 }, { 1047, 355 }, { 0, 23 }, { 932, 355 }, { 0, 23 }, { 587, 729 }, { 0, 23 },
    { 932, 167 }, { 0, 23 }, { 1047, 167 }, { 0, 23 }, { 1175, 167 }, { 0, 23 }, { 1047, 167 }, { 0, 23 },
    { 932, 167 }, { 0, 23 }, { 1047, 167 }, { 0, 23 }, { 698, 355 }, { 0, 23 }, { 1047, 355 }, { 0, 23 },
    { 932, 355 }, { 0, 23 }, { 587, 729 }, { 0, 23 }, { 932, 167 }, { 0, 23 }, { 1047, 167 }, { 0, 23 },
    { 1175, 167 }, { 0, 23 }, { 1047, 167 }, { 0, 23 }, { 932, 167 }, { 0, 23 }, { 1047, 167 }, { 0, 23 },
    { 698, 355 }, { 0, 23 }, { 1047, 355 }, { 0, 23 }, { 932, 355 }, { 0, 23 }, { 698, 729 }, { 0, 23 },
    { 932, 167 }, { 0, 23 }, { 1047, 167 }, { 0, 23 }, { 1175, 167 }, { 0, 23 }, { 1047, 167 }, { 0, 23 },
    { 932, 167 }, { 0, 23 }, { 1047, 167 }, { 0, 23 }, { 698, 355 }, { 0, 23 }, { 1047, 355 }, { 0, 23 },
    { 932, 355 }, { 0, 23 }, { 698, 729 }, { 0, 23 },
};

#define MELODY_NUM_NOTES (sizeof(see_you_again) / sizeof(see_you_again[0]))

/* ── 播放器状态 (文件作用域静态变量, 不污染全局结构体) ── */
static bool s_active;         // 旋律是否正在播放
static uint16_t s_note_index; // 当前音符索引
static uint32_t s_note_start; // 当前音符起始时刻 (g.timer.tick)

/* 启动旋律播放 */
void Melody_Start(void) {
    s_active = true;
    s_note_index = 0;
    s_note_start = g.timer.tick;
    PWMStart(see_you_again[0].freq_hz);
}

/* 推进音符 —— 每次主循环迭代调用 */
void Melody_Update(void) {
    const note_t* note;

    if (!s_active)
        return;

    note = &see_you_again[s_note_index];

    /* 检查当前音符时值是否已到 */
    if ((g.timer.tick - s_note_start) < note->duration_ms)
        return;

    /* 推进到下一个音符 */
    s_note_index++;
    if (s_note_index >= MELODY_NUM_NOTES) {
        /* 旋律结束 */
        PWMStop();
        s_active = false;
        return;
    }

    note = &see_you_again[s_note_index];
    s_note_start = g.timer.tick;

    if (note->freq_hz == 0) {
        PWMStop(); // 休止符
    } else {
        PWMStart(note->freq_hz);
    }
}

/* 查询播放状态 */
bool Melody_IsPlaying(void) {
    return s_active;
}

/* ===== alarm.c ===== */

// 检查并处理闹钟，如果时间到达则启动PWM
void HandleAlarm(void) {
    if (g.clock.hh == g.clock.alm_hh && g.clock.mm == g.clock.alm_mm && g.clock.ss == g.clock.alm_ss && g.clock.alm_hh
        != 25) {
        if (!g.disp.alarm_ringing && !g.disp.alarm_silenced) {
            g.disp.alarm_ringing = true;
            g.disp.alarm_ring_start = g.timer.tick;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:ALARM\r\n");
            g.disp.alarm_beep_phase = g.timer.tick;
            if (g.disp.night_mode) {
                g.disp.alarm_beep_on = false;
            } else {
                PWMStart(500);
                g.disp.alarm_beep_on = true;
            }
        }
    } else {
        g.disp.alarm_silenced = false;
    }

    if (!g.disp.alarm_ringing) {
        return;
    }

    if ((g.timer.tick - g.disp.alarm_ring_start) >= ALARM_RING_MAX_MS) {
        StopAlarmRinging(false);
        g.disp.alarm_silenced = true;
        return;
    }

    if (g.disp.night_mode) {
        if (g.disp.alarm_beep_on) {
            PWMStop();
            g.disp.alarm_ringing = true;
            g.disp.alarm_beep_on = false;
        }
        return;
    }

    if (!g.disp.alarm_beep_on) {
        if ((g.timer.tick - g.disp.alarm_beep_phase) >= ALARM_BEEP_OFF_MS) {
            PWMStart(500);
            g.disp.alarm_ringing = true;
            g.disp.alarm_beep_on = true;
            g.disp.alarm_beep_phase = g.timer.tick;
        }
    } else if ((g.timer.tick - g.disp.alarm_beep_phase) >= ALARM_BEEP_ON_MS) {
        PWMStop();
        g.disp.alarm_ringing = true;
        g.disp.alarm_beep_on = false;
        g.disp.alarm_beep_phase = g.timer.tick;
    }
}

// 运行启动初始化显示序列，包括RTC校准和学生信息显示

void StopAlarmRinging(bool silence_current_match) {
    bool was_ringing = g.disp.alarm_ringing;

    PWMStop();
    g.disp.alarm_ringing = false;
    g.disp.alarm_beep_on = false;
    g.disp.alarm_ring_start = 0;
    g.disp.alarm_beep_phase = 0;
    if (silence_current_match) {
        g.disp.alarm_silenced = true;
    }
    if (was_ringing) {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:ALARM OFF\r\n");
    }
}

/* ===== display.c ===== */

static uint8_t DigitToAscii(uint8_t digit) {
    return (uint8_t)('0' + digit);
}

static uint8_t HexDigit(uint8_t value) {
    value &= 0x0F;
    return (uint8_t)(value < 10 ? ('0' + value) : ('A' + value - 10));
}

static uint8_t SegmentForChar(uint8_t c) {
    if (c >= '0' && c <= '9')
        return g.disp.seg7[c - '0'];
    if (c >= 'a' && c <= 'z')
        c = (uint8_t)(c - 0x20);

    switch (c) {
        case 'A':
            return 0x77;
        case 'B':
            return 0x7C;
        case 'C':
            return 0x39;
        case 'D':
            return 0x5E;
        case 'E':
            return 0x79;
        case 'F':
            return 0x71;
        case 'G':
            return 0x3D;
        case 'H':
            return 0x76;
        case 'I':
            return 0x06;
        case 'J':
            return 0x1E;
        case 'K':
            return 0x76; // 7段码 K 与 H/X 共用 (竖+竖+中横)
        case 'L':
            return 0x38;
        case 'N':
            return 0x54;
        case 'O':
            return 0x3F;
        case 'P':
            return 0x73;
        case 'R':
            return 0x50;
        case 'S':
            return 0x6D;
        case 'T':
            return 0x78;
        case 'U':
            return 0x3E;
        case 'V':
            return 0x3E;
        case 'X':
            return 0x76;
        case 'Y':
            return 0x6E;
        case '-':
            return 0x5c;
        case '_':
        case ' ':
            return 0x00;
        default:
            return 0x00;
    }
}

/* 将 7 段码反向映射为 ASCII 字符 (用于流动显示 *EVT:DISP 上报) */
static uint8_t CharForSegment(uint8_t seg) {
    uint8_t base = seg & 0x7F; /* 屏蔽 dp 位 */
    uint8_t i;

    if (base == 0x00)
        return '_';

    for (i = 0; i < 18; ++i) {
        if (g.disp.seg7[i] == base) {
            if (i <= 9)
                return (uint8_t)('0' + i);
            if (i >= 10 && i <= 15)
                return (uint8_t)('A' + (uint8_t)(i - 10));
            if (i == 16)
                return '-';
            if (i == 17)
                return '_';
        }
    }
    return '_';
}

/* 将 n 字符内容在 8 位缓冲区中居中 (左右各留余量) */
static void CenterContent(uint8_t chars[8], uint8_t n, uint8_t* dp_hex) {
    uint8_t pad = (uint8_t)((8 - n) / 2U);
    uint8_t i;
    /* 右移腾出左侧 padding */
    for (i = 8; i > pad; --i)
        chars[i - 1U] = chars[i - 1U - pad];
    for (i = 0; i < pad; ++i)
        chars[i] = '_';
    *dp_hex = (uint8_t)(*dp_hex << pad);
}

static void BuildCurrentDisplay(uint8_t chars[8], uint8_t* dp_hex) {
    uint8_t i;

    for (i = 0; i < 8; ++i) {
        chars[i] = '_';
    }
    *dp_hex = 0x00;

    if (!g.disp.on) {
        return;
    }

    if (g.disp.msg_active) {
        for (i = 0; i < 8; ++i) {
            uint8_t msg_index = (uint8_t)(g.disp.msg_shift + i);
            if (msg_index < g.disp.msg_len) {
                chars[i] = g.disp.msg_buf[msg_index];
            }
        }
        return;
    }

    if (g.disp.night_mode) {
        chars[0] = DigitToAscii((uint8_t)(g.clock.hh / 10));
        chars[1] = DigitToAscii((uint8_t)(g.clock.hh % 10));
        chars[2] = DigitToAscii((uint8_t)(g.clock.mm / 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.mm % 10));
        *dp_hex = 0x02;
        return;
    }

    if (g.disp.mode == MODE_DATE_SET) {
        chars[0] = DigitToAscii((uint8_t)((g.clock.temp_year / 1000) % 10));
        chars[1] = DigitToAscii((uint8_t)((g.clock.temp_year / 100) % 10));
        chars[2] = DigitToAscii((uint8_t)((g.clock.temp_year / 10) % 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.temp_year % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.temp_month / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.temp_month % 10));
        chars[6] = DigitToAscii((uint8_t)(g.clock.temp_day / 10));
        chars[7] = DigitToAscii((uint8_t)(g.clock.temp_day % 10));
        *dp_hex = 0x28;
        return;
    }

    if (g.disp.mode == MODE_TIME_SET) {
        chars[0] = DigitToAscii((uint8_t)(g.clock.temp_hh / 10));
        chars[1] = DigitToAscii((uint8_t)(g.clock.temp_hh % 10));
        chars[2] = DigitToAscii((uint8_t)(g.clock.temp_mm / 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.temp_mm % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.temp_ss / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.temp_ss % 10));
        *dp_hex = 0x0A;
        CenterContent(chars, 6, dp_hex);
        return;
    }

    if (g.disp.mode == MODE_ALARM_SET || g.disp.mode == MODE_ALARM_DISPLAY) {
        uint8_t show_hh = (g.disp.mode == MODE_ALARM_SET) ? g.clock.temp_alm_hh : (uint8_t)g.clock.alm_hh;
        uint8_t show_mm = (g.disp.mode == MODE_ALARM_SET) ? g.clock.temp_alm_mm : (uint8_t)g.clock.alm_mm;
        uint8_t show_ss = (g.disp.mode == MODE_ALARM_SET) ? g.clock.temp_alm_ss : (uint8_t)g.clock.alm_ss;

        if (g.clock.alm_hh == 25 && g.disp.mode == MODE_ALARM_DISPLAY) {
            memcpy(chars, "AL xx xx", 8);
        } else {
            chars[0] = DigitToAscii((uint8_t)(show_hh / 10));
            chars[1] = DigitToAscii((uint8_t)(show_hh % 10));
            chars[2] = DigitToAscii((uint8_t)(show_mm / 10));
            chars[3] = DigitToAscii((uint8_t)(show_mm % 10));
            chars[4] = DigitToAscii((uint8_t)(show_ss / 10));
            chars[5] = DigitToAscii((uint8_t)(show_ss % 10));
            *dp_hex = 0x0A;
            CenterContent(chars, 6, dp_hex);
        }
        return;
    }

    if (g.disp.main_disp == MAIN_DISPLAY_DATE) {
        chars[0] = DigitToAscii((uint8_t)((g.clock.year / 10) % 10));
        chars[1] = DigitToAscii((uint8_t)(g.clock.year % 10));
        chars[2] = DigitToAscii((uint8_t)(g.clock.month / 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.month % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.day / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.day % 10));
        *dp_hex = 0x0A;
        CenterContent(chars, 6, dp_hex);
    } else if (g.disp.main_disp == MAIN_DISPLAY_YEAR) {
        chars[0] = DigitToAscii((uint8_t)((g.clock.year / 1000) % 10));
        chars[1] = DigitToAscii((uint8_t)((g.clock.year / 100) % 10));
        chars[2] = DigitToAscii((uint8_t)((g.clock.year / 10) % 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.year % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.month / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.month % 10));
        chars[6] = DigitToAscii((uint8_t)(g.clock.day / 10));
        chars[7] = DigitToAscii((uint8_t)(g.clock.day % 10));
        *dp_hex = 0x28;
    } else if (g.disp.main_disp == MAIN_DISPLAY_FLOW) {
        /* 流动模式: 取当前 8 位窗口的 7 段码，反向映射为 ASCII */
        for (i = 0; i < 8; ++i) {
            uint8_t idx = ((uint8_t)g.disp.shift + i) % 18U;
            uint8_t seg = g.disp.master_buf[idx];

            chars[i] = CharForSegment(seg);
            if (seg & 0x80)
                *dp_hex |= (uint8_t)(1U << i);
        }
    } else {
        /* MAIN_DISPLAY_TIME: 居中显示 HH.MM.SS */
        chars[0] = DigitToAscii((uint8_t)(g.clock.hh / 10));
        chars[1] = DigitToAscii((uint8_t)(g.clock.hh % 10));
        chars[2] = DigitToAscii((uint8_t)(g.clock.mm / 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.mm % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.ss / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.ss % 10));
        *dp_hex = 0x0A;
        CenterContent(chars, 6, dp_hex);
    }
}

static uint8_t ReverseDpBitmap(uint8_t left_dp) {
    uint8_t right_dp = 0;
    uint8_t i;

    for (i = 0; i < 8; ++i) {
        if (left_dp & (uint8_t)(1U << i)) {
            if (i == 7) {
                right_dp |= 1U; /* bit 7 → bit 0 */
            } else {
                right_dp |= (uint8_t)(1U << (6 - i)); /* bits 0-6 镜像 */
            }
        }
    }
    return right_dp;
}

void Display_SetLedOutput(uint8_t led_pattern) {
    g.disp.i2c_result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, (uint8_t)~led_pattern);
}

void Display_UpdateStatusLeds(void) {
    uint8_t pattern;

    if (g.disp.led_takeover) {
        pattern = g.disp.led_pattern;
        g.disp.current_led = pattern;
        Display_SetLedOutput(pattern);
        /* LED takeover 变化检测 */
        if (pattern != g.disp.last_sent_led) {
            g.disp.last_sent_led = pattern;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:LED ");
            UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(pattern >> 4)));
            UARTCharPutBlocking(UART0_BASE, HexDigit(pattern));
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
        }
        return;
    }
    if (g.in.state[4])
    {
        pattern = 0xFF;
        g.disp.current_led = pattern;
        Display_SetLedOutput(pattern);
        if (pattern != g.disp.last_sent_led)
        {
            g.disp.last_sent_led = pattern;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:LED ");
            UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(pattern >> 4)));
            UARTCharPutBlocking(UART0_BASE, HexDigit(pattern));
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
        }
        return;
    }
    pattern = 0x00;
    if ((g.timer.tick % V_T1s) < V_T500ms) {
        pattern |= 0x01;
    }

    if (!g.disp.night_mode) {
        if (g.disp.alarm_ringing) {
            if ((g.timer.tick % 400U) < 200U)
                pattern |= 0x02;
        } else if (g.clock.alm_hh != 25) {
            pattern |= 0x02;
        }

        if (g.disp.mode == MODE_DATE_SET || g.disp.mode == MODE_TIME_SET || g.disp.mode == MODE_ALARM_SET) {
            pattern |= 0x04;
        }

        if ((int32_t)(g.disp.uart_activity_until - g.timer.tick) > 0) {
            pattern |= 0x08;
        }

        if (g.disp.ntp_synced) {
            pattern |= 0x10; /* LED4: NTP 对时完成 */
        }
    }

    /* LED5-LED7: 天气指示 (PC 端 *SET:WEATHER 下发, 不进入接管模式) */
    pattern |= g.disp.weather_code;

    g.disp.current_led = pattern;
    Display_SetLedOutput(pattern);

    /* LED 状态变化时立即上报 *EVT:LED (心跳在 UpdateTimeAndDisplayBuffers 中每秒发送) */
    if (pattern != g.disp.last_sent_led) {
        g.disp.last_sent_led = pattern;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:LED ");
        UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(pattern >> 4)));
        UARTCharPutBlocking(UART0_BASE, HexDigit(pattern));
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
    }
}

void Display_FormatBufferForProtocol(const uint8_t* src, uint8_t len, uint8_t* dst) {
    uint8_t i;

    if (g.disp.reversed) {
        for (i = 0; i < len; ++i) {
            dst[i] = src[len - 1U - i];
        }
    } else {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

void Display_SendEvent(void) {
    uint8_t chars[8];
    uint8_t out_chars[9];
    uint8_t dp_hex;
    uint8_t out_dp_hex;

    BuildCurrentDisplay(chars, &dp_hex);
    if (g.disp.reversed) {
        Display_FormatBufferForProtocol(chars, 8, out_chars);
        out_dp_hex = ReverseDpBitmap(dp_hex);
    } else {
        memcpy(out_chars, chars, 8);
        out_chars[8] = '\0';
        out_dp_hex = dp_hex;
    }

    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:DISP ");
    UARTStringPutNOBlocking(UART0_BASE, out_chars);
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)" ");
    UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(out_dp_hex >> 4)));
    UARTCharPutBlocking(UART0_BASE, HexDigit(out_dp_hex));
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
    g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
}

void Display_SendLedEvent(void) {
    uint8_t pattern;

    /* 读取当前 LED 输出值 — 优先 takeover 模式，否则用最后一次计算的状态 */
    if (g.disp.led_takeover) {
        pattern = g.disp.led_pattern;
    } else
        pattern = g.disp.current_led;

    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:LED ");
    UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(pattern >> 4)));
    UARTCharPutBlocking(UART0_BASE, HexDigit(pattern));
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
    g.disp.last_sent_led = pattern;
}

void Display_SendModeEvent(const char* state) {
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:MODE ");
    while (*state)
        UARTCharPutBlocking(UART0_BASE, (uint8_t)(*state++));
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
    g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
}

void Display_SendEditEvent(const char* type, const uint8_t* value) {
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:EDIT ");
    while (*type)
        UARTCharPutBlocking(UART0_BASE, (uint8_t)(*type++));
    UARTCharPutBlocking(UART0_BASE, ' ');
    while (*value)
        UARTCharPutBlocking(UART0_BASE, *value++);
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
    g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
}

void Display_StartMessage(const uint8_t* text, uint8_t len) {
    if (len > 32)
        len = 32;

    memcpy(g.disp.msg_buf, text, len);
    g.disp.msg_buf[len] = '\0';
    g.disp.msg_len = len;
    g.disp.msg_shift = 0;
    g.disp.msg_active = true;
    g.disp.msg_scroll = (len > 8);
    g.disp.msg_start = g.timer.tick;
    g.disp.msg_last_shift = g.timer.tick;
    g.disp.shifting = false;
    g.disp.on = true;
    Display_SendEvent();
}

void Display_StopMessage(void) {
    g.disp.msg_active = false;
    g.disp.msg_scroll = false;
    g.disp.msg_len = 0;
    g.disp.msg_shift = 0;
    g.disp.shifting = true;
    UpdateTimeAndDisplayBuffers();
}

// 更新7段数码管显示
void Update7SegmentDisplay(void) {
    uint8_t segment_data;           // 当前数码管段码
    uint8_t local_cnt;              // 本地数码管计数
    uint8_t display_cnt;            // 考虑FORMAT后的显示位置
    uint8_t effective_segment_data; // 实际发送的段码

    if (!g.disp.on) // 如果7段数码管关闭
    {
        // 关闭所有数码管
        g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
        g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, 0x00);
        Display_UpdateStatusLeds();
        return;
    }

    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00); // 确保所有位选都关闭

    local_cnt = g.disp.cnt; // 获取当前轮询的数码管索引
    display_cnt = g.disp.reversed ? (uint8_t)(7 - local_cnt) : local_cnt;

    if (g.disp.msg_active) {
        uint8_t msg_index = (uint8_t)(g.disp.msg_shift + local_cnt);
        if (msg_index < g.disp.msg_len)
            segment_data = SegmentForChar(g.disp.msg_buf[msg_index]);
        else
            segment_data = 0x00;
    } else if (g.disp.night_mode) {
        if (local_cnt == 0)
            segment_data = g.disp.seg7[g.clock.hh / 10];
        else if (local_cnt == 1)
            segment_data = g.disp.seg7[g.clock.hh % 10] | 0x80;
        else if (local_cnt == 2)
            segment_data = g.disp.seg7[g.clock.mm / 10];
        else if (local_cnt == 3)
            segment_data = g.disp.seg7[g.clock.mm % 10];
        else
            segment_data = 0x00;
    } else if (g.disp.mode == MODE_FLOWING) // 流动显示模式
    {
        if (g.disp.main_disp == MAIN_DISPLAY_FLOW) {
            segment_data = g.disp.master_buf[(g.disp.shift + local_cnt) % 18];
        } else if (g.disp.main_disp == MAIN_DISPLAY_TIME) {
            if (g.disp.reversed) {
                if (display_cnt == 0 || display_cnt == 7)
                    segment_data = 0x00;
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[g.clock.hh / 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.hh % 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.mm / 10] | 0x80;
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.mm % 10];
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.ss / 10] | 0x80;
                else
                    segment_data = g.disp.seg7[g.clock.ss % 10];
            } else {
                if (display_cnt == 0 || display_cnt == 7)
                    segment_data = 0x00;
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[g.clock.hh / 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.hh % 10] | 0x80;
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.mm / 10];
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.mm % 10] | 0x80;
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.ss / 10];
                else
                    segment_data = g.disp.seg7[g.clock.ss % 10];
            }
        } else if (g.disp.main_disp == MAIN_DISPLAY_DATE) {
            if (g.disp.reversed) {
                if (display_cnt == 0 || display_cnt == 7)
                    segment_data = 0x00;
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[(g.clock.year / 10) % 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.year % 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.month / 10] | 0x80;
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.month % 10];
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.day / 10] | 0x80;
                else
                    segment_data = g.disp.seg7[g.clock.day % 10];
            } else {
                if (display_cnt == 0 || display_cnt == 7)
                    segment_data = 0x00;
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[(g.clock.year / 10) % 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.year % 10] | 0x80;
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.month / 10];
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.month % 10] | 0x80;
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.day / 10];
                else
                    segment_data = g.disp.seg7[g.clock.day % 10];
            }
        } else if (g.disp.main_disp == MAIN_DISPLAY_YEAR) {
            if (g.disp.reversed) {
                if (display_cnt == 0)
                    segment_data = g.disp.seg7[(g.clock.year / 1000) % 10];
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[(g.clock.year / 100) % 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[(g.clock.year / 10) % 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.year % 10];
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.month / 10] | 0x80;
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.month % 10];
                else if (display_cnt == 6)
                    segment_data = g.disp.seg7[g.clock.day / 10] | 0x80;
                else
                    segment_data = g.disp.seg7[g.clock.day % 10];
            } else {
                if (display_cnt == 0)
                    segment_data = g.disp.seg7[(g.clock.year / 1000) % 10];
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[(g.clock.year / 100) % 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[(g.clock.year / 10) % 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.year % 10] | 0x80;
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.month / 10];
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.month % 10] | 0x80;
                else if (display_cnt == 6)
                    segment_data = g.disp.seg7[g.clock.day / 10];
                else
                    segment_data = g.disp.seg7[g.clock.day % 10];
            }
        }
    } else if (g.disp.mode == MODE_DATE_SET) // 日期设置模式
    {
        if (g.disp.reversed) {
            if (display_cnt == 0)
                segment_data = g.disp.seg7[(g.clock.temp_year / 1000) % 10];
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[(g.clock.temp_year / 100) % 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[(g.clock.temp_year / 10) % 10];
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[(g.clock.temp_year % 10)];
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_month / 10] | 0x80;
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_month % 10];
            else if (display_cnt == 6)
                segment_data = g.disp.seg7[g.clock.temp_day / 10] | 0x80;
            else
                segment_data = g.disp.seg7[g.clock.temp_day % 10];
        } else {
            if (display_cnt == 0)
                segment_data = g.disp.seg7[(g.clock.temp_year / 1000) % 10];
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[(g.clock.temp_year / 100) % 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[(g.clock.temp_year / 10) % 10];
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[(g.clock.temp_year % 10)] | 0x80;
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_month / 10];
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_month % 10] | 0x80;
            else if (display_cnt == 6)
                segment_data = g.disp.seg7[g.clock.temp_day / 10];
            else
                segment_data = g.disp.seg7[g.clock.temp_day % 10];
        }
        if (g.disp.blinking && (g.timer.tick % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS)) >= BLINK_ON_TIME_MS) {
            if ((g.disp.field == FIELD_YEAR && display_cnt <= 3) ||
                (g.disp.field == FIELD_MONTH && display_cnt >= 4 && display_cnt <= 5) ||
                (g.disp.field == FIELD_DAY && display_cnt >= 6 && display_cnt <= 7)) {
                segment_data = 0x00;
            }
        }
    } else if (g.disp.mode == MODE_TIME_SET) // 时间设置模式
    {
        if (g.disp.reversed) {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[g.clock.temp_hh / 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[g.clock.temp_hh % 10];
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[g.clock.temp_mm / 10] | 0x80;
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_mm % 10];
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_ss / 10] | 0x80;
            else
                segment_data = g.disp.seg7[g.clock.temp_ss % 10];
        } else {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[g.clock.temp_hh / 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[g.clock.temp_hh % 10] | 0x80;
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[g.clock.temp_mm / 10];
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_mm % 10] | 0x80;
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_ss / 10];
            else
                segment_data = g.disp.seg7[g.clock.temp_ss % 10];
        }
        if (g.disp.blinking && (g.timer.tick % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS)) >= BLINK_ON_TIME_MS) {
            if ((g.disp.field == FIELD_HOUR && display_cnt >= 1 && display_cnt <= 2) ||
                (g.disp.field == FIELD_MINUTE && display_cnt >= 3 && display_cnt <= 4) ||
                (g.disp.field == FIELD_SECOND && display_cnt >= 5 && display_cnt <= 6)) {
                segment_data = 0x00;
            }
        }
    } else if (g.disp.mode == MODE_ALARM_SET) // 闹钟设置模式
    {
        if (g.disp.reversed) {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[g.clock.temp_alm_hh / 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[g.clock.temp_alm_hh % 10];
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[g.clock.temp_alm_mm / 10] | 0x80;
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_alm_mm % 10];
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_alm_ss / 10] | 0x80;
            else
                segment_data = g.disp.seg7[g.clock.temp_alm_ss % 10];
        } else {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[g.clock.temp_alm_hh / 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[g.clock.temp_alm_hh % 10] | 0x80;
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[g.clock.temp_alm_mm / 10];
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_alm_mm % 10] | 0x80;
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_alm_ss / 10];
            else
                segment_data = g.disp.seg7[g.clock.temp_alm_ss % 10];
        }
        if (g.disp.blinking && (g.timer.tick % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS)) >= BLINK_ON_TIME_MS) {
            if ((g.disp.field == FIELD_ALARM_HOUR && display_cnt >= 1 && display_cnt <= 2) ||
                (g.disp.field == FIELD_ALARM_MINUTE && display_cnt >= 3 && display_cnt <= 4) ||
                (g.disp.field == FIELD_ALARM_SECOND && display_cnt >= 5 && display_cnt <= 6)) {
                segment_data = 0x00;
            }
        }
    } else if (g.disp.mode == MODE_ALARM_DISPLAY) // 闹钟显示模式
    {
        if (g.disp.reversed) {
            /* FORMAT RIGHT: AL + time 整体逆序 */
            if (g.clock.alm_hh == 25) {
                if (display_cnt <= 5)
                    segment_data = g.disp.seg7[17];
                else if (display_cnt == 6)
                    segment_data = 0x38; // 'L'
                else
                    segment_data = g.disp.seg7[10]; // 'A'
            } else {
                if (display_cnt == 0)
                    segment_data = g.disp.seg7[g.clock.alm_ss % 10];
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[g.clock.alm_ss / 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.alm_mm % 10] | 0x80;
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.alm_mm / 10];
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.alm_hh % 10] | 0x80;
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.alm_hh / 10];
                else if (display_cnt == 6)
                    segment_data = 0x38; // 'L'
                else
                    segment_data = g.disp.seg7[10]; // 'A'
            }
        } else {
            if (g.clock.alm_hh == 25) {
                if (display_cnt <= 1)
                    segment_data = (display_cnt == 0) ? g.disp.seg7[10] : (uint8_t)0x38;
                else
                    segment_data = g.disp.seg7[17];
            } else {
                if (display_cnt == 0)
                    segment_data = g.disp.seg7[10];
                else if (display_cnt == 1)
                    segment_data = 0x38;
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.alm_hh / 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.alm_hh % 10] | 0x80;
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.alm_mm / 10];
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.alm_mm % 10] | 0x80;
                else if (display_cnt == 6)
                    segment_data = g.disp.seg7[g.clock.alm_ss / 10];
                else
                    segment_data = g.disp.seg7[g.clock.alm_ss % 10];
            }
        }
    } else {
        segment_data = 0x00;
    }

    effective_segment_data = segment_data;

    // 处理保存成功后的闪烁效果
    if (g.disp.save_blink_active && (g.timer.tick - g.disp.save_blink_timer) % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS) >=
        BLINK_ON_TIME_MS) {
        effective_segment_data = 0x00; // 闪烁关闭时显示空白
    }

    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, effective_segment_data); // 发送段码
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, g.disp.rightshift);      // 发送位选
    Display_UpdateStatusLeds();

    g.disp.cnt++;                               // 切换到下一个数码管
    g.disp.rightshift = g.disp.rightshift << 1; // 移位位选
    if (g.disp.cnt >= 0x8)                      // 8个数码管循环
    {
        g.disp.rightshift = 0x01;
        g.disp.cnt = 0;
    }
}

// 更新显示移位效果
void UpdateDisplayShift(void) {
    if (g.disp.msg_active) {
        if (g.disp.msg_scroll) {
            if ((g.timer.tick - g.disp.msg_last_shift) >= V_T300ms) {
                g.disp.msg_last_shift = g.timer.tick;
                g.disp.msg_shift++;
                if (g.disp.msg_shift > (int8_t)g.disp.msg_len) {
                    Display_StopMessage();
                } else {
                    Display_SendEvent();
                }
            }
        } else if ((g.timer.tick - g.disp.msg_start) >= MESSAGE_STATIC_MS) {
            Display_StopMessage();
        }
        return;
    }

    if (g.disp.shift_mode == false) // 左移模式
    {
        g.disp.shift++;     // 移位量增加
        g.disp.shift %= 18; // 0-17循环
    } else                  // 右移模式
    {
        g.disp.shift--; // 移位量减少
        if (g.disp.shift < 0)
            g.disp.shift = 17; // 负数则回到17
    }
}

// 更新时间、日期和显示缓冲区 供UART/数码管参考
void UpdateTimeAndDisplayBuffers(void) {
    // 格式化时间字符串
    g.disp.time_buf[0] = (uint8_t)(g.clock.hh / 10) + '0';
    g.disp.time_buf[1] = (uint8_t)(g.clock.hh % 10) + '0';
    g.disp.time_buf[2] = ':';
    g.disp.time_buf[3] = (uint8_t)(g.clock.mm / 10) + '0';
    g.disp.time_buf[4] = (uint8_t)(g.clock.mm % 10) + '0';
    g.disp.time_buf[5] = ':';
    g.disp.time_buf[6] = (uint8_t)(g.clock.ss / 10) + '0';
    g.disp.time_buf[7] = (uint8_t)(g.clock.ss % 10) + '0';
    g.disp.time_buf[8] = '\0';

    // 格式化闹钟时间字符串，如果未设置则显示"xx:xx:xx"
    if (g.clock.alm_hh == 25) {
        memcpy(g.disp.alarm_buf, "xx:xx:xx", 8);
    } else {
        g.disp.alarm_buf[0] = (uint8_t)(g.clock.alm_hh / 10) + '0';
        g.disp.alarm_buf[1] = (uint8_t)(g.clock.alm_hh % 10) + '0';
        g.disp.alarm_buf[2] = ':';
        g.disp.alarm_buf[3] = (uint8_t)(g.clock.alm_mm / 10) + '0';
        g.disp.alarm_buf[4] = (uint8_t)(g.clock.alm_mm % 10) + '0';
        g.disp.alarm_buf[5] = ':';
        g.disp.alarm_buf[6] = (uint8_t)(g.clock.alm_ss / 10) + '0';
        g.disp.alarm_buf[7] = (uint8_t)(g.clock.alm_ss % 10) + '0';
    }
    g.disp.alarm_buf[8] = '\0';

    // 格式化日期字符串
    g.disp.date_buf[0] = (uint8_t)((g.clock.year / 1000) % 10) + '0';
    g.disp.date_buf[1] = (uint8_t)((g.clock.year / 100) % 10) + '0';
    g.disp.date_buf[2] = (uint8_t)((g.clock.year / 10) % 10) + '0';
    g.disp.date_buf[3] = (uint8_t)(g.clock.year % 10) + '0';
    g.disp.date_buf[4] = '-';
    g.disp.date_buf[5] = (uint8_t)(g.clock.month / 10) + '0';
    g.disp.date_buf[6] = (uint8_t)(g.clock.month % 10) + '0';
    g.disp.date_buf[7] = '-';
    g.disp.date_buf[8] = (uint8_t)(g.clock.day / 10) + '0';
    g.disp.date_buf[9] = (uint8_t)(g.clock.day % 10) + '0';
    g.disp.date_buf[10] = '\0';

    // 填充主显示缓冲区，用于7段数码管滚动显示
    g.disp.master_buf[0] = g.disp.seg7[(g.clock.year / 1000) % 10];
    g.disp.master_buf[1] = g.disp.seg7[(g.clock.year / 100) % 10];
    g.disp.master_buf[2] = g.disp.seg7[(g.clock.year / 10) % 10];
    g.disp.master_buf[3] = g.disp.seg7[(g.clock.year % 10)] | 0x80; // 年份末位带小数点
    g.disp.master_buf[4] = g.disp.seg7[g.clock.month / 10];
    g.disp.master_buf[5] = g.disp.seg7[g.clock.month % 10] | 0x80; // 月份末位带小数点
    g.disp.master_buf[6] = g.disp.seg7[g.clock.day / 10];
    g.disp.master_buf[7] = g.disp.seg7[g.clock.day % 10];

    g.disp.master_buf[8] = 0x00; // 分隔符
    g.disp.master_buf[9] = 0x00; // 分隔符

    g.disp.master_buf[10] = g.disp.seg7[g.clock.hh / 10];
    g.disp.master_buf[11] = g.disp.seg7[g.clock.hh % 10] | 0x80; // 小时末位带小数点
    g.disp.master_buf[12] = g.disp.seg7[g.clock.mm / 10];
    g.disp.master_buf[13] = g.disp.seg7[g.clock.mm % 10] | 0x80; // 分钟末位带小数点
    g.disp.master_buf[14] = g.disp.seg7[g.clock.ss / 10];
    g.disp.master_buf[15] = g.disp.seg7[g.clock.ss % 10];

    g.disp.master_buf[16] = 0x00; // 填充
    g.disp.master_buf[17] = 0x00; // 填充

    // 存储当前时间到休眠模块的RTC存储
    g.rtc.stored[0] = (uint32_t)g.clock.hh;
    g.rtc.stored[1] = (uint32_t)g.clock.mm;
    g.rtc.stored[2] = (uint32_t)g.clock.ss;
    g.rtc.stored[3] = HibernateRTCGet(); // 存储当前RTC计数值
    HibernateDataSet(g.rtc.stored, 4);   // 保存数据
    if (!g.disp.msg_active) {
        Display_SendEvent();
        Display_SendLedEvent();
    }
}

/* ===== boot_sequence.c ===== */

static const uint8_t kStudentIdFirst[8] = { 0x6D, 0x5B, 0x66, 0x3F, 0x4F, 0x06, 0x6F, 0x06 };  // 52403191
static const uint8_t kStudentIdSecond[8] = { 0x4F, 0x06, 0x6F, 0x06, 0x3F, 0x07, 0x07, 0x5B }; // 31910772
static const uint8_t kNameXuHaoran[8] = { 0x76, 0x3E, 0x76, 0x77, 0x3F, 0x50, 0x77, 0x54 };    // XUHAORAN
static const uint8_t kVersion[8] = { 0x00, 0x00, 0x3E, 0x06 | 0x80, 0x3F, 0x00, 0x00, 0x00 };  // v1.0

static void RestoreRtcOrDefaultTime(void) {
    uint8_t max_days_for_current_month;
    uint32_t past_sec;

    g.rtc.current = HibernateRTCGet();
    HibernateDataGet(g.rtc.fetched, 4);

    if (g.rtc.fetched[3] != 0 && g.rtc.current >= g.rtc.fetched[3]) {
        past_sec = g.rtc.current - g.rtc.fetched[3];
        g.clock.hh = (int8_t)g.rtc.fetched[0];
        g.clock.mm = (int8_t)g.rtc.fetched[1];
        g.clock.ss = (int8_t)g.rtc.fetched[2];

        while (past_sec > 0) {
            DateTime_TickOneSecond();
            past_sec--;
        }
    } else {
        g.clock.hh = 0;
        g.clock.mm = 0;
        g.clock.ss = 0;
        g.clock.year = 2025;
        g.clock.month = 6;
        g.clock.day = 3;
        g.clock.alm_hh = 25;
        g.clock.alm_mm = 0;
        g.clock.alm_ss = 0;
    }

    max_days_for_current_month = g.rtc.days_in_month[g.clock.month];
    if (g.clock.month == 2 && is_leap_year(g.clock.year))
        max_days_for_current_month = 29;
    if (!is_valid_date(g.clock.year, g.clock.month, g.clock.day) || g.clock.day > max_days_for_current_month) {
        g.clock.year = 2025;
        g.clock.month = 6;
        g.clock.day = 3;
    }
}

static void FinishBootSequence(void) {
    /* ── 显示扫描/动画状态重置 ── */
    g.disp.init_flag = false;
    g.disp.init_step = 0;
    g.disp.shift = 0;
    g.disp.rightshift = 0x01;
    g.disp.cnt = 0;

    /* ── 显示模式：正序、左移、流动、时间 ── */
    g.disp.reversed = false;
    g.disp.shift_mode = false;
    g.disp.shift_speed = false;
    g.disp.mode = MODE_FLOWING;
    g.disp.field = FIELD_NONE;
    g.disp.main_disp = MAIN_DISPLAY_TIME;

    /* ── 显示开关 ── */
    g.disp.on = true;
    g.disp.shifting = true;
    g.disp.blinking = false;

    /* ── 夜间模式 / LED 接管 / 天气 ── */
    g.disp.night_mode = false;
    g.disp.led_takeover = false;
    g.disp.led_pattern = 0x00;
    g.disp.weather_code = 0x00;
    g.disp.beep_until = 0;

    /* ── 消息 / 保存闪烁 / 长按 ── */
    g.disp.msg_active = false;
    g.disp.msg_scroll = false;
    g.disp.save_blink_active = false;
    g.disp.long_press_saving = false;

    /* ── 闹钟运行时状态 ── */
    g.disp.alarm_ringing = false;
    g.disp.alarm_beep_on = false;

    /* ── 时钟原始值备份 (用于回滚) ── */
    g.clock.original_year = g.clock.year;
    g.clock.original_month = g.clock.month;
    g.clock.original_day = g.clock.day;
    g.clock.original_hh = g.clock.hh;
    g.clock.original_mm = g.clock.mm;
    g.clock.original_ss = g.clock.ss;
    g.clock.original_alm_hh = g.clock.alm_hh;
    g.clock.original_alm_mm = g.clock.alm_mm;
    g.clock.original_alm_ss = g.clock.alm_ss;
    g.clock.unsaved_changes_active = false;

    UpdateTimeAndDisplayBuffers();
    Melody_Start(); // 开机旋律: See You Again
}

static void OutputBootFrame(const uint8_t frame[8], uint8_t led_pattern) {
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, frame[g.disp.cnt]);
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, g.disp.rightshift);
    Display_SetLedOutput(led_pattern);
}

void RunInitializationSequence(void) {
    static bool rtc_restored = false;
    static const uint8_t all_on_frame[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const uint8_t blank_frame[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    if (!rtc_restored) {
        RestoreRtcOrDefaultTime();
        rtc_restored = true;
    }

    if (g.timer.flag_2ms == true) {
        g.timer.flag_2ms = false;

        switch (g.disp.init_step) {
            case 0:
                OutputBootFrame(all_on_frame, 0xFF);
                break;
            case 1:
                OutputBootFrame(blank_frame, 0x00);
                break;
            // case 2:
                // OutputBootFrame(kStudentIdFirst, 0x00);
                // break;
            case 2:
                OutputBootFrame(kStudentIdSecond, 0x00);
                break;
            case 3:
                OutputBootFrame(kNameXuHaoran, 0x00);
                break;
            case 4:
                OutputBootFrame(kVersion, 0x00);
                break;
            default:
                FinishBootSequence();
                break;
        }

        g.disp.cnt++;
        g.disp.rightshift = (uint8_t)(g.disp.rightshift << 1);
        if (g.disp.cnt >= 0x8) {
            g.disp.rightshift = 0x01;
            g.disp.cnt = 0;
        }
    }

    if (g.timer.flag_900ms == true) {
        g.timer.flag_900ms = false;
        g.disp.init_step++;
    }
}

/* ===== button_logic.c ===== */

static void EnterNextEditMode(void);
static void CycleSettingField(void);
static void SaveCurrentSettingsAndExit(void);
static void SwitchMainDisplay(void);
static void ToggleDisplayFormat(void);
static void HandleButtonShortPress(uint8_t button_num);
static void HandleButtonLongPress(uint8_t button_num);
static void HandleButtonIncrement(bool is_long_press_repeat);

static bool alarm_was_unset_before_edit = false; // 记录进入闹钟编辑前是否未设置

// 遍历并处理所有按钮的事件
void ProcessButtonEvents(void) {
    int i = 0;

    for (i = 0; i < 8; ++i) {
        if (g.in.short_evt[i]) // 如果有短按事件
        {
            HandleButtonShortPress((uint8_t)(i + 1)); // 处理短按
            g.in.short_evt[i] = false;                // 清除标志
        }
        if (g.in.long_start_evt[i]) // 如果有长按开始事件
        {
            HandleButtonLongPress((uint8_t)(i + 1)); // 处理长按
            if (i != 2)                              // K3 ADD需要保持长按状态以产生重复按
            {
                g.in.long_start_evt[i] = false; // 清除标志
            }
        }
        if (g.in.repeat_evt[i]) // 如果有重复按事件
        {
            if (i == 2) // K3 ADD是递增按钮
            {
                HandleButtonIncrement(true); // 处理重复递增
            }
            g.in.repeat_evt[i] = false; // 清除标志
        }
    }

    if (g.in.user_short_evt[0]) {
        if (!g.in.suppress_key_events) {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:KEY USER1\r\n");
            g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
        }
        g.in.user_short_evt[0] = false;
    }
    if (g.in.user_short_evt[1]) {
        if (!g.in.suppress_key_events) {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:KEY USER2\r\n");
            g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
        }
        g.in.user_short_evt[1] = false;
    }
    g.in.suppress_key_events = false;
}

static void EnterNextEditMode(void) {
    system_mode_t old_mode = g.disp.mode;

    if (old_mode == MODE_FLOWING || old_mode == MODE_ALARM_DISPLAY) {
        g.clock.original_year = g.clock.year;
        g.clock.original_month = g.clock.month;
        g.clock.original_day = g.clock.day;
        g.clock.original_hh = g.clock.hh;
        g.clock.original_mm = g.clock.mm;
        g.clock.original_ss = g.clock.ss;
        g.clock.original_alm_hh = g.clock.alm_hh;
        g.clock.original_alm_mm = g.clock.alm_mm;
        g.clock.original_alm_ss = g.clock.alm_ss;
        g.clock.unsaved_changes_active = false;

        g.disp.mode = MODE_DATE_SET;
        g.clock.temp_year = g.clock.year;
        g.clock.temp_month = g.clock.month;
        g.clock.temp_day = g.clock.day;
        g.disp.prev_shifting = g.disp.shifting;
        g.disp.prev_shift_mode = g.disp.shift_mode;
        g.disp.prev_shift_speed = g.disp.shift_speed;
        g.disp.shifting = false;
        g.disp.field = FIELD_YEAR;
        g.disp.blinking = true;
        g.disp.on = true;
        Display_SendModeEvent("DATE");
    } else if (old_mode == MODE_DATE_SET) {
        g.disp.mode = MODE_TIME_SET;
        g.clock.temp_hh = g.clock.hh;
        g.clock.temp_mm = g.clock.mm;
        g.clock.temp_ss = g.clock.ss;
        g.disp.field = FIELD_HOUR;
        g.disp.blinking = true;
        g.disp.on = true;
        Display_SendModeEvent("TIME");
    } else if (old_mode == MODE_TIME_SET) {
        g.disp.mode = MODE_ALARM_SET;
        alarm_was_unset_before_edit = (g.clock.alm_hh == 25);
        g.clock.temp_alm_hh = (g.clock.alm_hh == 25) ? 0 : (uint8_t)g.clock.alm_hh;
        g.clock.temp_alm_mm = (uint8_t)g.clock.alm_mm;
        g.clock.temp_alm_ss = (uint8_t)g.clock.alm_ss;
        g.disp.field = FIELD_ALARM_HOUR;
        g.disp.blinking = true;
        g.disp.on = true;
        Display_SendModeEvent("ALARM");
    } else if (old_mode == MODE_ALARM_SET) {
        g.disp.mode = MODE_FLOWING;
        g.disp.field = FIELD_NONE;
        g.disp.blinking = false;
        g.disp.shifting = g.disp.prev_shifting;
        g.disp.shift_mode = g.disp.prev_shift_mode;
        g.disp.shift_speed = g.disp.prev_shift_speed;
        g.disp.on = true;
        Display_SendModeEvent("FLOWING");
    }
}

static void CycleSettingField(void) {
    if (g.disp.mode == MODE_DATE_SET) {
        if (g.disp.field == FIELD_YEAR)
            g.disp.field = FIELD_MONTH;
        else if (g.disp.field == FIELD_MONTH)
            g.disp.field = FIELD_DAY;
        else
            g.disp.field = FIELD_YEAR;
    } else if (g.disp.mode == MODE_TIME_SET) {
        if (g.disp.field == FIELD_HOUR)
            g.disp.field = FIELD_MINUTE;
        else if (g.disp.field == FIELD_MINUTE)
            g.disp.field = FIELD_SECOND;
        else
            g.disp.field = FIELD_HOUR;
    } else if (g.disp.mode == MODE_ALARM_SET) {
        if (g.disp.field == FIELD_ALARM_HOUR)
            g.disp.field = FIELD_ALARM_MINUTE;
        else if (g.disp.field == FIELD_ALARM_MINUTE)
            g.disp.field = FIELD_ALARM_SECOND;
        else
            g.disp.field = FIELD_ALARM_HOUR;
    } else {
        return;
    }

    g.disp.blinking = true;
    g.disp.on = true;
}

static void SaveCurrentSettingsAndExit(void) {
    bool restore_flow_state = (g.disp.mode != MODE_FLOWING);

    if (g.disp.mode == MODE_DATE_SET) {
        if (is_valid_date(g.clock.temp_year, g.clock.temp_month, g.clock.temp_day)) {
            g.clock.year = g.clock.temp_year;
            g.clock.month = g.clock.temp_month;
            g.clock.day = g.clock.temp_day;
            {
                uint8_t v[11];
                v[0] = (uint8_t)(g.clock.year / 1000 % 10) + '0';
                v[1] = (uint8_t)(g.clock.year / 100 % 10) + '0';
                v[2] = (uint8_t)(g.clock.year / 10 % 10) + '0';
                v[3] = (uint8_t)(g.clock.year % 10) + '0';
                v[4] = '.';
                v[5] = (uint8_t)(g.clock.month / 10) + '0';
                v[6] = (uint8_t)(g.clock.month % 10) + '0';
                v[7] = '.';
                v[8] = (uint8_t)(g.clock.day / 10) + '0';
                v[9] = (uint8_t)(g.clock.day % 10) + '0';
                v[10] = '\0';
                Display_SendEditEvent("DATE", v);
            }
        } else {
            g.clock.year = g.clock.original_year;
            g.clock.month = g.clock.original_month;
            g.clock.day = g.clock.original_day;
            /* invalid date — reverted silently */
        }
    } else if (g.disp.mode == MODE_TIME_SET) {
        if (is_valid_time(g.clock.temp_hh, g.clock.temp_mm, g.clock.temp_ss)) {
            g.clock.hh = g.clock.temp_hh;
            g.clock.mm = g.clock.temp_mm;
            g.clock.ss = g.clock.temp_ss;
            {
                uint8_t v[9];
                v[0] = (uint8_t)(g.clock.hh / 10) + '0';
                v[1] = (uint8_t)(g.clock.hh % 10) + '0';
                v[2] = '.';
                v[3] = (uint8_t)(g.clock.mm / 10) + '0';
                v[4] = (uint8_t)(g.clock.mm % 10) + '0';
                v[5] = '.';
                v[6] = (uint8_t)(g.clock.ss / 10) + '0';
                v[7] = (uint8_t)(g.clock.ss % 10) + '0';
                v[8] = '\0';
                Display_SendEditEvent("TIME", v);
            }
        } else {
            g.clock.hh = g.clock.original_hh;
            g.clock.mm = g.clock.original_mm;
            g.clock.ss = g.clock.original_ss;
            /* invalid time — reverted silently */
        }
    } else if (g.disp.mode == MODE_ALARM_SET) {
        if (alarm_was_unset_before_edit && g.clock.temp_alm_hh == 0 && g.clock.temp_alm_mm == 0 && g.clock.temp_alm_ss
            == 0) {
            // 闹钟原本未设置且用户未修改，保持未设置状态
            g.clock.alm_hh = 25;
            g.clock.alm_mm = 0;
            g.clock.alm_ss = 0;
            StopAlarmRinging(false);
            /* alarm kept unset */
        } else if (is_valid_time(g.clock.temp_alm_hh, g.clock.temp_alm_mm, g.clock.temp_alm_ss)) {
            g.clock.alm_hh = g.clock.temp_alm_hh;
            g.clock.alm_mm = g.clock.temp_alm_mm;
            g.clock.alm_ss = g.clock.temp_alm_ss;
            StopAlarmRinging(false);
            {
                uint8_t v[9];
                v[0] = (uint8_t)(g.clock.alm_hh / 10) + '0';
                v[1] = (uint8_t)(g.clock.alm_hh % 10) + '0';
                v[2] = '.';
                v[3] = (uint8_t)(g.clock.alm_mm / 10) + '0';
                v[4] = (uint8_t)(g.clock.alm_mm % 10) + '0';
                v[5] = '.';
                v[6] = (uint8_t)(g.clock.alm_ss / 10) + '0';
                v[7] = (uint8_t)(g.clock.alm_ss % 10) + '0';
                v[8] = '\0';
                Display_SendEditEvent("ALARM", v);
            }
        } else {
            g.clock.alm_hh = g.clock.original_alm_hh;
            g.clock.alm_mm = g.clock.original_alm_mm;
            g.clock.alm_ss = g.clock.original_alm_ss;
            /* invalid alarm — reverted silently */
        }
    } else {
        /* generic save — no structured edit event needed */
    }

    g.clock.original_year = g.clock.year;
    g.clock.original_month = g.clock.month;
    g.clock.original_day = g.clock.day;
    g.clock.original_hh = g.clock.hh;
    g.clock.original_mm = g.clock.mm;
    g.clock.original_ss = g.clock.ss;
    g.clock.original_alm_hh = g.clock.alm_hh;
    g.clock.original_alm_mm = g.clock.alm_mm;
    g.clock.original_alm_ss = g.clock.alm_ss;

    g.disp.mode = MODE_FLOWING;
    Display_SendModeEvent("FLOWING");
    g.disp.field = FIELD_NONE;
    g.disp.blinking = false;
    if (restore_flow_state) {
        g.disp.shifting = g.disp.prev_shifting;
        g.disp.shift_mode = g.disp.prev_shift_mode;
        g.disp.shift_speed = g.disp.prev_shift_speed;
    }
    g.clock.unsaved_changes_active = false;
    g.disp.long_press_saving = false;
    g.disp.save_blink_active = true;
    g.disp.save_blink_timer = g.timer.tick;
    g.disp.on = true;
}

static void SwitchMainDisplay(void) {
    if (g.disp.mode != MODE_FLOWING) {
        return;
    }

    if (g.disp.main_disp == MAIN_DISPLAY_FLOW) {
        g.disp.main_disp = MAIN_DISPLAY_TIME;
    } else if (g.disp.main_disp == MAIN_DISPLAY_TIME) {
        g.disp.main_disp = MAIN_DISPLAY_DATE;
    } else if (g.disp.main_disp == MAIN_DISPLAY_DATE) {
        g.disp.main_disp = MAIN_DISPLAY_YEAR;
    } else {
        g.disp.main_disp = MAIN_DISPLAY_FLOW;
    }
}

static void ToggleDisplayFormat(void) {
    if (g.disp.mode != MODE_FLOWING) {
        return;
    }

    g.disp.shift_mode = !g.disp.shift_mode;
    g.disp.reversed = g.disp.shift_mode;
}

// 根据按钮编号处理短按功能
static void HandleButtonShortPress(uint8_t button_num) {
    g.timer.mode_timeout = g.timer.tick; // 重置模式超时定时器

    if (g.disp.msg_active && button_num != 1) {
        Display_StopMessage();
        return;
    }

    switch (button_num) {
        case 1: // K1 FUNC: 响铃时停止；否则循环 date -> time -> alarm -> exit
            if (g.disp.alarm_ringing) {
                StopAlarmRinging(true);
            } else {
                EnterNextEditMode();
            }
            return;
        case 2: // K2 SHIFT
            CycleSettingField();
            return;
        case 3: // K3 ADD
            if (g.disp.blinking) {
                HandleButtonIncrement(false);
            } else {}
            return;
        case 4: // K4 SAVE
            SaveCurrentSettingsAndExit();
            return;
        case 5: // K5 DISP
            SwitchMainDisplay();
            return;
        case 6: // K6 SPEED
            if (g.disp.mode == MODE_FLOWING) {
                g.disp.shift_speed = !g.disp.shift_speed;
            } else {}
            return;
        case 7: // K7 FORMAT
            ToggleDisplayFormat();
            return;
        case 8: // K8 EXT
            if (!g.in.suppress_key_events) {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"*EVT:KEY EXT\r\n");
                g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
            }
            return;
        default:
            return;
    }
}

// 根据按钮编号处理长按功能
static void HandleButtonLongPress(uint8_t button_num) {
    g.timer.mode_timeout = g.timer.tick; // 重置模式超时定时器

    if (button_num == 1) // K1 FUNC长按: 保存并退出
    {
        SaveCurrentSettingsAndExit();
    }
    return;
}

// 处理参数递增逻辑
static void HandleButtonIncrement(bool is_long_press_repeat) {
    uint8_t current_days_in_month;

    (void)is_long_press_repeat;
    g.timer.mode_timeout = g.timer.tick; // 重置模式超时定时器

    if (g.disp.long_press_saving) // 如果正在保存，则不能递增
    {
        return;
    }

    if (!g.disp.blinking) // 如果没有字段在闪烁，则不能递增
    {
        return;
    }

    // 根据当前设置字段递增相应的值
    switch (g.disp.field) {
        case FIELD_YEAR: // 年份递增
            g.clock.temp_year++;
            if (g.clock.temp_year > 2099) // 2000-2099循环
                g.clock.temp_year = 2000;

            // 如果月份是2月，可能需要调整日期
            if (g.clock.temp_month == 2) {
                current_days_in_month = is_leap_year(g.clock.temp_year) ? 29 : 28;
                if (g.clock.temp_day > current_days_in_month)
                    g.clock.temp_day = current_days_in_month;
            }
            break;
        case FIELD_MONTH: // 月份递增
            g.clock.temp_month++;
            if (g.clock.temp_month > 12) // 1-12循环
                g.clock.temp_month = 1;

            // 根据新月份和年份调整日期
            current_days_in_month = g.rtc.days_in_month[g.clock.temp_month];
            if (g.clock.temp_month == 2 && is_leap_year(g.clock.temp_year))
                current_days_in_month = 29;
            if (g.clock.temp_day > current_days_in_month)
                g.clock.temp_day = current_days_in_month;
            break;
        case FIELD_DAY: // 日期递增
            g.clock.temp_day++;
            current_days_in_month = g.rtc.days_in_month[g.clock.temp_month];
            if (g.clock.temp_month == 2 && is_leap_year(g.clock.temp_year))
                current_days_in_month = 29;
            if (g.clock.temp_day > current_days_in_month) // 超过最大天数则回到1
                g.clock.temp_day = 1;
            break;
        case FIELD_HOUR: // 小时递增
            g.clock.temp_hh++;
            if (g.clock.temp_hh > 23) // 0-23循环
                g.clock.temp_hh = 0;
            break;
        case FIELD_MINUTE: // 分钟递增
            g.clock.temp_mm++;
            if (g.clock.temp_mm > 59) // 0-59循环
                g.clock.temp_mm = 0;
            break;
        case FIELD_SECOND: // 秒递增
            g.clock.temp_ss++;
            if (g.clock.temp_ss > 59) // 0-59循环
                g.clock.temp_ss = 0;
            break;
        case FIELD_ALARM_HOUR: // 闹钟小时递增
            g.clock.temp_alm_hh++;
            if (g.clock.temp_alm_hh > 23) // 0-23循环
                g.clock.temp_alm_hh = 0;
            break;
        case FIELD_ALARM_MINUTE: // 闹钟分钟递增
            g.clock.temp_alm_mm++;
            if (g.clock.temp_alm_mm > 59) // 0-59循环
                g.clock.temp_alm_mm = 0;
            break;
        case FIELD_ALARM_SECOND: // 闹钟秒递增
            g.clock.temp_alm_ss++;
            if (g.clock.temp_alm_ss > 59) // 0-59循环
                g.clock.temp_alm_ss = 0;
            break;
        default:
            break;
    }
}

// 处理模式超时，恢复到原始显示状态并放弃未保存的更改
void HandleModeTimeout(void) {
    // 只有在非流动模式或有未保存更改/正在长按保存时才处理超时
    if (g.disp.mode != MODE_FLOWING || g.clock.unsaved_changes_active == true || g.disp.long_press_saving == true) {
        // 恢复所有时间、日期、闹钟和显示设置到原始状态
        g.clock.year = g.clock.original_year;
        g.clock.month = g.clock.original_month;
        g.clock.day = g.clock.original_day;
        g.clock.hh = g.clock.original_hh;
        g.clock.mm = g.clock.original_mm;
        g.clock.ss = g.clock.original_ss;
        g.clock.alm_hh = g.clock.original_alm_hh;
        g.clock.alm_mm = g.clock.original_alm_mm;
        g.clock.alm_ss = g.clock.original_alm_ss;

        PWMStop(); // 停止闹钟

        g.disp.mode = MODE_FLOWING; // 切换回流动模式
        Display_SendModeEvent("FLOWING");
        g.disp.field = FIELD_NONE;                    // 清除设置字段
        g.disp.blinking = false;                      // 停止闪烁
        g.disp.shifting = g.disp.prev_shifting;       // 恢复之前的移位状态
        g.disp.shift_mode = g.disp.prev_shift_mode;   // 恢复之前的移位方向
        g.disp.shift_speed = g.disp.prev_shift_speed; // 恢复之前的移位速度
        g.clock.unsaved_changes_active = false;       // 清除未保存更改标志
        g.disp.long_press_saving = false;             // 清除长按保存标志
        g.disp.on = true;                             // 确保显示打开
    }
}

/* ===== command.c ===== */

// 将字符转换为大写
static uint8_t toUpper(uint8_t c) {
    if (c >= 'a' && c <= 'z') {
        return (uint8_t)(c - 0x20);
    }
    return c;
}

// 统计命令字符串中必输字符数 ('*' 和大写字母)
static uint8_t CountMandatory(const char* str) {
    uint8_t count = 0;
    while (*str) {
        if (*str == '*' || (*str >= 'A' && *str <= 'Z'))
            count++;
        str++;
    }
    return count;
}

static int8_t HexValue(uint8_t c) {
    if (c >= '0' && c <= '9')
        return (int8_t)(c - '0');
    c = toUpper(c);
    if (c >= 'A' && c <= 'F')
        return (int8_t)(c - 'A' + 10);
    return -1;
}

static bool ParseHexByte(const command_token_t* token, uint8_t* value) {
    int8_t hi;
    int8_t lo;

    if (token->token_len != 2)
        return false;

    hi = HexValue(token->token_str[0]);
    lo = HexValue(token->token_str[1]);
    if (hi < 0 || lo < 0)
        return false;

    *value = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)lo);
    return true;
}

static void PutProtocolBuffer(const uint8_t* buffer, uint8_t len) {
    uint8_t formatted[12];
    uint8_t normalized[12];
    uint8_t i;

    for (i = 0; i < len; ++i) {
        normalized[i] = (buffer[i] == ':' || buffer[i] == '-') ? '.' : buffer[i];
    }
    normalized[len] = '\0';

    Display_FormatBufferForProtocol(normalized, len, formatted);
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK ");
    UARTStringPutNOBlocking(UART0_BASE, formatted);
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
}

static void ResetProtocolState(void) {
    g.disp.on = true;
    g.disp.shift_mode = false;
    g.disp.reversed = false;
    g.disp.night_mode = false;
    g.disp.led_takeover = false;
    g.disp.led_pattern = 0x00;
    g.disp.weather_code = 0x00;
    g.disp.beep_until = 0;
    g.disp.ntp_synced = false;
    g.disp.msg_active = false;
    g.disp.msg_scroll = false;
    g.disp.msg_len = 0;
    g.disp.msg_shift = 0;
    g.disp.shifting = true;
    g.disp.mode = MODE_FLOWING;
    g.disp.field = FIELD_NONE;
    g.disp.blinking = false;
    g.disp.main_disp = MAIN_DISPLAY_TIME;
    Display_SendModeEvent("FLOWING");
    StopAlarmRinging(false);
    UpdateTimeAndDisplayBuffers();
    Display_UpdateStatusLeds();
}

static uint8_t FindRawPayloadOffset(uint8_t token_idx) {
    uint8_t i;
    uint8_t current_token = 0;
    bool in_token = false;

    for (i = 0; i < g.uart.rx_len; ++i) {
        if (g.uart.rx_buf[i] != ' ' && g.uart.rx_buf[i] != '\t') {
            if (!in_token) {
                if (current_token == token_idx)
                    return i;
                current_token++;
                in_token = true;
            }
        } else {
            in_token = false;
        }
    }
    return g.uart.rx_len;
}

// 比较命令Token与字符串，支持最小匹配长度
bool compareTokens(const command_token_t* token, const char* str_literal, uint8_t min_match_len) {
    uint8_t i;
    uint8_t literal_len;

    literal_len = 0;
    while (str_literal[literal_len] != '\0') {
        literal_len++;
    }

    // Token长度必须在最小匹配长度和字面量长度之间
    if (token->token_len < min_match_len || token->token_len > literal_len) {
        return false;
    }

    // 逐个字符比较 (不区分大小写)
    for (i = 0; i < token->token_len; ++i) {
        if (toUpper(token->token_str[i]) != toUpper((uint8_t)str_literal[i])) {
            return false;
        }
    }
    return true;
}

// 比较字段关键字（如"YEAR", "MONTH"），支持最小匹配长度
static bool compareFieldKeyword(const command_token_t* token, const char* full_keyword, uint8_t min_match_len) {
    uint8_t i;
    uint8_t full_keyword_len = (uint8_t)strlen(full_keyword);

    // Token长度必须大于等于最小匹配长度
    if (token->token_len < min_match_len) {
        return false;
    }

    // Token长度不能超过完整关键字长度
    if (token->token_len > full_keyword_len) {
        return false;
    }

    // 逐个字符比较 (不区分大小写)
    for (i = 0; i < token->token_len; ++i) {
        if (toUpper(token->token_str[i]) != toUpper((uint8_t)full_keyword[i])) {
            return false;
        }
    }
    return true;
}

// 特殊的Token比较函数，用于处理带冒号前缀的Token

// 特殊的Token比较函数，用于处理带冒号前缀的Token
static bool compareTokens_modified_for_colon_prefix(const uint8_t* token_str_ptr,
                                                    uint8_t token_len,
                                                    const char* str_literal,
                                                    uint8_t min_match_len) {
    uint8_t i;
    uint8_t literal_len;

    literal_len = (uint8_t)strlen(str_literal);

    // Token长度必须在最小匹配长度和字面量长度之间
    if (token_len < min_match_len || token_len > literal_len) {
        return false;
    }

    // 逐个字符比较 (不区分大小写)
    for (i = 0; i < token_len; ++i) {
        if (toUpper(token_str_ptr[i]) != toUpper((uint8_t)str_literal[i])) {
            return false;
        }
    }
    return true;
}

// 匹配命令，支持形如 `*CMD:SUB_CMD` 的命令格式
static bool matchCommand(const command_token_t* t0,
                         const command_token_t* t1,
                         uint8_t num_tokens_total,
                         const char* cmd_full_str) {
    uint8_t colon_idx = 0;
    uint8_t i;
    bool has_colon_in_full_str = false;
    uint8_t literal_len_full_str;
    const char* prefix_str_literal;
    const char* suffix_str_literal;

    literal_len_full_str = 0;
    while (cmd_full_str[literal_len_full_str] != '\0') {
        literal_len_full_str++;
    }

    // 查找完整命令字符串中的冒号位置
    for (i = 0; i < literal_len_full_str; ++i) {
        if (cmd_full_str[i] == ':') {
            colon_idx = i;
            has_colon_in_full_str = true;
            break;
        }
    }

    if (!has_colon_in_full_str) // 如果命令字符串不包含冒号 (如 "*RST")
    {
        // 直接比较第一个Token与完整命令字符串 (支持缩写)
        return (num_tokens_total >= 1) && compareTokens(t0, cmd_full_str, CountMandatory(cmd_full_str));
    }

    // 如果命令字符串包含冒号 (如 "*SET:DATE")
    prefix_str_literal = cmd_full_str;                 // 冒号前的部分
    suffix_str_literal = cmd_full_str + colon_idx + 1; // 冒号后的部分

    // 首先尝试匹配整个命令作为一个Token (如 "*SET:DATE", 支持缩写)
    if (compareTokens(t0, cmd_full_str, CountMandatory(cmd_full_str))) {
        return true;
    }

    // 如果不匹配整个命令，则尝试匹配为两个Token (如 "*SET" 和 ":DATE" 或 "DATE")
    if (num_tokens_total >= 2 && t1 != NULL) {
        // 比较第一个Token与前缀部分 (支持缩写)
        if (!compareTokens(t0, cmd_full_str, CountMandatory(prefix_str_literal) + 1U /* colon */)) {
            return false;
        }

        // 比较第二个Token与后缀部分 (支持缩写)
        if (t1->token_len > 0 && t1->token_str[0] == ':') // 如果第二个Token以冒号开头 (如 ":DATE")
        {
            return compareTokens_modified_for_colon_prefix(t1->token_str + 1,
                                                           t1->token_len - 1,
                                                           suffix_str_literal,
                                                           CountMandatory(suffix_str_literal));
        } else // 如果第二个Token不以冒号开头 (如 "DATE")
        {
            return compareTokens(t1, suffix_str_literal, CountMandatory(suffix_str_literal));
        }
    }

    return false;
}

// 解析UART输入字符串为命令Token
static void ParseUartInput(void) {
    uint8_t i = 0;
    uint8_t current_token_len = 0;
    bool in_token = false; // 是否在Token中

    g.uart.num_tokens = 0; // 重置Token数量

    // 清空之前解析的Token
    for (i = 0; i < MAX_COMMAND_TOKENS; ++i) {
        g.uart.tokens[i].token_len = 0;
        memset(g.uart.tokens[i].token_str, 0, MAX_TOKEN_LENGTH);
    }

    i = 0;

    // 跳过开头的空格/Tab
    while (i < g.uart.rx_len && (g.uart.rx_buf[i] == ' ' || g.uart.rx_buf[i] == '\t')) {
        i++;
    }

    // 遍历接收缓冲区，解析Token (遇到 null 则停止，仅处理第一条命令)
    while (i < g.uart.rx_len && g.uart.rx_buf[i] != '\0' && g.uart.num_tokens < MAX_COMMAND_TOKENS) {
        if (g.uart.rx_buf[i] != ' ' && g.uart.rx_buf[i] != '\t') // 如果当前字符不是空格/Tab
        {
            if (!in_token) // 如果不在Token中，开始一个新Token
            {
                current_token_len = 0;
                in_token = true;
            }
            if (current_token_len < MAX_TOKEN_LENGTH - 1) // 检查Token长度是否超出限制
            {
                // 将字符添加到当前Token
                g.uart.tokens[g.uart.num_tokens].token_str[current_token_len] = g.uart.rx_buf[i];
                current_token_len++;
            }
        } else // 如果当前字符是空格/Tab
        {
            if (in_token) // 如果在Token中，则当前Token结束
            {
                g.uart.tokens[g.uart.num_tokens].token_len = current_token_len;
                g.uart.tokens[g.uart.num_tokens].token_str[current_token_len] = '\0'; // 字符串结束符
                g.uart.num_tokens++;                                                  // 增加Token数量
                in_token = false;                                                     // 退出Token状态
            }
            // 跳过连续的空格/Tab
            while (i + 1 < g.uart.rx_len && (g.uart.rx_buf[i + 1] == ' ' || g.uart.rx_buf[i + 1] == '\t')) {
                i++;
            }
        }
        i++;
    }

    // 处理最后一个Token (如果字符串不是以空格结束)
    if (in_token && g.uart.num_tokens < MAX_COMMAND_TOKENS) {
        g.uart.tokens[g.uart.num_tokens].token_len = current_token_len;
        g.uart.tokens[g.uart.num_tokens].token_str[current_token_len] = '\0';
        g.uart.num_tokens++;
    }
}

// 处理UART接收到的命令
void ProcessUartCommand(void) {
    uint8_t i;
    int parsed_val[6];         // 用于存储解析出的数值参数
    bool parse_ok;             // 解析是否成功
    uint8_t current_param_idx; // 当前参数的起始索引
    uint8_t field_token_idx;   // 字段Token的起始索引
    uint8_t val_token_idx;     // 值Token的起始索引
    bool found_arg;            // 是否找到有效参数

    if (g.uart.cmd_state == 0) // 如果没有新命令
    {
        return;
    }

    g.uart.cmd_state = 0; // 清除命令状态
    ParseUartInput();     // 解析UART输入

    // 确定参数的起始索引，处理形如 "*CMD:SUB_CMD" 的命令
    if (g.uart.num_tokens >= 2 && g.uart.tokens[0].token_str[0] == '*' && g.uart.tokens[1].token_len > 0 && g.uart.
        tokens[1].token_str[0] == ':') {
        current_param_idx = 2; // 参数从第三个Token开始
    } else {
        current_param_idx = 1; // 参数从第二个Token开始
    }

    /* 开机动画期间禁止修改状态，仅放行只读命令和系统命令 */
    if (g.disp.init_flag && g.uart.num_tokens > 0) {
        /* 检查首 Token 是否为 *GET: 前缀 (允许所有只读查询) */
        bool is_get = (g.uart.tokens[0].token_len >= 5 &&
            toUpper(g.uart.tokens[0].token_str[0]) == '*' &&
            toUpper(g.uart.tokens[0].token_str[1]) == 'G' &&
            toUpper(g.uart.tokens[0].token_str[2]) == 'E' &&
            toUpper(g.uart.tokens[0].token_str[3]) == 'T' &&
            (g.uart.tokens[0].token_str[4] == ':' ||
                (g.uart.num_tokens >= 2 && g.uart.tokens[1].token_len > 0 &&
                    g.uart.tokens[1].token_str[0] == ':')));
        if (!(compareTokens(&g.uart.tokens[0], "*PING", 5) ||
            compareTokens(&g.uart.tokens[0], "HELP", 4) ||
            compareTokens(&g.uart.tokens[0], "INIT", 4) ||
            is_get ||
            matchCommand(&g.uart.tokens[0],
                         (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                         g.uart.num_tokens,
                         "*RST"))) {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR BUSY\r\n");
            g.uart.rx_len = 0;
            memset(g.uart.rx_buf, 0, sizeof(g.uart.rx_buf));
            return;
        }
    }

    // 处理 "*RST" 命令 (复位)
    if (matchCommand(&g.uart.tokens[0],
                     (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                     g.uart.num_tokens,
                     "*RST")) {
        if (g.uart.num_tokens == current_param_idx) // 确保没有额外参数
        {
            ResetProtocolState();
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:DATE" 命令 (设置日期)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:DATE")) {
        bool syntax_matched = false; /* 是否有字段组合匹配 (区分 PARAM / RANGE) */
        parse_ok = false;
        field_token_idx = current_param_idx; // 字段Token的起始索引

        // 匹配 "YEAR MONTH DATE YYYY MM DD" 格式
        if (g.uart.num_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "YEAR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MONTH", 5) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 2], "DATE", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 3;                                     // 值Token的起始索引
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 年
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 月
            parsed_val[2] = atoi((char*)g.uart.tokens[val_token_idx + 2].token_str); // 日
            if (is_valid_date((uint16_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2])) {
                g.clock.year = (uint16_t)parsed_val[0];
                g.clock.month = (uint8_t)parsed_val[1];
                g.clock.day = (uint8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "YEAR MONTH YYYY MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "YEAR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MONTH", 5)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 年
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 月
            if (parsed_val[0] >= 2000 && parsed_val[0] <= 2099 && parsed_val[1] >= 1 && parsed_val[1] <= 12) {
                g.clock.year = (uint16_t)parsed_val[0];
                g.clock.month = (uint8_t)parsed_val[1];
                if (!is_valid_date(g.clock.year, g.clock.month, g.clock.day)) // 如果新年月导致日期无效，调整日期
                {
                    g.clock.day = (uint8_t)(is_leap_year(g.clock.year) && g.clock.month == 2
                        ? 29
                        : g.rtc.days_in_month[g.clock.month]);
                }
                parse_ok = true;
            }
        }

        // 匹配 "YEAR DATE YYYY DD" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "YEAR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "DATE", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 年
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 日
            if (is_valid_date((uint16_t)parsed_val[0], g.clock.month, (uint8_t)parsed_val[1])) {
                g.clock.year = (uint16_t)parsed_val[0];
                g.clock.day = (uint8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MONTH DATE MM DD" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "MONTH", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "DATE", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 月
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 日
            if (is_valid_date(g.clock.year, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1])) {
                g.clock.month = (uint8_t)parsed_val[0];
                g.clock.day = (uint8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "YEAR YYYY" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "YEAR", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str); // 年
            if (parsed_val[0] >= 2000 && parsed_val[0] <= 2099) {
                g.clock.year = (uint16_t)parsed_val[0];
                if (!is_valid_date(g.clock.year, g.clock.month, g.clock.day)) // 如果新年份导致日期无效，调整日期
                {
                    if (g.clock.month == 2) {
                        g.clock.day = (uint8_t)(is_leap_year(g.clock.year) ? 29 : 28);
                    }
                }
                parse_ok = true;
            }
        }

        // 匹配 "MONTH MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "MONTH", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str); // 月
            if (parsed_val[0] >= 1 && parsed_val[0] <= 12) {
                g.clock.month = (uint8_t)parsed_val[0];
                if (!is_valid_date(g.clock.year, g.clock.month, g.clock.day)) // 如果新月份导致日期无效，调整日期
                {
                    g.clock.day = g.rtc.days_in_month[g.clock.month];
                    if (g.clock.month == 2 && is_leap_year(g.clock.year)) {
                        g.clock.day = 29;
                    }
                }
                parse_ok = true;
            }
        }

        // 匹配 "DATE DD" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "DATE", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str); // 日
            if (is_valid_date(g.clock.year, g.clock.month, (uint8_t)parsed_val[0])) {
                g.clock.day = (uint8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，保存原始值并发送成功消息
        {
            uint8_t v[11];
            g.clock.original_year = g.clock.year;
            g.clock.original_month = g.clock.month;
            g.clock.original_day = g.clock.day;
            g.clock.unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            v[0] = (uint8_t)(g.clock.year / 1000 % 10) + '0';
            v[1] = (uint8_t)(g.clock.year / 100 % 10) + '0';
            v[2] = (uint8_t)(g.clock.year / 10 % 10) + '0';
            v[3] = (uint8_t)(g.clock.year % 10) + '0';
            v[4] = '.';
            v[5] = (uint8_t)(g.clock.month / 10) + '0';
            v[6] = (uint8_t)(g.clock.month % 10) + '0';
            v[7] = '.';
            v[8] = (uint8_t)(g.clock.day / 10) + '0';
            v[9] = (uint8_t)(g.clock.day % 10) + '0';
            v[10] = '\0';
            Display_SendEditEvent("DATE", v);
            UpdateTimeAndDisplayBuffers();
        } else {
            UARTStringPutNOBlocking(UART0_BASE,
                                    syntax_matched
                                    ? (uint8_t*)"ERROR RANGE\r\n"
                                    : (uint8_t*)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:TIME" 命令 (设置时间)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:TIME")) {
        bool syntax_matched = false; /* 是否有字段组合匹配 */
        parse_ok = false;
        field_token_idx = current_param_idx;

        // 匹配 "HOUR MINUTE SECOND HH MM SS" 格式
        if (g.uart.num_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MINUTE", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 2], "SECOND", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 3;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 分
            parsed_val[2] = atoi((char*)g.uart.tokens[val_token_idx + 2].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2])) {
                g.clock.hh = (int8_t)parsed_val[0];
                g.clock.mm = (int8_t)parsed_val[1];
                g.clock.ss = (int8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR MINUTE HH MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MINUTE", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 分
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], g.clock.ss)) {
                g.clock.hh = (int8_t)parsed_val[0];
                g.clock.mm = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR SECOND HH SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "SECOND", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], g.clock.mm, (uint8_t)parsed_val[1])) {
                g.clock.hh = (int8_t)parsed_val[0];
                g.clock.ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE SECOND MM SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "MINUTE", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "SECOND", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 分
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time(g.clock.hh, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1])) {
                g.clock.mm = (int8_t)parsed_val[0];
                g.clock.ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR HH" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str); // 时
            if (parsed_val[0] >= 0 && parsed_val[0] < 24) {
                g.clock.hh = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "MINUTE", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str); // 分
            if (parsed_val[0] >= 0 && parsed_val[0] < 60) {
                g.clock.mm = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "SECOND SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "SECOND", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str); // 秒
            if (parsed_val[0] >= 0 && parsed_val[0] < 60) {
                g.clock.ss = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，保存原始值并发送成功消息
        {
            uint8_t v[9];
            g.clock.original_hh = g.clock.hh;
            g.clock.original_mm = g.clock.mm;
            g.clock.original_ss = g.clock.ss;
            g.clock.unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            v[0] = (uint8_t)(g.clock.hh / 10) + '0';
            v[1] = (uint8_t)(g.clock.hh % 10) + '0';
            v[2] = '.';
            v[3] = (uint8_t)(g.clock.mm / 10) + '0';
            v[4] = (uint8_t)(g.clock.mm % 10) + '0';
            v[5] = '.';
            v[6] = (uint8_t)(g.clock.ss / 10) + '0';
            v[7] = (uint8_t)(g.clock.ss % 10) + '0';
            v[8] = '\0';
            Display_SendEditEvent("TIME", v);
            UpdateTimeAndDisplayBuffers();
        } else {
            UARTStringPutNOBlocking(UART0_BASE,
                                    syntax_matched
                                    ? (uint8_t*)"ERROR RANGE\r\n"
                                    : (uint8_t*)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:ALARM" 命令 (设置闹钟)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:ALARM")) {
        bool syntax_matched = false; /* 是否有字段组合匹配 */
        parse_ok = false;
        field_token_idx = current_param_idx;

        /* *SET:ALARM OFF — 关闭闹钟 (设 alm_hh=25 为哨兵值) */
        if (g.uart.num_tokens == field_token_idx + 1 &&
            compareTokens(&g.uart.tokens[field_token_idx], "OFF", 3)) {
            g.clock.alm_hh = 25;
            g.clock.alm_mm = 0;
            g.clock.alm_ss = 0;
            syntax_matched = true;
            parse_ok = true;
        }

        // 匹配 "HOUR MINUTE SECOND HH MM SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MINUTE", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 2], "SECOND", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 3;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 分
            parsed_val[2] = atoi((char*)g.uart.tokens[val_token_idx + 2].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2])) {
                g.clock.alm_hh = (int8_t)parsed_val[0];
                g.clock.alm_mm = (int8_t)parsed_val[1];
                g.clock.alm_ss = (int8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR MINUTE HH MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MINUTE", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 分
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], g.clock.alm_ss)) {
                g.clock.alm_hh = (int8_t)parsed_val[0];
                g.clock.alm_mm = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR SECOND HH SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "SECOND", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], g.clock.alm_mm, (uint8_t)parsed_val[1])) {
                g.clock.alm_hh = (int8_t)parsed_val[0];
                g.clock.alm_ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE SECOND MM SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "MINUTE", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "SECOND", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str);     // 分
            parsed_val[1] = atoi((char*)g.uart.tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time(g.clock.alm_hh, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1])) {
                g.clock.alm_mm = (int8_t)parsed_val[0];
                g.clock.alm_ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR HH" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str); // 时
            if (parsed_val[0] >= 0 && parsed_val[0] < 24) {
                g.clock.alm_hh = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "MINUTE", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str); // 分
            if (parsed_val[0] >= 0 && parsed_val[0] < 60) {
                g.clock.alm_mm = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "SECOND SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "SECOND", 3)) {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char*)g.uart.tokens[val_token_idx].token_str); // 秒
            if (parsed_val[0] >= 0 && parsed_val[0] < 60) {
                g.clock.alm_ss = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，停止闹钟，保存原始值并发送成功消息
        {
            StopAlarmRinging(false);
            g.clock.original_alm_hh = g.clock.alm_hh;
            g.clock.original_alm_mm = g.clock.alm_mm;
            g.clock.original_alm_ss = g.clock.alm_ss;
            g.clock.unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            if (g.clock.alm_hh == 25) /* 闹钟已关闭 */
            {
                Display_SendEditEvent("ALARM", (const uint8_t*)"xx:xx:xx");
            } else {
                uint8_t v[9];
                v[0] = (uint8_t)(g.clock.alm_hh / 10) + '0';
                v[1] = (uint8_t)(g.clock.alm_hh % 10) + '0';
                v[2] = '.';
                v[3] = (uint8_t)(g.clock.alm_mm / 10) + '0';
                v[4] = (uint8_t)(g.clock.alm_mm % 10) + '0';
                v[5] = '.';
                v[6] = (uint8_t)(g.clock.alm_ss / 10) + '0';
                v[7] = (uint8_t)(g.clock.alm_ss % 10) + '0';
                v[8] = '\0';
                Display_SendEditEvent("ALARM", v);
            }
            UpdateTimeAndDisplayBuffers();
        } else {
            UARTStringPutNOBlocking(UART0_BASE,
                                    syntax_matched
                                    ? (uint8_t*)"ERROR RANGE\r\n"
                                    : (uint8_t*)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:DISPLAY" 命令 (设置显示开关)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:DISPLAY")) {
        if (g.uart.num_tokens == current_param_idx + 1) // 确保参数数量正确
        {
            if (compareTokens(&g.uart.tokens[current_param_idx], "ON", 2)) // "ON"
            {
                g.disp.shifting = true; // 开启流动
                g.disp.on = true;       // 开启数码管显示
                g.disp.msg_active = false;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
                Display_SendEvent();
                g.clock.unsaved_changes_active = false;
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "OFF", 3)) // "OFF"
            {
                g.disp.shifting = false; // 停止流动
                g.disp.on = false;       // 关闭数码管显示
                g.disp.msg_active = false;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
                Display_SendEvent();
                g.clock.unsaved_changes_active = false;
            } else // 无效参数
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR PARAM\r\n");
            }
        } else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:FORMAT" 命令 (设置显示格式)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:FORMAT")) {
        if (g.uart.num_tokens == current_param_idx + 1) // 确保参数数量正确
        {
            if (compareTokens(&g.uart.tokens[current_param_idx], "LEFT", 4)) // "LEFT"
            {
                g.disp.shift_mode = false;
                g.disp.reversed = false;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
                Display_SendEvent();
                g.clock.unsaved_changes_active = false;
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "RIGHT", 5)) // "RIGHT"
            {
                g.disp.shift_mode = true;
                g.disp.reversed = true;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
                Display_SendEvent();
                g.clock.unsaved_changes_active = false;
            } else {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR PARAM\r\n");
            }
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:MSG" 命令 (临时消息显示)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:MSG")) {
        uint8_t payload_offset;
        uint8_t payload_len;

        if (g.uart.num_tokens > current_param_idx) {
            payload_offset = FindRawPayloadOffset(current_param_idx);
            /* 使用 strlen 限制到第一个 null (支持多命令缓冲区中只取当前命令) */
            payload_len = (uint8_t)strlen((char*)&g.uart.rx_buf[payload_offset]);
            /* 修剪尾部 \r \n (防御性: 裸串口终端可能发送 \r\n) */
            while (payload_len > 0 && (g.uart.rx_buf[payload_offset + payload_len - 1U] == '\r' || g.uart.rx_buf[
                payload_offset + payload_len - 1U] == '\n'))
                payload_len--;
            if (payload_len > 32)
                payload_len = 32;

            Display_StartMessage(&g.uart.rx_buf[payload_offset], payload_len);
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:LED" 命令 (LED接管)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:LED")) {
        uint8_t led_value;

        if (g.uart.num_tokens == current_param_idx + 1 && ParseHexByte(&g.uart.tokens[current_param_idx], &led_value)) {
            if (led_value == 0x00) {
                g.disp.led_takeover = false;
                g.disp.led_pattern = 0x00;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else {
                g.disp.led_takeover = true;
                g.disp.led_pattern = led_value;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            }
            Display_UpdateStatusLeds();
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:WEATHER" 命令 (天气 LED5-LED7 指示, 不进入 LED 接管)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:WEATHER")) {
        uint8_t weather_value;

        if (g.uart.num_tokens == current_param_idx + 1 && ParseHexByte(&g.uart.tokens[current_param_idx],
                                                                       &weather_value)) {
            g.disp.weather_code = (uint8_t)(weather_value & 0xE0); /* 仅保留 LED5-7 */
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            Display_UpdateStatusLeds();
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:BEEP" 命令 (远程蜂鸣, 10-5000 ms)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:BEEP")) {
        if (g.uart.num_tokens == current_param_idx + 1) {
            int ms = atoi((char*)g.uart.tokens[current_param_idx].token_str);
            if (ms >= 10 && ms <= 5000) {
                PWMStart(2400); /* 2400 Hz 蜂鸣 */
                g.disp.beep_until = g.timer.tick + (uint32_t)ms;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR RANGE\r\n");
            }
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:MODE" 命令 (夜间模式)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:MODE")) {
        if (g.uart.num_tokens == current_param_idx + 1) {
            if (compareTokens(&g.uart.tokens[current_param_idx], "NIGHT", 5)) {
                g.disp.night_mode = true;
                g.disp.on = true;
                g.disp.msg_active = false;
                PWMStop();
                if (g.disp.alarm_ring_start != 0)
                    g.disp.alarm_ringing = true;
                Display_SendModeEvent("NIGHT");
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
                Display_SendEvent();
                Display_UpdateStatusLeds();
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "DAY", 3)) {
                g.disp.night_mode = false;
                Display_SendModeEvent("DAY");
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
                Display_SendEvent();
                Display_UpdateStatusLeds();
            } else {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR PARAM\r\n");
            }
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:KEY" 命令 (虚拟按键注入, 不回报 *EVT:KEY 避免环回)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*SET:KEY")) {
        if (g.uart.num_tokens == current_param_idx + 1) {
            if (compareTokens(&g.uart.tokens[current_param_idx], "FUNC", 4)) {
                g.in.suppress_key_events = true;
                g.in.short_evt[0] = true; // K1
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "SHIFT", 5)) {
                g.in.suppress_key_events = true;
                g.in.short_evt[1] = true; // K2
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "ADD", 3)) {
                g.in.suppress_key_events = true;
                g.in.short_evt[2] = true; // K3
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "SAVE", 4)) {
                g.in.suppress_key_events = true;
                g.in.short_evt[3] = true; // K4
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "DISP", 4)) {
                g.in.suppress_key_events = true;
                g.in.short_evt[4] = true; // K5
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "SPEED", 5)) {
                g.in.suppress_key_events = true;
                g.in.short_evt[5] = true; // K6
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "FORMAT", 6)) {
                g.in.suppress_key_events = true;
                g.in.short_evt[6] = true; // K7
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "EXT", 3)) {
                g.in.suppress_key_events = true;
                g.in.short_evt[7] = true; // K8
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "USER1", 5)) {
                /* 不设置 suppress_key_events: PC 端需要 *EVT:KEY USER1 来触发 NTP 对时 */
                g.in.user_short_evt[0] = true;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else if (compareTokens(&g.uart.tokens[current_param_idx], "USER2", 5)) {
                /* 不设置 suppress_key_events: PC 端需要 *EVT:KEY USER2 来触发天气刷新 */
                g.in.user_short_evt[1] = true;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            } else {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR PARAM\r\n");
            }
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*NTP SYNC" 命令：PC 已成功获取标准时间并完成下发
    else if (g.uart.num_tokens >= 1 && compareTokens(&g.uart.tokens[0], "*NTP", 4)) {
        if (g.uart.num_tokens == 2 && compareTokens(&g.uart.tokens[1], "SYNC", 4)) {
            g.disp.ntp_synced = true;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
            Display_UpdateStatusLeds();
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*GET:DATE" 命令 (获取日期信息)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*GET:DATE")) {
        field_token_idx = current_param_idx;
        found_arg = false;

        if (g.uart.num_tokens == field_token_idx) // 如果没有指定字段，则返回完整日期
        {
            PutProtocolBuffer(g.disp.date_buf, 10);
            found_arg = true;
        } else // 根据指定字段返回信息
        {
            /* 预扫描确认至少有一个合法字段再发 OK 前缀 */
            found_arg = false;
            for (i = field_token_idx; i < g.uart.num_tokens; ++i) {
                if (compareFieldKeyword(&g.uart.tokens[i], "YEAR", 3) ||
                    compareFieldKeyword(&g.uart.tokens[i], "MONTH", 3) ||
                    compareFieldKeyword(&g.uart.tokens[i], "DATE", 3)) {
                    found_arg = true;
                    break;
                }
            }
            if (!found_arg) {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR PARAM\r\n");
            } else {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK ");
                for (i = field_token_idx; i < g.uart.num_tokens; ++i) {
                    if (compareFieldKeyword(&g.uart.tokens[i], "YEAR", 3)) // "YEAR"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)((g.clock.year / 1000) % 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)((g.clock.year / 100) % 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)((g.clock.year / 10) % 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.year % 10) + '0');
                    } else if (compareFieldKeyword(&g.uart.tokens[i], "MONTH", 3)) // "MONTH"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.month / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.month % 10) + '0');
                    } else if (compareFieldKeyword(&g.uart.tokens[i], "DATE", 3)) // "DATE" (日)
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.day / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.day % 10) + '0');
                    }
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
            }
        }
    }

    // 处理 "*GET:TIME" 命令 (获取时间信息)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*GET:TIME")) {
        field_token_idx = current_param_idx;
        found_arg = false;

        if (g.uart.num_tokens == field_token_idx) // 如果没有指定字段，则返回完整时间
        {
            PutProtocolBuffer(g.disp.time_buf, 8);
            found_arg = true;
        } else // 根据指定字段返回信息
        {
            /* 预扫描确认至少有一个合法字段再发 OK 前缀 */
            found_arg = false;
            for (i = field_token_idx; i < g.uart.num_tokens; ++i) {
                if (compareFieldKeyword(&g.uart.tokens[i], "HOUR", 3) ||
                    compareFieldKeyword(&g.uart.tokens[i], "MINUTE", 3) ||
                    compareFieldKeyword(&g.uart.tokens[i], "SECOND", 3)) {
                    found_arg = true;
                    break;
                }
            }
            if (!found_arg) {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR PARAM\r\n");
            } else {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK ");
                for (i = field_token_idx; i < g.uart.num_tokens; ++i) {
                    if (compareFieldKeyword(&g.uart.tokens[i], "HOUR", 3)) // "HOUR"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.hh / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.hh % 10) + '0');
                    } else if (compareFieldKeyword(&g.uart.tokens[i], "MINUTE", 3)) // "MINUTE"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.mm / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.mm % 10) + '0');
                    } else if (compareFieldKeyword(&g.uart.tokens[i], "SECOND", 3)) // "SECOND"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.ss / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.ss % 10) + '0');
                    }
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
            }
        }
    }

    // 处理 "*GET:ALARM" 命令 (获取闹钟时间)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*GET:ALARM")) {
        if (g.uart.num_tokens == current_param_idx) // 确保没有额外参数
        {
            PutProtocolBuffer(g.disp.alarm_buf, 8);
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*GET:FORMAT" 命令 (获取显示格式)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*GET:FORMAT")) {
        if (g.uart.num_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, g.disp.shift_mode ? (uint8_t*)"OK RIGHT\r\n" : (uint8_t*)"OK LEFT\r\n");
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*GET:DISPLAY" 命令 (获取显示状态)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*GET:DISPLAY")) {
        if (g.uart.num_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, g.disp.on ? (uint8_t*)"OK ON\r\n" : (uint8_t*)"OK OFF\r\n");
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*MOTOR:START" 命令 (启动步进电机)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*MOTOR:START")) {
        if (g.uart.num_tokens == current_param_idx) {
            g.motor.running = 1;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*MOTOR:STOP" 命令 (停止步进电机)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*MOTOR:STOP")) {
        if (g.uart.num_tokens == current_param_idx) {
            g.motor.running = 0;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*MOTOR:FWD" 命令 (设置步进电机正转)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*MOTOR:FWD")) {
        if (g.uart.num_tokens == current_param_idx) {
            g.motor.direction = 0;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*MOTOR:REV" 命令 (设置步进电机反转)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*MOTOR:REV")) {
        if (g.uart.num_tokens == current_param_idx) {
            g.motor.direction = 1;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*GET:MOTOR" 命令 (获取步进电机状态)
    else if (matchCommand(&g.uart.tokens[0],
                          (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                          g.uart.num_tokens,
                          "*GET:MOTOR")) {
        if (g.uart.num_tokens == current_param_idx) {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK ");
            UARTStringPutNOBlocking(UART0_BASE, g.motor.running ? (uint8_t*)"1" : (uint8_t*)"0");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)" ");
            UARTStringPutNOBlocking(UART0_BASE, g.motor.direction ? (uint8_t*)"1" : (uint8_t*)"0");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"\r\n");
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "INIT" 命令 (复位)
    else if (compareTokens(&g.uart.tokens[0], "INIT", 4)) {
        if (g.uart.num_tokens == current_param_idx)
            SysCtlReset(); // 系统复位
        else
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
    }

    // 处理 "*PING" 命令 (连接保活)
    else if (compareTokens(&g.uart.tokens[0], "*PING", 5)) {
        if (g.uart.num_tokens == current_param_idx) {
            char pong_buf[24];
            uint32_t uptime_s = g.timer.tick / 1000U;
            uint8_t len = 0;
            uint8_t ij;

            /* 手动构建 "*PONG <uptime_s>\r\n" 避免依赖 sprintf */
            pong_buf[0] = '*';
            pong_buf[1] = 'P';
            pong_buf[2] = 'O';
            pong_buf[3] = 'N';
            pong_buf[4] = 'G';
            pong_buf[5] = ' ';
            len = 6;

            /* 将 uptime_s 转为 ASCII (简易 itoa) */
            {
                char num_buf[12];
                uint8_t num_len = 0;
                uint32_t val = uptime_s;
                if (val == 0) {
                    num_buf[num_len++] = '0';
                } else {
                    while (val > 0 && num_len < 11) {
                        num_buf[num_len++] = (char)('0' + (val % 10U));
                        val /= 10U;
                    }
                }
                /* 逆序写入 */
                for (ij = num_len; ij > 0; --ij)
                    pong_buf[len++] = num_buf[ij - 1U];
            }

            pong_buf[len++] = '\r';
            pong_buf[len++] = '\n';
            pong_buf[len] = '\0';

            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)pong_buf);
        } else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    else if (g.uart.num_tokens == 1 && g.uart.tokens[0].token_len == 1) {
        if (g.uart.tokens[0].token_str[0] == '1') {
            Display_StartMessage((const uint8_t *)"11111111", 8);
        }else if (g.uart.tokens[0].token_str[0] == '2') {
            Display_StartMessage((const uint8_t *)"22222222", 8);
        }else {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "HELP" 命令 (显示帮助)
    else if (compareTokens(&g.uart.tokens[0], "HELP", 4)) {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"OK\r\n");
        UARTStringPutNOBlocking(UART0_BASE,
                                (uint8_t*)
                                "*RST                                       : Reset protocol state.\r\n"
                                "*SET:DATE YEAR MONTH DATE YYYY MM DD      : Set date (partial fields supported).\r\n"
                                "*SET:TIME HOUR MINUTE SECOND HH MM SS     : Set time (partial fields supported).\r\n"
                                "*SET:ALARM HOUR MINUTE SECOND HH MM SS    : Set alarm; OFF to disable.\r\n"
                                "*SET:DISPLAY ON/OFF                       : Turn 7-segment display on/off.\r\n"
                                "*SET:FORMAT LEFT/RIGHT                    : Set display direction.\r\n"
                                "*SET:MSG <text>                           : Show temporary message (max 32 bytes).\r\n"
                                "*SET:LED <hex2>                           : LED takeover; 00 restores default.\r\n"
                                "*SET:WEATHER <hex2>                        : Weather LED5-7 indicator (no takeover).\r\n"
                                "*SET:BEEP <ms>                            : Remote beep 10-5000 ms at 2400 Hz.\r\n"
                                "*SET:MODE NIGHT/DAY                       : Night/day mode.\r\n"
                                "*SET:KEY FUNC/SHIFT/ADD/SAVE/DISP/SPEED/FORMAT/EXT/USER1/USER2 : Virtual key injection.\r\n"
                                "*NTP SYNC                                 : Mark NTP sync complete; LED4 on.\r\n"
                                "*GET:DATE [YEAR] [MONTH] [DATE]           : Get date.\r\n"
                                "*GET:TIME [HOUR] [MINUTE] [SECOND]        : Get time.\r\n"
                                "*GET:ALARM                                : Get alarm time.\r\n"
                                "*GET:DISPLAY                              : Get display state.\r\n"
                                "*GET:FORMAT                               : Get display format.\r\n"
                                "*MOTOR:START/STOP/FWD/REV                 : Stepper motor control.\r\n"
                                "*GET:MOTOR                                : Get motor status.\r\n"
                                "*PING                                     : Responds *PONG <uptime_s>.\r\n"
                                "INIT                                      : Software system reset.\r\n"
                                "HELP                                      : Show this help.\r\n");
    } else // 未知命令
    {
        if (g.uart.num_tokens > 0) {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t*)"ERROR SYNTAX\r\n");
        }
    }

    /* 检查缓冲区中是否还有下一条命令 (紧接在 null 之后，由 ISR 连续接收) */
    {
        uint8_t pos = 0;
        while (pos < g.uart.rx_len && g.uart.rx_buf[pos] != '\0')
            pos++;
        if (pos + 1U < g.uart.rx_len) /* 第一条命令的 null 之后还有数据 */
        {
            uint8_t tail = (uint8_t)(g.uart.rx_len - pos - 1U);
            memmove(g.uart.rx_buf, &g.uart.rx_buf[pos + 1U], tail);
            g.uart.rx_len = tail;
            g.uart.cmd_state = 1; /* 下一条命令待处理 */
            memset(&g.uart.rx_buf[tail], 0, sizeof(g.uart.rx_buf) - tail);
        } else {
            g.uart.rx_len = 0;
            memset(g.uart.rx_buf, 0, sizeof(g.uart.rx_buf));
        }
    }
}

/* ===== app.c ===== */

void App_Init(void) {
    int i;

    memset(g.disp.time_buf, 0, sizeof(g.disp.time_buf));
    memset(g.disp.alarm_buf, 0, sizeof(g.disp.alarm_buf));
    memset(g.disp.date_buf, 0, sizeof(g.disp.date_buf));

    UpdateTimeAndDisplayBuffers();
    g.disp.shift = 0;

    for (i = 0; i < 8; ++i) {
        g.in.debounce[i] = 0;
        g.in.press_start[i] = 0;
        g.in.long_press[i] = 0;
        g.in.state[i] = false;
        g.in.short_evt[i] = false;
        g.in.long_start_evt[i] = false;
        g.in.repeat_evt[i] = false;
    }
    for (i = 0; i < 2; ++i) {
        g.in.user_debounce[i] = 0;
        g.in.user_press_start[i] = 0;
        g.in.user_state[i] = false;
        g.in.user_short_evt[i] = false;
    }
}

void App_RunOnce(void) {
    Melody_Update(); // 旋律播放器 (非阻塞, 不播放时立即返回)
    HandleAlarm();

    if (g.disp.init_flag) {
        RunInitializationSequence();
    } else {
        ProcessButtonEvents();

        if (g.timer.mode_timeout_flag) {
            HandleModeTimeout();
            g.timer.mode_timeout_flag = false;
        }

        if (g.timer.flag_1s) {
            UpdateTimeAndDisplayBuffers();
            g.timer.flag_1s = false;
        }

        if (g.timer.flag_2ms) {
            Update7SegmentDisplay();
            g.timer.flag_2ms = false;
        }

        if (g.disp.msg_active ||
            (g.disp.mode == MODE_FLOWING && g.disp.shifting == true && g.disp.on == true)) {
            if (g.disp.shift_speed == false && g.timer.flag_500ms) {
                UpdateDisplayShift();
                g.timer.flag_500ms = false;
            } else if (g.disp.shift_speed == true && g.timer.flag_300ms) {
                UpdateDisplayShift();
                g.timer.flag_300ms = false;
            }
        }
        Display_UpdateStatusLeds();
    }

    /* 远程蜂鸣超时检查 (非阻塞, 与闹钟/旋律互不干扰) */
    if (g.disp.beep_until != 0 && (int32_t)(g.timer.tick - g.disp.beep_until) >= 0) {
        PWMStop();
        g.disp.beep_until = 0;
    }

    ProcessUartCommand();
}

/* ===== callback.c ===== */

// UART0中断处理函数
void UART0_Handler(void) {
    int32_t uart0_int_status;
    uint8_t uart_receive_char;

    uart0_int_status = UARTIntStatus(UART0_BASE, true); // 获取中断状态
    UARTIntClear(UART0_BASE, uart0_int_status);         // 清除中断标志
    g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;

    while (UARTCharsAvail(UART0_BASE)) // 检查是否有可用字符
    {
        uart_receive_char = UARTCharGetNonBlocking(UART0_BASE); // 非阻塞获取字符

        if (uart_receive_char == '\r' || uart_receive_char == '\n') // 如果是回车或换行，表示命令输入结束
        {
            if (g.uart.rx_len > 0) // 有内容
            {
                if (g.uart.cmd_state == 0) // 第一条命令
                {
                    /* 正常路径使用 post-increment，rx_len 指向最后一个字符之后 */
                    g.uart.rx_buf[g.uart.rx_len] = '\0';
                    g.uart.cmd_state = 1;
                } else // 后续追加命令
                {
                    /* 追加路径使用 pre-increment，rx_len 指向最后一个字符；
                       将 \0 放在最后一个字符之后，并推进 rx_len */
                    g.uart.rx_buf[++g.uart.rx_len] = '\0';
                }
            }
            continue; // 丢弃 \r \n 字符，不存入缓冲区
        }

        /* 若上一条命令尚未处理，新字符追加在 null 之后 (跳过 null) */
        if (g.uart.cmd_state != 0) {
            if (g.uart.rx_len < (sizeof(g.uart.rx_buf) - 2)) {
                g.uart.rx_buf[++g.uart.rx_len] = uart_receive_char;
            }
            continue;
        }

        if (g.uart.rx_len < (sizeof(g.uart.rx_buf) - 1)) // 检查缓冲区是否已满
        {
            g.uart.rx_buf[g.uart.rx_len++] = uart_receive_char; // 存储字符并增加长度
        }
    }
}

// SysTick定时器中断处理函数
void SysTick_Handler(void) {
    int i = 0;
    uint8_t current_button_raw_value;
    uint8_t current_user_raw_value;
    uint8_t current_user_pin;
    uint8_t max_days_for_current_month;
    bool any_button_is_currently_pressed_debounced = false; // 任何按钮是否处于去抖后的按下状态
    uint32_t press_duration;                                // 按钮按下持续时间

    g.timer.tick++; // 增加系统滴答计数

    // 根据系统滴答更新各种定时器标志
    if (g.timer.tick % V_T2ms == 0) {
        g.timer.flag_2ms = true;
    }
    if (g.timer.tick % V_T500ms == 0) {
        g.timer.flag_500ms = true;
    }
    if (g.timer.tick % V_T300ms == 0) {
        g.timer.flag_300ms = true;
    }
    if (g.timer.tick % V_T900ms == 0) {
        g.timer.flag_900ms = true;
    }

    if (g.timer.tick % V_T1s == 0) // 每秒更新时钟
    {
        g.timer.flag_1s = true;
        g.clock.ss++;
    }

    // 处理保存成功后的闪烁计时
    if (g.disp.save_blink_active) {
        if (g.timer.tick - g.disp.save_blink_timer >= SAVE_BLINK_DURATION_MS) {
            g.disp.save_blink_active = false;
            g.disp.save_blink_timer = 0;
        }
    }

    // 时间和日期进位逻辑
    if (g.clock.ss >= 60) {
        g.clock.mm++;
        g.clock.ss = 0;
    }
    if (g.clock.mm >= 60) {
        g.clock.hh++;
        g.clock.mm = 0;
    }
    if (g.clock.hh >= 24) {
        g.clock.day++;
        g.clock.hh = 0;
        max_days_for_current_month = g.rtc.days_in_month[g.clock.month];
        if (g.clock.month == 2 && is_leap_year(g.clock.year)) {
            max_days_for_current_month = 29;
        }
        if (g.clock.day > max_days_for_current_month) {
            g.clock.day = 1;
            g.clock.month++;
            if (g.clock.month > 12) {
                g.clock.month = 1;
                g.clock.year++;
            }
        }
    }

    // 按钮状态检测和去抖
    current_button_raw_value = I2C0_ReadByte(TCA6424_I2CADDR, TCA6424_INPUT_PORT0);
    for (i = 0; i < 8; ++i) {
        if (!((current_button_raw_value >> i) & 0x01)) // 读到按下 (低电平有效)
        {
            if (g.in.debounce[i] < DEBOUNCE_TIME_MS)
                g.in.debounce[i]++;

            if (g.in.debounce[i] == DEBOUNCE_TIME_MS) // 去抖完成，确认按下
            {
                if (g.in.state[i] == false) // 从释放转为按下
                {
                    g.in.state[i] = true;
                    g.in.press_start[i] = g.timer.tick;
                    g.in.long_press[i] = 0;
                    g.in.long_start_evt[i] = false;
                    g.in.repeat_evt[i] = false;
                } else // 持续按下
                {
                    g.in.long_press[i]++;

                    if (g.in.long_start_evt[i] == false && g.in.long_press[i] >= LONG_PRESS_TIME_MS) {
                        g.in.long_start_evt[i] = true;
                        if (i == 2)
                            g.in.long_press[i] = 0;
                    } else if (i == 2 && g.in.long_start_evt[i] == true && g.in.long_press[i] >= REPEAT_PRESS_TIME_MS) {
                        g.in.repeat_evt[i] = true;
                        g.in.long_press[i] = 0;
                    }
                }
                any_button_is_currently_pressed_debounced = true;
            }
        } else // 读到释放 (高电平)
        {
            if (g.in.debounce[i] > 0)
                g.in.debounce[i]--;

            if (g.in.debounce[i] == 0 && g.in.state[i] == true) // 去抖完成，确认释放
            {
                g.in.state[i] = false;

                press_duration = g.timer.tick - g.in.press_start[i];

                if (g.in.long_start_evt[i] == false && press_duration >= DEBOUNCE_TIME_MS && press_duration <
                    LONG_PRESS_TIME_MS) {
                    g.in.short_evt[i] = true;
                } else if (i == 0 && g.disp.long_press_saving) {
                    g.disp.mode = MODE_FLOWING;
                    g.disp.shifting = g.disp.prev_shifting;
                    g.disp.shift_mode = g.disp.prev_shift_mode;
                    g.disp.shift_speed = g.disp.prev_shift_speed;
                    g.disp.long_press_saving = false;
                    g.disp.on = true;
                }

                g.in.long_start_evt[i] = false;
                g.in.repeat_evt[i] = false;
                g.in.long_press[i] = 0;
                g.in.press_start[i] = 0;
            }
        }
    }

    if (g.in.user_gpio_ready) {
        current_user_raw_value = GPIOPinRead(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1);
        for (i = 0; i < 2; ++i) {
            current_user_pin = (i == 0) ? GPIO_PIN_0 : GPIO_PIN_1;
            if ((current_user_raw_value & current_user_pin) == 0) {
                if (g.in.user_debounce[i] < DEBOUNCE_TIME_MS)
                    g.in.user_debounce[i]++;

                if (g.in.user_debounce[i] == DEBOUNCE_TIME_MS && g.in.user_state[i] == false) {
                    g.in.user_state[i] = true;
                    g.in.user_press_start[i] = g.timer.tick;
                }
            } else {
                if (g.in.user_debounce[i] > 0)
                    g.in.user_debounce[i]--;

                if (g.in.user_debounce[i] == 0 && g.in.user_state[i] == true) {
                    g.in.user_state[i] = false;
                    if (g.timer.tick - g.in.user_press_start[i] >= DEBOUNCE_TIME_MS) {
                        g.in.user_short_evt[i] = true;
                    }
                    g.in.user_press_start[i] = 0;
                }
            }
        }
    }

    // 处理模式超时 (仅在非初始化阶段)
    if (!g.disp.init_flag) {
        if (any_button_is_currently_pressed_debounced) // 如果有按钮被按下，重置超时定时器
        {
            g.timer.mode_timeout = g.timer.tick;
        } else // 没有按钮被按下，检查是否超时
        {
            if (g.timer.tick - g.timer.mode_timeout >= MODE_TIMEOUT_MS) {
                g.timer.mode_timeout_flag = true;    // 触发模式超时
                g.timer.mode_timeout = g.timer.tick; // 重置超时定时器
            }
        }
    }
}

void TIMER0A_Handler(void) {
    StepperTimerTick();
}

/* ===== main.c — 程序入口 ===== */
int main(void) {
    DevicesInit();
    App_Init();

    while (1) {
        App_RunOnce();
    }
}