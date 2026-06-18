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
    MAIN_DISPLAY_FLOW,
    MAIN_DISPLAY_TIME,
    MAIN_DISPLAY_DATE,
    MAIN_DISPLAY_YEAR
} main_display_t;

typedef struct // 命令Token结构体
{
    uint8_t token_str[MAX_TOKEN_LENGTH]; // Token字符串
    uint8_t token_len;                   // Token长度
} command_token_t;

/* ── 时钟数据：时间、日期、闹钟的当前值 / 临时编辑值 / 原始回滚值 ── */
typedef struct {
    int8_t   hh, mm, ss;
    uint16_t year;
    uint8_t  month, day;
    int8_t   alm_hh, alm_mm, alm_ss;
    bool     alarm_enabled;            /* alm_hh != 25 时有效 */

    uint16_t temp_year;
    uint8_t  temp_month, temp_day;
    uint8_t  temp_hh, temp_mm, temp_ss;
    uint8_t  temp_alm_hh, temp_alm_mm, temp_alm_ss;

    int8_t   original_hh, original_mm, original_ss;
    uint16_t original_year;
    uint8_t  original_month, original_day;
    int8_t   original_alm_hh, original_alm_mm, original_alm_ss;
    bool     unsaved_changes_active;
} clock_data_t;

/* ── 显示上下文：7-SEG 缓冲区、模式、标志、消息、LED ── */
typedef struct {
    uint8_t  time_buf[9];
    uint8_t  alarm_buf[9];
    uint8_t  date_buf[11];
    uint8_t  seg7[18];
    uint8_t  master_buf[18];

    uint8_t  cnt;
    int8_t   shift;
    uint8_t  rightshift;
    bool     shift_mode;
    bool     shift_speed;
    bool     shifting;
    bool     reversed;
    bool     on;
    bool     blinking;

    system_mode_t mode;
    setting_field_t field;
    main_display_t main_disp;
    bool     prev_shifting, prev_shift_mode, prev_shift_speed;

    bool     night_mode;
    bool     led_takeover;
    uint8_t  led_pattern;
    volatile uint32_t uart_activity_until;

    bool     alarm_ringing;
    bool     alarm_silenced;
    uint32_t alarm_ring_start;
    uint32_t alarm_beep_phase;
    bool     alarm_beep_on;

    bool     msg_active;
    bool     msg_scroll;
    uint8_t  msg_buf[33];
    uint8_t  msg_len;
    int8_t   msg_shift;
    uint32_t msg_start;
    uint32_t msg_last_shift;

    bool     save_blink_active;
    uint32_t save_blink_timer;
    bool     long_press_saving;

    bool     init_flag;
    uint8_t  init_step;
    uint8_t  i2c_result;
} display_ctx_t;

/* ── 系统时基：SysTick 计数器与时间片标志 ── */
typedef struct {
    volatile uint32_t tick;
    volatile bool flag_2ms;
    volatile bool flag_500ms;
    volatile bool flag_300ms;
    volatile bool flag_900ms;
    volatile bool flag_1s;
    volatile uint32_t mode_timeout;
    volatile bool mode_timeout_flag;
} sys_timer_t;

/* ── UART 上下文：接收缓冲区与命令解析状态 ── */
typedef struct {
    uint8_t  rx_buf[100];
    uint8_t  rx_len;
    volatile uint8_t cmd_state;
    command_token_t tokens[MAX_COMMAND_TOKENS];
    uint8_t  num_tokens;
} uart_ctx_t;

/* ── 按键输入状态：8 个矩阵按键 + 2 个用户按键 ── */
typedef struct {
    volatile uint32_t debounce[8];
    volatile uint32_t press_start[8];
    volatile uint32_t long_press[8];
    volatile bool     state[8];
    volatile bool     short_evt[8];
    volatile bool     long_start_evt[8];
    volatile bool     repeat_evt[8];

    volatile uint32_t user_debounce[2];
    volatile uint32_t user_press_start[2];
    volatile bool     user_state[2];
    volatile bool     user_short_evt[2];
    volatile bool     user_gpio_ready;
} input_state_t;

/* ── 步进电机状态 ── */
typedef struct {
    volatile uint16_t position;
    volatile uint8_t  direction;   /* 0=forward, 1=reverse */
    volatile uint8_t  running;     /* 0=stopped, 1=running */
} motor_state_t;

/* ── 休眠 RTC 数据 ── */
typedef struct {
    uint32_t stored[4];
    uint32_t current;
    uint32_t fetched[4];
    uint8_t  days_in_month[13];
} rtc_data_t;

/* ── 顶层应用状态 ── */
typedef struct {
    uint32_t       sys_clock_hz;
    clock_data_t   clock;
    display_ctx_t  disp;
    sys_timer_t    timer;
    uart_ctx_t     uart;
    input_state_t  in;
    motor_state_t  motor;
    rtc_data_t     rtc;
} app_state_t;

#endif
