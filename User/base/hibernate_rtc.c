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
#include "hibernate_rtc.h"

// 初始化休眠模块
void HibernateInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_HIBERNATE);        // 使能休眠模块时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_HIBERNATE)) // 等待模块就绪
        ;
    HibernateEnableExpClk(ui32SysClock);          // 使用系统时钟初始化休眠模块
    HibernateClockConfig(HIBERNATE_OSC_LOWDRIVE); // 配置低功耗振荡器
    HibernateRTCEnable();                         // 使能RTC
}

// 初始化SysTick定时器，用于生成系统滴答
