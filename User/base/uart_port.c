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

// 初始化UART0
void UARTInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);        // 使能UART0时钟
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);        // 使能GPIOA时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) // 等待GPIOA就绪
        ;

    GPIOPinConfigure(GPIO_PA0_U0RX);                           // 配置PA0为UART0 RX
    GPIOPinConfigure(GPIO_PA1_U0TX);                           // 配置PA1为UART0 TX
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1); // 设置PA0和PA1为UART功能

    // 配置UART0，波特率115200，8位数据，1位停止位，无奇偶校验
    UARTConfigSetExpClk(UART0_BASE,
                        g.sys_clock_hz,
                        115200,
                        (UART_CONFIG_WLEN_8 |
                         UART_CONFIG_STOP_ONE |
                         UART_CONFIG_PAR_NONE));

    UARTFIFOLevelSet(UART0_BASE, UART_FIFO_TX1_8, UART_FIFO_RX7_8); // 设置FIFO触发级别
    IntEnable(INT_UART0);                                           // 使能UART0中断
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);           // 使能接收和接收超时中断
}

// 非阻塞方式发送字符串

// 非阻塞方式发送字符串
void UARTStringPutNOBlocking(uint32_t ui32Base, uint8_t *cMessage)
{
    bool TXFIFO_free;

    while (*cMessage != '\0') // 遍历字符串
    {
        TXFIFO_free = UARTCharPutNonBlocking(ui32Base, *(cMessage)); // 尝试非阻塞发送字符
        if (TXFIFO_free)                                             // 如果发送成功
        {
            cMessage++; // 移动到下一个字符
        }
        // 如果发送失败 (FIFO满)，则等待下一个SysTick周期再尝试
    }
}

// 非阻塞方式反向发送字符串

// 非阻塞方式反向发送字符串
void UARTStringPutReversedNOBlocking(uint32_t ui32Base, uint8_t *cMessage)
{
    int len;
    int i;
    bool TXFIFO_free;

    // 计算字符串长度
    len = 0;
    while (cMessage[len] != '\0')
    {
        len++;
    }
    // 从字符串末尾开始向前遍历发送
    for (i = len - 1; i >= 0; --i)
    {
        TXFIFO_free = UARTCharPutNonBlocking(ui32Base, cMessage[i]); // 尝试非阻塞发送字符
        while (!TXFIFO_free)                                         // 如果发送失败，则阻塞直到发送成功
        {
            TXFIFO_free = UARTCharPutNonBlocking(ui32Base, cMessage[i]);
        }
    }
}

// 阻塞方式发送单个字符

// 阻塞方式发送单个字符
void UARTCharPutBlocking(uint32_t ui32Base, uint8_t ucData)
{
    while (!UARTCharPutNonBlocking(ui32Base, ucData)) // 循环直到字符成功放入FIFO
    {
    }
}

// UART0中断处理函数
