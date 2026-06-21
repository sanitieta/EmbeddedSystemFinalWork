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

// SysTick定时器中断处理函数 — 仅 tick 计数、时基 flag、时钟进位、GPIO 用户按键
// 注意：矩阵按键 (K1-K8) 的 I2C 读取已移至主循环 PollMatrixButtons()，
//       避免 ISR 内阻塞 I2C 操作与主循环的显示扫描 I2C 产生竞态。
void SysTick_Handler(void)
{
    int i;
    uint8_t current_user_raw_value;
    uint8_t current_user_pin;
    uint8_t max_days_for_current_month;

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

    // GPIO 用户按键 (PJ0/PJ1) — GPIO 直读，无阻塞，可安全放在 ISR
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
}

// 初始化PWM模块

void TIMER0A_Handler(void)
{
    StepperTimerTick();
}

// 初始化I2C0模块和I/O扩展器
