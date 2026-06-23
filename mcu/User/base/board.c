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
#include "board.h"
#include "uart_port.h"
#include "timebase.h"
#include "pwm_buzzer.h"
#include "i2c_bus.h"
#include "hibernate_rtc.h"
#include "stepper.h"

// 初始化所有外设
void DevicesInit(void)
{
    // 设置系统时钟频率为20MHz
    g.sys_clock_hz = SysCtlClockFreqSet((SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN |
                                       SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480),
                                      20000000);

    UARTInit();         // 初始化UART
    PWMInit();          // 初始化PWM
    S800_I2C0_Init();   // 初始化I2C0
    HibernateInit();    // 初始化休眠模块
    UserKeyGPIOInit();  // 初始化USER1/USER2按键
    StepperGPIOInit();  // 初始化步进电机GPIO
    StepperTimerInit(); // 初始化步进电机Timer
    SysTickInit();      // 初始化SysTick定时器
    IntMasterEnable();  // 开启总中断
}

void UserKeyGPIOInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOJ);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOJ))
        ;

    GPIOPinTypeGPIOInput(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    GPIOPadConfigSet(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    g.in.user_gpio_ready = true;
}

// 处理模式超时，恢复到原始显示状态并放弃未保存的更改
