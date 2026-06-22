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
#include "command.h"
#include "display.h"
#include "alarm.h"

// 将字符转换为大写
static uint8_t toUpper(uint8_t c)
{
    if (c >= 'a' && c <= 'z')
    {
        return (uint8_t)(c - 0x20);
    }
    return c;
}

// 统计命令字符串中必输字符数 ('*' 和大写字母)
static uint8_t CountMandatory(const char *str)
{
    uint8_t count = 0;
    while (*str)
    {
        if (*str == '*' || (*str >= 'A' && *str <= 'Z'))
            count++;
        str++;
    }
    return count;
}

static int8_t HexValue(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return (int8_t)(c - '0');
    c = toUpper(c);
    if (c >= 'A' && c <= 'F')
        return (int8_t)(c - 'A' + 10);
    return -1;
}

static bool ParseHexByte(const command_token_t *token, uint8_t *value)
{
    int8_t hi;
    int8_t lo;

    if (token->token_len != 2)
        return false;

    hi = HexValue(token->token_str[0]);
    lo = HexValue(token->token_str[1]);
    if (hi < 0 || lo < 0)
        return false;

    *value = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)lo);
    return true;
}

static void PutProtocolBuffer(const uint8_t *buffer, uint8_t len)
{
    uint8_t formatted[12];
    uint8_t normalized[12];
    uint8_t i;

    for (i = 0; i < len; ++i)
    {
        normalized[i] = (buffer[i] == ':' || buffer[i] == '-') ? '.' : buffer[i];
    }
    normalized[len] = '\0';

    Display_FormatBufferForProtocol(normalized, len, formatted);
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK ");
    UARTStringPutNOBlocking(UART0_BASE, formatted);
    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
}

static void ResetProtocolState(void)
{
    g.disp.on = true;
    g.disp.shift_mode = false;
    g.disp.reversed = false;
    g.disp.night_mode = false;
    g.disp.led_takeover = false;
    g.disp.led_pattern = 0x00;
    g.disp.weather_code = 0x00;
    g.disp.beep_until = 0;
    g.disp.ntp_synced = false;
    g.disp.msg_active = false;
    g.disp.msg_scroll = false;
    g.disp.msg_len = 0;
    g.disp.msg_shift = 0;
    g.disp.shifting = true;
    g.disp.mode = MODE_FLOWING;
    g.disp.field = FIELD_NONE;
    g.disp.blinking = false;
    g.disp.main_disp = MAIN_DISPLAY_TIME;
    Display_SendModeEvent("FLOWING");
    StopAlarmRinging(false);
    UpdateTimeAndDisplayBuffers();
    Display_UpdateStatusLeds();
}

static uint8_t FindRawPayloadOffset(uint8_t token_idx)
{
    uint8_t i;
    uint8_t current_token = 0;
    bool in_token = false;

    for (i = 0; i < g.uart.rx_len; ++i)
    {
        if (g.uart.rx_buf[i] != ' ' && g.uart.rx_buf[i] != '\t')
        {
            if (!in_token)
            {
                if (current_token == token_idx)
                    return i;
                current_token++;
                in_token = true;
            }
        }
        else
        {
            in_token = false;
        }
    }
    return g.uart.rx_len;
}

// 比较命令Token与字符串，支持最小匹配长度
bool compareTokens(const command_token_t *token, const char *str_literal, uint8_t min_match_len)
{
    uint8_t i;
    uint8_t literal_len;

    literal_len = 0;
    while (str_literal[literal_len] != '\0')
    {
        literal_len++;
    }

    // Token长度必须在最小匹配长度和字面量长度之间
    if (token->token_len < min_match_len || token->token_len > literal_len)
    {
        return false;
    }

    // 逐个字符比较 (不区分大小写)
    for (i = 0; i < token->token_len; ++i)
    {
        if (toUpper(token->token_str[i]) != toUpper((uint8_t)str_literal[i]))
        {
            return false;
        }
    }
    return true;
}

// 比较字段关键字（如"YEAR", "MONTH"），支持最小匹配长度

// 比较字段关键字（如"YEAR", "MONTH"），支持最小匹配长度
static bool compareFieldKeyword(const command_token_t *token, const char *full_keyword, uint8_t min_match_len)
{
    uint8_t i;
    uint8_t full_keyword_len = (uint8_t)strlen(full_keyword);

    // Token长度必须大于等于最小匹配长度
    if (token->token_len < min_match_len)
    {
        return false;
    }

    // Token长度不能超过完整关键字长度
    if (token->token_len > full_keyword_len)
    {
        return false;
    }

    // 逐个字符比较 (不区分大小写)
    for (i = 0; i < token->token_len; ++i)
    {
        if (toUpper(token->token_str[i]) != toUpper((uint8_t)full_keyword[i]))
        {
            return false;
        }
    }
    return true;
}

// 特殊的Token比较函数，用于处理带冒号前缀的Token

// 特殊的Token比较函数，用于处理带冒号前缀的Token
static bool compareTokens_modified_for_colon_prefix(const uint8_t *token_str_ptr, uint8_t token_len, const char *str_literal, uint8_t min_match_len)
{
    uint8_t i;
    uint8_t literal_len;

    literal_len = (uint8_t)strlen(str_literal);

    // Token长度必须在最小匹配长度和字面量长度之间
    if (token_len < min_match_len || token_len > literal_len)
    {
        return false;
    }

    // 逐个字符比较 (不区分大小写)
    for (i = 0; i < token_len; ++i)
    {
        if (toUpper(token_str_ptr[i]) != toUpper((uint8_t)str_literal[i]))
        {
            return false;
        }
    }
    return true;
}

// 匹配命令，支持形如 `*CMD:SUB_CMD` 的命令格式
static bool matchCommand(const command_token_t *t0, const command_token_t *t1, uint8_t num_tokens_total, const char *cmd_full_str)
{
    uint8_t colon_idx = 0;
    uint8_t i;
    bool has_colon_in_full_str = false;
    uint8_t literal_len_full_str;
    const char *prefix_str_literal;
    const char *suffix_str_literal;

    literal_len_full_str = 0;
    while (cmd_full_str[literal_len_full_str] != '\0')
    {
        literal_len_full_str++;
    }

    // 查找完整命令字符串中的冒号位置
    for (i = 0; i < literal_len_full_str; ++i)
    {
        if (cmd_full_str[i] == ':')
        {
            colon_idx = i;
            has_colon_in_full_str = true;
            break;
        }
    }

    if (!has_colon_in_full_str) // 如果命令字符串不包含冒号 (如 "*RST")
    {
        // 直接比较第一个Token与完整命令字符串 (支持缩写)
        return (num_tokens_total >= 1) && compareTokens(t0, cmd_full_str, CountMandatory(cmd_full_str));
    }

    // 如果命令字符串包含冒号 (如 "*SET:DATE")
    prefix_str_literal = cmd_full_str; // 冒号前的部分
    suffix_str_literal = cmd_full_str + colon_idx + 1; // 冒号后的部分

    // 首先尝试匹配整个命令作为一个Token (如 "*SET:DATE", 支持缩写)
    if (compareTokens(t0, cmd_full_str, CountMandatory(cmd_full_str)))
    {
        return true;
    }

    // 如果不匹配整个命令，则尝试匹配为两个Token (如 "*SET" 和 ":DATE" 或 "DATE")
    if (num_tokens_total >= 2 && t1 != NULL)
    {
        // 比较第一个Token与前缀部分 (支持缩写)
        if (!compareTokens(t0, cmd_full_str, CountMandatory(prefix_str_literal) + 1U /* colon */))
        {
            return false;
        }

        // 比较第二个Token与后缀部分 (支持缩写)
        if (t1->token_len > 0 && t1->token_str[0] == ':') // 如果第二个Token以冒号开头 (如 ":DATE")
        {
            return compareTokens_modified_for_colon_prefix(t1->token_str + 1, t1->token_len - 1, suffix_str_literal, CountMandatory(suffix_str_literal));
        }
        else // 如果第二个Token不以冒号开头 (如 "DATE")
        {
            return compareTokens(t1, suffix_str_literal, CountMandatory(suffix_str_literal));
        }
    }

    return false;
}

// 解析UART输入字符串为命令Token

// 解析UART输入字符串为命令Token
static void ParseUartInput(void)
{
    uint8_t i = 0;
    uint8_t current_token_len = 0;
    bool in_token = false; // 是否在Token中

    g.uart.num_tokens = 0; // 重置Token数量

    // 清空之前解析的Token
    for (i = 0; i < MAX_COMMAND_TOKENS; ++i)
    {
        g.uart.tokens[i].token_len = 0;
        memset(g.uart.tokens[i].token_str, 0, MAX_TOKEN_LENGTH);
    }

    i = 0;

    // 跳过开头的空格/Tab
    while (i < g.uart.rx_len && (g.uart.rx_buf[i] == ' ' || g.uart.rx_buf[i] == '\t'))
    {
        i++;
    }

    // 遍历接收缓冲区，解析Token (遇到 null 则停止，仅处理第一条命令)
    while (i < g.uart.rx_len && g.uart.rx_buf[i] != '\0' && g.uart.num_tokens < MAX_COMMAND_TOKENS)
    {
        if (g.uart.rx_buf[i] != ' ' && g.uart.rx_buf[i] != '\t') // 如果当前字符不是空格/Tab
        {
            if (!in_token) // 如果不在Token中，开始一个新Token
            {
                current_token_len = 0;
                in_token = true;
            }
            if (current_token_len < MAX_TOKEN_LENGTH - 1) // 检查Token长度是否超出限制
            {
                // 将字符添加到当前Token
                g.uart.tokens[g.uart.num_tokens].token_str[current_token_len] = g.uart.rx_buf[i];
                current_token_len++;
            }
        }
        else // 如果当前字符是空格/Tab
        {
            if (in_token) // 如果在Token中，则当前Token结束
            {
                g.uart.tokens[g.uart.num_tokens].token_len = current_token_len;
                g.uart.tokens[g.uart.num_tokens].token_str[current_token_len] = '\0'; // 字符串结束符
                g.uart.num_tokens++;                                                  // 增加Token数量
                in_token = false;                                                     // 退出Token状态
            }
            // 跳过连续的空格/Tab
            while (i + 1 < g.uart.rx_len && (g.uart.rx_buf[i + 1] == ' ' || g.uart.rx_buf[i + 1] == '\t'))
            {
                i++;
            }
        }
        i++;
    }

    // 处理最后一个Token (如果字符串不是以空格结束)
    if (in_token && g.uart.num_tokens < MAX_COMMAND_TOKENS)
    {
        g.uart.tokens[g.uart.num_tokens].token_len = current_token_len;
        g.uart.tokens[g.uart.num_tokens].token_str[current_token_len] = '\0';
        g.uart.num_tokens++;
    }
}

// 处理UART接收到的命令
void ProcessUartCommand(void)
{
    uint8_t i;
    int parsed_val[6];         // 用于存储解析出的数值参数
    bool parse_ok;             // 解析是否成功
    uint8_t current_param_idx; // 当前参数的起始索引
    uint8_t field_token_idx;   // 字段Token的起始索引
    uint8_t val_token_idx;     // 值Token的起始索引
    bool found_arg;            // 是否找到有效参数

    if (g.uart.cmd_state == 0) // 如果没有新命令
    {
        return;
    }

    g.uart.cmd_state = 0;    // 清除命令状态
    ParseUartInput(); // 解析UART输入

    // 确定参数的起始索引，处理形如 "*CMD:SUB_CMD" 的命令
    if (g.uart.num_tokens >= 2 && g.uart.tokens[0].token_str[0] == '*' && g.uart.tokens[1].token_len > 0 && g.uart.tokens[1].token_str[0] == ':')
    {
        current_param_idx = 2; // 参数从第三个Token开始
    }
    else
    {
        current_param_idx = 1; // 参数从第二个Token开始
    }

    /* 开机动画期间禁止修改状态，仅放行只读命令和系统命令 */
    if (g.disp.init_flag && g.uart.num_tokens > 0)
    {
        /* 检查首 Token 是否为 *GET: 前缀 (允许所有只读查询) */
        bool is_get = (g.uart.tokens[0].token_len >= 5 &&
                       toUpper(g.uart.tokens[0].token_str[0]) == '*' &&
                       toUpper(g.uart.tokens[0].token_str[1]) == 'G' &&
                       toUpper(g.uart.tokens[0].token_str[2]) == 'E' &&
                       toUpper(g.uart.tokens[0].token_str[3]) == 'T' &&
                       (g.uart.tokens[0].token_str[4] == ':' ||
                        (g.uart.num_tokens >= 2 && g.uart.tokens[1].token_len > 0 &&
                         g.uart.tokens[1].token_str[0] == ':')));
        if (!(compareTokens(&g.uart.tokens[0], "*PING", 5) ||
              compareTokens(&g.uart.tokens[0], "HELP", 4) ||
              compareTokens(&g.uart.tokens[0], "INIT", 4) ||
              is_get ||
              matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL),
                           g.uart.num_tokens, "*RST")))
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR BUSY\r\n");
            g.uart.rx_len = 0;
            memset(g.uart.rx_buf, 0, sizeof(g.uart.rx_buf));
            return;
        }
    }

    // 处理 "*RST" 命令 (复位)
    if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*RST"))
    {
        if (g.uart.num_tokens == current_param_idx) // 确保没有额外参数
        {
            ResetProtocolState();
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:DATE" 命令 (设置日期)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:DATE"))
    {
        bool syntax_matched = false; /* 是否有字段组合匹配 (区分 PARAM / RANGE) */
        parse_ok = false;
        field_token_idx = current_param_idx; // 字段Token的起始索引

        // 匹配 "YEAR MONTH DATE YYYY MM DD" 格式
        if (g.uart.num_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "YEAR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MONTH", 5) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 2], "DATE", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 3;                                                        // 值Token的起始索引
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);                       // 年
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str);                   // 月
            parsed_val[2] = atoi((char *)g.uart.tokens[val_token_idx + 2].token_str);                   // 日
            if (is_valid_date((uint16_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2]))
            {
                g.clock.year = (uint16_t)parsed_val[0];
                g.clock.month = (uint8_t)parsed_val[1];
                g.clock.day = (uint8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "YEAR MONTH YYYY MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "YEAR", 3) &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MONTH", 5))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 年
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 月
            if (parsed_val[0] >= 2000 && parsed_val[0] <= 2099 && parsed_val[1] >= 1 && parsed_val[1] <= 12)
            {
                g.clock.year = (uint16_t)parsed_val[0];
                g.clock.month = (uint8_t)parsed_val[1];
                if (!is_valid_date(g.clock.year, g.clock.month, g.clock.day)) // 如果新年月导致日期无效，调整日期
                {
                    g.clock.day = (uint8_t)(is_leap_year(g.clock.year) && g.clock.month == 2 ? 29 : g.rtc.days_in_month[g.clock.month]);
                }
                parse_ok = true;
            }
        }

        // 匹配 "YEAR DATE YYYY DD" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "YEAR", 3) &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "DATE", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 年
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 日
            if (is_valid_date((uint16_t)parsed_val[0], g.clock.month, (uint8_t)parsed_val[1]))
            {
                g.clock.year = (uint16_t)parsed_val[0];
                g.clock.day = (uint8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MONTH DATE MM DD" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "MONTH", 3) &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "DATE", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 月
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 日
            if (is_valid_date(g.clock.year, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1]))
            {
                g.clock.month = (uint8_t)parsed_val[0];
                g.clock.day = (uint8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "YEAR YYYY" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "YEAR", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str); // 年
            if (parsed_val[0] >= 2000 && parsed_val[0] <= 2099)
            {
                g.clock.year = (uint16_t)parsed_val[0];
                if (!is_valid_date(g.clock.year, g.clock.month, g.clock.day)) // 如果新年份导致日期无效，调整日期
                {
                    if (g.clock.month == 2)
                    {
                        g.clock.day = (uint8_t)(is_leap_year(g.clock.year) ? 29 : 28);
                    }
                }
                parse_ok = true;
            }
        }

        // 匹配 "MONTH MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "MONTH", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str); // 月
            if (parsed_val[0] >= 1 && parsed_val[0] <= 12)
            {
                g.clock.month = (uint8_t)parsed_val[0];
                if (!is_valid_date(g.clock.year, g.clock.month, g.clock.day)) // 如果新月份导致日期无效，调整日期
                {
                    g.clock.day = g.rtc.days_in_month[g.clock.month];
                    if (g.clock.month == 2 && is_leap_year(g.clock.year))
                    {
                        g.clock.day = 29;
                    }
                }
                parse_ok = true;
            }
        }

        // 匹配 "DATE DD" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "DATE", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str); // 日
            if (is_valid_date(g.clock.year, g.clock.month, (uint8_t)parsed_val[0]))
            {
                g.clock.day = (uint8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，保存原始值并发送成功消息
        {
            uint8_t v[11];
            g.clock.original_year = g.clock.year;
            g.clock.original_month = g.clock.month;
            g.clock.original_day = g.clock.day;
            g.clock.unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            v[0] = (uint8_t)(g.clock.year / 1000 % 10) + '0';
            v[1] = (uint8_t)(g.clock.year / 100 % 10) + '0';
            v[2] = (uint8_t)(g.clock.year / 10 % 10) + '0';
            v[3] = (uint8_t)(g.clock.year % 10) + '0';
            v[4] = '.'; v[5] = (uint8_t)(g.clock.month / 10) + '0';
            v[6] = (uint8_t)(g.clock.month % 10) + '0';
            v[7] = '.'; v[8] = (uint8_t)(g.clock.day / 10) + '0';
            v[9] = (uint8_t)(g.clock.day % 10) + '0';
            v[10] = '\0';
            Display_SendEditEvent("DATE", v);
            UpdateTimeAndDisplayBuffers();
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE,
                syntax_matched ? (uint8_t *)"ERROR RANGE\r\n"
                               : (uint8_t *)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:TIME" 命令 (设置时间)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:TIME"))
    {
        bool syntax_matched = false; /* 是否有字段组合匹配 */
        parse_ok = false;
        field_token_idx = current_param_idx;

        // 匹配 "HOUR MINUTE SECOND HH MM SS" 格式
        if (g.uart.num_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MINUTE", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 2], "SECOND", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 3;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 分
            parsed_val[2] = atoi((char *)g.uart.tokens[val_token_idx + 2].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2]))
            {
                g.clock.hh = (int8_t)parsed_val[0];
                g.clock.mm = (int8_t)parsed_val[1];
                g.clock.ss = (int8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR MINUTE HH MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MINUTE", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 分
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], g.clock.ss))
            {
                g.clock.hh = (int8_t)parsed_val[0];
                g.clock.mm = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR SECOND HH SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "SECOND", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], g.clock.mm, (uint8_t)parsed_val[1]))
            {
                g.clock.hh = (int8_t)parsed_val[0];
                g.clock.ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE SECOND MM SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "MINUTE", 3) &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "SECOND", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 分
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time(g.clock.hh, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1]))
            {
                g.clock.mm = (int8_t)parsed_val[0];
                g.clock.ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR HH" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str); // 时
            if (parsed_val[0] >= 0 && parsed_val[0] < 24)
            {
                g.clock.hh = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "MINUTE", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str); // 分
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                g.clock.mm = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "SECOND SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "SECOND", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str); // 秒
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                g.clock.ss = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，保存原始值并发送成功消息
        {
            uint8_t v[9];
            g.clock.original_hh = g.clock.hh;
            g.clock.original_mm = g.clock.mm;
            g.clock.original_ss = g.clock.ss;
            g.clock.unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            v[0] = (uint8_t)(g.clock.hh / 10) + '0';
            v[1] = (uint8_t)(g.clock.hh % 10) + '0';
            v[2] = '.'; v[3] = (uint8_t)(g.clock.mm / 10) + '0';
            v[4] = (uint8_t)(g.clock.mm % 10) + '0';
            v[5] = '.'; v[6] = (uint8_t)(g.clock.ss / 10) + '0';
            v[7] = (uint8_t)(g.clock.ss % 10) + '0';
            v[8] = '\0';
            Display_SendEditEvent("TIME", v);
            UpdateTimeAndDisplayBuffers();
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE,
                syntax_matched ? (uint8_t *)"ERROR RANGE\r\n"
                               : (uint8_t *)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:ALARM" 命令 (设置闹钟)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:ALARM"))
    {
        bool syntax_matched = false; /* 是否有字段组合匹配 */
        parse_ok = false;
        field_token_idx = current_param_idx;

        /* *SET:ALARM OFF — 关闭闹钟 (设 alm_hh=25 为哨兵值) */
        if (g.uart.num_tokens == field_token_idx + 1 &&
            compareTokens(&g.uart.tokens[field_token_idx], "OFF", 3))
        {
            g.clock.alm_hh = 25;
            g.clock.alm_mm = 0;
            g.clock.alm_ss = 0;
            syntax_matched = true;
            parse_ok = true;
        }

        // 匹配 "HOUR MINUTE SECOND HH MM SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MINUTE", 3) &&
            compareFieldKeyword(&g.uart.tokens[field_token_idx + 2], "SECOND", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 3;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 分
            parsed_val[2] = atoi((char *)g.uart.tokens[val_token_idx + 2].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2]))
            {
                g.clock.alm_hh = (int8_t)parsed_val[0];
                g.clock.alm_mm = (int8_t)parsed_val[1];
                g.clock.alm_ss = (int8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR MINUTE HH MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "MINUTE", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 分
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], g.clock.alm_ss))
            {
                g.clock.alm_hh = (int8_t)parsed_val[0];
                g.clock.alm_mm = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR SECOND HH SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3) &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "SECOND", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], g.clock.alm_mm, (uint8_t)parsed_val[1]))
            {
                g.clock.alm_hh = (int8_t)parsed_val[0];
                g.clock.alm_ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE SECOND MM SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "MINUTE", 3) &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx + 1], "SECOND", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str);     // 分
            parsed_val[1] = atoi((char *)g.uart.tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time(g.clock.alm_hh, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1]))
            {
                g.clock.alm_mm = (int8_t)parsed_val[0];
                g.clock.alm_ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR HH" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "HOUR", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str); // 时
            if (parsed_val[0] >= 0 && parsed_val[0] < 24)
            {
                g.clock.alm_hh = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE MM" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "MINUTE", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str); // 分
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                g.clock.alm_mm = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "SECOND SS" 格式
        else if (g.uart.num_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&g.uart.tokens[field_token_idx], "SECOND", 3))
        {
            syntax_matched = true;
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)g.uart.tokens[val_token_idx].token_str); // 秒
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                g.clock.alm_ss = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，停止闹钟，保存原始值并发送成功消息
        {
            StopAlarmRinging(false);
            g.clock.original_alm_hh = g.clock.alm_hh;
            g.clock.original_alm_mm = g.clock.alm_mm;
            g.clock.original_alm_ss = g.clock.alm_ss;
            g.clock.unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            if (g.clock.alm_hh == 25) /* 闹钟已关闭 */
            {
                Display_SendEditEvent("ALARM", (const uint8_t *)"xx:xx:xx");
            }
            else
            {
                uint8_t v[9];
                v[0] = (uint8_t)(g.clock.alm_hh / 10) + '0';
                v[1] = (uint8_t)(g.clock.alm_hh % 10) + '0';
                v[2] = '.'; v[3] = (uint8_t)(g.clock.alm_mm / 10) + '0';
                v[4] = (uint8_t)(g.clock.alm_mm % 10) + '0';
                v[5] = '.'; v[6] = (uint8_t)(g.clock.alm_ss / 10) + '0';
                v[7] = (uint8_t)(g.clock.alm_ss % 10) + '0';
                v[8] = '\0';
                Display_SendEditEvent("ALARM", v);
            }
            UpdateTimeAndDisplayBuffers();
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE,
                syntax_matched ? (uint8_t *)"ERROR RANGE\r\n"
                               : (uint8_t *)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:DISPLAY" 命令 (设置显示开关)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:DISPLAY"))
    {
        if (g.uart.num_tokens == current_param_idx + 1) // 确保参数数量正确
        {
            if (compareTokens(&g.uart.tokens[current_param_idx], "ON", 2)) // "ON"
            {
                g.disp.shifting = true;                 // 开启流动
                g.disp.on = true; // 开启数码管显示
                g.disp.msg_active = false;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
                Display_SendEvent();
                g.clock.unsaved_changes_active = false;
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "OFF", 3)) // "OFF"
            {
                g.disp.shifting = false;                 // 停止流动
                g.disp.on = false; // 关闭数码管显示
                g.disp.msg_active = false;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
                Display_SendEvent();
                g.clock.unsaved_changes_active = false;
            }
            else // 无效参数
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR PARAM\r\n");
            }
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:FORMAT" 命令 (设置显示格式)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:FORMAT"))
    {
        if (g.uart.num_tokens == current_param_idx + 1) // 确保参数数量正确
        {
            if (compareTokens(&g.uart.tokens[current_param_idx], "LEFT", 4)) // "LEFT"
            {
                g.disp.shift_mode = false;
                g.disp.reversed = false;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
                Display_SendEvent();
                g.clock.unsaved_changes_active = false;
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "RIGHT", 5)) // "RIGHT"
            {
                g.disp.shift_mode = true;
                g.disp.reversed = true;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
                Display_SendEvent();
                g.clock.unsaved_changes_active = false;
            }
            else
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR PARAM\r\n");
            }
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:MSG" 命令 (临时消息显示)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:MSG"))
    {
        uint8_t payload_offset;
        uint8_t payload_len;

        if (g.uart.num_tokens > current_param_idx)
        {
            payload_offset = FindRawPayloadOffset(current_param_idx);
            /* 使用 strlen 限制到第一个 null (支持多命令缓冲区中只取当前命令) */
            payload_len = (uint8_t)strlen((char *)&g.uart.rx_buf[payload_offset]);
            /* 修剪尾部 \r \n (防御性: 裸串口终端可能发送 \r\n) */
            while (payload_len > 0 && (g.uart.rx_buf[payload_offset + payload_len - 1U] == '\r' || g.uart.rx_buf[payload_offset + payload_len - 1U] == '\n'))
                payload_len--;
            if (payload_len > 32)
                payload_len = 32;

            Display_StartMessage(&g.uart.rx_buf[payload_offset], payload_len);
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:LED" 命令 (LED接管)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:LED"))
    {
        uint8_t led_value;

        if (g.uart.num_tokens == current_param_idx + 1 && ParseHexByte(&g.uart.tokens[current_param_idx], &led_value))
        {
            if (led_value == 0x00)
            {
                g.disp.led_takeover = false;
                g.disp.led_pattern = 0x00;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else
            {
                g.disp.led_takeover = true;
                g.disp.led_pattern = led_value;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            Display_UpdateStatusLeds();
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:WEATHER" 命令 (天气 LED5-LED7 指示, 不进入 LED 接管)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:WEATHER"))
    {
        uint8_t weather_value;

        if (g.uart.num_tokens == current_param_idx + 1 && ParseHexByte(&g.uart.tokens[current_param_idx], &weather_value))
        {
            g.disp.weather_code = (uint8_t)(weather_value & 0xE0); /* 仅保留 LED5-7 */
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            Display_UpdateStatusLeds();
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR PARAM\r\n");
        }
    }

    // 处理 "*SET:BEEP" 命令 (远程蜂鸣, 10-5000 ms)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:BEEP"))
    {
        if (g.uart.num_tokens == current_param_idx + 1)
        {
            int ms = atoi((char *)g.uart.tokens[current_param_idx].token_str);
            if (ms >= 10 && ms <= 5000)
            {
                PWMStart(2400);  /* 2400 Hz 蜂鸣 */
                g.disp.beep_until = g.timer.tick + (uint32_t)ms;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR RANGE\r\n");
            }
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:MODE" 命令 (夜间模式)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:MODE"))
    {
        if (g.uart.num_tokens == current_param_idx + 1)
        {
            if (compareTokens(&g.uart.tokens[current_param_idx], "NIGHT", 5))
            {
                g.disp.night_mode = true;
                g.disp.on = true;
                g.disp.msg_active = false;
                PWMStop();
                if (g.disp.alarm_ring_start != 0)
                    g.disp.alarm_ringing = true;
                Display_SendModeEvent("NIGHT");
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
                Display_SendEvent();
                Display_UpdateStatusLeds();
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "DAY", 3))
            {
                g.disp.night_mode = false;
                Display_SendModeEvent("DAY");
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
                Display_SendEvent();
                Display_UpdateStatusLeds();
            }
            else
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR PARAM\r\n");
            }
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*SET:KEY" 命令 (虚拟按键注入, 不回报 *EVT:KEY 避免环回)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*SET:KEY"))
    {
        if (g.uart.num_tokens == current_param_idx + 1)
        {
            if (compareTokens(&g.uart.tokens[current_param_idx], "FUNC", 4))
            {
                g.in.suppress_key_events = true;
                g.in.short_evt[0] = true;              // K1
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "SHIFT", 5))
            {
                g.in.suppress_key_events = true;
                g.in.short_evt[1] = true;              // K2
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "ADD", 3))
            {
                g.in.suppress_key_events = true;
                g.in.short_evt[2] = true;              // K3
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "SAVE", 4))
            {
                g.in.suppress_key_events = true;
                g.in.short_evt[3] = true;              // K4
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "DISP", 4))
            {
                g.in.suppress_key_events = true;
                g.in.short_evt[4] = true;              // K5
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "SPEED", 5))
            {
                g.in.suppress_key_events = true;
                g.in.short_evt[5] = true;              // K6
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "FORMAT", 6))
            {
                g.in.suppress_key_events = true;
                g.in.short_evt[6] = true;              // K7
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "EXT", 3))
            {
                g.in.suppress_key_events = true;
                g.in.short_evt[7] = true;              // K8
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "USER1", 5))
            {
                /* 不设置 suppress_key_events: PC 端需要 *EVT:KEY USER1 来触发 NTP 对时 */
                g.in.user_short_evt[0] = true;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else if (compareTokens(&g.uart.tokens[current_param_idx], "USER2", 5))
            {
                /* 不设置 suppress_key_events: PC 端需要 *EVT:KEY USER2 来触发天气刷新 */
                g.in.user_short_evt[1] = true;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            }
            else
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR PARAM\r\n");
            }
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*NTP SYNC" 命令：PC 已成功获取标准时间并完成下发
    else if (g.uart.num_tokens >= 1 && compareTokens(&g.uart.tokens[0], "*NTP", 4))
    {
        if (g.uart.num_tokens == 2 && compareTokens(&g.uart.tokens[1], "SYNC", 4))
        {
            g.disp.ntp_synced = true;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
            Display_UpdateStatusLeds();
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*GET:DATE" 命令 (获取日期信息)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*GET:DATE"))
    {
        field_token_idx = current_param_idx;
        found_arg = false;

        if (g.uart.num_tokens == field_token_idx) // 如果没有指定字段，则返回完整日期
        {
            PutProtocolBuffer(g.disp.date_buf, 10);
            found_arg = true;
        }
        else // 根据指定字段返回信息
        {
            /* 预扫描确认至少有一个合法字段再发 OK 前缀 */
            found_arg = false;
            for (i = field_token_idx; i < g.uart.num_tokens; ++i)
            {
                if (compareFieldKeyword(&g.uart.tokens[i], "YEAR", 3) ||
                    compareFieldKeyword(&g.uart.tokens[i], "MONTH", 3) ||
                    compareFieldKeyword(&g.uart.tokens[i], "DATE", 3))
                {
                    found_arg = true;
                    break;
                }
            }
            if (!found_arg)
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR PARAM\r\n");
            }
            else
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK ");
                for (i = field_token_idx; i < g.uart.num_tokens; ++i)
                {
                    if (compareFieldKeyword(&g.uart.tokens[i], "YEAR", 3)) // "YEAR"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)((g.clock.year / 1000) % 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)((g.clock.year / 100) % 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)((g.clock.year / 10) % 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.year % 10) + '0');
                    }
                    else if (compareFieldKeyword(&g.uart.tokens[i], "MONTH", 3)) // "MONTH"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.month / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.month % 10) + '0');
                    }
                    else if (compareFieldKeyword(&g.uart.tokens[i], "DATE", 3)) // "DATE" (日)
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.day / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.day % 10) + '0');
                    }
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
            }
        }
    }

    // 处理 "*GET:TIME" 命令 (获取时间信息)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*GET:TIME"))
    {
        field_token_idx = current_param_idx;
        found_arg = false;


        if (g.uart.num_tokens == field_token_idx) // 如果没有指定字段，则返回完整时间
        {
            PutProtocolBuffer(g.disp.time_buf, 8);
            found_arg = true;
        }
        else // 根据指定字段返回信息
        {
            /* 预扫描确认至少有一个合法字段再发 OK 前缀 */
            found_arg = false;
            for (i = field_token_idx; i < g.uart.num_tokens; ++i)
            {
                if (compareFieldKeyword(&g.uart.tokens[i], "HOUR", 3) ||
                    compareFieldKeyword(&g.uart.tokens[i], "MINUTE", 3) ||
                    compareFieldKeyword(&g.uart.tokens[i], "SECOND", 3))
                {
                    found_arg = true;
                    break;
                }
            }
            if (!found_arg)
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR PARAM\r\n");
            }
            else
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK ");
                for (i = field_token_idx; i < g.uart.num_tokens; ++i)
                {
                    if (compareFieldKeyword(&g.uart.tokens[i], "HOUR", 3)) // "HOUR"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.hh / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.hh % 10) + '0');
                    }
                    else if (compareFieldKeyword(&g.uart.tokens[i], "MINUTE", 3)) // "MINUTE"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.mm / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.mm % 10) + '0');
                    }
                    else if (compareFieldKeyword(&g.uart.tokens[i], "SECOND", 3)) // "SECOND"
                    {
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.ss / 10) + '0');
                        UARTCharPutBlocking(UART0_BASE, (uint8_t)(g.clock.ss % 10) + '0');
                    }
                }
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
            }
        }
    }

    // 处理 "*GET:ALARM" 命令 (获取闹钟时间)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*GET:ALARM"))
    {
        if (g.uart.num_tokens == current_param_idx) // 确保没有额外参数
        {
            PutProtocolBuffer(g.disp.alarm_buf, 8);
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*GET:FORMAT" 命令 (获取显示格式)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*GET:FORMAT"))
    {
        if (g.uart.num_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, g.disp.shift_mode ? (uint8_t *)"OK RIGHT\r\n" : (uint8_t *)"OK LEFT\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*GET:DISPLAY" 命令 (获取显示状态)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*GET:DISPLAY"))
    {
        if (g.uart.num_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, g.disp.on ? (uint8_t *)"OK ON\r\n" : (uint8_t *)"OK OFF\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*MOTOR:START" 命令 (启动步进电机)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*MOTOR:START"))
    {
        if (g.uart.num_tokens == current_param_idx)
        {
            g.motor.running = 1;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*MOTOR:STOP" 命令 (停止步进电机)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*MOTOR:STOP"))
    {
        if (g.uart.num_tokens == current_param_idx)
        {
            g.motor.running = 0;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*MOTOR:FWD" 命令 (设置步进电机正转)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*MOTOR:FWD"))
    {
        if (g.uart.num_tokens == current_param_idx)
        {
            g.motor.direction = 0;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*MOTOR:REV" 命令 (设置步进电机反转)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*MOTOR:REV"))
    {
        if (g.uart.num_tokens == current_param_idx)
        {
            g.motor.direction = 1;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "*GET:MOTOR" 命令 (获取步进电机状态)
    else if (matchCommand(&g.uart.tokens[0], (g.uart.num_tokens > 1 ? &g.uart.tokens[1] : NULL), g.uart.num_tokens, "*GET:MOTOR"))
    {
        if (g.uart.num_tokens == current_param_idx)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK ");
            UARTStringPutNOBlocking(UART0_BASE, g.motor.running ? (uint8_t *)"1" : (uint8_t *)"0");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
            UARTStringPutNOBlocking(UART0_BASE, g.motor.direction ? (uint8_t *)"1" : (uint8_t *)"0");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    // 处理 "INIT" 命令 (复位)
    else if (compareTokens(&g.uart.tokens[0], "INIT", 4))
    {
        if (g.uart.num_tokens == current_param_idx)
            SysCtlReset(); // 系统复位
        else
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
    }

    // 处理 "*PING" 命令 (连接保活)
    else if (compareTokens(&g.uart.tokens[0], "*PING", 5))
    {
        if (g.uart.num_tokens == current_param_idx)
        {
            char pong_buf[24];
            uint32_t uptime_s = g.timer.tick / 1000U;
            uint8_t len = 0;
            uint8_t ij;

            /* 手动构建 "*PONG <uptime_s>\r\n" 避免依赖 sprintf */
            pong_buf[0] = '*'; pong_buf[1] = 'P'; pong_buf[2] = 'O'; pong_buf[3] = 'N';
            pong_buf[4] = 'G'; pong_buf[5] = ' ';
            len = 6;

            /* 将 uptime_s 转为 ASCII (简易 itoa) */
            {
                char num_buf[12];
                uint8_t num_len = 0;
                uint32_t val = uptime_s;
                if (val == 0)
                {
                    num_buf[num_len++] = '0';
                }
                else
                {
                    while (val > 0 && num_len < 11)
                    {
                        num_buf[num_len++] = (char)('0' + (val % 10U));
                        val /= 10U;
                    }
                }
                /* 逆序写入 */
                for (ij = num_len; ij > 0; --ij)
                    pong_buf[len++] = num_buf[ij - 1U];
            }

            pong_buf[len++] = '\r';
            pong_buf[len++] = '\n';
            pong_buf[len] = '\0';

            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)pong_buf);

        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }
    // 处理 "HELP" 命令 (显示帮助)
    else if (compareTokens(&g.uart.tokens[0], "HELP", 4))
    {
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OK\r\n");
        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)
            "*RST                                       : Reset protocol state.\r\n"
            "*SET:DATE YEAR MONTH DATE YYYY MM DD      : Set date (partial fields supported).\r\n"
            "*SET:TIME HOUR MINUTE SECOND HH MM SS     : Set time (partial fields supported).\r\n"
            "*SET:ALARM HOUR MINUTE SECOND HH MM SS    : Set alarm; OFF to disable.\r\n"
            "*SET:DISPLAY ON/OFF                       : Turn 7-segment display on/off.\r\n"
            "*SET:FORMAT LEFT/RIGHT                    : Set display direction.\r\n"
            "*SET:MSG <text>                           : Show temporary message (max 32 bytes).\r\n"
            "*SET:LED <hex2>                           : LED takeover; 00 restores default.\r\n"
            "*SET:WEATHER <hex2>                        : Weather LED5-7 indicator (no takeover).\r\n"
            "*SET:BEEP <ms>                            : Remote beep 10-5000 ms at 2400 Hz.\r\n"
            "*SET:MODE NIGHT/DAY                       : Night/day mode.\r\n"
            "*SET:KEY FUNC/SHIFT/ADD/SAVE/DISP/SPEED/FORMAT/EXT/USER1/USER2 : Virtual key injection.\r\n"
            "*NTP SYNC                                 : Mark NTP sync complete; LED4 on.\r\n"
            "*GET:DATE [YEAR] [MONTH] [DATE]           : Get date.\r\n"
            "*GET:TIME [HOUR] [MINUTE] [SECOND]        : Get time.\r\n"
            "*GET:ALARM                                : Get alarm time.\r\n"
            "*GET:DISPLAY                              : Get display state.\r\n"
            "*GET:FORMAT                               : Get display format.\r\n"
            "*MOTOR:START/STOP/FWD/REV                 : Stepper motor control.\r\n"
            "*GET:MOTOR                                : Get motor status.\r\n"
            "*PING                                     : Responds *PONG <uptime_s>.\r\n"
            "INIT                                      : Software system reset.\r\n"
            "HELP                                      : Show this help.\r\n");
    }
    else // 未知命令
    {
        if (g.uart.num_tokens > 0)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ERROR SYNTAX\r\n");
        }
    }

    /* 检查缓冲区中是否还有下一条命令 (紧接在 null 之后，由 ISR 连续接收) */
    {
        uint8_t pos = 0;
        while (pos < g.uart.rx_len && g.uart.rx_buf[pos] != '\0')
            pos++;
        if (pos + 1U < g.uart.rx_len) /* 第一条命令的 null 之后还有数据 */
        {
            uint8_t tail = (uint8_t)(g.uart.rx_len - pos - 1U);
            memmove(g.uart.rx_buf, &g.uart.rx_buf[pos + 1U], tail);
            g.uart.rx_len = tail;
            g.uart.cmd_state = 1; /* 下一条命令待处理 */
            memset(&g.uart.rx_buf[tail], 0, sizeof(g.uart.rx_buf) - tail);
        }
        else
        {
            g.uart.rx_len = 0;
            memset(g.uart.rx_buf, 0, sizeof(g.uart.rx_buf));
        }
    }
}
