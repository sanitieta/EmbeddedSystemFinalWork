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

// 遍历并处理所有按钮的事件
void ProcessButtonEvents(void)
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
            if (i != 2)                              // K3 ADD需要保持长按状态以产生重复按
            {
                key_long_press_start_event[i] = false; // 清除标志
            }
        }
        if (key_repeat_press_event[i]) // 如果有重复按事件
        {
            if (i == 2) // K3 ADD是递增按钮
            {
                HandleButtonIncrement(true); // 处理重复递增
            }
            key_repeat_press_event[i] = false; // 清除标志
        }
    }

    // if (user_key_short_press_event[0])
    // {
    //     UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:KEY USER1\r\n");
    //     user_key_short_press_event[0] = false;
    // }
    // if (user_key_short_press_event[1])
    // {
    //     UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:KEY USER2\r\n");
    //     user_key_short_press_event[1] = false;
    // }
}

static void EnterNextEditMode(void)
{
    system_mode_t old_mode = current_mode;

    if (old_mode == MODE_FLOWING || old_mode == MODE_ALARM_DISPLAY)
    {
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

        current_mode = MODE_DATE_SET;
        temp_year = year;
        temp_month = month;
        temp_day = day;
        prev_shifting = shifting;
        prev_shift_mode = shift_mode;
        prev_shift_speed = shift_speed;
        shifting = false;
        current_setting_field = FIELD_YEAR;
        is_blinking = true;
        blink_timer = 0;
        seven_segment_display_on = true;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FUNC: Date edit mode.\r\n");
    }
    else if (old_mode == MODE_DATE_SET)
    {
        current_mode = MODE_TIME_SET;
        temp_hh = hh;
        temp_mm = mm;
        temp_ss = ss;
        current_setting_field = FIELD_HOUR;
        is_blinking = true;
        blink_timer = 0;
        seven_segment_display_on = true;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FUNC: Time edit mode.\r\n");
    }
    else if (old_mode == MODE_TIME_SET)
    {
        current_mode = MODE_ALARM_SET;
        temp_alm_hh = (alm_hh == 25) ? 0 : (uint8_t)alm_hh;
        temp_alm_mm = (uint8_t)alm_mm;
        temp_alm_ss = (uint8_t)alm_ss;
        current_setting_field = FIELD_ALARM_HOUR;
        is_blinking = true;
        blink_timer = 0;
        seven_segment_display_on = true;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FUNC: Alarm edit mode.\r\n");
    }
    else if (old_mode == MODE_ALARM_SET)
    {
        current_mode = MODE_FLOWING;
        current_setting_field = FIELD_NONE;
        is_blinking = false;
        shifting = prev_shifting;
        shift_mode = prev_shift_mode;
        shift_speed = prev_shift_speed;
        seven_segment_display_on = true;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FUNC: Exit edit mode.\r\n");
    }
}

static void CycleSettingField(void)
{
    if (current_mode == MODE_DATE_SET)
    {
        if (current_setting_field == FIELD_YEAR)
            current_setting_field = FIELD_MONTH;
        else if (current_setting_field == FIELD_MONTH)
            current_setting_field = FIELD_DAY;
        else
            current_setting_field = FIELD_YEAR;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SHIFT: Date field.\r\n");
    }
    else if (current_mode == MODE_TIME_SET)
    {
        if (current_setting_field == FIELD_HOUR)
            current_setting_field = FIELD_MINUTE;
        else if (current_setting_field == FIELD_MINUTE)
            current_setting_field = FIELD_SECOND;
        else
            current_setting_field = FIELD_HOUR;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SHIFT: Time field.\r\n");
    }
    else if (current_mode == MODE_ALARM_SET)
    {
        if (current_setting_field == FIELD_ALARM_HOUR)
            current_setting_field = FIELD_ALARM_MINUTE;
        else if (current_setting_field == FIELD_ALARM_MINUTE)
            current_setting_field = FIELD_ALARM_SECOND;
        else
            current_setting_field = FIELD_ALARM_HOUR;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SHIFT: Alarm field.\r\n");
    }
    else
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SHIFT is only valid in edit mode.\r\n");
        return;
    }

    is_blinking = true;
    blink_timer = 0;
    seven_segment_display_on = true;
}

static void SaveCurrentSettingsAndExit(void)
{
    bool restore_flow_state = (current_mode != MODE_FLOWING);

    if (current_mode == MODE_DATE_SET)
    {
        if (is_valid_date(temp_year, temp_month, temp_day))
        {
            year = temp_year;
            month = temp_month;
            day = temp_day;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Date saved.\r\n");
        }
        else
        {
            year = original_year;
            month = original_month;
            day = original_day;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Invalid date, reverted.\r\n");
        }
    }
    else if (current_mode == MODE_TIME_SET)
    {
        if (is_valid_time(temp_hh, temp_mm, temp_ss))
        {
            hh = temp_hh;
            mm = temp_mm;
            ss = temp_ss;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Time saved.\r\n");
        }
        else
        {
            hh = original_hh;
            mm = original_mm;
            ss = original_ss;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Invalid time, reverted.\r\n");
        }
    }
    else if (current_mode == MODE_ALARM_SET)
    {
        if (is_valid_time(temp_alm_hh, temp_alm_mm, temp_alm_ss))
        {
            alm_hh = temp_alm_hh;
            alm_mm = temp_alm_mm;
            alm_ss = temp_alm_ss;
            StopAlarmRinging(false);
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Alarm saved.\r\n");
        }
        else
        {
            alm_hh = original_alm_hh;
            alm_mm = original_alm_mm;
            alm_ss = original_alm_ss;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Invalid alarm, reverted.\r\n");
        }
    }
    else
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"SAVE: Current settings saved.\r\n");
    }

    original_year = year;
    original_month = month;
    original_day = day;
    original_hh = hh;
    original_mm = mm;
    original_ss = ss;
    original_alm_hh = alm_hh;
    original_alm_mm = alm_mm;
    original_alm_ss = alm_ss;

    current_mode = MODE_FLOWING;
    current_setting_field = FIELD_NONE;
    is_blinking = false;
    if (restore_flow_state)
    {
        shifting = prev_shifting;
        shift_mode = prev_shift_mode;
        shift_speed = prev_shift_speed;
    }
    unsaved_changes_active = false;
    long_press_saving_in_progress = false;
    save_blink_active = true;
    save_blink_timer = g_system_tick;
    seven_segment_display_on = true;
}

static void SwitchMainDisplay(void)
{
    if (current_mode != MODE_FLOWING)
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP is only valid in normal mode.\r\n");
        return;
    }

    if (main_display_mode == MAIN_DISPLAY_FLOW)
    {
        main_display_mode = MAIN_DISPLAY_TIME;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP: HH.MM.SS.\r\n");
    }
    else if (main_display_mode == MAIN_DISPLAY_TIME)
    {
        main_display_mode = MAIN_DISPLAY_DATE;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP: YY.MM.DD.\r\n");
    }
    else if (main_display_mode == MAIN_DISPLAY_DATE)
    {
        main_display_mode = MAIN_DISPLAY_YEAR;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP: YYYY.MMDD.\r\n");
    }
    else
    {
        main_display_mode = MAIN_DISPLAY_FLOW;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"DISP: Flow.\r\n");
    }
}

static void ToggleDisplayFormat(void)
{
    if (current_mode != MODE_FLOWING)
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"FORMAT is only valid in normal mode.\r\n");
        return;
    }

    shift_mode = !shift_mode;
    display_reversed_order = shift_mode;
    UARTStringPutNOBlocking(UART0_BASE, display_reversed_order ? (uint8_t *)"FORMAT: RIGHT.\r\n" : (uint8_t *)"FORMAT: LEFT.\r\n");
}

// 根据按钮编号处理短按功能

// 根据按钮编号处理短按功能
static void HandleButtonShortPress(uint8_t button_num)
{
    mode_timeout_timer = g_system_tick; // 重置模式超时定时器

    switch (button_num)
    {
    case 1: // K1 FUNC: 响铃时停止；否则循环 date -> time -> alarm -> exit
        if (alarm_ringing)
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
        if (is_blinking)
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
        if (current_mode == MODE_FLOWING)
        {
            shift_speed = !shift_speed;
            UARTStringPutNOBlocking(UART0_BASE, shift_speed ? (uint8_t *)"SPEED: Fast.\r\n" : (uint8_t *)"SPEED: Slow.\r\n");
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

#endif

// 根据按钮编号处理长按功能

// 根据按钮编号处理长按功能
static void HandleButtonLongPress(uint8_t button_num)
{
    mode_timeout_timer = g_system_tick; // 重置模式超时定时器

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

#endif

// 处理参数递增逻辑

// 处理参数递增逻辑
static void HandleButtonIncrement(bool is_long_press_repeat)
{
    uint8_t current_days_in_month;

    (void)is_long_press_repeat;
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

// 处理模式超时，恢复到原始显示状态并放弃未保存的更改
void HandleModeTimeout(void)
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
