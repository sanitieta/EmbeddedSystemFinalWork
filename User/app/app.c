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
#include "uart_port.h"

void App_Init(void)
{
    int i;

    memset(time_transmit_buffer, 0, sizeof(time_transmit_buffer));
    memset(alarm_transmit_buffer, 0, sizeof(alarm_transmit_buffer));
    memset(date_transmit_buffer, 0, sizeof(date_transmit_buffer));

    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Initialization successful! Type HELP for commands.\r\n");

    UpdateTimeAndDisplayBuffers();
    shift = 0;

    for (i = 0; i < 8; ++i)
    {
        key_debounce_timer[i] = 0;
        key_press_start_time[i] = 0;
        key_long_press_timer[i] = 0;
        key_states[i] = false;
        key_short_press_event[i] = false;
        key_long_press_start_event[i] = false;
        key_repeat_press_event[i] = false;
    }
    for (i = 0; i < 2; ++i)
    {
        user_key_debounce_timer[i] = 0;
        user_key_press_start_time[i] = 0;
        user_key_states[i] = false;
        user_key_short_press_event[i] = false;
    }
}

void App_RunOnce(void)
{
    HandleAlarm();

    if (init_flag)
    {
        RunInitializationSequence();
    }
    else
    {
        ProcessButtonEvents();

        if (mode_timeout_flag)
        {
            HandleModeTimeout();
            mode_timeout_flag = false;
        }

        if (clock1s_flag)
        {
            UpdateTimeAndDisplayBuffers();
            clock1s_flag = false;
        }

        if (clock2ms_flag)
        {
            Update7SegmentDisplay();
            clock2ms_flag = false;
        }

        if (message_active ||
            (current_mode == MODE_FLOWING && shifting == true && seven_segment_display_on == true))
        {
            if (shift_speed == false && clock500ms_flag)
            {
                UpdateDisplayShift();
                clock500ms_flag = false;
            }
            else if (shift_speed == true && clock300ms_flag)
            {
                UpdateDisplayShift();
                clock300ms_flag = false;
            }
        }
        Display_UpdateStatusLeds();
    }
    ProcessUartCommand();
}
