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
#include "datetime.h"

// 判断给定年份是否为闰年
bool is_leap_year(uint16_t year_val)
{
    return ((year_val % 4 == 0 && year_val % 100 != 0) || year_val % 400 == 0);
}

// 检查给定的年、月、日组合是否为有效日期

// 检查给定的年、月、日组合是否为有效日期
bool is_valid_date(uint16_t y, uint8_t m, uint8_t d)
{
    uint8_t max_days;

    // 检查年份、月份、日期范围
    if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1)
    {
        return false;
    }

    max_days = g.rtc.days_in_month[m];   // 获取当前月份的最大天数
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

// 检查给定的时、分、秒组合是否为有效时间
bool is_valid_time(uint8_t h, uint8_t m, uint8_t s)
{
    return (h < 24 && m < 60 && s < 60); // 时:0-23, 分:0-59, 秒:0-59
}

// 初始化所有外设

void DateTime_TickOneSecond(void)
{
    uint8_t max_days_for_current_month;

    g.clock.ss++;
    if (g.clock.ss >= 60)
    {
        g.clock.mm++;
        g.clock.ss = 0;
    }
    if (g.clock.mm >= 60)
    {
        g.clock.hh++;
        g.clock.mm = 0;
    }
    if (g.clock.hh >= 24)
    {
        g.clock.day++;
        g.clock.hh = 0;
        max_days_for_current_month = g.rtc.days_in_month[g.clock.month];
        if (g.clock.month == 2 && is_leap_year(g.clock.year))
        {
            max_days_for_current_month = 29;
        }
        if (g.clock.day > max_days_for_current_month)
        {
            g.clock.day = 1;
            g.clock.month++;
            if (g.clock.month > 12)
            {
                g.clock.month = 1;
                g.clock.year++;
            }
        }
    }
}
