#include "app_state.h"

volatile uint16_t motor_position = 0;
volatile uint8_t  motor_direction = 1;  // 0=forward, 1=reverse
volatile uint8_t  motor_running = 1;    // 0=stopped, 1=running

uint32_t ui32SysClock; // 系统时钟频率

int8_t hh = 0;        // 当前小时
int8_t mm = 0;        // 当前分钟
int8_t ss = 0;        // 当前秒
uint16_t year = 2025; // 当前年份
uint8_t month = 6;    // 当前月份
uint8_t day = 3;      // 当前日期

int8_t alm_hh = 25; // 闹钟小时，25表示未设置
int8_t alm_mm = 0;  // 闹钟分钟
int8_t alm_ss = 0;  // 闹钟秒

uint16_t temp_year;  // 临时年份
uint8_t temp_month;  // 临时月份
uint8_t temp_day;    // 临时日期
uint8_t temp_hh;     // 临时小时
uint8_t temp_mm;     // 临时分钟
uint8_t temp_ss;     // 临时秒
uint8_t temp_alm_hh; // 临时闹钟小时
uint8_t temp_alm_mm; // 临时闹钟分钟
uint8_t temp_alm_ss; // 临时闹钟秒

int8_t original_hh;     // 原始小时
int8_t original_mm;     // 原始分钟
int8_t original_ss;     // 原始秒
uint16_t original_year; // 原始年份
uint8_t original_month; // 原始月份
uint8_t original_day;   // 原始日期
int8_t original_alm_hh; // 原始闹钟小时
int8_t original_alm_mm; // 原始闹钟分钟
int8_t original_alm_ss; // 原始闹钟秒

bool unsaved_changes_active = false; // 标志是否存在未保存的更改

volatile uint32_t g_system_tick = 0; // 系统滴答计数器

volatile uint16_t clock1s = 0;     // 1秒定时器
volatile uint8_t clock20ms = 0;    // 20毫秒定时器
volatile uint8_t clock2ms = 0;     // 2毫秒定时器
volatile uint16_t clock500ms = 0;  // 500毫秒定时器
volatile uint16_t clock300ms = 0;  // 300毫秒定时器
volatile uint16_t clock900ms = 0;  // 900毫秒定时器
volatile uint16_t blink_timer = 0; // 闪烁定时器

volatile bool clock1s_flag = false;     // 1秒定时器标志
volatile bool clock20ms_flag = false;   // 20毫秒定时器标志
volatile bool clock2ms_flag = false;    // 2毫秒定时器标志
volatile bool clock500ms_flag = false;  // 500毫秒定时器标志
volatile bool clock300ms_flag = false;  // 300毫秒定时器标志
volatile bool clock900ms_flag = false;  // 900毫秒定时器标志
volatile bool blink_timer_flag = false; // 闪烁定时器标志

volatile uint32_t mode_timeout_timer = 0; // 模式超时定时器
volatile bool mode_timeout_flag = false;  // 模式超时标志

volatile bool save_blink_active = false; // 保存闪烁激活标志
volatile uint32_t save_blink_timer = 0;  // 保存闪烁定时器

volatile bool long_press_saving_in_progress = false; // 长按保存进行中标志

uint8_t uart_receive_buffer[100]; // UART接收缓冲区
uint8_t uart_receive_len = 0;     // UART接收长度
volatile uint8_t cmd_state = 0;   // 命令状态

uint8_t time_transmit_buffer[9];  // 时间显示缓冲区
uint8_t alarm_transmit_buffer[9]; // 闹钟显示缓冲区
uint8_t date_transmit_buffer[11]; // 日期显示缓冲区

uint8_t seg7[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
                  0x77, 0x7c, 0x58, 0x5e, 0x79, 0x71, 0x5c, 0x00}; // 7段数码管字形码

uint8_t stuID[8] = {0x6D, 0x5B, 0x66, 0x3F, 0x4F, 0x06, 0x6F, 0x06}; // 学生ID前8位 52403191
uint8_t name[8] = {0x76, 0x3E, 0x76, 0x77, 0x3F, 0x77, 0x77, 0x54};  // 名字字形码 XUHAORAN

uint8_t result;            // I2C操作结果
uint8_t cnt;               // 数码管刷新计数
int8_t shift = 0;          // 显示移位量
uint8_t rightshift = 0x01; // 位选控制

uint8_t master_display_buffer[18]; // 主显示缓冲区

bool shift_mode = false;             // 移位模式 (左右)
bool shift_speed = false;            // 移位速度
bool shifting = true;                // 是否正在移位
bool display_reversed_order = false; // 显示是否反向

bool seven_segment_display_on = true; // 7段数码管是否开启
bool alarm_ringing = false;           // 闹钟是否正在响铃
bool alarm_silenced_for_match = false; // 当前闹钟匹配秒是否已被手动静音
bool night_mode_active = false;        // 夜间模式
bool led_takeover_active = false;      // LED上位机接管
uint8_t led_takeover_pattern = 0x00;   // LED接管输出
volatile uint32_t uart_activity_until_tick = 0; // UART活动指示截止时间
uint32_t alarm_ring_start_tick = 0;    // 闹钟响铃开始时间
uint32_t alarm_beep_phase_tick = 0;    // 闹钟蜂鸣相位起点
bool alarm_beep_on = false;            // 闹钟蜂鸣当前是否开启
bool message_active = false;           // 临时消息显示
bool message_scroll_active = false;    // 临时消息是否滚动
uint8_t message_buffer[33];            // 临时消息文本
uint8_t message_len = 0;               // 临时消息长度
int8_t message_shift = 0;              // 临时消息滚动偏移
uint32_t message_start_tick = 0;       // 临时消息开始时间
uint32_t message_last_shift_tick = 0;  // 临时消息上次滚动时间

volatile uint8_t raw_key_value = 0xFF;       // 按钮原始值
volatile uint8_t prev_raw_key_value = 0xFF;  // 上一个按钮原始值
volatile uint32_t key_debounce_timer[8];     // 按钮去抖定时器
volatile uint32_t key_press_start_time[8];   // 按钮按下起始时间
volatile uint32_t key_long_press_timer[8];   // 按钮长按定时器
volatile bool key_states[8];                 // 按钮当前状态
volatile bool key_short_press_event[8];      // 按钮短按事件标志
volatile bool key_long_press_start_event[8]; // 按钮长按开始事件标志
volatile bool key_repeat_press_event[8];     // 按钮重复按事件标志

volatile uint32_t user_key_debounce_timer[2];   // USER1/USER2去抖定时器
volatile uint32_t user_key_press_start_time[2]; // USER1/USER2按下起始时间
volatile bool user_key_states[2];               // USER1/USER2当前状态
volatile bool user_key_short_press_event[2];    // USER1/USER2短按事件
volatile bool user_key_gpio_ready = false;      // USER1/USER2 GPIO是否已初始化

system_mode_t current_mode = MODE_FLOWING;          // 当前系统模式
setting_field_t current_setting_field = FIELD_NONE; // 当前设置字段
main_display_t main_display_mode = MAIN_DISPLAY_FLOW; // 正常模式显示内容
bool is_blinking = false;                           // 闪烁标志
bool prev_shifting = true;                          // 之前的移位状态
bool prev_shift_mode = false;                       // 之前的移位方向模式
bool prev_shift_speed = false;                      // 之前的移位速度模式

bool init_flag = true;      // 初始化流程标志
uint8_t init_procedure = 0; // 初始化步骤

command_token_t parsed_tokens[MAX_COMMAND_TOKENS]; // 解析后的Token数组
uint8_t num_parsed_tokens;                         // 解析出的Token数量

uint32_t storedRTC[4]; // 存储RTC数据
uint32_t currentRTC;   // 当前RTC值
uint32_t fetchRTC[4];  // 获取的RTC数据

uint8_t days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; // 每月天数
