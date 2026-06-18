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
#include "i2c_bus.h"
#include "delay.h"

// 初始化I2C0模块和I/O扩展器
void S800_I2C0_Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);        // 使能I2C0时钟
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);       // 使能GPIOB时钟
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_I2C0)) // 等待I2C0就绪
        ;
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) // 等待GPIOB就绪
        ;

    GPIOPinConfigure(GPIO_PB2_I2C0SCL);             // 配置PB2为I2C0 SCL
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);             // 配置PB3为I2C0 SDA
    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2); // 设置PB2为I2C SCL引脚类型
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);    // 设置PB3为I2C SDA引脚类型

    I2CMasterInitExpClk(I2C0_BASE, g.sys_clock_hz, true); // 初始化I2C0为主机，使用快速模式
    I2CMasterEnable(I2C0_BASE);                         // 使能I2C0

    // 配置TCA6424 I/O扩展器
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT0, 0x0ff); // PORT0配置为输入
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT1, 0x0);   // PORT1配置为输出
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT2, 0x0);   // PORT2配置为输出

    // 配置PCA9557 I/O扩展器
    g.disp.i2c_result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_CONFIG, 0x00);  // 配置为输出
    g.disp.i2c_result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x0ff); // 设置所有输出为高电平
}

// 通过I2C向指定设备地址的寄存器写入一个字节

// 通过I2C向指定设备地址的寄存器写入一个字节
uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData)
{
    uint8_t rop; // 操作结果

    while (I2CMasterBusy(I2C0_BASE)) // 等待I2C总线空闲
        ;
    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);             // 设置从设备地址，写入模式
    I2CMasterDataPut(I2C0_BASE, RegAddr);                         // 放置寄存器地址
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START); // 发送启动信号和寄存器地址
    while (I2CMasterBusy(I2C0_BASE))                              // 等待发送完成
        ;
    rop = (uint8_t)I2CMasterErr(I2C0_BASE);                        // 获取错误状态
    if (rop != I2C_MASTER_ERR_NONE)                                // 如果有错误
        return rop;                                                // 返回错误码
    I2CMasterDataPut(I2C0_BASE, WriteData);                        // 放置要写入的数据
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH); // 发送数据和停止信号
    while (I2CMasterBusy(I2C0_BASE))                               // 等待发送完成
        ;
    rop = (uint8_t)I2CMasterErr(I2C0_BASE); // 获取最终错误状态
    return rop;                             // 返回操作结果
}

// 通过I2C从指定设备地址的寄存器读取一个字节

// 通过I2C从指定设备地址的寄存器读取一个字节
uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr)
{
    uint8_t value; // 读取到的数据

    while (I2CMasterBusy(I2C0_BASE)) // 等待I2C总线空闲
        ;
    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);        // 设置从设备地址，写入模式
    I2CMasterDataPut(I2C0_BASE, RegAddr);                    // 放置要读取的寄存器地址
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_SEND); // 发送启动信号、设备地址和寄存器地址
    while (I2CMasterBusBusy(I2C0_BASE))                      // 等待总线操作完成
        ;

    if (I2CMasterErr(I2C0_BASE) != I2C_MASTER_ERR_NONE) // 如果有错误
    {
        return 0xFF; // 返回错误码
    }
    Delay(5); // 延时等待

    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, true);            // 设置从设备地址，读取模式
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_RECEIVE); // 发送启动信号、设备地址和读取命令
    while (I2CMasterBusBusy(I2C0_BASE))                         // 等待总线操作完成
        ;

    if (I2CMasterErr(I2C0_BASE) != I2C_MASTER_ERR_NONE) // 如果有错误
    {
        return 0xFF; // 返回错误码
    }
    value = I2CMasterDataGet(I2C0_BASE); // 获取读取到的数据
    Delay(5);                            // 延时等待
    return value;                        // 返回读取到的数据
}
