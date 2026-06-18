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
#include "stepper.h"

static const uint8_t stepper_sequence[8] = {
    0x01,  // Beat 1: PF0
    0x03,  // Beat 2: PF0+PF1
    0x02,  // Beat 3: PF1
    0x06,  // Beat 4: PF1+PF2
    0x04,  // Beat 5: PF2
    0x0C,  // Beat 6: PF2+PF3
    0x08,  // Beat 7: PF3
    0x09,  // Beat 8: PF3+PF0
};

// 初始化步进电机GPIO (PF0-PF3)
void StepperGPIOInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF))
        ;

    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE + GPIO_O_CR) |= GPIO_PIN_0;

    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, STEPPER_PINS);
}

// 初始化步进电机Timer0

// 初始化步进电机Timer0
void StepperTimerInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0))
        ;

    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(TIMER0_BASE, TIMER_A, STEPPER_TIMER_LOAD);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);
    TimerEnable(TIMER0_BASE, TIMER_A);
}

// Timer0A中断处理 — 步进电机驱动
// Timer0A中断处理 — 步进电机驱动
void StepperTimerTick(void)
{
    uint8_t output = 0;
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

    if (!g.motor.running)
        return;

    output = stepper_sequence[g.motor.position & 0x07];
    GPIOPinWrite(GPIO_PORTF_BASE, STEPPER_PINS, output);

    if (g.motor.direction == 0)
    {
        g.motor.position++;
        if (g.motor.position >= STEPPER_BEATS_PER_REVOLUTION)
            g.motor.position = 0;
    }
    else
    {
        if (g.motor.position == 0)
            g.motor.position = STEPPER_BEATS_PER_REVOLUTION - 1;
        else
            g.motor.position--;
    }
}

// 初始化I2C0模块和I/O扩展器
