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
#include "datetime.h"
#include "stepper.h"
#include "callback.h"

// UART0中断处理函数
void UART0_Handler(void)
{
    int32_t uart0_int_status;
    uint8_t uart_receive_char;

    uart0_int_status = UARTIntStatus(UART0_BASE, true); // 获取中断状态
    UARTIntClear(UART0_BASE, uart0_int_status);         // 清除中断标志
    g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;

    while (UARTCharsAvail(UART0_BASE)) // 检查是否有可用字符
    {
        uart_receive_char = UARTCharGetNonBlocking(UART0_BASE);     // 非阻塞获取字符

        if (uart_receive_char == '\r' || uart_receive_char == '\n') // 如果是回车或换行，表示命令输入结束
        {
            if (g.uart.rx_len > 0 && g.uart.cmd_state == 0) // 有内容且上一条命令已处理
            {
                g.uart.rx_buf[g.uart.rx_len] = '\0'; // 字符串结束符
                g.uart.cmd_state = 1;                                // 设置命令状态标志
            }
            continue; // 丢弃 \r \n 字符，不存入缓冲区
        }

        if (g.uart.cmd_state != 0) // 上一条命令尚未被主循环处理，排空 FIFO 防止追加到 null 之后
        {
            continue;
        }

        if (g.uart.rx_len < (sizeof(g.uart.rx_buf) - 1)) // 检查缓冲区是否已满
        {
            g.uart.rx_buf[g.uart.rx_len++] = uart_receive_char; // 存储字符并增加长度
        }
    }
}

// SysTick定时器中断处理函数

// SysTick定时器中断处理函数
void SysTick_Handler(void)
{
    int i = 0;
    uint8_t current_button_raw_value;
    uint8_t current_user_raw_value;
    uint8_t current_user_pin;
    uint8_t max_days_for_current_month;
    bool any_button_is_currently_pressed_debounced = false; // 任何按钮是否处于去抖后的按下状态
    uint32_t press_duration;                                // 按钮按下持续时间

    g.timer.tick++; // 增加系统滴答计数

    // 根据系统滴答更新各种定时器标志
    if (g.timer.tick % V_T2ms == 0)
    {
        g.timer.flag_2ms = true;
    }
    if (g.timer.tick % V_T500ms == 0)
    {
        g.timer.flag_500ms = true;
    }
    if (g.timer.tick % V_T300ms == 0)
    {
        g.timer.flag_300ms = true;
    }
    if (g.timer.tick % V_T900ms == 0)
    {
        g.timer.flag_900ms = true;
    }

    if (g.timer.tick % V_T1s == 0) // 每秒更新时钟
    {
        g.timer.flag_1s = true;
        g.clock.ss++;
    }

    // 处理保存成功后的闪烁计时
    if (g.disp.save_blink_active)
    {
        if (g.timer.tick - g.disp.save_blink_timer >= SAVE_BLINK_DURATION_MS)
        {
            g.disp.save_blink_active = false;
            g.disp.save_blink_timer = 0;
        }
    }

    // 时间和日期进位逻辑
    if (g.clock.ss >= 60)
    {
        g.clock.mm++;
        g.clock.ss = 0;
    }
    if (g.clock.mm >= 60)
    {
        g.clock.hh++;
        g.clock.mm = 0;
    }
    if (g.clock.hh >= 24)
    {
        g.clock.day++;
        g.clock.hh = 0;
        max_days_for_current_month = g.rtc.days_in_month[g.clock.month];
        if (g.clock.month == 2 && is_leap_year(g.clock.year))
        {
            max_days_for_current_month = 29;
        }
        if (g.clock.day > max_days_for_current_month)
        {
            g.clock.day = 1;
            g.clock.month++;
            if (g.clock.month > 12)
            {
                g.clock.month = 1;
                g.clock.year++;
            }
        }
    }

    // 按钮状态检测和去抖
    current_button_raw_value = I2C0_ReadByte(TCA6424_I2CADDR, TCA6424_INPUT_PORT0);
    for (i = 0; i < 8; ++i)
    {
        if (!((current_button_raw_value >> i) & 0x01)) // 读到按下 (低电平有效)
        {
            if (g.in.debounce[i] < DEBOUNCE_TIME_MS)
                g.in.debounce[i]++;

            if (g.in.debounce[i] == DEBOUNCE_TIME_MS) // 去抖完成，确认按下
            {
                if (g.in.state[i] == false) // 从释放转为按下
                {
                    g.in.state[i] = true;
                    g.in.press_start[i] = g.timer.tick;
                    g.in.long_press[i] = 0;
                    g.in.long_start_evt[i] = false;
                    g.in.repeat_evt[i] = false;
                }
                else // 持续按下
                {
                    g.in.long_press[i]++;

                    if (g.in.long_start_evt[i] == false && g.in.long_press[i] >= LONG_PRESS_TIME_MS)
                    {
                        g.in.long_start_evt[i] = true;
                        if (i == 2)
                            g.in.long_press[i] = 0;
                    }
                    else if (i == 2 && g.in.long_start_evt[i] == true && g.in.long_press[i] >= REPEAT_PRESS_TIME_MS)
                    {
                        g.in.repeat_evt[i] = true;
                        g.in.long_press[i] = 0;
                    }
                }
                any_button_is_currently_pressed_debounced = true;
            }
        }
        else // 读到释放 (高电平)
        {
            if (g.in.debounce[i] > 0)
                g.in.debounce[i]--;

            if (g.in.debounce[i] == 0 && g.in.state[i] == true) // 去抖完成，确认释放
            {
                g.in.state[i] = false;

                press_duration = g.timer.tick - g.in.press_start[i];

                if (g.in.long_start_evt[i] == false && press_duration >= DEBOUNCE_TIME_MS && press_duration < LONG_PRESS_TIME_MS)
                {
                    g.in.short_evt[i] = true;
                }
                else if (i == 0 && g.disp.long_press_saving)
                {
                    g.disp.mode = MODE_FLOWING;
                    g.disp.shifting = g.disp.prev_shifting;
                    g.disp.shift_mode = g.disp.prev_shift_mode;
                    g.disp.shift_speed = g.disp.prev_shift_speed;
                    g.disp.long_press_saving = false;
                    g.disp.on = true;
                }

                g.in.long_start_evt[i] = false;
                g.in.repeat_evt[i] = false;
                g.in.long_press[i] = 0;
                g.in.press_start[i] = 0;
            }
        }
    }

    if (g.in.user_gpio_ready)
    {
        current_user_raw_value = GPIOPinRead(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1);
        for (i = 0; i < 2; ++i)
        {
            current_user_pin = (i == 0) ? GPIO_PIN_0 : GPIO_PIN_1;
            if ((current_user_raw_value & current_user_pin) == 0)
            {
                if (g.in.user_debounce[i] < DEBOUNCE_TIME_MS)
                    g.in.user_debounce[i]++;

                if (g.in.user_debounce[i] == DEBOUNCE_TIME_MS && g.in.user_state[i] == false)
                {
                    g.in.user_state[i] = true;
                    g.in.user_press_start[i] = g.timer.tick;
                }
            }
            else
            {
                if (g.in.user_debounce[i] > 0)
                    g.in.user_debounce[i]--;

                if (g.in.user_debounce[i] == 0 && g.in.user_state[i] == true)
                {
                    g.in.user_state[i] = false;
                    if (g.timer.tick - g.in.user_press_start[i] >= DEBOUNCE_TIME_MS)
                    {
                        g.in.user_short_evt[i] = true;
                    }
                    g.in.user_press_start[i] = 0;
                }
            }
        }
    }

    // 处理模式超时 (仅在非初始化阶段)
    if (!g.disp.init_flag)
    {
        if (any_button_is_currently_pressed_debounced) // 如果有按钮被按下，重置超时定时器
        {
            g.timer.mode_timeout = g.timer.tick;
        }
        else // 没有按钮被按下，检查是否超时
        {
            if (g.timer.tick - g.timer.mode_timeout >= MODE_TIMEOUT_MS)
            {
                g.timer.mode_timeout_flag = true;           // 触发模式超时
                g.timer.mode_timeout = g.timer.tick; // 重置超时定时器
            }
        }
    }
}

// 初始化PWM模块

void TIMER0A_Handler(void)
{
    StepperTimerTick();
}

// 初始化I2C0模块和I/O扩展器
