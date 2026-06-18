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
#include "pwm_buzzer.h"

// 初始化PWM模块
void PWMInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0);        // 使能PWM0模块时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_PWM0)) // 等待PWM0模块就绪
        ;

    PWMOutputState(PWM0_BASE, PWM_OUT_7_BIT, true); // 使能PWM0模块的PWM7输出位

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOK);        // 使能GPIOK时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOK)) // 等待GPIOK就绪
        ;

    GPIOPinConfigure(GPIO_PK5_M0PWM7);           // 配置PK5引脚为M0PWM7功能
    GPIOPinTypePWM(GPIO_PORTK_BASE, GPIO_PIN_5); // 设置PK5为PWM引脚类型

    // 配置PWM发生器3，为向下计数模式，无同步
    PWMGenConfigure(PWM0_BASE, PWM_GEN_3, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC);
}

// 启动PWM输出，设置指定频率

// 启动PWM输出，设置指定频率
void PWMStart(uint32_t ui32Freq_Hz)
{
    PWMGenDisable(PWM0_BASE, PWM_GEN_3); // 先禁用PWM发生器

    // 设置PWM周期，决定频率
    PWMGenPeriodSet(PWM0_BASE, PWM_GEN_3, g.sys_clock_hz / ui32Freq_Hz);
    // 设置PWM脉冲宽度为周期的一半 (50%占空比)
    PWMPulseWidthSet(PWM0_BASE, PWM_OUT_7, (PWMGenPeriodGet(PWM0_BASE, PWM_GEN_3) / 2));

    PWMGenEnable(PWM0_BASE, PWM_GEN_3); // 启用PWM发生器
}

// 停止PWM输出

// 停止PWM输出
void PWMStop(void)
{
    PWMGenDisable(PWM0_BASE, PWM_GEN_3); // 禁用PWM发生器3
    g.disp.alarm_ringing = false;
}

// 简单的软件延时
