#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
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
#include "board_config.h"
#include "app_state.h"
#include "uart_port.h"
#include "i2c_bus.h"
#include "pwm_buzzer.h"
#include "hibernate_rtc.h"
#include "datetime.h"
#include "button_logic.h"
#include "alarm.h"
#include "display.h"

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
void ProcessButtonEvents(void)
{
    int i = 0;

    for (i = 0; i < 8; ++i)
    {
        if (g.in.short_evt[i]) // 如果有短按事件
        {
            HandleButtonShortPress((uint8_t)(i + 1)); // 处理短按
            g.in.short_evt[i] = false;         // 清除标志
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

    if (g.in.user_short_evt[0])
    {
        if (!g.in.suppress_key_events)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:KEY USER1\r\n");
            g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
        }
        g.in.user_short_evt[0] = false;
    }
    if (g.in.user_short_evt[1])
    {
        if (!g.in.suppress_key_events)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:KEY USER2\r\n");
            g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
        }
        g.in.user_short_evt[1] = false;
    }
    g.in.suppress_key_events = false;
}

static void EnterNextEditMode(void)
{
    system_mode_t old_mode = g.disp.mode;

    if (old_mode == MODE_FLOWING || old_mode == MODE_ALARM_DISPLAY)
    {
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
    }
    else if (old_mode == MODE_DATE_SET)
    {
        g.disp.mode = MODE_TIME_SET;
        g.clock.temp_hh = g.clock.hh;
        g.clock.temp_mm = g.clock.mm;
        g.clock.temp_ss = g.clock.ss;
        g.disp.field = FIELD_HOUR;
        g.disp.blinking = true;
        g.disp.on = true;
        Display_SendModeEvent("TIME");
    }
    else if (old_mode == MODE_TIME_SET)
    {
        g.disp.mode = MODE_ALARM_SET;
        alarm_was_unset_before_edit = (g.clock.alm_hh == 25);
        g.clock.temp_alm_hh = (g.clock.alm_hh == 25) ? 0 : (uint8_t)g.clock.alm_hh;
        g.clock.temp_alm_mm = (uint8_t)g.clock.alm_mm;
        g.clock.temp_alm_ss = (uint8_t)g.clock.alm_ss;
        g.disp.field = FIELD_ALARM_HOUR;
        g.disp.blinking = true;
        g.disp.on = true;
        Display_SendModeEvent("ALARM");
    }
    else if (old_mode == MODE_ALARM_SET)
    {
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

static void CycleSettingField(void)
{
    if (g.disp.mode == MODE_DATE_SET)
    {
        if (g.disp.field == FIELD_YEAR)
            g.disp.field = FIELD_MONTH;
        else if (g.disp.field == FIELD_MONTH)
            g.disp.field = FIELD_DAY;
        else
            g.disp.field = FIELD_YEAR;
    }
    else if (g.disp.mode == MODE_TIME_SET)
    {
        if (g.disp.field == FIELD_HOUR)
            g.disp.field = FIELD_MINUTE;
        else if (g.disp.field == FIELD_MINUTE)
            g.disp.field = FIELD_SECOND;
        else
            g.disp.field = FIELD_HOUR;
    }
    else if (g.disp.mode == MODE_ALARM_SET)
    {
        if (g.disp.field == FIELD_ALARM_HOUR)
            g.disp.field = FIELD_ALARM_MINUTE;
        else if (g.disp.field == FIELD_ALARM_MINUTE)
            g.disp.field = FIELD_ALARM_SECOND;
        else
            g.disp.field = FIELD_ALARM_HOUR;
    }
    else
    {
        return;
    }

    g.disp.blinking = true;
    g.disp.on = true;
}

static void SaveCurrentSettingsAndExit(void)
{
    bool restore_flow_state = (g.disp.mode != MODE_FLOWING);

    if (g.disp.mode == MODE_DATE_SET)
    {
        if (is_valid_date(g.clock.temp_year, g.clock.temp_month, g.clock.temp_day))
        {
            g.clock.year = g.clock.temp_year;
            g.clock.month = g.clock.temp_month;
            g.clock.day = g.clock.temp_day;
            {
                uint8_t v[11];
                v[0] = (uint8_t)(g.clock.year / 1000 % 10) + '0';
                v[1] = (uint8_t)(g.clock.year / 100 % 10) + '0';
                v[2] = (uint8_t)(g.clock.year / 10 % 10) + '0';
                v[3] = (uint8_t)(g.clock.year % 10) + '0';
                v[4] = '.'; v[5] = (uint8_t)(g.clock.month / 10) + '0';
                v[6] = (uint8_t)(g.clock.month % 10) + '0';
                v[7] = '.'; v[8] = (uint8_t)(g.clock.day / 10) + '0';
                v[9] = (uint8_t)(g.clock.day % 10) + '0';
                v[10] = '\0';
                Display_SendEditEvent("DATE", v);
            }
        }
        else
        {
            g.clock.year = g.clock.original_year;
            g.clock.month = g.clock.original_month;
            g.clock.day = g.clock.original_day;
            /* invalid date — reverted silently */
        }
    }
    else if (g.disp.mode == MODE_TIME_SET)
    {
        if (is_valid_time(g.clock.temp_hh, g.clock.temp_mm, g.clock.temp_ss))
        {
            g.clock.hh = g.clock.temp_hh;
            g.clock.mm = g.clock.temp_mm;
            g.clock.ss = g.clock.temp_ss;
            {
                uint8_t v[9];
                v[0] = (uint8_t)(g.clock.hh / 10) + '0';
                v[1] = (uint8_t)(g.clock.hh % 10) + '0';
                v[2] = '.'; v[3] = (uint8_t)(g.clock.mm / 10) + '0';
                v[4] = (uint8_t)(g.clock.mm % 10) + '0';
                v[5] = '.'; v[6] = (uint8_t)(g.clock.ss / 10) + '0';
                v[7] = (uint8_t)(g.clock.ss % 10) + '0';
                v[8] = '\0';
                Display_SendEditEvent("TIME", v);
            }
        }
        else
        {
            g.clock.hh = g.clock.original_hh;
            g.clock.mm = g.clock.original_mm;
            g.clock.ss = g.clock.original_ss;
            /* invalid time — reverted silently */
        }
    }
    else if (g.disp.mode == MODE_ALARM_SET)
    {
        if (alarm_was_unset_before_edit && g.clock.temp_alm_hh == 0 && g.clock.temp_alm_mm == 0 && g.clock.temp_alm_ss == 0)
        {
            // 闹钟原本未设置且用户未修改，保持未设置状态
            g.clock.alm_hh = 25;
            g.clock.alm_mm = 0;
            g.clock.alm_ss = 0;
            StopAlarmRinging(false);
            /* alarm kept unset */
        }
        else if (is_valid_time(g.clock.temp_alm_hh, g.clock.temp_alm_mm, g.clock.temp_alm_ss))
        {
            g.clock.alm_hh = g.clock.temp_alm_hh;
            g.clock.alm_mm = g.clock.temp_alm_mm;
            g.clock.alm_ss = g.clock.temp_alm_ss;
            StopAlarmRinging(false);
            {
                uint8_t v[9];
                v[0] = (uint8_t)(g.clock.alm_hh / 10) + '0';
                v[1] = (uint8_t)(g.clock.alm_hh % 10) + '0';
                v[2] = '.'; v[3] = (uint8_t)(g.clock.alm_mm / 10) + '0';
                v[4] = (uint8_t)(g.clock.alm_mm % 10) + '0';
                v[5] = '.'; v[6] = (uint8_t)(g.clock.alm_ss / 10) + '0';
                v[7] = (uint8_t)(g.clock.alm_ss % 10) + '0';
                v[8] = '\0';
                Display_SendEditEvent("ALARM", v);
            }
        }
        else
        {
            g.clock.alm_hh = g.clock.original_alm_hh;
            g.clock.alm_mm = g.clock.original_alm_mm;
            g.clock.alm_ss = g.clock.original_alm_ss;
            /* invalid alarm — reverted silently */
        }
    }
    else
    {
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
    if (restore_flow_state)
    {
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

static void SwitchMainDisplay(void)
{
    if (g.disp.mode != MODE_FLOWING)
    {
        return;
    }

    if (g.disp.main_disp == MAIN_DISPLAY_FLOW)
    {
        g.disp.main_disp = MAIN_DISPLAY_TIME;
    }
    else if (g.disp.main_disp == MAIN_DISPLAY_TIME)
    {
        g.disp.main_disp = MAIN_DISPLAY_DATE;
    }
    else if (g.disp.main_disp == MAIN_DISPLAY_DATE)
    {
        g.disp.main_disp = MAIN_DISPLAY_YEAR;
    }
    else
    {
        g.disp.main_disp = MAIN_DISPLAY_FLOW;
    }
}

static void ToggleDisplayFormat(void)
{
    if (g.disp.mode != MODE_FLOWING)
    {
        return;
    }

    g.disp.shift_mode = !g.disp.shift_mode;
    g.disp.reversed = g.disp.shift_mode;
}

// 根据按钮编号处理短按功能
static void HandleButtonShortPress(uint8_t button_num)
{
    g.timer.mode_timeout = g.timer.tick; // 重置模式超时定时器

    if (g.disp.msg_active && button_num != 1)
    {
        Display_StopMessage();
        return;
    }

    switch (button_num)
    {
    case 1: // K1 FUNC: 响铃时停止；否则循环 date -> time -> alarm -> exit
        if (g.disp.alarm_ringing)
        {
            StopAlarmRinging(true);
        }
        else
        {
            EnterNextEditMode();
        }
        return;
    case 2: // K2 SHIFT
        CycleSettingField();
        return;
    case 3: // K3 ADD
        if (g.disp.blinking)
        {
            HandleButtonIncrement(false);
        }
        else
        {
        }
        return;
    case 4: // K4 SAVE
        SaveCurrentSettingsAndExit();
        return;
    case 5: // K5 DISP
        SwitchMainDisplay();
        return;
    case 6: // K6 SPEED
        if (g.disp.mode == MODE_FLOWING)
        {
            g.disp.shift_speed = !g.disp.shift_speed;
        }
        else
        {
        }
        return;
    case 7: // K7 FORMAT
        ToggleDisplayFormat();
        return;
    case 8: // K8 EXT
        if (!g.in.suppress_key_events)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:KEY EXT\r\n");
            g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
        }
        return;
    default:
        return;
    }
}

// 根据按钮编号处理长按功能
static void HandleButtonLongPress(uint8_t button_num)
{
    g.timer.mode_timeout = g.timer.tick; // 重置模式超时定时器

    if (button_num == 1) // K1 FUNC长按: 保存并退出
    {
        SaveCurrentSettingsAndExit();
    }
    return;
}

// 处理参数递增逻辑
static void HandleButtonIncrement(bool is_long_press_repeat)
{
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
    switch (g.disp.field)
    {
    case FIELD_YEAR: // 年份递增
        g.clock.temp_year++;
        if (g.clock.temp_year > 2099) // 2000-2099循环
            g.clock.temp_year = 2000;

        // 如果月份是2月，可能需要调整日期
        if (g.clock.temp_month == 2)
        {
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
void HandleModeTimeout(void)
{
    // 只有在非流动模式或有未保存更改/正在长按保存时才处理超时
    if (g.disp.mode != MODE_FLOWING || g.clock.unsaved_changes_active == true || g.disp.long_press_saving == true)
    {
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

        g.disp.mode = MODE_FLOWING;           // 切换回流动模式
        Display_SendModeEvent("FLOWING");
        g.disp.field = FIELD_NONE;    // 清除设置字段
        g.disp.blinking = false;                   // 停止闪烁
        g.disp.shifting = g.disp.prev_shifting;              // 恢复之前的移位状态
        g.disp.shift_mode = g.disp.prev_shift_mode;          // 恢复之前的移位方向
        g.disp.shift_speed = g.disp.prev_shift_speed;        // 恢复之前的移位速度
        g.clock.unsaved_changes_active = false;        // 清除未保存更改标志
        g.disp.long_press_saving = false; // 清除长按保存标志
        g.disp.on = true;       // 确保显示打开
    }
}
