#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "inc/hw_memmap.h"
#include "app.h"
#include "app_state.h"
#include "alarm.h"
#include "boot_sequence.h"
#include "button_logic.h"
#include "display.h"
#include "command.h"
#include "melody.h"
#include "uart_port.h"

void App_Init(void)
{
    int i;

    memset(g.disp.time_buf, 0, sizeof(g.disp.time_buf));
    memset(g.disp.alarm_buf, 0, sizeof(g.disp.alarm_buf));
    memset(g.disp.date_buf, 0, sizeof(g.disp.date_buf));

    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Initialization successful! Type HELP for commands.\r\n");

    UpdateTimeAndDisplayBuffers();
    g.disp.shift = 0;

    for (i = 0; i < 8; ++i)
    {
        g.in.debounce[i] = 0;
        g.in.press_start[i] = 0;
        g.in.long_press[i] = 0;
        g.in.state[i] = false;
        g.in.short_evt[i] = false;
        g.in.long_start_evt[i] = false;
        g.in.repeat_evt[i] = false;
    }
    for (i = 0; i < 2; ++i)
    {
        g.in.user_debounce[i] = 0;
        g.in.user_press_start[i] = 0;
        g.in.user_state[i] = false;
        g.in.user_short_evt[i] = false;
    }
}

void App_RunOnce(void)
{
    Melody_Update();  // 旋律播放器 (非阻塞, 不播放时立即返回)
    HandleAlarm();

    if (g.disp.init_flag)
    {
        RunInitializationSequence();
    }
    else
    {
        ProcessButtonEvents();

        if (g.timer.mode_timeout_flag)
        {
            HandleModeTimeout();
            g.timer.mode_timeout_flag = false;
        }

        if (g.timer.flag_1s)
        {
            UpdateTimeAndDisplayBuffers();
            g.timer.flag_1s = false;
        }

        if (g.timer.flag_2ms)
        {
            Update7SegmentDisplay();
            g.timer.flag_2ms = false;
        }

        if (g.disp.msg_active ||
            (g.disp.mode == MODE_FLOWING && g.disp.shifting == true && g.disp.on == true))
        {
            if (g.disp.shift_speed == false && g.timer.flag_500ms)
            {
                UpdateDisplayShift();
                g.timer.flag_500ms = false;
            }
            else if (g.disp.shift_speed == true && g.timer.flag_300ms)
            {
                UpdateDisplayShift();
                g.timer.flag_300ms = false;
            }
        }
        Display_UpdateStatusLeds();
    }
    ProcessUartCommand();
}
