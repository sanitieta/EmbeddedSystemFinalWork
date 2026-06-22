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
#include "melody.h"

static const uint8_t kStudentIdFirst[8] = {0x6D, 0x5B, 0x66, 0x3F, 0x4F, 0x06, 0x6F, 0x06};  // 52403191
static const uint8_t kStudentIdSecond[8] = {0x4F, 0x06, 0x6F, 0x06, 0x3F, 0x07, 0x07, 0x5B}; // 31910772
static const uint8_t kNameXuHaoran[8] = {0x76, 0x3E, 0x76, 0x77, 0x3F, 0x50, 0x77, 0x54};    // XUHAORAN
static const uint8_t kVersion[8] = {0x00, 0x00, 0x3E, 0x06 | 0x80, 0x3F, 0x00, 0x00, 0x00};   // v1.0

static void RestoreRtcOrDefaultTime(void)
{
    uint8_t max_days_for_current_month;
    uint32_t past_sec;

    g.rtc.current = HibernateRTCGet();
    HibernateDataGet(g.rtc.fetched, 4);

    if (g.rtc.fetched[3] != 0 && g.rtc.current >= g.rtc.fetched[3])
    {
        past_sec = g.rtc.current - g.rtc.fetched[3];
        g.clock.hh = (int8_t)g.rtc.fetched[0];
        g.clock.mm = (int8_t)g.rtc.fetched[1];
        g.clock.ss = (int8_t)g.rtc.fetched[2];

        while (past_sec > 0)
        {
            DateTime_TickOneSecond();
            past_sec--;
        }
    }
    else
    {
        g.clock.hh = 0;
        g.clock.mm = 0;
        g.clock.ss = 0;
        g.clock.year = 2025;
        g.clock.month = 6;
        g.clock.day = 3;
        g.clock.alm_hh = 25;
        g.clock.alm_mm = 0;
        g.clock.alm_ss = 0;
    }

    max_days_for_current_month = g.rtc.days_in_month[g.clock.month];
    if (g.clock.month == 2 && is_leap_year(g.clock.year))
        max_days_for_current_month = 29;
    if (!is_valid_date(g.clock.year, g.clock.month, g.clock.day) || g.clock.day > max_days_for_current_month)
    {
        g.clock.year = 2025;
        g.clock.month = 6;
        g.clock.day = 3;
    }
}

static void FinishBootSequence(void)
{
    /* ── 显示扫描/动画状态重置 ── */
    g.disp.init_flag = false;
    g.disp.init_step = 0;
    g.disp.shift = 0;
    g.disp.rightshift = 0x01;
    g.disp.cnt = 0;

    /* ── 显示模式：正序、左移、流动、时间 ── */
    g.disp.reversed = false;
    g.disp.shift_mode = false;
    g.disp.shift_speed = false;
    g.disp.mode = MODE_FLOWING;
    g.disp.field = FIELD_NONE;
    g.disp.main_disp = MAIN_DISPLAY_TIME;

    /* ── 显示开关 ── */
    g.disp.on = true;
    g.disp.shifting = true;
    g.disp.blinking = false;

    /* ── 夜间模式 / LED 接管 / 天气 ── */
    g.disp.night_mode = false;
    g.disp.led_takeover = false;
    g.disp.led_pattern = 0x00;
    g.disp.weather_code = 0x00;

    /* ── 消息 / 保存闪烁 / 长按 ── */
    g.disp.msg_active = false;
    g.disp.msg_scroll = false;
    g.disp.save_blink_active = false;
    g.disp.long_press_saving = false;

    /* ── 闹钟运行时状态 ── */
    g.disp.alarm_ringing = false;
    g.disp.alarm_beep_on = false;

    /* ── 时钟原始值备份 (用于回滚) ── */
    g.clock.original_year = g.clock.year;
    g.clock.original_month = g.clock.month;
    g.clock.original_day = g.clock.day;
    g.clock.original_hh = g.clock.hh;
    g.clock.original_mm = g.clock.mm;
    g.clock.original_ss = g.clock.ss;
    g.clock.original_alm_hh = g.clock.alm_hh;
    g.clock.original_alm_mm = g.clock.alm_mm;
    g.clock.original_alm_ss = g.clock.alm_ss;
    g.clock.unsaved_changes_active = false;

    UpdateTimeAndDisplayBuffers();
    Melody_Start();  // 开机旋律: See You Again
}

static void OutputBootFrame(const uint8_t frame[8], uint8_t led_pattern)
{
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, frame[g.disp.cnt]);
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, g.disp.rightshift);
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

    if (g.timer.flag_2ms == true)
    {
        g.timer.flag_2ms = false;

        switch (g.disp.init_step)
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

        g.disp.cnt++;
        g.disp.rightshift = (uint8_t)(g.disp.rightshift << 1);
        if (g.disp.cnt >= 0x8)
        {
            g.disp.rightshift = 0x01;
            g.disp.cnt = 0;
        }
    }

    if (g.timer.flag_900ms == true)
    {
        g.timer.flag_900ms = false;
        g.disp.init_step++;
    }
}
