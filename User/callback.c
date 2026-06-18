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
    uart_activity_until_tick = g_system_tick + UART_ACTIVITY_FLASH_MS;

    while (UARTCharsAvail(UART0_BASE)) // 检查是否有可用字符
    {
        uart_receive_char = UARTCharGetNonBlocking(UART0_BASE);     // 非阻塞获取字符
        if (uart_receive_char != '\r' && uart_receive_char != '\n') // 如果不是回车或换行
        {
            if (uart_receive_len < (sizeof(uart_receive_buffer) - 1)) // 检查缓冲区是否已满
            {
                uart_receive_buffer[uart_receive_len++] = uart_receive_char; // 存储字符并增加长度
            }
        }
        else // 如果是回车或换行，表示命令输入结束
        {
            cmd_state = 1;                                // 设置命令状态标志
            uart_receive_buffer[uart_receive_len] = '\0'; // 字符串结束符
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

    g_system_tick++; // 增加系统滴答计数

    // 根据系统滴答更新各种定时器标志
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

    if (g_system_tick % V_T1s == 0) // 每秒更新时钟
    {
        clock1s_flag = true;
        ss++;
    }

    // 处理保存成功后的闪烁计时
    if (save_blink_active)
    {
        if (g_system_tick - save_blink_timer >= SAVE_BLINK_DURATION_MS)
        {
            save_blink_active = false;
            save_blink_timer = 0;
        }
    }

    // 时间和日期进位逻辑
    if (ss >= 60)
    {
        mm++;
        ss = 0;
    }
    if (mm >= 60)
    {
        hh++;
        mm = 0;
    }
    if (hh >= 24)
    {
        day++;
        hh = 0;
        max_days_for_current_month = days_in_month[month];
        if (month == 2 && is_leap_year(year))
        {
            max_days_for_current_month = 29;
        }
        if (day > max_days_for_current_month)
        {
            day = 1;
            month++;
            if (month > 12)
            {
                month = 1;
                year++;
            }
        }
    }

    // 按钮状态检测和去抖
    current_button_raw_value = I2C0_ReadByte(TCA6424_I2CADDR, TCA6424_INPUT_PORT0);
    for (i = 0; i < 8; ++i)
    {
        if (!((current_button_raw_value >> i) & 0x01)) // 读到按下 (低电平有效)
        {
            if (key_debounce_timer[i] < DEBOUNCE_TIME_MS)
                key_debounce_timer[i]++;

            if (key_debounce_timer[i] == DEBOUNCE_TIME_MS) // 去抖完成，确认按下
            {
                if (key_states[i] == false) // 从释放转为按下
                {
                    key_states[i] = true;
                    key_press_start_time[i] = g_system_tick;
                    key_long_press_timer[i] = 0;
                    key_long_press_start_event[i] = false;
                    key_repeat_press_event[i] = false;
                }
                else // 持续按下
                {
                    key_long_press_timer[i]++;

                    if (key_long_press_start_event[i] == false && key_long_press_timer[i] >= LONG_PRESS_TIME_MS)
                    {
                        key_long_press_start_event[i] = true;
                        if (i == 2)
                            key_long_press_timer[i] = 0;
                    }
                    else if (i == 2 && key_long_press_start_event[i] == true && key_long_press_timer[i] >= REPEAT_PRESS_TIME_MS)
                    {
                        key_repeat_press_event[i] = true;
                        key_long_press_timer[i] = 0;
                    }
                }
                any_button_is_currently_pressed_debounced = true;
            }
        }
        else // 读到释放 (高电平)
        {
            if (key_debounce_timer[i] > 0)
                key_debounce_timer[i]--;

            if (key_debounce_timer[i] == 0 && key_states[i] == true) // 去抖完成，确认释放
            {
                key_states[i] = false;

                press_duration = g_system_tick - key_press_start_time[i];

                if (key_long_press_start_event[i] == false && press_duration >= DEBOUNCE_TIME_MS && press_duration < LONG_PRESS_TIME_MS)
                {
                    key_short_press_event[i] = true;
                }
                else if (i == 0 && long_press_saving_in_progress)
                {
                    current_mode = MODE_FLOWING;
                    shifting = prev_shifting;
                    shift_mode = prev_shift_mode;
                    shift_speed = prev_shift_speed;
                    long_press_saving_in_progress = false;
                    seven_segment_display_on = true;
                }

                key_long_press_start_event[i] = false;
                key_repeat_press_event[i] = false;
                key_long_press_timer[i] = 0;
                key_press_start_time[i] = 0;
            }
        }
    }

    current_user_raw_value = GPIOPinRead(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    for (i = 0; i < 2; ++i)
    {
        current_user_pin = (i == 0) ? GPIO_PIN_0 : GPIO_PIN_1;
        if ((current_user_raw_value & current_user_pin) == 0)
        {
            if (user_key_debounce_timer[i] < DEBOUNCE_TIME_MS)
                user_key_debounce_timer[i]++;

            if (user_key_debounce_timer[i] == DEBOUNCE_TIME_MS && user_key_states[i] == false)
            {
                user_key_states[i] = true;
                user_key_press_start_time[i] = g_system_tick;
            }
        }
        else
        {
            if (user_key_debounce_timer[i] > 0)
                user_key_debounce_timer[i]--;

            if (user_key_debounce_timer[i] == 0 && user_key_states[i] == true)
            {
                user_key_states[i] = false;
                if (g_system_tick - user_key_press_start_time[i] >= DEBOUNCE_TIME_MS)
                {
                    user_key_short_press_event[i] = true;
                }
                user_key_press_start_time[i] = 0;
            }
        }
    }

    // 处理模式超时 (仅在非初始化阶段)
    if (!init_flag)
    {
        if (any_button_is_currently_pressed_debounced) // 如果有按钮被按下，重置超时定时器
        {
            mode_timeout_timer = g_system_tick;
        }
        else // 没有按钮被按下，检查是否超时
        {
            if (g_system_tick - mode_timeout_timer >= MODE_TIMEOUT_MS)
            {
                mode_timeout_flag = true;           // 触发模式超时
                mode_timeout_timer = g_system_tick; // 重置超时定时器
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
