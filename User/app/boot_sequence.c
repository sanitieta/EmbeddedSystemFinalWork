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
#include "i2c_bus.h"
#include "pwm_buzzer.h"
#include "hibernate_rtc.h"
#include "datetime.h"
#include "boot_sequence.h"
#include "display.h"

static const uint8_t kStudentIdFirst[8] = {0x6D, 0x5B, 0x66, 0x3F, 0x4F, 0x06, 0x6F, 0x06};  // 52403191
static const uint8_t kStudentIdSecond[8] = {0x4F, 0x06, 0x6F, 0x06, 0x3F, 0x07, 0x07, 0x5B}; // 31910772
static const uint8_t kNameXuHaoran[8] = {0x76, 0x3E, 0x76, 0x77, 0x3F, 0x50, 0x77, 0x54};    // XUHAORAN
static const uint8_t kVersion[8] = {0x00, 0x00, 0x3E, 0x06 | 0x80, 0x3F, 0x00, 0x00, 0x00};   // v1.0

static void RestoreRtcOrDefaultTime(void)
{
    uint8_t max_days_for_current_month;
    uint32_t past_sec;

    currentRTC = HibernateRTCGet();
    HibernateDataGet(fetchRTC, 4);

    if (fetchRTC[3] != 0 && currentRTC >= fetchRTC[3])
    {
        past_sec = currentRTC - fetchRTC[3];
        hh = (int8_t)fetchRTC[0];
        mm = (int8_t)fetchRTC[1];
        ss = (int8_t)fetchRTC[2];

        while (past_sec > 0)
        {
            DateTime_TickOneSecond();
            past_sec--;
        }
    }
    else
    {
        hh = 0;
        mm = 0;
        ss = 0;
        year = 2025;
        month = 6;
        day = 3;
        alm_hh = 25;
        alm_mm = 0;
        alm_ss = 0;
    }

    max_days_for_current_month = days_in_month[month];
    if (month == 2 && is_leap_year(year))
        max_days_for_current_month = 29;
    if (!is_valid_date(year, month, day) || day > max_days_for_current_month)
    {
        year = 2025;
        month = 6;
        day = 3;
    }
}

static void FinishBootSequence(void)
{
    init_flag = false;
    shift = 0;
    rightshift = 0x01;
    cnt = 0;
    init_procedure = 0;

    original_year = year;
    original_month = month;
    original_day = day;
    original_hh = hh;
    original_mm = mm;
    original_ss = ss;
    original_alm_hh = alm_hh;
    original_alm_mm = alm_mm;
    original_alm_ss = alm_ss;
    unsaved_changes_active = false;
    seven_segment_display_on = true;
    shifting = true;
    main_display_mode = MAIN_DISPLAY_TIME;
    UpdateTimeAndDisplayBuffers();
}

static void OutputBootFrame(const uint8_t frame[8], uint8_t led_pattern)
{
    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, frame[cnt]);
    result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);
    Display_SetLedOutput(led_pattern);
}

void RunInitializationSequence(void)
{
    static bool rtc_restored = false;
    static const uint8_t all_on_frame[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t blank_frame[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    if (!rtc_restored)
    {
        RestoreRtcOrDefaultTime();
        rtc_restored = true;
    }

    if (clock2ms_flag == true)
    {
        clock2ms_flag = false;

        switch (init_procedure)
        {
        case 0:
            OutputBootFrame(all_on_frame, 0xFF);
            break;
        case 1:
            OutputBootFrame(blank_frame, 0x00);
            break;
        case 2:
            OutputBootFrame(kStudentIdFirst, 0x00);
            break;
        case 3:
            OutputBootFrame(kStudentIdSecond, 0x00);
            break;
        case 4:
            OutputBootFrame(kNameXuHaoran, 0x00);
            break;
        case 5:
            OutputBootFrame(kVersion, 0x00);
            break;
        default:
            FinishBootSequence();
            break;
        }

        cnt++;
        rightshift = (uint8_t)(rightshift << 1);
        if (cnt >= 0x8)
        {
            rightshift = 0x01;
            cnt = 0;
        }
    }

    if (clock900ms_flag == true)
    {
        clock900ms_flag = false;
        init_procedure++;
    }
}
