#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_i2c.h"
#include "inc/hw_gpio.h"
#include "driverlib/debug.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/systick.h"
#include "driverlib/interrupt.h"
#include "driverlib/uart.h"
#include "driverlib/timer.h"
#include "driverlib/i2c.h"
#include "driverlib/pwm.h"
#include "driverlib/hibernate.h"

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

#define DEBOUNCE_TIME_MS 10
#define LONG_PRESS_TIME_MS 3000
#define REPEAT_PRESS_TIME_MS 200

#define STEPPER_SYSTEM_CLOCK_HZ      20000000UL
#define STEPPER_BEATS_PER_REVOLUTION 4096UL
#define STEPPER_TARGET_RPM           1UL
#define STEPPER_TIMER_LOAD           ((STEPPER_SYSTEM_CLOCK_HZ * 60UL) / (STEPPER_BEATS_PER_REVOLUTION * STEPPER_TARGET_RPM) - 1)

#define STEPPER_PINS (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3)

static const uint8_t stepper_sequence[8] = {
    0x01,  // Beat 1: PF0
    0x03,  // Beat 2: PF0+PF1
    0x02,  // Beat 3: PF1
    0x06,  // Beat 4: PF1+PF2
    0x04,  // Beat 5: PF2
    0x0C,  // Beat 6: PF2+PF3
    0x08,  // Beat 7: PF3
    0x09,  // Beat 8: PF3+PF0
};

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

uint8_t stuID[8] = {0x4F, 0x06, 0x6F, 0x06, 0x3F, 0x07, 0x07, 0x5B}; // 学生ID字形码 31910772
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

volatile uint8_t raw_key_value = 0xFF;       // 按钮原始值
volatile uint8_t prev_raw_key_value = 0xFF;  // 上一个按钮原始值
volatile uint32_t key_debounce_timer[8];     // 按钮去抖定时器
volatile uint32_t key_press_start_time[8];   // 按钮按下起始时间
volatile uint32_t key_long_press_timer[8];   // 按钮长按定时器
volatile bool key_states[8];                 // 按钮当前状态
volatile bool key_short_press_event[8];      // 按钮短按事件标志
volatile bool key_long_press_start_event[8]; // 按钮长按开始事件标志
volatile bool key_repeat_press_event[8];     // 按钮重复按事件标志

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

system_mode_t current_mode = MODE_FLOWING;          // 当前系统模式
setting_field_t current_setting_field = FIELD_NONE; // 当前设置字段
bool is_blinking = false;                           // 闪烁标志
bool prev_shifting = true;                          // 之前的移位状态
bool prev_shift_mode = false;                       // 之前的移位方向模式
bool prev_shift_speed = false;                      // 之前的移位速度模式

bool init_flag = true;      // 初始化流程标志
uint8_t init_procedure = 0; // 初始化步骤

typedef struct // 命令Token结构体
{
    uint8_t token_str[MAX_TOKEN_LENGTH]; // Token字符串
    uint8_t token_len;                   // Token长度
} command_token_t;

command_token_t parsed_tokens[MAX_COMMAND_TOKENS]; // 解析后的Token数组
uint8_t num_parsed_tokens;                         // 解析出的Token数量

uint32_t storedRTC[4]; // 存储RTC数据
uint32_t currentRTC;   // 当前RTC值
uint32_t fetchRTC[4];  // 获取的RTC数据

uint8_t days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; // 每月天数

void SysTickInit(void);                                                                                                                               // 初始化SysTick定时器
void DevicesInit(void);                                                                                                                               // 初始化所有外设
void UARTInit(void);                                                                                                                                  // 初始化UART0
void UARTStringPutNOBlocking(uint32_t ui32Base, uint8_t *cMessage);                                                                                   // 非阻塞方式发送字符串
static void UARTStringPutReversedNOBlocking(uint32_t ui32Base, uint8_t *cMessage);                                                                    // 非阻塞方式反向发送字符串
void UARTCharPutBlocking(uint32_t ui32Base, uint8_t ucData);                                                                                          // 阻塞方式发送单个字符
void StepperGPIOInit(void);                                                                                                                            // 初始化步进电机GPIO
void StepperTimerInit(void);                                                                                                                          // 初始化步进电机Timer
void PWMInit(void);                                                                                                                                   // 初始化PWM
void PWMStart(uint32_t ui32Freq_Hz);                                                                                                                  // 启动PWM输出
void PWMStop(void);                                                                                                                                   // 停止PWM输出
uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData);                                                                          // I2C写入单个字节
uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr);                                                                                              // I2C读取单个字节
void S800_I2C0_Init(void);                                                                                                                            // 初始化I2C0
void HibernateInit(void);                                                                                                                             // 初始化休眠模块
void Delay(uint32_t value);                                                                                                                           // 简单的软件延时
static void HandleAlarm(void);                                                                                                                        // 处理闹钟触发
static void RunInitializationSequence(void);                                                                                                          // 运行启动初始化显示序列
static void ProcessButtonEvents(void);                                                                                                                // 处理按钮事件
static void HandleButtonShortPress(uint8_t button_num);                                                                                               // 处理短按按钮事件
static void HandleButtonLongPress(uint8_t button_num);                                                                                                // 处理长按按钮事件
static void HandleButtonIncrement(bool is_long_press_repeat);                                                                                         // 处理参数递增逻辑
static void Update7SegmentDisplay(void);                                                                                                              // 更新7段数码管显示
static void UpdateDisplayShift(void);                                                                                                                 // 更新显示移位效果
static void UpdateTimeAndDisplayBuffers(void);                                                                                                        // 更新时间、日期和显示缓冲区
static void ProcessUartCommand(void);                                                                                                                 // 处理UART接收到的命令
static void ParseUartInput(void);                                                                                                                     // 解析UART输入字符串为命令Token
static bool compareTokens(const command_token_t *token, const char *str_literal, uint8_t min_match_len);                                              // 比较命令Token与字符串
static bool compareFieldKeyword(const command_token_t *token, const char *full_keyword, uint8_t min_match_len);                                       // 比较字段关键字
static uint8_t toUpper(uint8_t c);                                                                                                                    // 字符转大写
static bool is_leap_year(uint16_t year_val);                                                                                                          // 判断是否为闰年
static bool is_valid_date(uint16_t y, uint8_t m, uint8_t d);                                                                                          // 检查日期是否有效
static bool is_valid_time(uint8_t h, uint8_t m, uint8_t s);                                                                                           // 检查时间是否有效
static void HandleModeTimeout(void);                                                                                                                  // 处理模式超时
static bool matchCommand(const command_token_t *t0, const command_token_t *t1, uint8_t num_tokens_total, const char *cmd_full_str);                   // 匹配命令，支持带冒号的命令
static bool compareTokens_modified_for_colon_prefix(const uint8_t *token_str_ptr, uint8_t token_len, const char *str_literal, uint8_t min_match_len); // 针对带冒号前缀的Token进行比较

int main(void)
{
    int i;
    DevicesInit(); // 初始化所有外设

    memset(time_transmit_buffer, 0, sizeof(time_transmit_buffer));   // 清空时间缓冲区
    memset(alarm_transmit_buffer, 0, sizeof(alarm_transmit_buffer)); // 清空闹钟缓冲区
    memset(date_transmit_buffer, 0, sizeof(date_transmit_buffer));   // 清空日期缓冲区

    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Initialization successful! Type HELP for commands.\r\n"); // 发送初始化成功消息

    UpdateTimeAndDisplayBuffers(); // 更新时间显示缓冲区
    shift = 0;                     // 重置显示移位量

    // 初始化按钮状态和定时器
    for (i = 0; i < 8; ++i)
    {
        key_debounce_timer[i] = 0;
        key_press_start_time[i] = 0;
        key_long_press_timer[i] = 0;
        key_states[i] = false;
        key_short_press_event[i] = false;
        key_long_press_start_event[i] = false;
        key_repeat_press_event[i] = false;
    }

    while (true) // 主循环
    {
        HandleAlarm(); // 处理闹钟触发

        if (init_flag) // 如果在初始化阶段
        {
            RunInitializationSequence(); // 运行初始化序列
        }
        else // 正常运行阶段
        {
            ProcessButtonEvents(); // 处理按钮事件

            if (mode_timeout_flag) // 如果模式超时
            {
                HandleModeTimeout();       // 处理模式超时逻辑
                mode_timeout_flag = false; // 清除标志
            }

            if (clock1s_flag) // 每秒更新
            {
                UpdateTimeAndDisplayBuffers(); // 更新时间、日期和显示缓冲区
                clock1s_flag = false;          // 清除标志
            }

            if (clock2ms_flag) // 每2毫秒更新7段数码管显示
            {
                Update7SegmentDisplay(); // 更新7段数码管显示
                clock2ms_flag = false;   // 清除标志
            }

            // 在流动模式下，根据移位设置更新显示移位效果
            if (current_mode == MODE_FLOWING && shifting == true && seven_segment_display_on == true)
            {
                if (shift_speed == false && clock500ms_flag) // 慢速移位
                {
                    UpdateDisplayShift();    // 更新显示移位
                    clock500ms_flag = false; // 清除标志
                }
                else if (shift_speed == true && clock300ms_flag) // 快速移位
                {
                    UpdateDisplayShift();    // 更新显示移位
                    clock300ms_flag = false; // 清除标志
                }
            }
        }
        ProcessUartCommand(); // 处理UART命令
    }
}

// 检查并处理闹钟，如果时间到达则启动PWM
static void HandleAlarm(void)
{
    if (hh == alm_hh && mm == alm_mm && ss == alm_ss && alm_hh != 25)
    {
        PWMStart(500); // 启动PWM作为闹钟提示音
    }
}

// 运行启动初始化显示序列，包括RTC校准和学生信息显示
static void RunInitializationSequence(void)
{
    uint8_t max_days_for_current_month;
    uint32_t pastSec_local_in_init;

    currentRTC = HibernateRTCGet(); // 获取当前RTC值
    HibernateDataGet(fetchRTC, 4);  // 获取休眠模块存储的数据

    // 如果有历史RTC数据且当前RTC大于上次存储的RTC，则根据时间差校准时钟
    if (fetchRTC[3] != 0 && currentRTC >= fetchRTC[3])
    {
        pastSec_local_in_init = currentRTC - fetchRTC[3]; // 计算休眠期间经过的秒数
        hh = (int8_t)fetchRTC[0];                         // 恢复小时
        mm = (int8_t)fetchRTC[1];                         // 恢复分钟
        ss = (int8_t)fetchRTC[2];                         // 恢复秒

        ss = (int8_t)(ss + pastSec_local_in_init); // 增加经过的秒数
        while (ss >= 60)                           // 处理秒溢出
        {
            mm++;
            ss -= 60;
        }
        while (mm >= 60) // 处理分钟溢出
        {
            hh++;
            mm -= 60;
        }
        while (hh >= 24) // 处理小时溢出和日期更新
        {
            day++;
            hh = 0;

            max_days_for_current_month = days_in_month[month];
            if (month == 2 && is_leap_year(year))
            {
                max_days_for_current_month = 29;
            }
            if (day > max_days_for_current_month) // 处理日期溢出
            {
                day = 1;
                month++;
                if (month > 12) // 处理月份溢出
                {
                    month = 1;
                    year++;
                }
            }
        }
    }
    else // 首次启动或RTC数据无效，设置默认时间
    {
        hh = 0;
        mm = 0;
        ss = 0;
        year = 2025;
        month = 6;
        day = 3;
        alm_hh = 25;
        alm_mm = 0;
        alm_ss = 0;
    }

    if (clock2ms_flag == true) // 每2ms刷新一次显示
    {
        clock2ms_flag = false;
        switch (init_procedure) // 根据初始化步骤显示不同内容
        {
        case 0:
            // 显示学生ID的一个字符，并关闭LED
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, stuID[cnt]);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
            break;
        case 1:
            // 清除数码管显示，并打开LED
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xff);
            break;
        case 2:
            // 显示名字的一个字符，并关闭LED
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, name[cnt]);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
            break;
        case 3:
            // 清除数码管显示，并打开LED
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xff);
            break;
        default:
            // 初始化完成，进入正常模式
            init_flag = false;
            shift = 0;
            rightshift = 0x01;
            cnt = 0;
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);

            // 保存当前时间为原始值，用于后续回滚
            original_year = year;
            original_month = month;
            original_day = day;
            original_hh = hh;
            original_mm = mm;
            original_ss = ss;
            original_alm_hh = alm_hh;
            original_alm_mm = alm_mm;
            original_alm_ss = alm_ss;
            unsaved_changes_active = false;
            seven_segment_display_on = true;
            break;
        }

        cnt++;                        // 切换到下一个数码管
        rightshift = rightshift << 1; // 移位位选
        if (cnt >= 0x8)               // 8个数码管循环
        {
            rightshift = 0x01;
            cnt = 0;
        }
    }

    if (clock900ms_flag == true) // 每900ms切换初始化步骤
    {
        clock900ms_flag = false;
        init_procedure++;
    }
}

// 遍历并处理所有按钮的事件
static void ProcessButtonEvents(void)
{
    int i = 0;

    for (i = 0; i < 8; ++i)
    {
        if (key_short_press_event[i]) // 如果有短按事件
        {
            HandleButtonShortPress((uint8_t)(i + 1)); // 处理短按
            key_short_press_event[i] = false;         // 清除标志
        }
        if (key_long_press_start_event[i]) // 如果有长按开始事件
        {
            HandleButtonLongPress((uint8_t)(i + 1)); // 处理长按
            key_long_press_start_event[i] = false;   // 清除标志
        }
        if (key_repeat_press_event[i]) // 如果有重复按事件
        {
            if (i == 5) // 按钮6 (i=5)是递增按钮
            {
                HandleButtonIncrement(true); // 处理重复递增
            }
            key_repeat_press_event[i] = false; // 清除标志
        }
    }
}

// 根据按钮编号处理短按功能
static void HandleButtonShortPress(uint8_t button_num)
{
    system_mode_t old_mode; // 保存旧模式

    mode_timeout_timer = g_system_tick; // 重置模式超时定时器

    switch (button_num)
    {
    case 1: // 按钮1: 切换流动方向
        if (current_mode == MODE_FLOWING)
        {
            shift_mode = !shift_mode;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flow direction toggled.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flow direction cannot be changed in setting mode.\r\n");
        }
        break;
    case 2: // 按钮2: 切换流动速度
        if (current_mode == MODE_FLOWING)
        {
            shift_speed = !shift_speed;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flow speed toggled.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flow speed cannot be changed in setting mode.\r\n");
        }
        break;
    case 3: // 按钮3: 暂停/恢复流动
        if (current_mode == MODE_FLOWING)
        {
            shifting = !shifting;
            if (shifting)
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flowing resumed.\r\n");
            }
            else
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flowing paused.\r\n");
            }
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flowing cannot be paused/resumed in setting mode.\r\n");
        }
        break;
    case 4: // 按钮4: 放弃更改并恢复原始设置
        if (current_mode == MODE_DATE_SET ||
            current_mode == MODE_TIME_SET ||
            current_mode == MODE_ALARM_SET ||
            (current_mode == MODE_FLOWING && unsaved_changes_active == true) ||
            current_mode == MODE_ALARM_DISPLAY ||
            long_press_saving_in_progress == true)
        {
            // 恢复所有时间、日期、闹钟和显示设置到原始状态
            year = original_year;
            month = original_month;
            day = original_day;
            hh = original_hh;
            mm = original_mm;
            ss = original_ss;
            alm_hh = original_alm_hh;
            alm_mm = original_alm_mm;
            alm_ss = original_alm_ss;
            PWMStop();                             // 停止闹钟
            current_mode = MODE_FLOWING;           // 切换回流动模式
            current_setting_field = FIELD_NONE;    // 清除设置字段
            is_blinking = false;                   // 停止闪烁
            shifting = prev_shifting;              // 恢复之前的移位状态
            shift_mode = prev_shift_mode;          // 恢复之前的移位方向
            shift_speed = prev_shift_speed;        // 恢复之前的移位速度
            unsaved_changes_active = false;        // 清除未保存更改标志
            long_press_saving_in_progress = false; // 清除长按保存标志
            seven_segment_display_on = true;       // 确保显示打开
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Changes abandoned, reverted to original settings.\r\n");
        }
        else if (current_mode == MODE_FLOWING && unsaved_changes_active == false)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"No pending changes or active modes to abandon.\r\n");
        }
        break;
    case 5: // 按钮5: 临时应用更改或切换闹钟显示模式
        if (current_mode == MODE_DATE_SET ||
            current_mode == MODE_TIME_SET ||
            current_mode == MODE_ALARM_SET)
        {
            // 在设置模式下，将临时值应用到当前值
            if (current_mode == MODE_DATE_SET)
            {
                if (is_valid_date(temp_year, temp_month, temp_day)) // 检查日期有效性
                {
                    year = temp_year;
                    month = temp_month;
                    day = temp_day;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Date temporarily applied.\r\n");
                }
                else
                {
                    // 如果日期无效，回滚到原始值
                    year = original_year;
                    month = original_month;
                    day = original_day;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid date, changes not applied!\r\n");
                }
            }
            else if (current_mode == MODE_TIME_SET)
            {
                if (is_valid_time(temp_hh, temp_mm, temp_ss)) // 检查时间有效性
                {
                    hh = temp_hh;
                    mm = temp_mm;
                    ss = temp_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Time temporarily applied.\r\n");
                }
                else
                {
                    // 如果时间无效，回滚到原始值
                    hh = original_hh;
                    mm = original_mm;
                    ss = original_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid time, changes not applied!\r\n");
                }
            }
            else if (current_mode == MODE_ALARM_SET)
            {
                if (is_valid_time(temp_alm_hh, temp_alm_mm, temp_alm_ss)) // 检查闹钟时间有效性
                {
                    alm_hh = temp_alm_hh;
                    alm_mm = temp_alm_mm;
                    alm_ss = temp_alm_ss;
                    PWMStop(); // 停止闹钟
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Alarm temporarily applied.\r\n");
                }
                else
                {
                    // 如果闹钟时间无效，回滚到原始值
                    alm_hh = original_alm_hh;
                    alm_mm = original_alm_mm;
                    alm_ss = original_alm_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid alarm time, changes not applied!\r\n");
                }
            }
            current_mode = MODE_FLOWING;        // 切换回流动模式
            current_setting_field = FIELD_NONE; // 清除设置字段
            is_blinking = false;                // 停止闪烁
            shifting = true;                    // 恢复流动
            shift_mode = prev_shift_mode;       // 恢复之前的移位方向
            shift_speed = prev_shift_speed;     // 恢复之前的移位速度
            unsaved_changes_active = true;      // 标记有未保存更改
            seven_segment_display_on = true;    // 确保显示打开
        }
        else if (current_mode == MODE_FLOWING) // 如果在流动模式下
        {
            // 切换到闹钟显示模式
            current_mode = MODE_ALARM_DISPLAY;
            prev_shifting = shifting;        // 保存当前移位状态
            prev_shift_mode = shift_mode;    // 保存当前移位方向
            prev_shift_speed = shift_speed;  // 保存当前移位速度
            shifting = false;                // 暂停流动
            seven_segment_display_on = true; // 确保显示打开
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Displaying alarm time.\r\n");
        }
        else if (current_mode == MODE_ALARM_DISPLAY) // 如果在闹钟显示模式下
        {
            // 切换回流动模式
            current_mode = MODE_FLOWING;
            shifting = prev_shifting;        // 恢复之前的移位状态
            shift_mode = prev_shift_mode;    // 恢复之前的移位方向
            shift_speed = prev_shift_speed;  // 恢复之前的移位速度
            seven_segment_display_on = true; // 确保显示打开
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Exited alarm display, returning to flowing mode.\r\n");
        }
        break;
    case 6:              // 按钮6: 递增当前设置字段
        if (is_blinking) // 只有当有字段正在闪烁时才能递增
        {
            HandleButtonIncrement(false); // 递增字段
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Field incremented.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"No field selected for incrementing.\r\n");
        }
        break;
    case 7:                                                                     // 按钮7: 切换设置字段
        if (current_mode != MODE_FLOWING && current_mode != MODE_ALARM_DISPLAY) // 仅在设置模式下有效
        {
            // 根据当前设置模式，循环切换设置字段（年/月/日 或 时/分/秒）
            if (current_mode == MODE_DATE_SET)
            {
                if (current_setting_field == FIELD_YEAR)
                {
                    current_setting_field = FIELD_MONTH;
                }
                else if (current_setting_field == FIELD_MONTH)
                {
                    current_setting_field = FIELD_DAY;
                }
                else
                {
                    current_setting_field = FIELD_YEAR;
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Cycling date field.\r\n");
            }
            else if (current_mode == MODE_TIME_SET)
            {
                if (current_setting_field == FIELD_HOUR)
                {
                    current_setting_field = FIELD_MINUTE;
                }
                else if (current_setting_field == FIELD_MINUTE)
                {
                    current_setting_field = FIELD_SECOND;
                }
                else
                {
                    current_setting_field = FIELD_HOUR;
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Cycling time field.\r\n");
            }
            else if (current_mode == MODE_ALARM_SET)
            {
                if (current_setting_field == FIELD_ALARM_HOUR)
                {
                    current_setting_field = FIELD_ALARM_MINUTE;
                }
                else if (current_setting_field == FIELD_ALARM_MINUTE)
                {
                    current_setting_field = FIELD_ALARM_SECOND;
                }
                else
                {
                    current_setting_field = FIELD_ALARM_HOUR;
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Cycling alarm field.\r\n");
            }
            is_blinking = true;              // 启动闪烁
            blink_timer = 0;                 // 重置闪烁定时器
            seven_segment_display_on = true; // 确保显示打开
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Field cycling not available in current mode.\r\n");
        }
        break;
    case 8: // 按钮8: 进入/退出设置模式或停止闪烁
        old_mode = current_mode;
        if (is_blinking && (old_mode == MODE_DATE_SET || old_mode == MODE_TIME_SET || old_mode == MODE_ALARM_SET))
        {
            // 如果正在闪烁，停止闪烁
            is_blinking = false;
            current_setting_field = FIELD_NONE;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Blinking stopped.\r\n");
        }
        else
        {
            // 根据当前模式，切换到下一个设置模式
            if (old_mode == MODE_FLOWING || old_mode == MODE_ALARM_DISPLAY)
            {
                // 保存当前状态作为原始值，用于回滚
                original_year = year;
                original_month = month;
                original_day = day;
                original_hh = hh;
                original_mm = mm;
                original_ss = ss;
                original_alm_hh = alm_hh;
                original_alm_mm = alm_mm;
                original_alm_ss = alm_ss;
                unsaved_changes_active = false; // 清除未保存更改标志

                current_mode = MODE_DATE_SET; // 进入日期设置模式
                temp_year = year;             // 将当前值复制到临时变量
                temp_month = month;
                temp_day = day;
                prev_shifting = shifting;           // 保存当前移位状态
                prev_shift_mode = shift_mode;       // 保存当前移位方向
                prev_shift_speed = shift_speed;     // 保存当前移位速度
                shifting = false;                   // 暂停流动
                current_setting_field = FIELD_YEAR; // 默认设置年份
                is_blinking = true;                 // 开始闪烁
                blink_timer = 0;                    // 重置闪烁定时器
                seven_segment_display_on = true;    // 确保显示打开
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Entering Date Setting Mode. Year field blinking.\r\n");
            }
            else if (old_mode == MODE_DATE_SET)
            {
                current_mode = MODE_TIME_SET; // 进入时间设置模式
                temp_hh = hh;                 // 将当前值复制到临时变量
                temp_mm = mm;
                temp_ss = ss;
                current_setting_field = FIELD_HOUR; // 默认设置小时
                is_blinking = true;                 // 开始闪烁
                blink_timer = 0;                    // 重置闪烁定时器
                seven_segment_display_on = true;    // 确保显示打开
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Entering Time Setting Mode. Hour field blinking.\r\n");
            }
            else if (old_mode == MODE_TIME_SET)
            {
                current_mode = MODE_ALARM_SET; // 进入闹钟设置模式
                temp_alm_hh = alm_hh;          // 将当前值复制到临时变量
                temp_alm_mm = alm_mm;
                temp_alm_ss = alm_ss;
                current_setting_field = FIELD_ALARM_HOUR; // 默认设置闹钟小时
                is_blinking = true;                       // 开始闪烁
                blink_timer = 0;                          // 重置闪烁定时器
                seven_segment_display_on = true;          // 确保显示打开
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Entering Alarm Setting Mode. Alarm Hour field blinking.\r\n");
            }
            else if (old_mode == MODE_ALARM_SET)
            {
                current_mode = MODE_FLOWING;        // 退出设置模式，回到流动显示
                current_setting_field = FIELD_NONE; // 清除设置字段
                is_blinking = false;                // 停止闪烁
                shifting = prev_shifting;           // 恢复之前的移位状态
                shift_mode = prev_shift_mode;       // 恢复之前的移位方向
                shift_speed = prev_shift_speed;     // 恢复之前的移位速度
                unsaved_changes_active = true;      // 标记有未保存更改
                seven_segment_display_on = true;    // 确保显示打开
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Exiting Setting Modes, returning to flowing display. Changes temporarily applied.\r\n");
            }
        }
        break;
    }
}

// 根据按钮编号处理长按功能
static void HandleButtonLongPress(uint8_t button_num)
{
    mode_timeout_timer = g_system_tick; // 重置模式超时定时器

    switch (button_num)
    {
    case 8:                                 // 按钮8: 永久保存设置
        if (!long_press_saving_in_progress) // 避免重复保存
        {
            // 根据当前模式，将临时或当前值永久保存
            if (current_mode == MODE_DATE_SET)
            {
                if (is_valid_date(temp_year, temp_month, temp_day)) // 检查日期有效性
                {
                    year = temp_year;
                    month = temp_month;
                    day = temp_day;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Date truly saved.\r\n");
                }
                else
                {
                    // 如果日期无效，回滚到原始值
                    year = original_year;
                    month = original_month;
                    day = original_day;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid date, not truly saved! Reverted.\r\n");
                }
            }
            else if (current_mode == MODE_TIME_SET)
            {
                if (is_valid_time(temp_hh, temp_mm, temp_ss)) // 检查时间有效性
                {
                    hh = temp_hh;
                    mm = temp_mm;
                    ss = temp_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Time truly saved.\r\n");
                }
                else
                {
                    // 如果时间无效，回滚到原始值
                    hh = original_hh;
                    mm = original_mm;
                    ss = original_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid time, not truly saved! Reverted.\r\n");
                }
            }
            else if (current_mode == MODE_ALARM_SET)
            {
                if (is_valid_time(temp_alm_hh, temp_alm_mm, temp_alm_ss)) // 检查闹钟时间有效性
                {
                    alm_hh = temp_alm_hh;
                    alm_mm = temp_alm_mm;
                    alm_ss = temp_alm_ss;
                    PWMStop(); // 停止闹钟
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Alarm truly saved.\r\n");
                }
                else
                {
                    // 如果闹钟时间无效，回滚到原始值
                    alm_hh = original_alm_hh;
                    alm_mm = original_alm_mm;
                    alm_ss = original_alm_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid alarm time, not truly saved! Reverted.\r\n");
                }
            }
            else if (current_mode == MODE_FLOWING || current_mode == MODE_ALARM_DISPLAY)
            {
                // 保存当前所有设置作为原始值
                original_year = year;
                original_month = month;
                original_day = day;
                original_hh = hh;
                original_mm = mm;
                original_ss = ss;
                original_alm_hh = alm_hh;
                original_alm_mm = alm_mm;
                original_alm_ss = alm_ss;
                PWMStop(); // 停止闹钟
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current settings truly saved.\r\n");
            }

            long_press_saving_in_progress = true; // 标记正在保存
            unsaved_changes_active = false;       // 清除未保存更改标志

            save_blink_active = true;         // 激活保存成功闪烁
            save_blink_timer = g_system_tick; // 重置保存闪烁定时器

            is_blinking = false;                // 停止普通闪烁
            current_setting_field = FIELD_NONE; // 清除设置字段
            seven_segment_display_on = true;    // 确保显示打开
        }
        break;

    default: // 其他按钮长按无功能
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Long press has no function for this button.\r\n");
        break;
    }
}

// 处理参数递增逻辑
static void HandleButtonIncrement(bool is_long_press_repeat)
{
    uint8_t current_days_in_month;

    mode_timeout_timer = g_system_tick; // 重置模式超时定时器

    if (long_press_saving_in_progress) // 如果正在保存，则不能递增
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Saving in progress, cannot increment.\r\n");
        return;
    }

    if (!is_blinking) // 如果没有字段在闪烁，则不能递增
    {
        return;
    }

    // 根据当前设置字段递增相应的值
    switch (current_setting_field)
    {
    case FIELD_YEAR: // 年份递增
        temp_year++;
        if (temp_year > 2099) // 2000-2099循环
            temp_year = 2000;

        // 如果月份是2月，可能需要调整日期
        if (temp_month == 2)
        {
            current_days_in_month = is_leap_year(temp_year) ? 29 : 28;
            if (temp_day > current_days_in_month)
                temp_day = current_days_in_month;
        }
        break;
    case FIELD_MONTH: // 月份递增
        temp_month++;
        if (temp_month > 12) // 1-12循环
            temp_month = 1;

        // 根据新月份和年份调整日期
        current_days_in_month = days_in_month[temp_month];
        if (temp_month == 2 && is_leap_year(temp_year))
            current_days_in_month = 29;
        if (temp_day > current_days_in_month)
            temp_day = current_days_in_month;
        break;
    case FIELD_DAY: // 日期递增
        temp_day++;
        current_days_in_month = days_in_month[temp_month];
        if (temp_month == 2 && is_leap_year(temp_year))
            current_days_in_month = 29;
        if (temp_day > current_days_in_month) // 超过最大天数则回到1
            temp_day = 1;
        break;
    case FIELD_HOUR: // 小时递增
        temp_hh++;
        if (temp_hh > 23) // 0-23循环
            temp_hh = 0;
        break;
    case FIELD_MINUTE: // 分钟递增
        temp_mm++;
        if (temp_mm > 59) // 0-59循环
            temp_mm = 0;
        break;
    case FIELD_SECOND: // 秒递增
        temp_ss++;
        if (temp_ss > 59) // 0-59循环
            temp_ss = 0;
        break;
    case FIELD_ALARM_HOUR: // 闹钟小时递增
        temp_alm_hh++;
        if (temp_alm_hh > 23) // 0-23循环
            temp_alm_hh = 0;
        break;
    case FIELD_ALARM_MINUTE: // 闹钟分钟递增
        temp_alm_mm++;
        if (temp_alm_mm > 59) // 0-59循环
            temp_alm_mm = 0;
        break;
    case FIELD_ALARM_SECOND: // 闹钟秒递增
        temp_alm_ss++;
        if (temp_alm_ss > 59) // 0-59循环
            temp_alm_ss = 0;
        break;
    default:
        break;
    }
}

// 更新7段数码管显示
static void Update7SegmentDisplay(void)
{
    uint8_t segment_data;                // 当前数码管段码
    bool digit_should_blink_off = false; // 数字是否应该闪烁关闭
    uint8_t local_cnt;                   // 本地数码管计数
    uint8_t displayed_char_idx;          // 显示字符在主缓冲区中的索引
    uint8_t effective_segment_data;      // 实际发送的段码
    uint8_t prev_original_char_idx;      // 前一个字符的原始索引

    if (!seven_segment_display_on) // 如果7段数码管关闭
    {
        // 关闭所有数码管
        result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
        result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, 0x00);
        result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
        return;
    }

    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00); // 确保所有位选都关闭

    local_cnt = cnt; // 获取当前轮询的数码管索引

    if (current_mode == MODE_FLOWING) // 流动显示模式
    {
        if (display_reversed_order) // 反向显示
        {
            displayed_char_idx = (uint8_t)((shift + (7 - local_cnt)) % 18);
        }
        else // 正常显示
        {
            displayed_char_idx = (uint8_t)((shift + local_cnt) % 18);
        }
        segment_data = master_display_buffer[displayed_char_idx]; // 从主显示缓冲区获取数据
    }
    else if (current_mode == MODE_DATE_SET) // 日期设置模式
    {
        // 根据local_cnt显示年、月、日
        if (local_cnt == 0)
            segment_data = seg7[(temp_year / 1000) % 10];
        else if (local_cnt == 1)
            segment_data = seg7[(temp_year / 100) % 10];
        else if (local_cnt == 2)
            segment_data = seg7[(temp_year / 10) % 10];
        else if (local_cnt == 3)
            segment_data = seg7[(temp_year % 10)] | 0x80; // 年份末位带小数点
        else if (local_cnt == 4)
            segment_data = seg7[temp_month / 10];
        else if (local_cnt == 5)
            segment_data = seg7[temp_month % 10] | 0x80; // 月份末位带小数点
        else if (local_cnt == 6)
            segment_data = seg7[temp_day / 10];
        else if (local_cnt == 7)
            segment_data = seg7[temp_day % 10];
        else
            segment_data = 0x00; // 未使用数码管

        // 处理闪烁效果
        if (is_blinking && (g_system_tick % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS)) >= BLINK_ON_TIME_MS)
        {
            // 根据当前设置字段，判断是否需要闪烁关闭
            if ((current_setting_field == FIELD_YEAR && local_cnt <= 3) ||
                (current_setting_field == FIELD_MONTH && local_cnt >= 4 && local_cnt <= 5) ||
                (current_setting_field == FIELD_DAY && local_cnt >= 6 && local_cnt <= 7))
            {
                digit_should_blink_off = true;
            }
        }
        if (digit_should_blink_off)
            segment_data = 0x00; // 闪烁关闭时显示空白
    }
    else if (current_mode == MODE_TIME_SET) // 时间设置模式
    {
        // 根据local_cnt显示时、分、秒
        if (local_cnt == 0 || local_cnt == 7) // 两端空白
            segment_data = 0x00;
        else if (local_cnt == 1)
            segment_data = seg7[temp_hh / 10];
        else if (local_cnt == 2)
            segment_data = seg7[temp_hh % 10] | 0x80; // 小时末位带小数点
        else if (local_cnt == 3)
            segment_data = seg7[temp_mm / 10];
        else if (local_cnt == 4)
            segment_data = seg7[temp_mm % 10] | 0x80; // 分钟末位带小数点
        else if (local_cnt == 5)
            segment_data = seg7[temp_ss / 10];
        else if (local_cnt == 6)
            segment_data = seg7[temp_ss % 10];
        else
            segment_data = 0x00; // 未使用数码管

        // 处理闪烁效果
        if (is_blinking && (g_system_tick % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS)) >= BLINK_ON_TIME_MS)
        {
            // 根据当前设置字段，判断是否需要闪烁关闭
            if ((current_setting_field == FIELD_HOUR && local_cnt >= 1 && local_cnt <= 2) ||
                (current_setting_field == FIELD_MINUTE && local_cnt >= 3 && local_cnt <= 4) ||
                (current_setting_field == FIELD_SECOND && local_cnt >= 5 && local_cnt <= 6))
            {
                digit_should_blink_off = true;
            }
        }
        if (digit_should_blink_off)
            segment_data = 0x00; // 闪烁关闭时显示空白
    }
    else if (current_mode == MODE_ALARM_SET) // 闹钟设置模式
    {
        // 根据local_cnt显示闹钟时、分、秒
        if (local_cnt == 0 || local_cnt == 7) // 两端空白
            segment_data = 0x00;
        else if (local_cnt == 1)
            segment_data = seg7[temp_alm_hh / 10];
        else if (local_cnt == 2)
            segment_data = seg7[temp_alm_hh % 10] | 0x80; // 闹钟小时末位带小数点
        else if (local_cnt == 3)
            segment_data = seg7[temp_alm_mm / 10];
        else if (local_cnt == 4)
            segment_data = seg7[temp_alm_mm % 10] | 0x80; // 闹钟分钟末位带小数点
        else if (local_cnt == 5)
            segment_data = seg7[temp_alm_ss / 10];
        else if (local_cnt == 6)
            segment_data = seg7[temp_alm_ss % 10];
        else
            segment_data = 0x00; // 未使用数码管

        // 处理闪烁效果
        if (is_blinking && (g_system_tick % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS)) >= BLINK_ON_TIME_MS)
        {
            // 根据当前设置字段，判断是否需要闪烁关闭
            if ((current_setting_field == FIELD_ALARM_HOUR && local_cnt >= 1 && local_cnt <= 2) ||
                (current_setting_field == FIELD_ALARM_MINUTE && local_cnt >= 3 && local_cnt <= 4) ||
                (current_setting_field == FIELD_ALARM_SECOND && local_cnt >= 5 && local_cnt <= 6))
            {
                digit_should_blink_off = true;
            }
        }
        if (digit_should_blink_off)
            segment_data = 0x00; // 闪烁关闭时显示空白
    }
    else if (current_mode == MODE_ALARM_DISPLAY) // 闹钟显示模式
    {
        // 显示“ALARM”字样和闹钟时间
        if (local_cnt == 0) // 'A'
            segment_data = seg7[10];
        else if (local_cnt == 1) // 'L'
            segment_data = 0x38;
        else if (local_cnt == 2) // 小时高位 或 'x' (如果未设置)
            segment_data = (alm_hh == 25) ? seg7[17] : seg7[alm_hh / 10];
        else if (local_cnt == 3) // 小时低位 或 'x' (如果未设置), 带小数点
            segment_data = (alm_hh == 25) ? seg7[17] : (seg7[alm_hh % 10] | 0x80);
        else if (local_cnt == 4) // 分钟高位 或 'x' (如果未设置)
            segment_data = (alm_hh == 25) ? seg7[17] : seg7[alm_mm / 10];
        else if (local_cnt == 5) // 分钟低位 或 'x' (如果未设置), 带小数点
            segment_data = (alm_hh == 25) ? seg7[17] : (seg7[alm_mm % 10] | 0x80);
        else if (local_cnt == 6) // 秒高位 或 'x' (如果未设置)
            segment_data = (alm_hh == 25) ? seg7[17] : seg7[alm_ss / 10];
        else if (local_cnt == 7) // 秒低位 或 'x' (如果未设置)
            segment_data = (alm_hh == 25) ? seg7[17] : seg7[alm_ss % 10];
        else
            segment_data = 0x00; // 未使用数码管
    }
    else
    {
        // 默认情况下，显示主显示缓冲区的内容
        segment_data = master_display_buffer[(local_cnt + shift) % 18];
    }

    effective_segment_data = segment_data;

    if (current_mode == MODE_FLOWING && display_reversed_order) // 反向流动模式下处理小数点显示
    {
        effective_segment_data &= ~0x80;                                 // 清除当前位的小数点
        prev_original_char_idx = (displayed_char_idx - 1 + 18) % 18;     // 获取前一个字符的原始索引
        if ((master_display_buffer[prev_original_char_idx] & 0x80) != 0) // 如果前一个字符有小数点
        {
            effective_segment_data |= 0x80; // 则当前位显示小数点
        }
    }

    // 处理保存成功后的闪烁效果
    if (save_blink_active && (g_system_tick - save_blink_timer) % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS) >= BLINK_ON_TIME_MS)
    {
        effective_segment_data = 0x00; // 闪烁关闭时显示空白
    }

    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, effective_segment_data); // 发送段码
    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);             // 发送位选
    result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, ~rightshift);                  // 外部LED的位选（与数码管位选互补）

    cnt++;                        // 切换到下一个数码管
    rightshift = rightshift << 1; // 移位位选
    if (cnt >= 0x8)               // 8个数码管循环
    {
        rightshift = 0x01;
        cnt = 0;
    }
}

// 更新显示移位效果
static void UpdateDisplayShift(void)
{
    if (shift_mode == false) // 左移模式
    {
        shift++;     // 移位量增加
        shift %= 18; // 0-17循环
    }
    else // 右移模式
    {
        shift--; // 移位量减少
        if (shift < 0)
            shift = 17; // 负数则回到17
    }
}

// 更新时间、日期和显示缓冲区
static void UpdateTimeAndDisplayBuffers(void)
{
    // 格式化时间字符串
    time_transmit_buffer[0] = (uint8_t)(hh / 10) + '0';
    time_transmit_buffer[1] = (uint8_t)(hh % 10) + '0';
    time_transmit_buffer[2] = ':';
    time_transmit_buffer[3] = (uint8_t)(mm / 10) + '0';
    time_transmit_buffer[4] = (uint8_t)(mm % 10) + '0';
    time_transmit_buffer[5] = ':';
    time_transmit_buffer[6] = (uint8_t)(ss / 10) + '0';
    time_transmit_buffer[7] = (uint8_t)(ss % 10) + '0';
    time_transmit_buffer[8] = '\0';

    // 格式化闹钟时间字符串，如果未设置则显示"xx:xx:xx"
    if (alm_hh == 25)
    {
        memcpy(alarm_transmit_buffer, "xx:xx:xx", 8);
    }
    else
    {
        alarm_transmit_buffer[0] = (uint8_t)(alm_hh / 10) + '0';
        alarm_transmit_buffer[1] = (uint8_t)(alm_hh % 10) + '0';
        alarm_transmit_buffer[2] = ':';
        alarm_transmit_buffer[3] = (uint8_t)(alm_mm / 10) + '0';
        alarm_transmit_buffer[4] = (uint8_t)(alm_mm % 10) + '0';
        alarm_transmit_buffer[5] = ':';
        alarm_transmit_buffer[6] = (uint8_t)(alm_ss / 10) + '0';
        alarm_transmit_buffer[7] = (uint8_t)(alm_ss % 10) + '0';
    }
    alarm_transmit_buffer[8] = '\0';

    // 格式化日期字符串
    date_transmit_buffer[0] = (uint8_t)((year / 1000) % 10) + '0';
    date_transmit_buffer[1] = (uint8_t)((year / 100) % 10) + '0';
    date_transmit_buffer[2] = (uint8_t)((year / 10) % 10) + '0';
    date_transmit_buffer[3] = (uint8_t)(year % 10) + '0';
    date_transmit_buffer[4] = '-';
    date_transmit_buffer[5] = (uint8_t)(month / 10) + '0';
    date_transmit_buffer[6] = (uint8_t)(month % 10) + '0';
    date_transmit_buffer[7] = '-';
    date_transmit_buffer[8] = (uint8_t)(day / 10) + '0';
    date_transmit_buffer[9] = (uint8_t)(day % 10) + '0';
    date_transmit_buffer[10] = '\0';

    // 填充主显示缓冲区，用于7段数码管滚动显示
    master_display_buffer[0] = seg7[(year / 1000) % 10];
    master_display_buffer[1] = seg7[(year / 100) % 10];
    master_display_buffer[2] = seg7[(year / 10) % 10];
    master_display_buffer[3] = seg7[(year % 10)] | 0x80; // 年份末位带小数点
    master_display_buffer[4] = seg7[month / 10];
    master_display_buffer[5] = seg7[month % 10] | 0x80; // 月份末位带小数点
    master_display_buffer[6] = seg7[day / 10];
    master_display_buffer[7] = seg7[day % 10];

    master_display_buffer[8] = 0x00; // 分隔符
    master_display_buffer[9] = 0x00; // 分隔符

    master_display_buffer[10] = seg7[hh / 10];
    master_display_buffer[11] = seg7[hh % 10] | 0x80; // 小时末位带小数点
    master_display_buffer[12] = seg7[mm / 10];
    master_display_buffer[13] = seg7[mm % 10] | 0x80; // 分钟末位带小数点
    master_display_buffer[14] = seg7[ss / 10];
    master_display_buffer[15] = seg7[ss % 10];

    master_display_buffer[16] = 0x00; // 填充
    master_display_buffer[17] = 0x00; // 填充

    // 存储当前时间到休眠模块的RTC存储
    storedRTC[0] = (uint32_t)hh;
    storedRTC[1] = (uint32_t)mm;
    storedRTC[2] = (uint32_t)ss;
    storedRTC[3] = HibernateRTCGet(); // 存储当前RTC计数值
    HibernateDataSet(storedRTC, 4);   // 保存数据
}

// 将字符转换为大写
static uint8_t toUpper(uint8_t c)
{
    if (c >= 'a' && c <= 'z')
    {
        return (uint8_t)(c - 0x20);
    }
    return c;
}

// 比较命令Token与字符串，支持最小匹配长度
static bool compareTokens(const command_token_t *token, const char *str_literal, uint8_t min_match_len)
{
    uint8_t i;
    uint8_t literal_len;

    literal_len = 0;
    while (str_literal[literal_len] != '\0')
    {
        literal_len++;
    }

    // Token长度必须在最小匹配长度和字面量长度之间
    if (token->token_len < min_match_len || token->token_len > literal_len)
    {
        return false;
    }

    // 逐个字符比较 (不区分大小写)
    for (i = 0; i < token->token_len; ++i)
    {
        if (toUpper(token->token_str[i]) != toUpper((uint8_t)str_literal[i]))
        {
            return false;
        }
    }
    return true;
}

// 比较字段关键字（如"YEAR", "MONTH"），支持最小匹配长度
static bool compareFieldKeyword(const command_token_t *token, const char *full_keyword, uint8_t min_match_len)
{
    uint8_t i;
    uint8_t full_keyword_len = (uint8_t)strlen(full_keyword);

    // Token长度必须大于等于最小匹配长度
    if (token->token_len < min_match_len)
    {
        return false;
    }

    // Token长度不能超过完整关键字长度
    if (token->token_len > full_keyword_len)
    {
        return false;
    }

    // 逐个字符比较 (不区分大小写)
    for (i = 0; i < token->token_len; ++i)
    {
        if (toUpper(token->token_str[i]) != toUpper((uint8_t)full_keyword[i]))
        {
            return false;
        }
    }
    return true;
}

// 特殊的Token比较函数，用于处理带冒号前缀的Token
static bool compareTokens_modified_for_colon_prefix(const uint8_t *token_str_ptr, uint8_t token_len, const char *str_literal, uint8_t min_match_len)
{
    uint8_t i;
    uint8_t literal_len;

    literal_len = (uint8_t)strlen(str_literal);

    // Token长度必须在最小匹配长度和字面量长度之间
    if (token_len < min_match_len || token_len > literal_len)
    {
        return false;
    }

    // 逐个字符比较 (不区分大小写)
    for (i = 0; i < token_len; ++i)
    {
        if (toUpper(token_str_ptr[i]) != toUpper((uint8_t)str_literal[i]))
        {
            return false;
        }
    }
    return true;
}

// 匹配命令，支持形如 `*CMD:SUB_CMD` 的命令格式
static bool matchCommand(const command_token_t *t0, const command_token_t *t1, uint8_t num_tokens_total, const char *cmd_full_str)
{
    uint8_t colon_idx = 0;
    uint8_t i;
    bool has_colon_in_full_str = false;
    uint8_t literal_len_full_str;
    const char *prefix_str_literal;
    uint8_t prefix_len_literal;
    const char *suffix_str_literal;
    uint8_t suffix_len_literal;

    literal_len_full_str = 0;
    while (cmd_full_str[literal_len_full_str] != '\0')
    {
        literal_len_full_str++;
    }

    // 查找完整命令字符串中的冒号位置
    for (i = 0; i < literal_len_full_str; ++i)
    {
        if (cmd_full_str[i] == ':')
        {
            colon_idx = i;
            has_colon_in_full_str = true;
            break;
        }
    }

    if (!has_colon_in_full_str) // 如果命令字符串不包含冒号 (如 "*RST")
    {
        // 直接比较第一个Token与完整命令字符串
        return (num_tokens_total >= 1) && compareTokens(t0, cmd_full_str, literal_len_full_str);
    }

    // 如果命令字符串包含冒号 (如 "*SET:DATE")
    prefix_str_literal = cmd_full_str; // 冒号前的部分
    prefix_len_literal = colon_idx;
    suffix_str_literal = cmd_full_str + colon_idx + 1; // 冒号后的部分
    suffix_len_literal = literal_len_full_str - (colon_idx + 1);

    // 首先尝试匹配整个命令作为一个Token (如 "*SET:DATE")
    if (compareTokens(t0, cmd_full_str, literal_len_full_str))
    {
        return true;
    }

    // 如果不匹配整个命令，则尝试匹配为两个Token (如 "*SET" 和 ":DATE" 或 "DATE")
    if (num_tokens_total >= 2 && t1 != NULL)
    {
        // 比较第一个Token与前缀部分
        if (!compareTokens(t0, prefix_str_literal, prefix_len_literal))
        {
            return false;
        }

        // 比较第二个Token与后缀部分
        if (t1->token_len > 0 && t1->token_str[0] == ':') // 如果第二个Token以冒号开头 (如 ":DATE")
        {
            return compareTokens_modified_for_colon_prefix(t1->token_str + 1, t1->token_len - 1, suffix_str_literal, suffix_len_literal);
        }
        else // 如果第二个Token不以冒号开头 (如 "DATE")
        {
            return compareTokens(t1, suffix_str_literal, suffix_len_literal);
        }
    }

    return false;
}

// 解析UART输入字符串为命令Token
static void ParseUartInput(void)
{
    uint8_t i = 0;
    uint8_t current_token_len = 0;
    bool in_token = false; // 是否在Token中

    num_parsed_tokens = 0; // 重置Token数量

    // 清空之前解析的Token
    for (i = 0; i < MAX_COMMAND_TOKENS; ++i)
    {
        parsed_tokens[i].token_len = 0;
        memset(parsed_tokens[i].token_str, 0, MAX_TOKEN_LENGTH);
    }

    i = 0;

    // 跳过开头的空格
    while (i < uart_receive_len && uart_receive_buffer[i] == ' ')
    {
        i++;
    }

    // 遍历接收缓冲区，解析Token
    while (i < uart_receive_len && num_parsed_tokens < MAX_COMMAND_TOKENS)
    {
        if (uart_receive_buffer[i] != ' ') // 如果当前字符不是空格
        {
            if (!in_token) // 如果不在Token中，开始一个新Token
            {
                current_token_len = 0;
                in_token = true;
            }
            if (current_token_len < MAX_TOKEN_LENGTH - 1) // 检查Token长度是否超出限制
            {
                // 将字符添加到当前Token
                parsed_tokens[num_parsed_tokens].token_str[current_token_len] = uart_receive_buffer[i];
                current_token_len++;
            }
        }
        else // 如果当前字符是空格
        {
            if (in_token) // 如果在Token中，则当前Token结束
            {
                parsed_tokens[num_parsed_tokens].token_len = current_token_len;
                parsed_tokens[num_parsed_tokens].token_str[current_token_len] = '\0'; // 字符串结束符
                num_parsed_tokens++;                                                  // 增加Token数量
                in_token = false;                                                     // 退出Token状态
            }
            // 跳过连续的空格
            while (i + 1 < uart_receive_len && uart_receive_buffer[i + 1] == ' ')
            {
                i++;
            }
        }
        i++;
    }

    // 处理最后一个Token (如果字符串不是以空格结束)
    if (in_token && num_parsed_tokens < MAX_COMMAND_TOKENS)
    {
        parsed_tokens[num_parsed_tokens].token_len = current_token_len;
        parsed_tokens[num_parsed_tokens].token_str[current_token_len] = '\0';
        num_parsed_tokens++;
    }
}

// 处理UART接收到的命令
static void ProcessUartCommand(void)
{
    uint8_t i;
    int parsed_val[6];         // 用于存储解析出的数值参数
    bool parse_ok;             // 解析是否成功
    uint8_t current_param_idx; // 当前参数的起始索引
    uint8_t field_token_idx;   // 字段Token的起始索引
    uint8_t val_token_idx;     // 值Token的起始索引
    bool found_arg;            // 是否找到有效参数

    if (cmd_state == 0) // 如果没有新命令
    {
        return;
    }

    cmd_state = 0;    // 清除命令状态
    ParseUartInput(); // 解析UART输入

    // 确定参数的起始索引，处理形如 "*CMD:SUB_CMD" 的命令
    if (num_parsed_tokens >= 2 && parsed_tokens[0].token_str[0] == '*' && parsed_tokens[1].token_len > 0 && parsed_tokens[1].token_str[0] == ':')
    {
        current_param_idx = 2; // 参数从第三个Token开始
    }
    else
    {
        current_param_idx = 1; // 参数从第二个Token开始
    }

    // 处理 "*RST" 命令 (复位)
    if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*RST"))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            SysCtlReset(); // 系统复位
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"System reset initiated.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *RST\r\n");
        }
    }

    // 处理 "*SET:DATE" 命令 (设置日期)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:DATE"))
    {
        parse_ok = false;
        field_token_idx = current_param_idx; // 字段Token的起始索引

        // 匹配 "YEAR MONTH DATE YYYY MM DD" 格式
        if (num_parsed_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&parsed_tokens[field_token_idx], "YEAR", 4) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MONTH", 5) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 2], "DATE", 3))
        {
            val_token_idx = field_token_idx + 3;                                                        // 值Token的起始索引
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);                       // 年
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str);                   // 月
            parsed_val[2] = atoi((char *)parsed_tokens[val_token_idx + 2].token_str);                   // 日
            if (is_valid_date((uint16_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2])) // 检查日期有效性
            {
                year = (uint16_t)parsed_val[0];
                month = (uint8_t)parsed_val[1];
                day = (uint8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "YEAR MONTH YYYY MM" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "YEAR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MONTH", 5))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 年
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 月
            if (parsed_val[0] >= 2000 && parsed_val[0] <= 2099 && parsed_val[1] >= 1 && parsed_val[1] <= 12)
            {
                year = (uint16_t)parsed_val[0];
                month = (uint8_t)parsed_val[1];
                if (!is_valid_date(year, month, day)) // 如果新年月导致日期无效，调整日期
                {
                    day = (uint8_t)(is_leap_year(year) && month == 2 ? 29 : days_in_month[month]);
                }
                parse_ok = true;
            }
        }

        // 匹配 "YEAR DATE YYYY DD" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "YEAR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "DATE", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 年
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 日
            if (is_valid_date((uint16_t)parsed_val[0], month, (uint8_t)parsed_val[1]))
            {
                year = (uint16_t)parsed_val[0];
                day = (uint8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MONTH DATE MM DD" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MONTH", 5) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "DATE", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 月
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 日
            if (is_valid_date(year, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1]))
            {
                month = (uint8_t)parsed_val[0];
                day = (uint8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "YEAR YYYY" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "YEAR", 4))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 年
            if (parsed_val[0] >= 2000 && parsed_val[0] <= 2099)
            {
                year = (uint16_t)parsed_val[0];
                if (!is_valid_date(year, month, day)) // 如果新年份导致日期无效，调整日期
                {
                    if (month == 2)
                    {
                        day = (uint8_t)(is_leap_year(year) ? 29 : 28);
                    }
                }
                parse_ok = true;
            }
        }

        // 匹配 "MONTH MM" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MONTH", 5))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 月
            if (parsed_val[0] >= 1 && parsed_val[0] <= 12)
            {
                month = (uint8_t)parsed_val[0];
                if (!is_valid_date(year, month, day)) // 如果新月份导致日期无效，调整日期
                {
                    day = days_in_month[month];
                    if (month == 2 && is_leap_year(year))
                    {
                        day = 29;
                    }
                }
                parse_ok = true;
            }
        }

        // 匹配 "DATE DD" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "DATE", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 日
            if (is_valid_date(year, month, (uint8_t)parsed_val[0]))
            {
                day = (uint8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，保存原始值并发送成功消息
        {
            original_year = year;
            original_month = month;
            original_day = day;
            unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Date set successfully.\r\n");
        }
        else // 解析失败，发送错误消息
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format or date values. Type HELP for usage.\r\n");
        }
    }

    // 处理 "*SET:TIME" 命令 (设置时间)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:TIME"))
    {
        parse_ok = false;
        field_token_idx = current_param_idx;

        // 匹配 "HOUR MINUTE SECOND HH MM SS" 格式
        if (num_parsed_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MINUTE", 3) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 2], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 3;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 分
            parsed_val[2] = atoi((char *)parsed_tokens[val_token_idx + 2].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2]))
            {
                hh = (int8_t)parsed_val[0];
                mm = (int8_t)parsed_val[1];
                ss = (int8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR MINUTE HH MM" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MINUTE", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 分
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], ss))
            {
                hh = (int8_t)parsed_val[0];
                mm = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR SECOND HH SS" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], mm, (uint8_t)parsed_val[1]))
            {
                hh = (int8_t)parsed_val[0];
                ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE SECOND MM SS" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MINUTE", 3) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 分
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time(hh, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1]))
            {
                mm = (int8_t)parsed_val[0];
                ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR HH" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 时
            if (parsed_val[0] >= 0 && parsed_val[0] < 24)
            {
                hh = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE MM" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MINUTE", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 分
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                mm = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "SECOND SS" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 秒
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                ss = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，保存原始值并发送成功消息
        {
            original_hh = hh;
            original_mm = mm;
            original_ss = ss;
            unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Time set successfully.\r\n");
        }
        else // 解析失败，发送错误消息
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format or time values. Type HELP for usage.\r\n");
        }
    }

    // 处理 "*SET:ALARM" 命令 (设置闹钟)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:ALARM"))
    {
        parse_ok = false;
        field_token_idx = current_param_idx;

        // 匹配 "HOUR MINUTE SECOND HH MM SS" 格式
        if (num_parsed_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MINUTE", 3) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 2], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 3;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 分
            parsed_val[2] = atoi((char *)parsed_tokens[val_token_idx + 2].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2]))
            {
                alm_hh = (int8_t)parsed_val[0];
                alm_mm = (int8_t)parsed_val[1];
                alm_ss = (int8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR MINUTE HH MM" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MINUTE", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 分
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], alm_ss))
            {
                alm_hh = (int8_t)parsed_val[0];
                alm_mm = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR SECOND HH SS" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], alm_mm, (uint8_t)parsed_val[1]))
            {
                alm_hh = (int8_t)parsed_val[0];
                alm_ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE SECOND MM SS" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MINUTE", 3) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 分
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time(alm_hh, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1]))
            {
                alm_mm = (int8_t)parsed_val[0];
                alm_ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR HH" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 时
            if (parsed_val[0] >= 0 && parsed_val[0] < 24)
            {
                alm_hh = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE MM" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MINUTE", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 分
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                alm_mm = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "SECOND SS" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 秒
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                alm_ss = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，停止闹钟，保存原始值并发送成功消息
        {
            PWMStop();
            original_alm_hh = alm_hh;
            original_alm_mm = alm_mm;
            original_alm_ss = alm_ss;
            unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Alarm set successfully.\r\n");
        }
        else // 解析失败，发送错误消息
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format or alarm time values. Type HELP for usage.\r\n");
        }
    }

    // 处理 "*SET:DISPLAY" 命令 (设置显示开关)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:DISPLAY"))
    {
        if (num_parsed_tokens == current_param_idx + 1) // 确保参数数量正确
        {
            if (compareTokens(&parsed_tokens[current_param_idx], "ON", 2)) // "ON"
            {
                shifting = true;                 // 开启流动
                seven_segment_display_on = true; // 开启数码管显示
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"7-Segment Display turned ON.\r\n");
                unsaved_changes_active = false;
            }
            else if (compareTokens(&parsed_tokens[current_param_idx], "OFF", 3)) // "OFF"
            {
                shifting = false;                 // 停止流动
                seven_segment_display_on = false; // 关闭数码管显示
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"7-Segment Display turned OFF.\r\n");
                unsaved_changes_active = false;
            }
            else // 无效参数
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid argument. Usage: *SET:DISPLAY ON/OFF\r\n");
            }
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *SET:DISPLAY ON/OFF\r\n");
        }
    }

    // 处理 "*SET:FORMAT" 命令 (设置显示格式)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:FORMAT"))
    {
        if (num_parsed_tokens == current_param_idx + 1) // 确保参数数量正确
        {
            if (compareTokens(&parsed_tokens[current_param_idx], "LEFT", 4)) // "LEFT" (左移，正常顺序)
            {
                shift_mode = false;
                display_reversed_order = false;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Display format set to LEFT flow (normal order).\r\n");

                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Time: ");
                UARTStringPutNOBlocking(UART0_BASE, time_transmit_buffer);
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Date: ");
                UARTStringPutNOBlocking(UART0_BASE, date_transmit_buffer);
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
                unsaved_changes_active = false;
            }
            else if (compareTokens(&parsed_tokens[current_param_idx], "RIGHT", 5)) // "RIGHT" (右移，反向顺序)
            {
                shift_mode = true;
                display_reversed_order = true;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Display format set to RIGHT flow (reversed order).\r\n");

                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Time: ");
                UARTStringPutReversedNOBlocking(UART0_BASE, time_transmit_buffer); // 反向显示时间
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Date: ");
                UARTStringPutReversedNOBlocking(UART0_BASE, date_transmit_buffer); // 反向显示日期
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
                unsaved_changes_active = false;
            }
            else // 无效参数
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid argument. Usage: *SET:FORMAT LEFT/RIGHT\r\n");
            }
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *SET:FORMAT LEFT/RIGHT\r\n");
        }
    }

    // 处理 "*GET:DATE" 命令 (获取日期信息)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:DATE"))
    {
        field_token_idx = current_param_idx;
        found_arg = false;

        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Requested Date fields: ");

        if (num_parsed_tokens == field_token_idx) // 如果没有指定字段，则返回完整日期
        {
            UARTStringPutNOBlocking(UART0_BASE, date_transmit_buffer);
            found_arg = true;
        }
        else // 根据指定字段返回信息
        {
            for (i = field_token_idx; i < num_parsed_tokens; ++i)
            {
                if (compareFieldKeyword(&parsed_tokens[i], "YEAR", 4)) // "YEAR"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Year=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)((year / 1000) % 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)((year / 100) % 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)((year / 10) % 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(year % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
                else if (compareFieldKeyword(&parsed_tokens[i], "MONTH", 5)) // "MONTH"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Month=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(month / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(month % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
                else if (compareFieldKeyword(&parsed_tokens[i], "DATE", 3)) // "DATE" (日)
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Day=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(day / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(day % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
            }
        }
        if (!found_arg) // 如果没有找到有效参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid argument.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
        }
    }

    // 处理 "*GET:TIME" 命令 (获取时间信息)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:TIME"))
    {
        field_token_idx = current_param_idx;
        found_arg = false;

        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Requested Time fields: ");

        if (num_parsed_tokens == field_token_idx) // 如果没有指定字段，则返回完整时间
        {
            UARTStringPutNOBlocking(UART0_BASE, time_transmit_buffer);
            found_arg = true;
        }
        else // 根据指定字段返回信息
        {
            for (i = field_token_idx; i < num_parsed_tokens; ++i)
            {
                if (compareFieldKeyword(&parsed_tokens[i], "HOUR", 4)) // "HOUR"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Hour=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(hh / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(hh % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
                else if (compareFieldKeyword(&parsed_tokens[i], "MINUTE", 3)) // "MINUTE"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Minute=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(mm / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(mm % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
                else if (compareFieldKeyword(&parsed_tokens[i], "SECOND", 3)) // "SECOND"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Second=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(ss / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(ss % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
            }
        }
        if (!found_arg) // 如果没有找到有效参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid argument.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
        }
    }

    // 处理 "*GET:ALARM" 命令 (获取闹钟时间)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:ALARM"))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Alarm: ");
            if (alm_hh == 25) // 未设置闹钟
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Alarm not set.\r\n");
            else // 显示闹钟时间
            {
                UARTStringPutNOBlocking(UART0_BASE, alarm_transmit_buffer);
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
            }
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *GET:ALARM\r\n");
        }
    }

    // 处理 "*GET:FORMAT" 命令 (获取显示格式)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:FORMAT"))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Display Format: ");
            if (shift_mode == false) // 左移 (正常顺序)
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"LEFT (Normal Order)\r\n");
            else // 右移 (反向顺序)
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"RIGHT (Reversed Order)\r\n");
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *GET:FORMAT\r\n");
        }
    }

    // 处理 "*GET:DISPLAY" 命令 (获取显示状态)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:DISPLAY"))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"7-Segment Display Status: ");
            if (seven_segment_display_on == true) // 开启
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ON (Normal Display)\r\n");
            else // 关闭
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OFF (All Segments Off)\r\n");
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *GET:DISPLAY\r\n");
        }
    }

    // 处理 "*MOTOR:START" 命令 (启动步进电机)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*MOTOR:START"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            motor_running = 1;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper motor started.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *MOTOR:START\r\n");
        }
    }

    // 处理 "*MOTOR:STOP" 命令 (停止步进电机)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*MOTOR:STOP"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            motor_running = 0;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper motor stopped.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *MOTOR:STOP\r\n");
        }
    }

    // 处理 "*MOTOR:FWD" 命令 (设置步进电机正转)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*MOTOR:FWD"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            motor_direction = 0;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper motor direction set to forward.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *MOTOR:FWD\r\n");
        }
    }

    // 处理 "*MOTOR:REV" 命令 (设置步进电机反转)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*MOTOR:REV"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            motor_direction = 1;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper motor direction set to reverse.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *MOTOR:REV\r\n");
        }
    }

    // 处理 "*GET:MOTOR" 命令 (获取步进电机状态)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:MOTOR"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper Motor Status:\r\n");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"  State: ");
            UARTStringPutNOBlocking(UART0_BASE, motor_running ? (uint8_t *)"RUNNING" : (uint8_t *)"STOPPED");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"  Direction: ");
            UARTStringPutNOBlocking(UART0_BASE, motor_direction ? (uint8_t *)"REVERSE" : (uint8_t *)"FORWARD");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"  Speed: 1 RPM\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *GET:MOTOR\r\n");
        }
    }

    // 处理 "INIT" 命令 (复位)
    else if (compareTokens(&parsed_tokens[0], "INIT", 4))
    {
        if (num_parsed_tokens == current_param_idx)
            SysCtlReset(); // 系统复位
        else
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: INIT\r\n");
    }

    // 处理 "HELP" 命令 (显示帮助文档)
    else if (compareTokens(&parsed_tokens[0], "HELP", 4))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"HELP Document:\r\n"
                                                           "*RST                                       : Reset the clock.\r\n"
                                                           "*SET :DATE YEAR MONTH DATE YYYY MM DD      : Set year, month and day.\r\n"
                                                           "*SET :DATE YEAR MONTH YYYY MM              : Set year and month.\r\n"
                                                           "*SET :DATE YEAR DATE YYYY DD               : Set year and day.\r\n"
                                                           "*SET :DATE MONTH DATE MM DD                : Set month and day.\r\n"
                                                           "*SET :DATE YEAR YYYY                       : Set year.\r\n"
                                                           "*SET :DATE MONTH MM                        : Set month.\r\n"
                                                           "*SET :DATE DATE DD                         : Set day.\r\n"
                                                           "*SET :TIME HOUR MINUTE SECOND HH MM SS     : Set hour, minute and second.\r\n"
                                                           "*SET :TIME HOUR MINUTE HH MM               : Set hour and minute.\r\n"
                                                           "*SET :TIME HOUR SECOND HH SS               : Set hour and second.\r\n"
                                                           "*SET :TIME MINUTE SECOND MM SS             : Set minute and second.\r\n"
                                                           "*SET :TIME HOUR HH                         : Set hour.\r\n"
                                                           "*SET :TIME MINUTE MM                       : Set minute.\r\n"
                                                           "*SET :TIME SECOND SS                       : Set second.\r\n"
                                                           "*SET :ALARM HOUR MINUTE SECOND HH MM SS    : Set alarm hour, minute and second.\r\n"
                                                           "*SET :ALARM HOUR MINUTE HH MM              : Set alarm hour and minute.\r\n"
                                                           "*SET :ALARM HOUR SECOND HH SS              : Set alarm hour and second.\r\n"
                                                           "*SET :ALARM MINUTE SECOND MM SS            : Set alarm minute and second.\r\n"
                                                           "*SET :ALARM HOUR HH                        : Set alarm hour.\r\n"
                                                           "*SET :ALARM MINUTE MM                      : Set alarm minute.\r\n"
                                                           "*SET :ALARM SECOND SS                      : Set alarm second.\r\n"
                                                           "*SET :DISPLAY ON/OFF                       : Set 7-segment display.\r\n"
                                                           "*SET :FORMAT LEFT/RIGHT                    : Set display flow direction and order.\r\n"
                                                           "*GET :DATE                                 : Get year, month and day.\r\n"
                                                           "*GET :DATE YEAR MONTH                      : Get year and month.\r\n"
                                                           "*GET :DATE YEAR DATE                       : Get year and day.\r\n"
                                                           "*GET :DATE MONTH DATE                      : Get month and day.\r\n"
                                                           "*GET :DATE YEAR                            : Get year.\r\n"
                                                           "*GET :DATE MONTH                           : Get month.\r\n"
                                                           "*GET :DATE DATE                            : Get day.\r\n"
                                                           "*GET :TIME                                 : Get hour, minute and second.\r\n"
                                                           "*GET :TIME HOUR MINUTE                     : Get hour and minute.\r\n"
                                                           "*GET :TIME HOUR SECOND                     : Get hour and second.\r\n"
                                                           "*GET :TIME MINUTE SECOND                   : Get minute and second.\r\n"
                                                           "*GET :TIME HOUR                            : Get hour.\r\n"
                                                           "*GET :TIME MINUTE                          : Get minute.\r\n"
                                                           "*GET :TIME SECOND                          : Get second.\r\n"
                                                           "*GET :ALARM                                : Get alarm time.\r\n"
                                                           "*GET :DISPLAY                              : Get 7-segment display status.\r\n"
                                                           "*GET :FORMAT                               : Get display flow format.\r\n"
                                                           "*MOTOR :START                              : Start stepper motor (1 RPM).\r\n"
                                                           "*MOTOR :STOP                               : Stop stepper motor.\r\n"
                                                           "*MOTOR :FWD                                : Set stepper motor forward.\r\n"
                                                           "*MOTOR :REV                                : Set stepper motor reverse.\r\n"
                                                           "*GET :MOTOR                                : Get stepper motor status.\r\n");
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: HELP\r\n");
        }
    }
    else // 未知命令
    {
        if (num_parsed_tokens > 0)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Unknown command. Type HELP for commands.\r\n");
        }
    }

    uart_receive_len = 0;                                        // 清空接收长度
    memset(uart_receive_buffer, 0, sizeof(uart_receive_buffer)); // 清空接收缓冲区
}

// 判断给定年份是否为闰年
static bool is_leap_year(uint16_t year_val)
{
    return ((year_val % 4 == 0 && year_val % 100 != 0) || year_val % 400 == 0);
}

// 检查给定的年、月、日组合是否为有效日期
static bool is_valid_date(uint16_t y, uint8_t m, uint8_t d)
{
    uint8_t max_days;

    // 检查年份、月份、日期范围
    if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1)
    {
        return false;
    }

    max_days = days_in_month[m];   // 获取当前月份的最大天数
    if (m == 2 && is_leap_year(y)) // 如果是闰年的2月
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
static bool is_valid_time(uint8_t h, uint8_t m, uint8_t s)
{
    return (h < 24 && m < 60 && s < 60); // 时:0-23, 分:0-59, 秒:0-59
}

// 初始化所有外设
void DevicesInit(void)
{
    // 设置系统时钟频率为20MHz
    ui32SysClock = SysCtlClockFreqSet((SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN |
                                       SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480),
                                      20000000);

    UARTInit();        // 初始化UART
    SysTickInit();     // 初始化SysTick定时器
    PWMInit();         // 初始化PWM
    S800_I2C0_Init();  // 初始化I2C0
    HibernateInit();   // 初始化休眠模块
    StepperGPIOInit(); // 初始化步进电机GPIO
    StepperTimerInit();// 初始化步进电机Timer
    IntMasterEnable(); // 开启总中断
}

// 处理模式超时，恢复到原始显示状态并放弃未保存的更改
static void HandleModeTimeout(void)
{
    // 只有在非流动模式或有未保存更改/正在长按保存时才处理超时
    if (current_mode != MODE_FLOWING || unsaved_changes_active == true || long_press_saving_in_progress == true)
    {
        // 恢复所有时间、日期、闹钟和显示设置到原始状态
        year = original_year;
        month = original_month;
        day = original_day;
        hh = original_hh;
        mm = original_mm;
        ss = original_ss;
        alm_hh = original_alm_hh;
        alm_mm = original_alm_mm;
        alm_ss = original_alm_ss;

        PWMStop(); // 停止闹钟

        current_mode = MODE_FLOWING;           // 切换回流动模式
        current_setting_field = FIELD_NONE;    // 清除设置字段
        is_blinking = false;                   // 停止闪烁
        shifting = prev_shifting;              // 恢复之前的移位状态
        shift_mode = prev_shift_mode;          // 恢复之前的移位方向
        shift_speed = prev_shift_speed;        // 恢复之前的移位速度
        unsaved_changes_active = false;        // 清除未保存更改标志
        long_press_saving_in_progress = false; // 清除长按保存标志
        seven_segment_display_on = true;       // 确保显示打开
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Timeout occurred. Reverted to original flowing display and discarded unsaved settings.\r\n");
    }
}

// 初始化休眠模块
void HibernateInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_HIBERNATE);        // 使能休眠模块时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_HIBERNATE)) // 等待模块就绪
        ;
    HibernateEnableExpClk(ui32SysClock);          // 使用系统时钟初始化休眠模块
    HibernateClockConfig(HIBERNATE_OSC_LOWDRIVE); // 配置低功耗振荡器
    HibernateRTCEnable();                         // 使能RTC
}

// 初始化SysTick定时器，用于生成系统滴答
void SysTickInit(void)
{
    SysTickPeriodSet(ui32SysClock / SYSTICK_FREQUENCY); // 设置SysTick周期
    SysTickEnable();                                    // 启动SysTick
    SysTickIntEnable();                                 // 使能SysTick中断
}

// 初始化UART0
void UARTInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);        // 使能UART0时钟
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);        // 使能GPIOA时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) // 等待GPIOA就绪
        ;

    GPIOPinConfigure(GPIO_PA0_U0RX);                           // 配置PA0为UART0 RX
    GPIOPinConfigure(GPIO_PA1_U0TX);                           // 配置PA1为UART0 TX
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1); // 设置PA0和PA1为UART功能

    // 配置UART0，波特率115200，8位数据，1位停止位，无奇偶校验
    UARTConfigSetExpClk(UART0_BASE,
                        ui32SysClock,
                        115200,
                        (UART_CONFIG_WLEN_8 |
                         UART_CONFIG_STOP_ONE |
                         UART_CONFIG_PAR_NONE));

    UARTFIFOLevelSet(UART0_BASE, UART_FIFO_TX1_8, UART_FIFO_RX7_8); // 设置FIFO触发级别
    IntEnable(INT_UART0);                                           // 使能UART0中断
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);           // 使能接收和接收超时中断
}

// 非阻塞方式发送字符串
void UARTStringPutNOBlocking(uint32_t ui32Base, uint8_t *cMessage)
{
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
static void UARTStringPutReversedNOBlocking(uint32_t ui32Base, uint8_t *cMessage)
{
    int len;
    int i;
    bool TXFIFO_free;

    // 计算字符串长度
    len = 0;
    while (cMessage[len] != '\0')
    {
        len++;
    }
    // 从字符串末尾开始向前遍历发送
    for (i = len - 1; i >= 0; --i)
    {
        TXFIFO_free = UARTCharPutNonBlocking(ui32Base, cMessage[i]); // 尝试非阻塞发送字符
        while (!TXFIFO_free)                                         // 如果发送失败，则阻塞直到发送成功
        {
            TXFIFO_free = UARTCharPutNonBlocking(ui32Base, cMessage[i]);
        }
    }
}

// 阻塞方式发送单个字符
void UARTCharPutBlocking(uint32_t ui32Base, uint8_t ucData)
{
    while (!UARTCharPutNonBlocking(ui32Base, ucData)) // 循环直到字符成功放入FIFO
    {
    }
}

// UART0中断处理函数
void UART0_Handler(void)
{
    int32_t uart0_int_status;
    uint8_t uart_receive_char;

    uart0_int_status = UARTIntStatus(UART0_BASE, true); // 获取中断状态
    UARTIntClear(UART0_BASE, uart0_int_status);         // 清除中断标志

    while (UARTCharsAvail(UART0_BASE)) // 检查是否有可用字符
    {
        uart_receive_char = UARTCharGetNonBlocking(UART0_BASE);     // 非阻塞获取字符
        if (uart_receive_char != '\r' && uart_receive_char != '\n') // 如果不是回车或换行
        {
            if (uart_receive_len < (sizeof(uart_receive_buffer) - 1)) // 检查缓冲区是否已满
            {
                uart_receive_buffer[uart_receive_len++] = uart_receive_char; // 存储字符并增加长度
            }
        }
        else // 如果是回车或换行，表示命令输入结束
        {
            cmd_state = 1;                                // 设置命令状态标志
            uart_receive_buffer[uart_receive_len] = '\0'; // 字符串结束符
        }
    }
}

// SysTick定时器中断处理函数
void SysTick_Handler(void)
{
    int i = 0;
    uint8_t current_button_raw_value;
    uint8_t max_days_for_current_month;
    bool any_button_is_currently_pressed_debounced = false; // 任何按钮是否处于去抖后的按下状态
    uint32_t press_duration;                                // 按钮按下持续时间

    g_system_tick++; // 增加系统滴答计数

    // 根据系统滴答更新各种定时器标志
    if (g_system_tick % V_T2ms == 0)
    {
        clock2ms_flag = true;
    }
    if (g_system_tick % V_T500ms == 0)
    {
        clock500ms_flag = true;
    }
    if (g_system_tick % V_T300ms == 0)
    {
        clock300ms_flag = true;
    }
    if (g_system_tick % V_T900ms == 0)
    {
        clock900ms_flag = true;
    }

    if (g_system_tick % V_T1s == 0) // 每秒更新时钟
    {
        clock1s_flag = true;
        ss++;
    }

    // 处理保存成功后的闪烁计时
    if (save_blink_active)
    {
        if (g_system_tick - save_blink_timer >= SAVE_BLINK_DURATION_MS)
        {
            save_blink_active = false;
            save_blink_timer = 0;
        }
    }

    // 时间和日期进位逻辑
    if (ss >= 60)
    {
        mm++;
        ss = 0;
    }
    if (mm >= 60)
    {
        hh++;
        mm = 0;
    }
    if (hh >= 24)
    {
        day++;
        hh = 0;
        max_days_for_current_month = days_in_month[month];
        if (month == 2 && is_leap_year(year))
        {
            max_days_for_current_month = 29;
        }
        if (day > max_days_for_current_month)
        {
            day = 1;
            month++;
            if (month > 12)
            {
                month = 1;
                year++;
            }
        }
    }

    // 按钮状态检测和去抖
    current_button_raw_value = I2C0_ReadByte(TCA6424_I2CADDR, TCA6424_INPUT_PORT0); // 读取按钮原始值
    for (i = 0; i < 8; ++i)
    {
        // 如果当前按钮被按下 (TCA6424为低电平有效)
        if (!((current_button_raw_value >> i) & 0x01))
        {
            if (key_debounce_timer[i] < DEBOUNCE_TIME_MS) // 去抖计时
            {
                key_debounce_timer[i]++;
            }
            else // 去抖完成
            {
                if (key_states[i] == false) // 如果按钮从释放变为按下
                {
                    key_states[i] = true;                    // 更新按钮状态
                    key_press_start_time[i] = g_system_tick; // 记录按下起始时间
                    key_long_press_timer[i] = 0;             // 重置长按定时器
                    key_long_press_start_event[i] = false;   // 清除长按开始事件
                    key_repeat_press_event[i] = false;       // 清除重复按事件
                }
                else // 按钮持续按下
                {
                    key_long_press_timer[i]++; // 长按计时

                    // 检测长按开始事件
                    if (key_long_press_start_event[i] == false && key_long_press_timer[i] >= LONG_PRESS_TIME_MS)
                    {
                        key_long_press_start_event[i] = true; // 触发长按开始事件

                        if (i == 5) // 如果是按钮6 (递增按钮)，立即重置长按定时器以实现重复按
                        {
                            key_long_press_timer[i] = 0;
                        }
                    }
                    // 检测重复按事件 (仅针对按钮6)
                    else if (i == 5 && key_long_press_start_event[i] == true && key_long_press_timer[i] >= REPEAT_PRESS_TIME_MS)
                    {
                        key_repeat_press_event[i] = true; // 触发重复按事件
                        key_long_press_timer[i] = 0;      // 重置定时器以实现连续重复
                    }
                }
                any_button_is_currently_pressed_debounced = true; // 标记有去抖后的按钮被按下
            }
        }
        else // 按钮未被按下 (释放状态)
        {
            key_debounce_timer[i] = 0; // 重置去抖定时器
            if (key_states[i] == true) // 如果按钮从按下变为释放
            {
                key_states[i] = false; // 更新按钮状态

                press_duration = g_system_tick - key_press_start_time[i]; // 计算按下持续时间

                // 检测短按事件 (在长按阈值之前释放)
                if (key_long_press_start_event[i] == false && press_duration >= DEBOUNCE_TIME_MS && press_duration < LONG_PRESS_TIME_MS)
                {
                    key_short_press_event[i] = true; // 触发短按事件
                }
                // 处理按钮8在长按保存过程中的释放
                else if (i == 7 && long_press_saving_in_progress)
                {
                    current_mode = MODE_FLOWING;
                    shifting = prev_shifting;
                    shift_mode = prev_shift_mode;
                    shift_speed = prev_shift_speed;
                    long_press_saving_in_progress = false;
                    seven_segment_display_on = true;
                }
            }
            // 清除所有长按和重复按相关的标志
            key_long_press_start_event[i] = false;
            key_repeat_press_event[i] = false;
            key_long_press_timer[i] = 0;
            key_press_start_time[i] = 0;
        }
    }

    // 处理模式超时 (仅在非初始化阶段)
    if (!init_flag)
    {
        if (any_button_is_currently_pressed_debounced) // 如果有按钮被按下，重置超时定时器
        {
            mode_timeout_timer = g_system_tick;
        }
        else // 没有按钮被按下，检查是否超时
        {
            if (g_system_tick - mode_timeout_timer >= MODE_TIMEOUT_MS)
            {
                mode_timeout_flag = true;           // 触发模式超时
                mode_timeout_timer = g_system_tick; // 重置超时定时器
            }
        }
    }
}

// 初始化PWM模块
void PWMInit(void)
{
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
void PWMStart(uint32_t ui32Freq_Hz)
{
    PWMGenDisable(PWM0_BASE, PWM_GEN_3); // 先禁用PWM发生器

    // 设置PWM周期，决定频率
    PWMGenPeriodSet(PWM0_BASE, PWM_GEN_3, ui32SysClock / ui32Freq_Hz);
    // 设置PWM脉冲宽度为周期的一半 (50%占空比)
    PWMPulseWidthSet(PWM0_BASE, PWM_OUT_7, (PWMGenPeriodGet(PWM0_BASE, PWM_GEN_3) / 2));

    PWMGenEnable(PWM0_BASE, PWM_GEN_3); // 启用PWM发生器
}

// 停止PWM输出
void PWMStop(void)
{
    PWMGenDisable(PWM0_BASE, PWM_GEN_3); // 禁用PWM发生器3
}

// 简单的软件延时
void Delay(uint32_t value)
{
    uint32_t ui32Loop;
    for (ui32Loop = 0; ui32Loop < value; ui32Loop++)
        ;
}

// 初始化步进电机GPIO (PF0-PF3)
void StepperGPIOInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF))
        ;

    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE + GPIO_O_CR) |= GPIO_PIN_0;

    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, STEPPER_PINS);
}

// 初始化步进电机Timer0
void StepperTimerInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0))
        ;

    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(TIMER0_BASE, TIMER_A, STEPPER_TIMER_LOAD);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);
    TimerEnable(TIMER0_BASE, TIMER_A);
}

// Timer0A中断处理 — 步进电机驱动
void TIMER0A_Handler(void)
{
    uint8_t output = 0;
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

    if (!motor_running)
        return;

    output = stepper_sequence[motor_position & 0x07];
    GPIOPinWrite(GPIO_PORTF_BASE, STEPPER_PINS, output);

    if (motor_direction == 0)
    {
        motor_position++;
        if (motor_position >= STEPPER_BEATS_PER_REVOLUTION)
            motor_position = 0;
    }
    else
    {
        if (motor_position == 0)
            motor_position = STEPPER_BEATS_PER_REVOLUTION - 1;
        else
            motor_position--;
    }
}

// 初始化I2C0模块和I/O扩展器
void S800_I2C0_Init(void)
{
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

    I2CMasterInitExpClk(I2C0_BASE, ui32SysClock, true); // 初始化I2C0为主机，使用快速模式
    I2CMasterEnable(I2C0_BASE);                         // 使能I2C0

    // 配置TCA6424 I/O扩展器
    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT0, 0x0ff); // PORT0配置为输入
    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT1, 0x0);   // PORT1配置为输出
    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT2, 0x0);   // PORT2配置为输出

    // 配置PCA9557 I/O扩展器
    result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_CONFIG, 0x00);  // 配置为输出
    result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x0ff); // 设置所有输出为高电平
}

// 通过I2C向指定设备地址的寄存器写入一个字节
uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData)
{
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
uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr)
{
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
