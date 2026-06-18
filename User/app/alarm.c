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
#include "alarm.h"

// 检查并处理闹钟，如果时间到达则启动PWM
void HandleAlarm(void)
{
    if (g.clock.hh == g.clock.alm_hh && g.clock.mm == g.clock.alm_mm && g.clock.ss == g.clock.alm_ss && g.clock.alm_hh != 25)
    {
        if (!g.disp.alarm_ringing && !g.disp.alarm_silenced)
        {
            g.disp.alarm_ringing = true;
            g.disp.alarm_ring_start = g.timer.tick;
            g.disp.alarm_beep_phase = g.timer.tick;
            if (g.disp.night_mode)
            {
                g.disp.alarm_beep_on = false;
            }
            else
            {
                PWMStart(500);
                g.disp.alarm_beep_on = true;
            }
        }
    }
    else
    {
        g.disp.alarm_silenced = false;
    }

    if (!g.disp.alarm_ringing)
    {
        return;
    }

    if ((g.timer.tick - g.disp.alarm_ring_start) >= ALARM_RING_MAX_MS)
    {
        StopAlarmRinging(false);
        g.disp.alarm_silenced = true;
        return;
    }

    if (g.disp.night_mode)
    {
        if (g.disp.alarm_beep_on)
        {
            PWMStop();
            g.disp.alarm_ringing = true;
            g.disp.alarm_beep_on = false;
        }
        return;
    }

    if (!g.disp.alarm_beep_on)
    {
        if ((g.timer.tick - g.disp.alarm_beep_phase) >= ALARM_BEEP_OFF_MS)
        {
            PWMStart(500);
            g.disp.alarm_ringing = true;
            g.disp.alarm_beep_on = true;
            g.disp.alarm_beep_phase = g.timer.tick;
        }
    }
    else if ((g.timer.tick - g.disp.alarm_beep_phase) >= ALARM_BEEP_ON_MS)
    {
        PWMStop();
        g.disp.alarm_ringing = true;
        g.disp.alarm_beep_on = false;
        g.disp.alarm_beep_phase = g.timer.tick;
    }
}

// 运行启动初始化显示序列，包括RTC校准和学生信息显示

void StopAlarmRinging(bool silence_current_match)
{
    PWMStop();
    g.disp.alarm_ringing = false;
    g.disp.alarm_beep_on = false;
    g.disp.alarm_ring_start = 0;
    g.disp.alarm_beep_phase = 0;
    if (silence_current_match)
    {
        g.disp.alarm_silenced = true;
    }
}
