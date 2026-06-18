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
#include "display.h"

static uint8_t DigitToAscii(uint8_t digit)
{
    return (uint8_t)('0' + digit);
}

static uint8_t HexDigit(uint8_t value)
{
    value &= 0x0F;
    return (uint8_t)(value < 10 ? ('0' + value) : ('A' + value - 10));
}

static uint8_t SegmentForChar(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return seg7[c - '0'];
    if (c >= 'a' && c <= 'z')
        c = (uint8_t)(c - 0x20);

    switch (c)
    {
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
        return 0x40;
    case '_':
    case ' ':
        return 0x00;
    default:
        return 0x00;
    }
}

static void BuildCurrentDisplay(uint8_t chars[8], uint8_t *dp_hex)
{
    uint8_t i;

    for (i = 0; i < 8; ++i)
    {
        chars[i] = '_';
    }
    *dp_hex = 0x00;

    if (!seven_segment_display_on)
    {
        return;
    }

    if (message_active)
    {
        for (i = 0; i < 8; ++i)
        {
            uint8_t msg_index = (uint8_t)(message_shift + i);
            if (msg_index < message_len)
            {
                chars[i] = message_buffer[msg_index];
            }
        }
        return;
    }

    if (night_mode_active)
    {
        chars[0] = DigitToAscii((uint8_t)(hh / 10));
        chars[1] = DigitToAscii((uint8_t)(hh % 10));
        chars[2] = DigitToAscii((uint8_t)(mm / 10));
        chars[3] = DigitToAscii((uint8_t)(mm % 10));
        *dp_hex = 0x02;
        return;
    }

    if (current_mode == MODE_DATE_SET)
    {
        chars[0] = DigitToAscii((uint8_t)((temp_year / 1000) % 10));
        chars[1] = DigitToAscii((uint8_t)((temp_year / 100) % 10));
        chars[2] = DigitToAscii((uint8_t)((temp_year / 10) % 10));
        chars[3] = DigitToAscii((uint8_t)(temp_year % 10));
        chars[4] = DigitToAscii((uint8_t)(temp_month / 10));
        chars[5] = DigitToAscii((uint8_t)(temp_month % 10));
        chars[6] = DigitToAscii((uint8_t)(temp_day / 10));
        chars[7] = DigitToAscii((uint8_t)(temp_day % 10));
        *dp_hex = 0x28;
        return;
    }

    if (current_mode == MODE_TIME_SET)
    {
        chars[0] = DigitToAscii((uint8_t)(temp_hh / 10));
        chars[1] = DigitToAscii((uint8_t)(temp_hh % 10));
        chars[2] = DigitToAscii((uint8_t)(temp_mm / 10));
        chars[3] = DigitToAscii((uint8_t)(temp_mm % 10));
        chars[4] = DigitToAscii((uint8_t)(temp_ss / 10));
        chars[5] = DigitToAscii((uint8_t)(temp_ss % 10));
        *dp_hex = 0x0A;
        return;
    }

    if (current_mode == MODE_ALARM_SET || current_mode == MODE_ALARM_DISPLAY)
    {
        uint8_t show_hh = (current_mode == MODE_ALARM_SET) ? temp_alm_hh : (uint8_t)alm_hh;
        uint8_t show_mm = (current_mode == MODE_ALARM_SET) ? temp_alm_mm : (uint8_t)alm_mm;
        uint8_t show_ss = (current_mode == MODE_ALARM_SET) ? temp_alm_ss : (uint8_t)alm_ss;

        if (alm_hh == 25 && current_mode == MODE_ALARM_DISPLAY)
        {
            memcpy(chars, "AL xx xx", 8);
        }
        else
        {
            chars[0] = DigitToAscii((uint8_t)(show_hh / 10));
            chars[1] = DigitToAscii((uint8_t)(show_hh % 10));
            chars[2] = DigitToAscii((uint8_t)(show_mm / 10));
            chars[3] = DigitToAscii((uint8_t)(show_mm % 10));
            chars[4] = DigitToAscii((uint8_t)(show_ss / 10));
            chars[5] = DigitToAscii((uint8_t)(show_ss % 10));
            *dp_hex = 0x0A;
        }
        return;
    }

    if (main_display_mode == MAIN_DISPLAY_DATE)
    {
        chars[0] = DigitToAscii((uint8_t)((year / 10) % 10));
        chars[1] = DigitToAscii((uint8_t)(year % 10));
        chars[2] = DigitToAscii((uint8_t)(month / 10));
        chars[3] = DigitToAscii((uint8_t)(month % 10));
        chars[4] = DigitToAscii((uint8_t)(day / 10));
        chars[5] = DigitToAscii((uint8_t)(day % 10));
        *dp_hex = 0x0A;
    }
    else if (main_display_mode == MAIN_DISPLAY_YEAR)
    {
        chars[0] = DigitToAscii((uint8_t)((year / 1000) % 10));
        chars[1] = DigitToAscii((uint8_t)((year / 100) % 10));
        chars[2] = DigitToAscii((uint8_t)((year / 10) % 10));
        chars[3] = DigitToAscii((uint8_t)(year % 10));
        chars[4] = DigitToAscii((uint8_t)(month / 10));
        chars[5] = DigitToAscii((uint8_t)(month % 10));
        chars[6] = DigitToAscii((uint8_t)(day / 10));
        chars[7] = DigitToAscii((uint8_t)(day % 10));
        *dp_hex = 0x28;
    }
    else
    {
        chars[0] = DigitToAscii((uint8_t)(hh / 10));
        chars[1] = DigitToAscii((uint8_t)(hh % 10));
        chars[2] = DigitToAscii((uint8_t)(mm / 10));
        chars[3] = DigitToAscii((uint8_t)(mm % 10));
        chars[4] = DigitToAscii((uint8_t)(ss / 10));
        chars[5] = DigitToAscii((uint8_t)(ss % 10));
        *dp_hex = 0x0A;
    }
}

static uint8_t ReverseDpBitmap(uint8_t left_dp)
{
    uint8_t right_dp = 0;
    uint8_t i;

    for (i = 0; i < 8; ++i)
    {
        if (left_dp & (uint8_t)(1U << i))
        {
            if (i <= 6)
            {
                right_dp |= (uint8_t)(1U << (6 - i));
            }
        }
    }
    return right_dp;
}

void Display_SetLedOutput(uint8_t led_pattern)
{
    result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, (uint8_t)~led_pattern);
}

void Display_UpdateStatusLeds(void)
{
    uint8_t pattern;

    if (led_takeover_active)
    {
        Display_SetLedOutput(led_takeover_pattern);
        return;
    }

    pattern = 0x00;
    if ((g_system_tick % V_T1s) < V_T500ms)
    {
        pattern |= 0x01;
    }

    if (!night_mode_active)
    {
        if (alarm_ringing)
        {
            if ((g_system_tick % 400U) < 200U)
                pattern |= 0x02;
        }
        else if (alm_hh != 25)
        {
            pattern |= 0x02;
        }

        if (current_mode == MODE_DATE_SET || current_mode == MODE_TIME_SET || current_mode == MODE_ALARM_SET)
        {
            pattern |= 0x04;
        }

        if ((int32_t)(uart_activity_until_tick - g_system_tick) > 0)
        {
            pattern |= 0x08;
        }
    }

    Display_SetLedOutput(pattern);
}

void Display_FormatBufferForProtocol(const uint8_t *src, uint8_t len, uint8_t *dst)
{
    uint8_t i;

    if (display_reversed_order)
    {
        for (i = 0; i < len; ++i)
        {
            dst[i] = src[len - 1U - i];
        }
    }
    else
    {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

void Display_SendEvent(void)
{
    uint8_t chars[8];
    uint8_t out_chars[9];
    uint8_t dp_hex;
    uint8_t out_dp_hex;

    BuildCurrentDisplay(chars, &dp_hex);
    if (display_reversed_order)
    {
        Display_FormatBufferForProtocol(chars, 8, out_chars);
        out_dp_hex = ReverseDpBitmap(dp_hex);
    }
    else
    {
        memcpy(out_chars, chars, 8);
        out_chars[8] = '\0';
        out_dp_hex = dp_hex;
    }

    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:DISP ");
    UARTStringPutNOBlocking(UART0_BASE, out_chars);
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
    UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(out_dp_hex >> 4)));
    UARTCharPutBlocking(UART0_BASE, HexDigit(out_dp_hex));
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
    uart_activity_until_tick = g_system_tick + UART_ACTIVITY_FLASH_MS;
}

void Display_StartMessage(const uint8_t *text, uint8_t len)
{
    if (len > 32)
        len = 32;

    memcpy(message_buffer, text, len);
    message_buffer[len] = '\0';
    message_len = len;
    message_shift = 0;
    message_active = true;
    message_scroll_active = (len > 8);
    message_start_tick = g_system_tick;
    message_last_shift_tick = g_system_tick;
    shifting = false;
    seven_segment_display_on = true;
    Display_SendEvent();
}

void Display_StopMessage(void)
{
    message_active = false;
    message_scroll_active = false;
    message_len = 0;
    message_shift = 0;
    shifting = true;
    UpdateTimeAndDisplayBuffers();
}

// 更新7段数码管显示
void Update7SegmentDisplay(void)
{
    uint8_t segment_data;                // 当前数码管段码
    bool digit_should_blink_off = false; // 数字是否应该闪烁关闭
    uint8_t local_cnt;                   // 本地数码管计数
    uint8_t display_cnt;                 // 考虑FORMAT后的显示位置
    uint8_t effective_segment_data;      // 实际发送的段码

    if (!seven_segment_display_on) // 如果7段数码管关闭
    {
        // 关闭所有数码管
        result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
        result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, 0x00);
        Display_UpdateStatusLeds();
        return;
    }

    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00); // 确保所有位选都关闭

    local_cnt = cnt; // 获取当前轮询的数码管索引

    if (message_active)
    {
        uint8_t msg_index = (uint8_t)(message_shift + local_cnt);
        if (msg_index < message_len)
            segment_data = SegmentForChar(message_buffer[msg_index]);
        else
            segment_data = 0x00;
    }
    else if (night_mode_active)
    {
        if (local_cnt == 0)
            segment_data = seg7[hh / 10];
        else if (local_cnt == 1)
            segment_data = seg7[hh % 10] | 0x80;
        else if (local_cnt == 2)
            segment_data = seg7[mm / 10];
        else if (local_cnt == 3)
            segment_data = seg7[mm % 10];
        else
            segment_data = 0x00;
    }
    else if (current_mode == MODE_FLOWING) // 流动显示模式
    {
        display_cnt = display_reversed_order ? (uint8_t)(7 - local_cnt) : local_cnt;

        if (main_display_mode == MAIN_DISPLAY_FLOW)
        {
            segment_data = master_display_buffer[(shift + local_cnt) % 18];
        }
        else if (main_display_mode == MAIN_DISPLAY_TIME)
        {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = seg7[hh / 10];
            else if (display_cnt == 2)
                segment_data = seg7[hh % 10] | 0x80;
            else if (display_cnt == 3)
                segment_data = seg7[mm / 10];
            else if (display_cnt == 4)
                segment_data = seg7[mm % 10] | 0x80;
            else if (display_cnt == 5)
                segment_data = seg7[ss / 10];
            else
                segment_data = seg7[ss % 10];
        }
        else if (main_display_mode == MAIN_DISPLAY_DATE)
        {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = seg7[(year / 10) % 10];
            else if (display_cnt == 2)
                segment_data = seg7[year % 10] | 0x80;
            else if (display_cnt == 3)
                segment_data = seg7[month / 10];
            else if (display_cnt == 4)
                segment_data = seg7[month % 10] | 0x80;
            else if (display_cnt == 5)
                segment_data = seg7[day / 10];
            else
                segment_data = seg7[day % 10];
        }
        else if (main_display_mode == MAIN_DISPLAY_YEAR)
        {
            if (display_cnt == 0)
                segment_data = seg7[(year / 1000) % 10];
            else if (display_cnt == 1)
                segment_data = seg7[(year / 100) % 10];
            else if (display_cnt == 2)
                segment_data = seg7[(year / 10) % 10];
            else if (display_cnt == 3)
                segment_data = seg7[year % 10] | 0x80;
            else if (display_cnt == 4)
                segment_data = seg7[month / 10];
            else if (display_cnt == 5)
                segment_data = seg7[month % 10] | 0x80;
            else if (display_cnt == 6)
                segment_data = seg7[day / 10];
            else
                segment_data = seg7[day % 10];
        }
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
        segment_data = 0x00;
    }

    effective_segment_data = segment_data;

    // 处理保存成功后的闪烁效果
    if (save_blink_active && (g_system_tick - save_blink_timer) % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS) >= BLINK_ON_TIME_MS)
    {
        effective_segment_data = 0x00; // 闪烁关闭时显示空白
    }

    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, effective_segment_data); // 发送段码
    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);             // 发送位选
    Display_UpdateStatusLeds();

    cnt++;                        // 切换到下一个数码管
    rightshift = rightshift << 1; // 移位位选
    if (cnt >= 0x8)               // 8个数码管循环
    {
        rightshift = 0x01;
        cnt = 0;
    }
}

// 更新显示移位效果

// 更新显示移位效果
void UpdateDisplayShift(void)
{
    if (message_active)
    {
        if (message_scroll_active)
        {
            if ((g_system_tick - message_last_shift_tick) >= V_T300ms)
            {
                message_last_shift_tick = g_system_tick;
                message_shift++;
                if (message_shift > (int8_t)message_len)
                {
                    Display_StopMessage();
                }
                else
                {
                    Display_SendEvent();
                }
            }
        }
        else if ((g_system_tick - message_start_tick) >= MESSAGE_STATIC_MS)
        {
            Display_StopMessage();
        }
        return;
    }

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

// 更新时间、日期和显示缓冲区
void UpdateTimeAndDisplayBuffers(void)
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
    if (!message_active)
    {
        Display_SendEvent();
    }
}

// 将字符转换为大写
