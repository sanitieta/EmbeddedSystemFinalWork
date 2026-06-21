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
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:KEY USER1\r\n");
        g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
        g.in.user_short_evt[0] = false;
    }
    if (g.in.user_short_evt[1])
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:KEY USER2\r\n");
        g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
        g.in.user_short_evt[1] = false;
    }
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
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FUNC: Date edit mode.\r\n");
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
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FUNC: Time edit mode.\r\n");
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
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FUNC: Alarm edit mode.\r\n");
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
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FUNC: Exit edit mode.\r\n");
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
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SHIFT: Date field.\r\n");
    }
    else if (g.disp.mode == MODE_TIME_SET)
    {
        if (g.disp.field == FIELD_HOUR)
            g.disp.field = FIELD_MINUTE;
        else if (g.disp.field == FIELD_MINUTE)
            g.disp.field = FIELD_SECOND;
        else
            g.disp.field = FIELD_HOUR;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SHIFT: Time field.\r\n");
    }
    else if (g.disp.mode == MODE_ALARM_SET)
    {
        if (g.disp.field == FIELD_ALARM_HOUR)
            g.disp.field = FIELD_ALARM_MINUTE;
        else if (g.disp.field == FIELD_ALARM_MINUTE)
            g.disp.field = FIELD_ALARM_SECOND;
        else
            g.disp.field = FIELD_ALARM_HOUR;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SHIFT: Alarm field.\r\n");
    }
    else
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SHIFT is only valid in edit mode.\r\n");
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
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Date saved.\r\n");
        }
        else
        {
            g.clock.year = g.clock.original_year;
            g.clock.month = g.clock.original_month;
            g.clock.day = g.clock.original_day;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Invalid date, reverted.\r\n");
        }
    }
    else if (g.disp.mode == MODE_TIME_SET)
    {
        if (is_valid_time(g.clock.temp_hh, g.clock.temp_mm, g.clock.temp_ss))
        {
            g.clock.hh = g.clock.temp_hh;
            g.clock.mm = g.clock.temp_mm;
            g.clock.ss = g.clock.temp_ss;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Time saved.\r\n");
        }
        else
        {
            g.clock.hh = g.clock.original_hh;
            g.clock.mm = g.clock.original_mm;
            g.clock.ss = g.clock.original_ss;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Invalid time, reverted.\r\n");
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
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Alarm kept unset.\r\n");
        }
        else if (is_valid_time(g.clock.temp_alm_hh, g.clock.temp_alm_mm, g.clock.temp_alm_ss))
        {
            g.clock.alm_hh = g.clock.temp_alm_hh;
            g.clock.alm_mm = g.clock.temp_alm_mm;
            g.clock.alm_ss = g.clock.temp_alm_ss;
            StopAlarmRinging(false);
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Alarm saved.\r\n");
        }
        else
        {
            g.clock.alm_hh = g.clock.original_alm_hh;
            g.clock.alm_mm = g.clock.original_alm_mm;
            g.clock.alm_ss = g.clock.original_alm_ss;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Invalid alarm, reverted.\r\n");
        }
    }
    else
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Current settings saved.\r\n");
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
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP is only valid in normal mode.\r\n");
        return;
    }

    if (g.disp.main_disp == MAIN_DISPLAY_FLOW)
    {
        g.disp.main_disp = MAIN_DISPLAY_TIME;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP: HH.MM.SS.\r\n");
    }
    else if (g.disp.main_disp == MAIN_DISPLAY_TIME)
    {
        g.disp.main_disp = MAIN_DISPLAY_DATE;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP: YY.MM.DD.\r\n");
    }
    else if (g.disp.main_disp == MAIN_DISPLAY_DATE)
    {
        g.disp.main_disp = MAIN_DISPLAY_YEAR;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP: YYYY.MMDD.\r\n");
    }
    else
    {
        g.disp.main_disp = MAIN_DISPLAY_FLOW;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP: Flow.\r\n");
    }
}

static void ToggleDisplayFormat(void)
{
    if (g.disp.mode != MODE_FLOWING)
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FORMAT is only valid in normal mode.\r\n");
        return;
    }

    g.disp.shift_mode = !g.disp.shift_mode;
    g.disp.reversed = g.disp.shift_mode;
    UARTStringPutNOBlocking(UART0_BASE, g.disp.reversed ? (uint8_t *)"FORMAT: RIGHT.\r\n" : (uint8_t *)"FORMAT: LEFT.\r\n");
}

// 根据按钮编号处理短按功能

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
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FUNC: Alarm stopped.\r\n");
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
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ADD: Field incremented.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ADD requires an active edit field.\r\n");
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
            UARTStringPutNOBlocking(UART0_BASE, g.disp.shift_speed ? (uint8_t *)"SPEED: Fast.\r\n" : (uint8_t *)"SPEED: Slow.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SPEED is only valid in normal mode.\r\n");
        }
        return;
    case 7: // K7 FORMAT
        ToggleDisplayFormat();
        return;
    case 8: // K8 EXT
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:KEY EXT\r\n");
        return;
    default:
        return;
    }
}

#if 0

    switch (button_num)
    {
    case 1: // 按钮1: 切换流动方向
        if (g.disp.mode == MODE_FLOWING)
        {
            g.disp.shift_mode = !g.disp.shift_mode;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flow direction toggled.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flow direction cannot be changed in setting mode.\r\n");
        }
        break;
    case 2: // 按钮2: 切换流动速度
        if (g.disp.mode == MODE_FLOWING)
        {
            g.disp.shift_speed = !g.disp.shift_speed;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flow speed toggled.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Flow speed cannot be changed in setting mode.\r\n");
        }
        break;
    case 3: // 按钮3: 暂停/恢复流动
        if (g.disp.mode == MODE_FLOWING)
        {
            g.disp.shifting = !g.disp.shifting;
            if (g.disp.shifting)
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
        if (g.disp.mode == MODE_DATE_SET ||
            g.disp.mode == MODE_TIME_SET ||
            g.disp.mode == MODE_ALARM_SET ||
            (g.disp.mode == MODE_FLOWING && g.clock.unsaved_changes_active == true) ||
            g.disp.mode == MODE_ALARM_DISPLAY ||
            g.disp.long_press_saving == true)
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
            PWMStop();                             // 停止闹钟
            g.disp.mode = MODE_FLOWING;           // 切换回流动模式
            g.disp.field = FIELD_NONE;    // 清除设置字段
            g.disp.blinking = false;                   // 停止闪烁
            g.disp.shifting = g.disp.prev_shifting;              // 恢复之前的移位状态
            g.disp.shift_mode = g.disp.prev_shift_mode;          // 恢复之前的移位方向
            g.disp.shift_speed = g.disp.prev_shift_speed;        // 恢复之前的移位速度
            g.clock.unsaved_changes_active = false;        // 清除未保存更改标志
            g.disp.long_press_saving = false; // 清除长按保存标志
            g.disp.on = true;       // 确保显示打开
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Changes abandoned, reverted to original settings.\r\n");
        }
        else if (g.disp.mode == MODE_FLOWING && g.clock.unsaved_changes_active == false)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"No pending changes or active modes to abandon.\r\n");
        }
        break;
    case 5: // 按钮5: 临时应用更改或切换闹钟显示模式
        if (g.disp.mode == MODE_DATE_SET ||
            g.disp.mode == MODE_TIME_SET ||
            g.disp.mode == MODE_ALARM_SET)
        {
            // 在设置模式下，将临时值应用到当前值
            if (g.disp.mode == MODE_DATE_SET)
            {
                if (is_valid_date(g.clock.temp_year, g.clock.temp_month, g.clock.temp_day)) // 检查日期有效性
                {
                    g.clock.year = g.clock.temp_year;
                    g.clock.month = g.clock.temp_month;
                    g.clock.day = g.clock.temp_day;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Date temporarily applied.\r\n");
                }
                else
                {
                    // 如果日期无效，回滚到原始值
                    g.clock.year = g.clock.original_year;
                    g.clock.month = g.clock.original_month;
                    g.clock.day = g.clock.original_day;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid date, changes not applied!\r\n");
                }
            }
            else if (g.disp.mode == MODE_TIME_SET)
            {
                if (is_valid_time(g.clock.temp_hh, g.clock.temp_mm, g.clock.temp_ss)) // 检查时间有效性
                {
                    g.clock.hh = g.clock.temp_hh;
                    g.clock.mm = g.clock.temp_mm;
                    g.clock.ss = g.clock.temp_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Time temporarily applied.\r\n");
                }
                else
                {
                    // 如果时间无效，回滚到原始值
                    g.clock.hh = g.clock.original_hh;
                    g.clock.mm = g.clock.original_mm;
                    g.clock.ss = g.clock.original_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid time, changes not applied!\r\n");
                }
            }
            else if (g.disp.mode == MODE_ALARM_SET)
            {
                if (is_valid_time(g.clock.temp_alm_hh, g.clock.temp_alm_mm, g.clock.temp_alm_ss)) // 检查闹钟时间有效性
                {
                    g.clock.alm_hh = g.clock.temp_alm_hh;
                    g.clock.alm_mm = g.clock.temp_alm_mm;
                    g.clock.alm_ss = g.clock.temp_alm_ss;
                    PWMStop(); // 停止闹钟
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Alarm temporarily applied.\r\n");
                }
                else
                {
                    // 如果闹钟时间无效，回滚到原始值
                    g.clock.alm_hh = g.clock.original_alm_hh;
                    g.clock.alm_mm = g.clock.original_alm_mm;
                    g.clock.alm_ss = g.clock.original_alm_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid alarm time, changes not applied!\r\n");
                }
            }
            g.disp.mode = MODE_FLOWING;        // 切换回流动模式
            g.disp.field = FIELD_NONE; // 清除设置字段
            g.disp.blinking = false;                // 停止闪烁
            g.disp.shifting = true;                    // 恢复流动
            g.disp.shift_mode = g.disp.prev_shift_mode;       // 恢复之前的移位方向
            g.disp.shift_speed = g.disp.prev_shift_speed;     // 恢复之前的移位速度
            g.clock.unsaved_changes_active = true;      // 标记有未保存更改
            g.disp.on = true;    // 确保显示打开
        }
        else if (g.disp.mode == MODE_FLOWING) // 如果在流动模式下
        {
            // 切换到闹钟显示模式
            g.disp.mode = MODE_ALARM_DISPLAY;
            g.disp.prev_shifting = g.disp.shifting;        // 保存当前移位状态
            g.disp.prev_shift_mode = g.disp.shift_mode;    // 保存当前移位方向
            g.disp.prev_shift_speed = g.disp.shift_speed;  // 保存当前移位速度
            g.disp.shifting = false;                // 暂停流动
            g.disp.on = true; // 确保显示打开
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Displaying alarm time.\r\n");
        }
        else if (g.disp.mode == MODE_ALARM_DISPLAY) // 如果在闹钟显示模式下
        {
            // 切换回流动模式
            g.disp.mode = MODE_FLOWING;
            g.disp.shifting = g.disp.prev_shifting;        // 恢复之前的移位状态
            g.disp.shift_mode = g.disp.prev_shift_mode;    // 恢复之前的移位方向
            g.disp.shift_speed = g.disp.prev_shift_speed;  // 恢复之前的移位速度
            g.disp.on = true; // 确保显示打开
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Exited alarm display, returning to flowing mode.\r\n");
        }
        break;
    case 6:              // 按钮6: 递增当前设置字段
        if (g.disp.blinking) // 只有当有字段正在闪烁时才能递增
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
        if (g.disp.mode != MODE_FLOWING && g.disp.mode != MODE_ALARM_DISPLAY) // 仅在设置模式下有效
        {
            // 根据当前设置模式，循环切换设置字段（年/月/日 或 时/分/秒）
            if (g.disp.mode == MODE_DATE_SET)
            {
                if (g.disp.field == FIELD_YEAR)
                {
                    g.disp.field = FIELD_MONTH;
                }
                else if (g.disp.field == FIELD_MONTH)
                {
                    g.disp.field = FIELD_DAY;
                }
                else
                {
                    g.disp.field = FIELD_YEAR;
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Cycling date field.\r\n");
            }
            else if (g.disp.mode == MODE_TIME_SET)
            {
                if (g.disp.field == FIELD_HOUR)
                {
                    g.disp.field = FIELD_MINUTE;
                }
                else if (g.disp.field == FIELD_MINUTE)
                {
                    g.disp.field = FIELD_SECOND;
                }
                else
                {
                    g.disp.field = FIELD_HOUR;
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Cycling time field.\r\n");
            }
            else if (g.disp.mode == MODE_ALARM_SET)
            {
                if (g.disp.field == FIELD_ALARM_HOUR)
                {
                    g.disp.field = FIELD_ALARM_MINUTE;
                }
                else if (g.disp.field == FIELD_ALARM_MINUTE)
                {
                    g.disp.field = FIELD_ALARM_SECOND;
                }
                else
                {
                    g.disp.field = FIELD_ALARM_HOUR;
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Cycling alarm field.\r\n");
            }
            g.disp.blinking = true;              // 启动闪烁
            g.disp.on = true; // 确保显示打开
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Field cycling not available in current mode.\r\n");
        }
        break;
    case 8: // 按钮8: 进入/退出设置模式或停止闪烁
        old_mode = g.disp.mode;
        if (g.disp.blinking && (old_mode == MODE_DATE_SET || old_mode == MODE_TIME_SET || old_mode == MODE_ALARM_SET))
        {
            // 如果正在闪烁，停止闪烁
            g.disp.blinking = false;
            g.disp.field = FIELD_NONE;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Blinking stopped.\r\n");
        }
        else
        {
            // 根据当前模式，切换到下一个设置模式
            if (old_mode == MODE_FLOWING || old_mode == MODE_ALARM_DISPLAY)
            {
                // 保存当前状态作为原始值，用于回滚
                g.clock.original_year = g.clock.year;
                g.clock.original_month = g.clock.month;
                g.clock.original_day = g.clock.day;
                g.clock.original_hh = g.clock.hh;
                g.clock.original_mm = g.clock.mm;
                g.clock.original_ss = g.clock.ss;
                g.clock.original_alm_hh = g.clock.alm_hh;
                g.clock.original_alm_mm = g.clock.alm_mm;
                g.clock.original_alm_ss = g.clock.alm_ss;
                g.clock.unsaved_changes_active = false; // 清除未保存更改标志

                g.disp.mode = MODE_DATE_SET; // 进入日期设置模式
                g.clock.temp_year = g.clock.year;             // 将当前值复制到临时变量
                g.clock.temp_month = g.clock.month;
                g.clock.temp_day = g.clock.day;
                g.disp.prev_shifting = g.disp.shifting;           // 保存当前移位状态
                g.disp.prev_shift_mode = g.disp.shift_mode;       // 保存当前移位方向
                g.disp.prev_shift_speed = g.disp.shift_speed;     // 保存当前移位速度
                g.disp.shifting = false;                   // 暂停流动
                g.disp.field = FIELD_YEAR; // 默认设置年份
                g.disp.blinking = true;                 // 开始闪烁
                g.disp.on = true;    // 确保显示打开
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Entering Date Setting Mode. Year field blinking.\r\n");
            }
            else if (old_mode == MODE_DATE_SET)
            {
                g.disp.mode = MODE_TIME_SET; // 进入时间设置模式
                g.clock.temp_hh = g.clock.hh;                 // 将当前值复制到临时变量
                g.clock.temp_mm = g.clock.mm;
                g.clock.temp_ss = g.clock.ss;
                g.disp.field = FIELD_HOUR; // 默认设置小时
                g.disp.blinking = true;                 // 开始闪烁
                g.disp.on = true;    // 确保显示打开
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Entering Time Setting Mode. Hour field blinking.\r\n");
            }
            else if (old_mode == MODE_TIME_SET)
            {
                g.disp.mode = MODE_ALARM_SET; // 进入闹钟设置模式
                g.clock.temp_alm_hh = g.clock.alm_hh;          // 将当前值复制到临时变量
                g.clock.temp_alm_mm = g.clock.alm_mm;
                g.clock.temp_alm_ss = g.clock.alm_ss;
                g.disp.field = FIELD_ALARM_HOUR; // 默认设置闹钟小时
                g.disp.blinking = true;                       // 开始闪烁
                g.disp.on = true;          // 确保显示打开
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Entering Alarm Setting Mode. Alarm Hour field blinking.\r\n");
            }
            else if (old_mode == MODE_ALARM_SET)
            {
                g.disp.mode = MODE_FLOWING;        // 退出设置模式，回到流动显示
                g.disp.field = FIELD_NONE; // 清除设置字段
                g.disp.blinking = false;                // 停止闪烁
                g.disp.shifting = g.disp.prev_shifting;           // 恢复之前的移位状态
                g.disp.shift_mode = g.disp.prev_shift_mode;       // 恢复之前的移位方向
                g.disp.shift_speed = g.disp.prev_shift_speed;     // 恢复之前的移位速度
                g.clock.unsaved_changes_active = true;      // 标记有未保存更改
                g.disp.on = true;    // 确保显示打开
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Exiting Setting Modes, returning to flowing display. Changes temporarily applied.\r\n");
            }
        }
        break;
    }
}

#endif

// 根据按钮编号处理长按功能

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

#if 0

    switch (button_num)
    {
    case 8:                                 // 按钮8: 永久保存设置
        if (!g.disp.long_press_saving) // 避免重复保存
        {
            // 根据当前模式，将临时或当前值永久保存
            if (g.disp.mode == MODE_DATE_SET)
            {
                if (is_valid_date(g.clock.temp_year, g.clock.temp_month, g.clock.temp_day)) // 检查日期有效性
                {
                    g.clock.year = g.clock.temp_year;
                    g.clock.month = g.clock.temp_month;
                    g.clock.day = g.clock.temp_day;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Date truly saved.\r\n");
                }
                else
                {
                    // 如果日期无效，回滚到原始值
                    g.clock.year = g.clock.original_year;
                    g.clock.month = g.clock.original_month;
                    g.clock.day = g.clock.original_day;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid date, not truly saved! Reverted.\r\n");
                }
            }
            else if (g.disp.mode == MODE_TIME_SET)
            {
                if (is_valid_time(g.clock.temp_hh, g.clock.temp_mm, g.clock.temp_ss)) // 检查时间有效性
                {
                    g.clock.hh = g.clock.temp_hh;
                    g.clock.mm = g.clock.temp_mm;
                    g.clock.ss = g.clock.temp_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Time truly saved.\r\n");
                }
                else
                {
                    // 如果时间无效，回滚到原始值
                    g.clock.hh = g.clock.original_hh;
                    g.clock.mm = g.clock.original_mm;
                    g.clock.ss = g.clock.original_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid time, not truly saved! Reverted.\r\n");
                }
            }
            else if (g.disp.mode == MODE_ALARM_SET)
            {
                if (is_valid_time(g.clock.temp_alm_hh, g.clock.temp_alm_mm, g.clock.temp_alm_ss)) // 检查闹钟时间有效性
                {
                    g.clock.alm_hh = g.clock.temp_alm_hh;
                    g.clock.alm_mm = g.clock.temp_alm_mm;
                    g.clock.alm_ss = g.clock.temp_alm_ss;
                    PWMStop(); // 停止闹钟
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Alarm truly saved.\r\n");
                }
                else
                {
                    // 如果闹钟时间无效，回滚到原始值
                    g.clock.alm_hh = g.clock.original_alm_hh;
                    g.clock.alm_mm = g.clock.original_alm_mm;
                    g.clock.alm_ss = g.clock.original_alm_ss;
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid alarm time, not truly saved! Reverted.\r\n");
                }
            }
            else if (g.disp.mode == MODE_FLOWING || g.disp.mode == MODE_ALARM_DISPLAY)
            {
                // 保存当前所有设置作为原始值
                g.clock.original_year = g.clock.year;
                g.clock.original_month = g.clock.month;
                g.clock.original_day = g.clock.day;
                g.clock.original_hh = g.clock.hh;
                g.clock.original_mm = g.clock.mm;
                g.clock.original_ss = g.clock.ss;
                g.clock.original_alm_hh = g.clock.alm_hh;
                g.clock.original_alm_mm = g.clock.alm_mm;
                g.clock.original_alm_ss = g.clock.alm_ss;
                PWMStop(); // 停止闹钟
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current settings truly saved.\r\n");
            }

            g.disp.long_press_saving = true; // 标记正在保存
            g.clock.unsaved_changes_active = false;       // 清除未保存更改标志

            g.disp.save_blink_active = true;         // 激活保存成功闪烁
            g.disp.save_blink_timer = g.timer.tick; // 重置保存闪烁定时器

            g.disp.blinking = false;                // 停止普通闪烁
            g.disp.field = FIELD_NONE; // 清除设置字段
            g.disp.on = true;    // 确保显示打开
        }
        break;

    default: // 其他按钮长按无功能
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Long press has no function for this button.\r\n");
        break;
    }
}

#endif

// 处理参数递增逻辑

// 处理参数递增逻辑
static void HandleButtonIncrement(bool is_long_press_repeat)
{
    uint8_t current_days_in_month;

    (void)is_long_press_repeat;
    g.timer.mode_timeout = g.timer.tick; // 重置模式超时定时器

    if (g.disp.long_press_saving) // 如果正在保存，则不能递增
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Saving in progress, cannot increment.\r\n");
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

// 更新7段数码管显示

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
        g.disp.field = FIELD_NONE;    // 清除设置字段
        g.disp.blinking = false;                   // 停止闪烁
        g.disp.shifting = g.disp.prev_shifting;              // 恢复之前的移位状态
        g.disp.shift_mode = g.disp.prev_shift_mode;          // 恢复之前的移位方向
        g.disp.shift_speed = g.disp.prev_shift_speed;        // 恢复之前的移位速度
        g.clock.unsaved_changes_active = false;        // 清除未保存更改标志
        g.disp.long_press_saving = false; // 清除长按保存标志
        g.disp.on = true;       // 确保显示打开
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Timeout occurred. Reverted to original flowing display and discarded unsaved settings.\r\n");
    }
}

// 初始化休眠模块
