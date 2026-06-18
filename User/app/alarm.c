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
    if (hh == alm_hh && mm == alm_mm && ss == alm_ss && alm_hh != 25)
    {
        if (!alarm_ringing && !alarm_silenced_for_match)
        {
            alarm_ringing = true;
            alarm_ring_start_tick = g_system_tick;
            alarm_beep_phase_tick = g_system_tick;
            if (night_mode_active)
            {
                alarm_beep_on = false;
            }
            else
            {
                PWMStart(500);
                alarm_beep_on = true;
            }
        }
    }
    else
    {
        alarm_silenced_for_match = false;
    }

    if (!alarm_ringing)
    {
        return;
    }

    if ((g_system_tick - alarm_ring_start_tick) >= ALARM_RING_MAX_MS)
    {
        StopAlarmRinging(false);
        alarm_silenced_for_match = true;
        return;
    }

    if (night_mode_active)
    {
        if (alarm_beep_on)
        {
            PWMStop();
            alarm_ringing = true;
            alarm_beep_on = false;
        }
        return;
    }

    if (!alarm_beep_on)
    {
        if ((g_system_tick - alarm_beep_phase_tick) >= ALARM_BEEP_OFF_MS)
        {
            PWMStart(500);
            alarm_ringing = true;
            alarm_beep_on = true;
            alarm_beep_phase_tick = g_system_tick;
        }
    }
    else if ((g_system_tick - alarm_beep_phase_tick) >= ALARM_BEEP_ON_MS)
    {
        PWMStop();
        alarm_ringing = true;
        alarm_beep_on = false;
        alarm_beep_phase_tick = g_system_tick;
    }
}

// 运行启动初始化显示序列，包括RTC校准和学生信息显示

void StopAlarmRinging(bool silence_current_match)
{
    PWMStop();
    alarm_ringing = false;
    alarm_beep_on = false;
    alarm_ring_start_tick = 0;
    alarm_beep_phase_tick = 0;
    if (silence_current_match)
    {
        alarm_silenced_for_match = true;
    }
}
