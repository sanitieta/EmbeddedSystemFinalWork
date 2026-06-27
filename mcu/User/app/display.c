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
#include "display.h"

static uint8_t DigitToAscii(uint8_t digit)
{
    return (uint8_t)('0' + digit);
}

static uint8_t HexDigit(uint8_t value)
{
    value &= 0x0F;
    return (uint8_t)(value < 10 ? ('0' + value) : ('A' + value - 10));
}

static uint8_t SegmentForChar(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return g.disp.seg7[c - '0'];
    if (c >= 'a' && c <= 'z')
        c = (uint8_t)(c - 0x20);

    switch (c)
    {
    case 'A':
        return 0x77;
    case 'B':
        return 0x7C;
    case 'C':
        return 0x39;
    case 'D':
        return 0x5E;
    case 'E':
        return 0x79;
    case 'F':
        return 0x71;
    case 'G':
        return 0x3D;
    case 'H':
        return 0x76;
    case 'I':
        return 0x06;
    case 'J':
        return 0x1E;
    case 'K':
        return 0x76; // 7段码 K 与 H/X 共用 (竖+竖+中横)
    case 'L':
        return 0x38;
    case 'N':
        return 0x54;
    case 'O':
        return 0x3F;
    case 'P':
        return 0x73;
    case 'R':
        return 0x50;
    case 'S':
        return 0x6D;
    case 'T':
        return 0x78;
    case 'U':
        return 0x3E;
    case 'V':
        return 0x3E;
    case 'X':
        return 0x76;
    case 'Y':
        return 0x6E;
    case '-':
        return 0x5c;
    case '_':
    case ' ':
        return 0x00;
    default:
        return 0x00;
    }
}

/* 将 7 段码反向映射为 ASCII 字符 (用于流动显示 *EVT:DISP 上报) */
static uint8_t CharForSegment(uint8_t seg)
{
    uint8_t base = seg & 0x7F;  /* 屏蔽 dp 位 */
    uint8_t i;

    if (base == 0x00)
        return '_';

    for (i = 0; i < 18; ++i)
    {
        if (g.disp.seg7[i] == base)
        {
            if (i <= 9)
                return (uint8_t)('0' + i);
            if (i >= 10 && i <= 15)
                return (uint8_t)('A' + (uint8_t)(i - 10));
            if (i == 16)
                return '-';
            if (i == 17)
                return '_';
        }
    }
    return '_';
}

/* 将 n 字符内容在 8 位缓冲区中居中 (左右各留余量) */
static void CenterContent(uint8_t chars[8], uint8_t n, uint8_t *dp_hex)
{
    uint8_t pad = (uint8_t)((8 - n) / 2U);
    uint8_t i;
    /* 右移腾出左侧 padding */
    for (i = 8; i > pad; --i)
        chars[i - 1U] = chars[i - 1U - pad];
    for (i = 0; i < pad; ++i)
        chars[i] = '_';
    *dp_hex = (uint8_t)(*dp_hex << pad);
}

static void BuildCurrentDisplay(uint8_t chars[8], uint8_t *dp_hex)
{
    uint8_t i;

    for (i = 0; i < 8; ++i)
    {
        chars[i] = '_';
    }
    *dp_hex = 0x00;

    if (!g.disp.on)
    {
        return;
    }

    if (g.disp.msg_active)
    {
        for (i = 0; i < 8; ++i)
        {
            uint8_t msg_index = (uint8_t)(g.disp.msg_shift + i);
            if (msg_index < g.disp.msg_len)
            {
                chars[i] = g.disp.msg_buf[msg_index];
            }
        }
        return;
    }

    if (g.disp.night_mode)
    {
        chars[0] = DigitToAscii((uint8_t)(g.clock.hh / 10));
        chars[1] = DigitToAscii((uint8_t)(g.clock.hh % 10));
        chars[2] = DigitToAscii((uint8_t)(g.clock.mm / 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.mm % 10));
        *dp_hex = 0x02;
        return;
    }

    if (g.disp.mode == MODE_DATE_SET)
    {
        chars[0] = DigitToAscii((uint8_t)((g.clock.temp_year / 1000) % 10));
        chars[1] = DigitToAscii((uint8_t)((g.clock.temp_year / 100) % 10));
        chars[2] = DigitToAscii((uint8_t)((g.clock.temp_year / 10) % 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.temp_year % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.temp_month / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.temp_month % 10));
        chars[6] = DigitToAscii((uint8_t)(g.clock.temp_day / 10));
        chars[7] = DigitToAscii((uint8_t)(g.clock.temp_day % 10));
        *dp_hex = 0x28;
        return;
    }

    if (g.disp.mode == MODE_TIME_SET)
    {
        chars[0] = DigitToAscii((uint8_t)(g.clock.temp_hh / 10));
        chars[1] = DigitToAscii((uint8_t)(g.clock.temp_hh % 10));
        chars[2] = DigitToAscii((uint8_t)(g.clock.temp_mm / 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.temp_mm % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.temp_ss / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.temp_ss % 10));
        *dp_hex = 0x0A;
        CenterContent(chars, 6, dp_hex);
        return;
    }

    if (g.disp.mode == MODE_ALARM_SET || g.disp.mode == MODE_ALARM_DISPLAY)
    {
        uint8_t show_hh = (g.disp.mode == MODE_ALARM_SET) ? g.clock.temp_alm_hh : (uint8_t)g.clock.alm_hh;
        uint8_t show_mm = (g.disp.mode == MODE_ALARM_SET) ? g.clock.temp_alm_mm : (uint8_t)g.clock.alm_mm;
        uint8_t show_ss = (g.disp.mode == MODE_ALARM_SET) ? g.clock.temp_alm_ss : (uint8_t)g.clock.alm_ss;

        if (g.clock.alm_hh == 25 && g.disp.mode == MODE_ALARM_DISPLAY)
        {
            memcpy(chars, "AL xx xx", 8);
        }
        else
        {
            chars[0] = DigitToAscii((uint8_t)(show_hh / 10));
            chars[1] = DigitToAscii((uint8_t)(show_hh % 10));
            chars[2] = DigitToAscii((uint8_t)(show_mm / 10));
            chars[3] = DigitToAscii((uint8_t)(show_mm % 10));
            chars[4] = DigitToAscii((uint8_t)(show_ss / 10));
            chars[5] = DigitToAscii((uint8_t)(show_ss % 10));
            *dp_hex = 0x0A;
            CenterContent(chars, 6, dp_hex);
        }
        return;
    }

    if (g.disp.main_disp == MAIN_DISPLAY_DATE)
    {
        chars[0] = DigitToAscii((uint8_t)((g.clock.year / 10) % 10));
        chars[1] = DigitToAscii((uint8_t)(g.clock.year % 10));
        chars[2] = DigitToAscii((uint8_t)(g.clock.month / 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.month % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.day / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.day % 10));
        *dp_hex = 0x0A;
        CenterContent(chars, 6, dp_hex);
    }
    else if (g.disp.main_disp == MAIN_DISPLAY_YEAR)
    {
        chars[0] = DigitToAscii((uint8_t)((g.clock.year / 1000) % 10));
        chars[1] = DigitToAscii((uint8_t)((g.clock.year / 100) % 10));
        chars[2] = DigitToAscii((uint8_t)((g.clock.year / 10) % 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.year % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.month / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.month % 10));
        chars[6] = DigitToAscii((uint8_t)(g.clock.day / 10));
        chars[7] = DigitToAscii((uint8_t)(g.clock.day % 10));
        *dp_hex = 0x28;
    }
    else if (g.disp.main_disp == MAIN_DISPLAY_FLOW)
    {
        /* 流动模式: 取当前 8 位窗口的 7 段码，反向映射为 ASCII */
        for (i = 0; i < 8; ++i)
        {
            uint8_t idx = ((uint8_t)g.disp.shift + i) % 18U;
            uint8_t seg = g.disp.master_buf[idx];

            chars[i] = CharForSegment(seg);
            if (seg & 0x80)
                *dp_hex |= (uint8_t)(1U << i);
        }
    }
    else
    {
        /* MAIN_DISPLAY_TIME: 居中显示 HH.MM.SS */
        chars[0] = DigitToAscii((uint8_t)(g.clock.hh / 10));
        chars[1] = DigitToAscii((uint8_t)(g.clock.hh % 10));
        chars[2] = DigitToAscii((uint8_t)(g.clock.mm / 10));
        chars[3] = DigitToAscii((uint8_t)(g.clock.mm % 10));
        chars[4] = DigitToAscii((uint8_t)(g.clock.ss / 10));
        chars[5] = DigitToAscii((uint8_t)(g.clock.ss % 10));
        *dp_hex = 0x0A;
        CenterContent(chars, 6, dp_hex);
    }
}

static uint8_t ReverseDpBitmap(uint8_t left_dp)
{
    uint8_t right_dp = 0;
    uint8_t i;

    for (i = 0; i < 8; ++i)
    {
        if (left_dp & (uint8_t)(1U << i))
        {
            if (i == 7)
            {
                right_dp |= 1U;           /* bit 7 → bit 0 */
            }
            else
            {
                right_dp |= (uint8_t)(1U << (6 - i)); /* bits 0-6 镜像 */
            }
        }
    }
    return right_dp;
}

void Display_SetLedOutput(uint8_t led_pattern)
{
    g.disp.i2c_result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, (uint8_t)~led_pattern);
}

void Display_UpdateStatusLeds(void)
{
    uint8_t pattern;

    if (g.disp.led_takeover)
    {
        pattern = g.disp.led_pattern;
        g.disp.current_led = pattern;
        Display_SetLedOutput(pattern);
        /* LED takeover 变化检测 */
        if (pattern != g.disp.last_sent_led)
        {
            g.disp.last_sent_led = pattern;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:LED ");
            UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(pattern >> 4)));
            UARTCharPutBlocking(UART0_BASE, HexDigit(pattern));
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
        }
        return;
    }

    /* K5 (DISP) 按下时 LED0-LED7 全亮, 松开恢复 */
    if (g.in.state[4])
    {
        pattern = 0xFF;
        g.disp.current_led = pattern;
        Display_SetLedOutput(pattern);
        if (pattern != g.disp.last_sent_led)
        {
            g.disp.last_sent_led = pattern;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:LED ");
            UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(pattern >> 4)));
            UARTCharPutBlocking(UART0_BASE, HexDigit(pattern));
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
        }
        return;
    }

    pattern = 0x00;
    if ((g.timer.tick % V_T1s) < V_T500ms)
    {
        pattern |= 0x01;
    }

    if (!g.disp.night_mode)
    {
        if (g.disp.alarm_ringing)
        {
            if ((g.timer.tick % 400U) < 200U)
                pattern |= 0x02;
        }
        else if (g.clock.alm_hh != 25)
        {
            pattern |= 0x02;
        }

        if (g.disp.mode == MODE_DATE_SET || g.disp.mode == MODE_TIME_SET || g.disp.mode == MODE_ALARM_SET)
        {
            pattern |= 0x04;
        }

        if ((int32_t)(g.disp.uart_activity_until - g.timer.tick) > 0)
        {
            pattern |= 0x08;
        }

        if (g.disp.ntp_synced)
        {
            pattern |= 0x10; /* LED4: NTP 对时完成 */
        }
    }

    /* LED5-LED7: 天气指示 (PC 端 *SET:WEATHER 下发, 不进入接管模式) */
    pattern |= g.disp.weather_code;

    g.disp.current_led = pattern;
    Display_SetLedOutput(pattern);

    /* LED 状态变化时立即上报 *EVT:LED (心跳在 UpdateTimeAndDisplayBuffers 中每秒发送) */
    if (pattern != g.disp.last_sent_led)
    {
        g.disp.last_sent_led = pattern;
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:LED ");
        UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(pattern >> 4)));
        UARTCharPutBlocking(UART0_BASE, HexDigit(pattern));
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
    }
}

void Display_FormatBufferForProtocol(const uint8_t *src, uint8_t len, uint8_t *dst)
{
    uint8_t i;

    if (g.disp.reversed)
    {
        for (i = 0; i < len; ++i)
        {
            dst[i] = src[len - 1U - i];
        }
    }
    else
    {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

void Display_SendEvent(void)
{
    uint8_t chars[8];
    uint8_t out_chars[9];
    uint8_t dp_hex;
    uint8_t out_dp_hex;

    BuildCurrentDisplay(chars, &dp_hex);
    if (g.disp.reversed)
    {
        Display_FormatBufferForProtocol(chars, 8, out_chars);
        out_dp_hex = ReverseDpBitmap(dp_hex);
    }
    else
    {
        memcpy(out_chars, chars, 8);
        out_chars[8] = '\0';
        out_dp_hex = dp_hex;
    }

    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:DISP ");
    UARTStringPutNOBlocking(UART0_BASE, out_chars);
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
    UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(out_dp_hex >> 4)));
    UARTCharPutBlocking(UART0_BASE, HexDigit(out_dp_hex));
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
    g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
}

void Display_SendLedEvent(void)
{
    uint8_t pattern;

    /* 读取当前 LED 输出值 — 优先 takeover 模式，否则用最后一次计算的状态 */
    if (g.disp.led_takeover)
    {
        pattern = g.disp.led_pattern;
    }
    else
        pattern = g.disp.current_led;

    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:LED ");
    UARTCharPutBlocking(UART0_BASE, HexDigit((uint8_t)(pattern >> 4)));
    UARTCharPutBlocking(UART0_BASE, HexDigit(pattern));
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
    g.disp.last_sent_led = pattern;
}

void Display_SendModeEvent(const char *state)
{
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:MODE ");
    while (*state)
        UARTCharPutBlocking(UART0_BASE, (uint8_t)(*state++));
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
    g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
}

void Display_SendEditEvent(const char *type, const uint8_t *value)
{
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"*EVT:EDIT ");
    while (*type)
        UARTCharPutBlocking(UART0_BASE, (uint8_t)(*type++));
    UARTCharPutBlocking(UART0_BASE, ' ');
    while (*value)
        UARTCharPutBlocking(UART0_BASE, *value++);
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
    g.disp.uart_activity_until = g.timer.tick + UART_ACTIVITY_FLASH_MS;
}

void Display_StartMessage(const uint8_t *text, uint8_t len)
{
    if (len > 32)
        len = 32;

    memcpy(g.disp.msg_buf, text, len);
    g.disp.msg_buf[len] = '\0';
    g.disp.msg_len = len;
    g.disp.msg_shift = 0;
    g.disp.msg_active = true;
    g.disp.msg_scroll = (len > 8);
    g.disp.msg_start = g.timer.tick;
    g.disp.msg_last_shift = g.timer.tick;
    g.disp.shifting = false;
    g.disp.on = true;
    Display_SendEvent();
}

void Display_StopMessage(void)
{
    g.disp.msg_active = false;
    g.disp.msg_scroll = false;
    g.disp.msg_len = 0;
    g.disp.msg_shift = 0;
    g.disp.shifting = true;
    UpdateTimeAndDisplayBuffers();
}

// 更新7段数码管显示
void Update7SegmentDisplay(void)
{
    uint8_t segment_data;                // 当前数码管段码
    uint8_t local_cnt;                   // 本地数码管计数
    uint8_t display_cnt;                 // 考虑FORMAT后的显示位置
    uint8_t effective_segment_data;      // 实际发送的段码

    if (!g.disp.on) // 如果7段数码管关闭
    {
        // 关闭所有数码管
        g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
        g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, 0x00);
        Display_UpdateStatusLeds();
        return;
    }

    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00); // 确保所有位选都关闭

    local_cnt = g.disp.cnt; // 获取当前轮询的数码管索引
    display_cnt = g.disp.reversed ? (uint8_t)(7 - local_cnt) : local_cnt;

    if (g.disp.msg_active)
    {
        uint8_t msg_index = (uint8_t)(g.disp.msg_shift + local_cnt);
        if (msg_index < g.disp.msg_len)
            segment_data = SegmentForChar(g.disp.msg_buf[msg_index]);
        else
            segment_data = 0x00;
    }
    else if (g.disp.night_mode)
    {
        if (local_cnt == 0)
            segment_data = g.disp.seg7[g.clock.hh / 10];
        else if (local_cnt == 1)
            segment_data = g.disp.seg7[g.clock.hh % 10] | 0x80;
        else if (local_cnt == 2)
            segment_data = g.disp.seg7[g.clock.mm / 10];
        else if (local_cnt == 3)
            segment_data = g.disp.seg7[g.clock.mm % 10];
        else
            segment_data = 0x00;
    }
    else if (g.disp.mode == MODE_FLOWING) // 流动显示模式
    {
        if (g.disp.main_disp == MAIN_DISPLAY_FLOW)
        {
            segment_data = g.disp.master_buf[(g.disp.shift + local_cnt) % 18];
        }
        else if (g.disp.main_disp == MAIN_DISPLAY_TIME)
        {
            if (g.disp.reversed)
            {
                if (display_cnt == 0 || display_cnt == 7)
                    segment_data = 0x00;
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[g.clock.hh / 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.hh % 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.mm / 10] | 0x80;
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.mm % 10];
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.ss / 10] | 0x80;
                else
                    segment_data = g.disp.seg7[g.clock.ss % 10];
            }
            else
            {
                if (display_cnt == 0 || display_cnt == 7)
                    segment_data = 0x00;
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[g.clock.hh / 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.hh % 10] | 0x80;
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.mm / 10];
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.mm % 10] | 0x80;
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.ss / 10];
                else
                    segment_data = g.disp.seg7[g.clock.ss % 10];
            }
        }
        else if (g.disp.main_disp == MAIN_DISPLAY_DATE)
        {
            if (g.disp.reversed)
            {
                if (display_cnt == 0 || display_cnt == 7)
                    segment_data = 0x00;
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[(g.clock.year / 10) % 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.year % 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.month / 10] | 0x80;
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.month % 10];
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.day / 10] | 0x80;
                else
                    segment_data = g.disp.seg7[g.clock.day % 10];
            }
            else
            {
                if (display_cnt == 0 || display_cnt == 7)
                    segment_data = 0x00;
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[(g.clock.year / 10) % 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.year % 10] | 0x80;
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.month / 10];
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.month % 10] | 0x80;
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.day / 10];
                else
                    segment_data = g.disp.seg7[g.clock.day % 10];
            }
        }
        else if (g.disp.main_disp == MAIN_DISPLAY_YEAR)
        {
            if (g.disp.reversed)
            {
                if (display_cnt == 0)
                    segment_data = g.disp.seg7[(g.clock.year / 1000) % 10];
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[(g.clock.year / 100) % 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[(g.clock.year / 10) % 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.year % 10];
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.month / 10] | 0x80;
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.month % 10];
                else if (display_cnt == 6)
                    segment_data = g.disp.seg7[g.clock.day / 10] | 0x80;
                else
                    segment_data = g.disp.seg7[g.clock.day % 10];
            }
            else
            {
                if (display_cnt == 0)
                    segment_data = g.disp.seg7[(g.clock.year / 1000) % 10];
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[(g.clock.year / 100) % 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[(g.clock.year / 10) % 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.year % 10] | 0x80;
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.month / 10];
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.month % 10] | 0x80;
                else if (display_cnt == 6)
                    segment_data = g.disp.seg7[g.clock.day / 10];
                else
                    segment_data = g.disp.seg7[g.clock.day % 10];
            }
        }
    }
    else if (g.disp.mode == MODE_DATE_SET) // 日期设置模式
    {
        if (g.disp.reversed)
        {
            if (display_cnt == 0)
                segment_data = g.disp.seg7[(g.clock.temp_year / 1000) % 10];
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[(g.clock.temp_year / 100) % 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[(g.clock.temp_year / 10) % 10];
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[(g.clock.temp_year % 10)];
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_month / 10] | 0x80;
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_month % 10];
            else if (display_cnt == 6)
                segment_data = g.disp.seg7[g.clock.temp_day / 10] | 0x80;
            else
                segment_data = g.disp.seg7[g.clock.temp_day % 10];
        }
        else
        {
            if (display_cnt == 0)
                segment_data = g.disp.seg7[(g.clock.temp_year / 1000) % 10];
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[(g.clock.temp_year / 100) % 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[(g.clock.temp_year / 10) % 10];
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[(g.clock.temp_year % 10)] | 0x80;
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_month / 10];
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_month % 10] | 0x80;
            else if (display_cnt == 6)
                segment_data = g.disp.seg7[g.clock.temp_day / 10];
            else
                segment_data = g.disp.seg7[g.clock.temp_day % 10];
        }
        if (g.disp.blinking && (g.timer.tick % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS)) >= BLINK_ON_TIME_MS)
        {
            if ((g.disp.field == FIELD_YEAR && display_cnt <= 3) ||
                (g.disp.field == FIELD_MONTH && display_cnt >= 4 && display_cnt <= 5) ||
                (g.disp.field == FIELD_DAY && display_cnt >= 6 && display_cnt <= 7))
            {
                segment_data = 0x00;
            }
        }
    }
    else if (g.disp.mode == MODE_TIME_SET) // 时间设置模式
    {
        if (g.disp.reversed)
        {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[g.clock.temp_hh / 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[g.clock.temp_hh % 10];
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[g.clock.temp_mm / 10] | 0x80;
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_mm % 10];
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_ss / 10] | 0x80;
            else
                segment_data = g.disp.seg7[g.clock.temp_ss % 10];
        }
        else
        {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[g.clock.temp_hh / 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[g.clock.temp_hh % 10] | 0x80;
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[g.clock.temp_mm / 10];
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_mm % 10] | 0x80;
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_ss / 10];
            else
                segment_data = g.disp.seg7[g.clock.temp_ss % 10];
        }
        if (g.disp.blinking && (g.timer.tick % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS)) >= BLINK_ON_TIME_MS)
        {
            if ((g.disp.field == FIELD_HOUR && display_cnt >= 1 && display_cnt <= 2) ||
                (g.disp.field == FIELD_MINUTE && display_cnt >= 3 && display_cnt <= 4) ||
                (g.disp.field == FIELD_SECOND && display_cnt >= 5 && display_cnt <= 6))
            {
                segment_data = 0x00;
            }
        }
    }
    else if (g.disp.mode == MODE_ALARM_SET) // 闹钟设置模式
    {
        if (g.disp.reversed)
        {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[g.clock.temp_alm_hh / 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[g.clock.temp_alm_hh % 10];
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[g.clock.temp_alm_mm / 10] | 0x80;
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_alm_mm % 10];
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_alm_ss / 10] | 0x80;
            else
                segment_data = g.disp.seg7[g.clock.temp_alm_ss % 10];
        }
        else
        {
            if (display_cnt == 0 || display_cnt == 7)
                segment_data = 0x00;
            else if (display_cnt == 1)
                segment_data = g.disp.seg7[g.clock.temp_alm_hh / 10];
            else if (display_cnt == 2)
                segment_data = g.disp.seg7[g.clock.temp_alm_hh % 10] | 0x80;
            else if (display_cnt == 3)
                segment_data = g.disp.seg7[g.clock.temp_alm_mm / 10];
            else if (display_cnt == 4)
                segment_data = g.disp.seg7[g.clock.temp_alm_mm % 10] | 0x80;
            else if (display_cnt == 5)
                segment_data = g.disp.seg7[g.clock.temp_alm_ss / 10];
            else
                segment_data = g.disp.seg7[g.clock.temp_alm_ss % 10];
        }
        if (g.disp.blinking && (g.timer.tick % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS)) >= BLINK_ON_TIME_MS)
        {
            if ((g.disp.field == FIELD_ALARM_HOUR && display_cnt >= 1 && display_cnt <= 2) ||
                (g.disp.field == FIELD_ALARM_MINUTE && display_cnt >= 3 && display_cnt <= 4) ||
                (g.disp.field == FIELD_ALARM_SECOND && display_cnt >= 5 && display_cnt <= 6))
            {
                segment_data = 0x00;
            }
        }
    }
    else if (g.disp.mode == MODE_ALARM_DISPLAY) // 闹钟显示模式
    {
        if (g.disp.reversed)
        {
            /* FORMAT RIGHT: AL + time 整体逆序 */
            if (g.clock.alm_hh == 25)
            {
                if (display_cnt <= 5)
                    segment_data = g.disp.seg7[17];
                else if (display_cnt == 6)
                    segment_data = 0x38;       // 'L'
                else
                    segment_data = g.disp.seg7[10]; // 'A'
            }
            else
            {
                if (display_cnt == 0)
                    segment_data = g.disp.seg7[g.clock.alm_ss % 10];
                else if (display_cnt == 1)
                    segment_data = g.disp.seg7[g.clock.alm_ss / 10];
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.alm_mm % 10] | 0x80;
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.alm_mm / 10];
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.alm_hh % 10] | 0x80;
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.alm_hh / 10];
                else if (display_cnt == 6)
                    segment_data = 0x38;       // 'L'
                else
                    segment_data = g.disp.seg7[10]; // 'A'
            }
        }
        else
        {
            if (g.clock.alm_hh == 25)
            {
                if (display_cnt <= 1)
                    segment_data = (display_cnt == 0) ? g.disp.seg7[10] : (uint8_t)0x38;
                else
                    segment_data = g.disp.seg7[17];
            }
            else
            {
                if (display_cnt == 0)
                    segment_data = g.disp.seg7[10];
                else if (display_cnt == 1)
                    segment_data = 0x38;
                else if (display_cnt == 2)
                    segment_data = g.disp.seg7[g.clock.alm_hh / 10];
                else if (display_cnt == 3)
                    segment_data = g.disp.seg7[g.clock.alm_hh % 10] | 0x80;
                else if (display_cnt == 4)
                    segment_data = g.disp.seg7[g.clock.alm_mm / 10];
                else if (display_cnt == 5)
                    segment_data = g.disp.seg7[g.clock.alm_mm % 10] | 0x80;
                else if (display_cnt == 6)
                    segment_data = g.disp.seg7[g.clock.alm_ss / 10];
                else
                    segment_data = g.disp.seg7[g.clock.alm_ss % 10];
            }
        }
    }
    else
    {
        segment_data = 0x00;
    }

    effective_segment_data = segment_data;

    // 处理保存成功后的闪烁效果
    if (g.disp.save_blink_active && (g.timer.tick - g.disp.save_blink_timer) % (BLINK_ON_TIME_MS + BLINK_OFF_TIME_MS) >= BLINK_ON_TIME_MS)
    {
        effective_segment_data = 0x00; // 闪烁关闭时显示空白
    }

    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, effective_segment_data); // 发送段码
    g.disp.i2c_result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, g.disp.rightshift);             // 发送位选
    Display_UpdateStatusLeds();

    g.disp.cnt++;                        // 切换到下一个数码管
    g.disp.rightshift = g.disp.rightshift << 1; // 移位位选
    if (g.disp.cnt >= 0x8)               // 8个数码管循环
    {
        g.disp.rightshift = 0x01;
        g.disp.cnt = 0;
    }
}

// 更新显示移位效果

// 更新显示移位效果
void UpdateDisplayShift(void)
{
    if (g.disp.msg_active)
    {
        if (g.disp.msg_scroll)
        {
            if ((g.timer.tick - g.disp.msg_last_shift) >= V_T300ms)
            {
                g.disp.msg_last_shift = g.timer.tick;
                g.disp.msg_shift++;
                if (g.disp.msg_shift > (int8_t)g.disp.msg_len)
                {
                    Display_StopMessage();
                }
                else
                {
                    Display_SendEvent();
                }
            }
        }
        else if ((g.timer.tick - g.disp.msg_start) >= MESSAGE_STATIC_MS)
        {
            Display_StopMessage();
        }
        return;
    }

    if (g.disp.shift_mode == false) // 左移模式
    {
        g.disp.shift++;     // 移位量增加
        g.disp.shift %= 18; // 0-17循环
    }
    else // 右移模式
    {
        g.disp.shift--; // 移位量减少
        if (g.disp.shift < 0)
            g.disp.shift = 17; // 负数则回到17
    }
}

// 更新时间、日期和显示缓冲区 供UART/数码管参考
void UpdateTimeAndDisplayBuffers(void)
{
    // 格式化时间字符串
    g.disp.time_buf[0] = (uint8_t)(g.clock.hh / 10) + '0';
    g.disp.time_buf[1] = (uint8_t)(g.clock.hh % 10) + '0';
    g.disp.time_buf[2] = ':';
    g.disp.time_buf[3] = (uint8_t)(g.clock.mm / 10) + '0';
    g.disp.time_buf[4] = (uint8_t)(g.clock.mm % 10) + '0';
    g.disp.time_buf[5] = ':';
    g.disp.time_buf[6] = (uint8_t)(g.clock.ss / 10) + '0';
    g.disp.time_buf[7] = (uint8_t)(g.clock.ss % 10) + '0';
    g.disp.time_buf[8] = '\0';

    // 格式化闹钟时间字符串，如果未设置则显示"xx:xx:xx"
    if (g.clock.alm_hh == 25)
    {
        memcpy(g.disp.alarm_buf, "xx:xx:xx", 8);
    }
    else
    {
        g.disp.alarm_buf[0] = (uint8_t)(g.clock.alm_hh / 10) + '0';
        g.disp.alarm_buf[1] = (uint8_t)(g.clock.alm_hh % 10) + '0';
        g.disp.alarm_buf[2] = ':';
        g.disp.alarm_buf[3] = (uint8_t)(g.clock.alm_mm / 10) + '0';
        g.disp.alarm_buf[4] = (uint8_t)(g.clock.alm_mm % 10) + '0';
        g.disp.alarm_buf[5] = ':';
        g.disp.alarm_buf[6] = (uint8_t)(g.clock.alm_ss / 10) + '0';
        g.disp.alarm_buf[7] = (uint8_t)(g.clock.alm_ss % 10) + '0';
    }
    g.disp.alarm_buf[8] = '\0';

    // 格式化日期字符串
    g.disp.date_buf[0] = (uint8_t)((g.clock.year / 1000) % 10) + '0';
    g.disp.date_buf[1] = (uint8_t)((g.clock.year / 100) % 10) + '0';
    g.disp.date_buf[2] = (uint8_t)((g.clock.year / 10) % 10) + '0';
    g.disp.date_buf[3] = (uint8_t)(g.clock.year % 10) + '0';
    g.disp.date_buf[4] = '-';
    g.disp.date_buf[5] = (uint8_t)(g.clock.month / 10) + '0';
    g.disp.date_buf[6] = (uint8_t)(g.clock.month % 10) + '0';
    g.disp.date_buf[7] = '-';
    g.disp.date_buf[8] = (uint8_t)(g.clock.day / 10) + '0';
    g.disp.date_buf[9] = (uint8_t)(g.clock.day % 10) + '0';
    g.disp.date_buf[10] = '\0';

    // 填充主显示缓冲区，用于7段数码管滚动显示
    g.disp.master_buf[0] = g.disp.seg7[(g.clock.year / 1000) % 10];
    g.disp.master_buf[1] = g.disp.seg7[(g.clock.year / 100) % 10];
    g.disp.master_buf[2] = g.disp.seg7[(g.clock.year / 10) % 10];
    g.disp.master_buf[3] = g.disp.seg7[(g.clock.year % 10)] | 0x80; // 年份末位带小数点
    g.disp.master_buf[4] = g.disp.seg7[g.clock.month / 10];
    g.disp.master_buf[5] = g.disp.seg7[g.clock.month % 10] | 0x80; // 月份末位带小数点
    g.disp.master_buf[6] = g.disp.seg7[g.clock.day / 10];
    g.disp.master_buf[7] = g.disp.seg7[g.clock.day % 10];

    g.disp.master_buf[8] = 0x00; // 分隔符
    g.disp.master_buf[9] = 0x00; // 分隔符

    g.disp.master_buf[10] = g.disp.seg7[g.clock.hh / 10];
    g.disp.master_buf[11] = g.disp.seg7[g.clock.hh % 10] | 0x80; // 小时末位带小数点
    g.disp.master_buf[12] = g.disp.seg7[g.clock.mm / 10];
    g.disp.master_buf[13] = g.disp.seg7[g.clock.mm % 10] | 0x80; // 分钟末位带小数点
    g.disp.master_buf[14] = g.disp.seg7[g.clock.ss / 10];
    g.disp.master_buf[15] = g.disp.seg7[g.clock.ss % 10];

    g.disp.master_buf[16] = 0x00; // 填充
    g.disp.master_buf[17] = 0x00; // 填充

    // 存储当前时间到休眠模块的RTC存储
    g.rtc.stored[0] = (uint32_t)g.clock.hh;
    g.rtc.stored[1] = (uint32_t)g.clock.mm;
    g.rtc.stored[2] = (uint32_t)g.clock.ss;
    g.rtc.stored[3] = HibernateRTCGet(); // 存储当前RTC计数值
    HibernateDataSet(g.rtc.stored, 4);   // 保存数据
    if (!g.disp.msg_active)
    {
        Display_SendEvent();
        Display_SendLedEvent();
    }
}
