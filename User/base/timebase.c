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
#include "timebase.h"

// 初始化SysTick定时器，用于生成系统滴答
void SysTickInit(void)
{
    SysTickPeriodSet(ui32SysClock / SYSTICK_FREQUENCY); // 设置SysTick周期
    SysTickEnable();                                    // 启动SysTick
    SysTickIntEnable();                                 // 使能SysTick中断
}

// 初始化UART0

void SysTickUpdateTimebaseFlags(void)
{
    g_system_tick++;

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
    if (g_system_tick % V_T1s == 0)
    {
        clock1s_flag = true;
    }
}
