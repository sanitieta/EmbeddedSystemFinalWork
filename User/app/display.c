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
        result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
        return;
    }

    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00); // 确保所有位选都关闭

    local_cnt = cnt; // 获取当前轮询的数码管索引

    if (current_mode == MODE_FLOWING) // 流动显示模式
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

// 更新显示移位效果
void UpdateDisplayShift(void)
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
}

// 将字符转换为大写
