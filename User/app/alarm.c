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
            PWMStart(500); // 启动PWM作为闹钟提示音
            alarm_ringing = true;
        }
    }
    else
    {
        alarm_silenced_for_match = false;
    }
}

// 运行启动初始化显示序列，包括RTC校准和学生信息显示

void StopAlarmRinging(bool silence_current_match)
{
    PWMStop();
    alarm_ringing = false;
    if (silence_current_match)
    {
        alarm_silenced_for_match = true;
    }
}
