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

// 比较命令Token与字符串，支持最小匹配长度

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

// 匹配命令，支持形如 `*CMD:SUB_CMD` 的命令格式
static bool matchCommand(const command_token_t *t0, const command_token_t *t1, uint8_t num_tokens_total, const char *cmd_full_str)
{
    uint8_t colon_idx = 0;
    uint8_t i;
    bool has_colon_in_full_str = false;
    uint8_t literal_len_full_str;
    const char *prefix_str_literal;
    uint8_t prefix_len_literal;
    const char *suffix_str_literal;
    uint8_t suffix_len_literal;

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
        // 直接比较第一个Token与完整命令字符串
        return (num_tokens_total >= 1) && compareTokens(t0, cmd_full_str, literal_len_full_str);
    }

    // 如果命令字符串包含冒号 (如 "*SET:DATE")
    prefix_str_literal = cmd_full_str; // 冒号前的部分
    prefix_len_literal = colon_idx;
    suffix_str_literal = cmd_full_str + colon_idx + 1; // 冒号后的部分
    suffix_len_literal = literal_len_full_str - (colon_idx + 1);

    // 首先尝试匹配整个命令作为一个Token (如 "*SET:DATE")
    if (compareTokens(t0, cmd_full_str, literal_len_full_str))
    {
        return true;
    }

    // 如果不匹配整个命令，则尝试匹配为两个Token (如 "*SET" 和 ":DATE" 或 "DATE")
    if (num_tokens_total >= 2 && t1 != NULL)
    {
        // 比较第一个Token与前缀部分
        if (!compareTokens(t0, prefix_str_literal, prefix_len_literal))
        {
            return false;
        }

        // 比较第二个Token与后缀部分
        if (t1->token_len > 0 && t1->token_str[0] == ':') // 如果第二个Token以冒号开头 (如 ":DATE")
        {
            return compareTokens_modified_for_colon_prefix(t1->token_str + 1, t1->token_len - 1, suffix_str_literal, suffix_len_literal);
        }
        else // 如果第二个Token不以冒号开头 (如 "DATE")
        {
            return compareTokens(t1, suffix_str_literal, suffix_len_literal);
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

    num_parsed_tokens = 0; // 重置Token数量

    // 清空之前解析的Token
    for (i = 0; i < MAX_COMMAND_TOKENS; ++i)
    {
        parsed_tokens[i].token_len = 0;
        memset(parsed_tokens[i].token_str, 0, MAX_TOKEN_LENGTH);
    }

    i = 0;

    // 跳过开头的空格
    while (i < uart_receive_len && uart_receive_buffer[i] == ' ')
    {
        i++;
    }

    // 遍历接收缓冲区，解析Token
    while (i < uart_receive_len && num_parsed_tokens < MAX_COMMAND_TOKENS)
    {
        if (uart_receive_buffer[i] != ' ') // 如果当前字符不是空格
        {
            if (!in_token) // 如果不在Token中，开始一个新Token
            {
                current_token_len = 0;
                in_token = true;
            }
            if (current_token_len < MAX_TOKEN_LENGTH - 1) // 检查Token长度是否超出限制
            {
                // 将字符添加到当前Token
                parsed_tokens[num_parsed_tokens].token_str[current_token_len] = uart_receive_buffer[i];
                current_token_len++;
            }
        }
        else // 如果当前字符是空格
        {
            if (in_token) // 如果在Token中，则当前Token结束
            {
                parsed_tokens[num_parsed_tokens].token_len = current_token_len;
                parsed_tokens[num_parsed_tokens].token_str[current_token_len] = '\0'; // 字符串结束符
                num_parsed_tokens++;                                                  // 增加Token数量
                in_token = false;                                                     // 退出Token状态
            }
            // 跳过连续的空格
            while (i + 1 < uart_receive_len && uart_receive_buffer[i + 1] == ' ')
            {
                i++;
            }
        }
        i++;
    }

    // 处理最后一个Token (如果字符串不是以空格结束)
    if (in_token && num_parsed_tokens < MAX_COMMAND_TOKENS)
    {
        parsed_tokens[num_parsed_tokens].token_len = current_token_len;
        parsed_tokens[num_parsed_tokens].token_str[current_token_len] = '\0';
        num_parsed_tokens++;
    }
}

// 处理UART接收到的命令

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

    if (cmd_state == 0) // 如果没有新命令
    {
        return;
    }

    cmd_state = 0;    // 清除命令状态
    ParseUartInput(); // 解析UART输入

    // 确定参数的起始索引，处理形如 "*CMD:SUB_CMD" 的命令
    if (num_parsed_tokens >= 2 && parsed_tokens[0].token_str[0] == '*' && parsed_tokens[1].token_len > 0 && parsed_tokens[1].token_str[0] == ':')
    {
        current_param_idx = 2; // 参数从第三个Token开始
    }
    else
    {
        current_param_idx = 1; // 参数从第二个Token开始
    }

    // 处理 "*RST" 命令 (复位)
    if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*RST"))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            SysCtlReset(); // 系统复位
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"System reset initiated.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *RST\r\n");
        }
    }

    // 处理 "*SET:DATE" 命令 (设置日期)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:DATE"))
    {
        parse_ok = false;
        field_token_idx = current_param_idx; // 字段Token的起始索引

        // 匹配 "YEAR MONTH DATE YYYY MM DD" 格式
        if (num_parsed_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&parsed_tokens[field_token_idx], "YEAR", 4) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MONTH", 5) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 2], "DATE", 3))
        {
            val_token_idx = field_token_idx + 3;                                                        // 值Token的起始索引
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);                       // 年
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str);                   // 月
            parsed_val[2] = atoi((char *)parsed_tokens[val_token_idx + 2].token_str);                   // 日
            if (is_valid_date((uint16_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2])) // 检查日期有效性
            {
                year = (uint16_t)parsed_val[0];
                month = (uint8_t)parsed_val[1];
                day = (uint8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "YEAR MONTH YYYY MM" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "YEAR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MONTH", 5))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 年
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 月
            if (parsed_val[0] >= 2000 && parsed_val[0] <= 2099 && parsed_val[1] >= 1 && parsed_val[1] <= 12)
            {
                year = (uint16_t)parsed_val[0];
                month = (uint8_t)parsed_val[1];
                if (!is_valid_date(year, month, day)) // 如果新年月导致日期无效，调整日期
                {
                    day = (uint8_t)(is_leap_year(year) && month == 2 ? 29 : days_in_month[month]);
                }
                parse_ok = true;
            }
        }

        // 匹配 "YEAR DATE YYYY DD" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "YEAR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "DATE", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 年
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 日
            if (is_valid_date((uint16_t)parsed_val[0], month, (uint8_t)parsed_val[1]))
            {
                year = (uint16_t)parsed_val[0];
                day = (uint8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MONTH DATE MM DD" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MONTH", 5) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "DATE", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 月
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 日
            if (is_valid_date(year, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1]))
            {
                month = (uint8_t)parsed_val[0];
                day = (uint8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "YEAR YYYY" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "YEAR", 4))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 年
            if (parsed_val[0] >= 2000 && parsed_val[0] <= 2099)
            {
                year = (uint16_t)parsed_val[0];
                if (!is_valid_date(year, month, day)) // 如果新年份导致日期无效，调整日期
                {
                    if (month == 2)
                    {
                        day = (uint8_t)(is_leap_year(year) ? 29 : 28);
                    }
                }
                parse_ok = true;
            }
        }

        // 匹配 "MONTH MM" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MONTH", 5))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 月
            if (parsed_val[0] >= 1 && parsed_val[0] <= 12)
            {
                month = (uint8_t)parsed_val[0];
                if (!is_valid_date(year, month, day)) // 如果新月份导致日期无效，调整日期
                {
                    day = days_in_month[month];
                    if (month == 2 && is_leap_year(year))
                    {
                        day = 29;
                    }
                }
                parse_ok = true;
            }
        }

        // 匹配 "DATE DD" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "DATE", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 日
            if (is_valid_date(year, month, (uint8_t)parsed_val[0]))
            {
                day = (uint8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，保存原始值并发送成功消息
        {
            original_year = year;
            original_month = month;
            original_day = day;
            unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Date set successfully.\r\n");
        }
        else // 解析失败，发送错误消息
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format or date values. Type HELP for usage.\r\n");
        }
    }

    // 处理 "*SET:TIME" 命令 (设置时间)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:TIME"))
    {
        parse_ok = false;
        field_token_idx = current_param_idx;

        // 匹配 "HOUR MINUTE SECOND HH MM SS" 格式
        if (num_parsed_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MINUTE", 3) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 2], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 3;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 分
            parsed_val[2] = atoi((char *)parsed_tokens[val_token_idx + 2].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2]))
            {
                hh = (int8_t)parsed_val[0];
                mm = (int8_t)parsed_val[1];
                ss = (int8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR MINUTE HH MM" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MINUTE", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 分
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], ss))
            {
                hh = (int8_t)parsed_val[0];
                mm = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR SECOND HH SS" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], mm, (uint8_t)parsed_val[1]))
            {
                hh = (int8_t)parsed_val[0];
                ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE SECOND MM SS" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MINUTE", 3) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 分
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time(hh, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1]))
            {
                mm = (int8_t)parsed_val[0];
                ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR HH" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 时
            if (parsed_val[0] >= 0 && parsed_val[0] < 24)
            {
                hh = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE MM" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MINUTE", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 分
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                mm = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "SECOND SS" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 秒
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                ss = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，保存原始值并发送成功消息
        {
            original_hh = hh;
            original_mm = mm;
            original_ss = ss;
            unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Time set successfully.\r\n");
        }
        else // 解析失败，发送错误消息
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format or time values. Type HELP for usage.\r\n");
        }
    }

    // 处理 "*SET:ALARM" 命令 (设置闹钟)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:ALARM"))
    {
        parse_ok = false;
        field_token_idx = current_param_idx;

        // 匹配 "HOUR MINUTE SECOND HH MM SS" 格式
        if (num_parsed_tokens == field_token_idx + 6 &&
            compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MINUTE", 3) &&
            compareFieldKeyword(&parsed_tokens[field_token_idx + 2], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 3;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 分
            parsed_val[2] = atoi((char *)parsed_tokens[val_token_idx + 2].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], (uint8_t)parsed_val[2]))
            {
                alm_hh = (int8_t)parsed_val[0];
                alm_mm = (int8_t)parsed_val[1];
                alm_ss = (int8_t)parsed_val[2];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR MINUTE HH MM" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "MINUTE", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 分
            if (is_valid_time((uint8_t)parsed_val[0], (uint8_t)parsed_val[1], alm_ss))
            {
                alm_hh = (int8_t)parsed_val[0];
                alm_mm = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR SECOND HH SS" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 时
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time((uint8_t)parsed_val[0], alm_mm, (uint8_t)parsed_val[1]))
            {
                alm_hh = (int8_t)parsed_val[0];
                alm_ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE SECOND MM SS" 格式
        else if (num_parsed_tokens == field_token_idx + 4 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MINUTE", 3) &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx + 1], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 2;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str);     // 分
            parsed_val[1] = atoi((char *)parsed_tokens[val_token_idx + 1].token_str); // 秒
            if (is_valid_time(alm_hh, (uint8_t)parsed_val[0], (uint8_t)parsed_val[1]))
            {
                alm_mm = (int8_t)parsed_val[0];
                alm_ss = (int8_t)parsed_val[1];
                parse_ok = true;
            }
        }

        // 匹配 "HOUR HH" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "HOUR", 4))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 时
            if (parsed_val[0] >= 0 && parsed_val[0] < 24)
            {
                alm_hh = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "MINUTE MM" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "MINUTE", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 分
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                alm_mm = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        // 匹配 "SECOND SS" 格式
        else if (num_parsed_tokens == field_token_idx + 2 &&
                 compareFieldKeyword(&parsed_tokens[field_token_idx], "SECOND", 3))
        {
            val_token_idx = field_token_idx + 1;
            parsed_val[0] = atoi((char *)parsed_tokens[val_token_idx].token_str); // 秒
            if (parsed_val[0] >= 0 && parsed_val[0] < 60)
            {
                alm_ss = (int8_t)parsed_val[0];
                parse_ok = true;
            }
        }

        if (parse_ok) // 如果解析成功，停止闹钟，保存原始值并发送成功消息
        {
            PWMStop();
            original_alm_hh = alm_hh;
            original_alm_mm = alm_mm;
            original_alm_ss = alm_ss;
            unsaved_changes_active = false;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Alarm set successfully.\r\n");
        }
        else // 解析失败，发送错误消息
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format or alarm time values. Type HELP for usage.\r\n");
        }
    }

    // 处理 "*SET:DISPLAY" 命令 (设置显示开关)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:DISPLAY"))
    {
        if (num_parsed_tokens == current_param_idx + 1) // 确保参数数量正确
        {
            if (compareTokens(&parsed_tokens[current_param_idx], "ON", 2)) // "ON"
            {
                shifting = true;                 // 开启流动
                seven_segment_display_on = true; // 开启数码管显示
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"7-Segment Display turned ON.\r\n");
                unsaved_changes_active = false;
            }
            else if (compareTokens(&parsed_tokens[current_param_idx], "OFF", 3)) // "OFF"
            {
                shifting = false;                 // 停止流动
                seven_segment_display_on = false; // 关闭数码管显示
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"7-Segment Display turned OFF.\r\n");
                unsaved_changes_active = false;
            }
            else // 无效参数
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid argument. Usage: *SET:DISPLAY ON/OFF\r\n");
            }
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *SET:DISPLAY ON/OFF\r\n");
        }
    }

    // 处理 "*SET:FORMAT" 命令 (设置显示格式)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*SET:FORMAT"))
    {
        if (num_parsed_tokens == current_param_idx + 1) // 确保参数数量正确
        {
            if (compareTokens(&parsed_tokens[current_param_idx], "LEFT", 4)) // "LEFT" (左移，正常顺序)
            {
                shift_mode = false;
                display_reversed_order = false;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Display format set to LEFT flow (normal order).\r\n");

                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Time: ");
                UARTStringPutNOBlocking(UART0_BASE, time_transmit_buffer);
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Date: ");
                UARTStringPutNOBlocking(UART0_BASE, date_transmit_buffer);
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
                unsaved_changes_active = false;
            }
            else if (compareTokens(&parsed_tokens[current_param_idx], "RIGHT", 5)) // "RIGHT" (右移，反向顺序)
            {
                shift_mode = true;
                display_reversed_order = true;
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Display format set to RIGHT flow (reversed order).\r\n");

                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Time: ");
                UARTStringPutReversedNOBlocking(UART0_BASE, time_transmit_buffer); // 反向显示时间
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Date: ");
                UARTStringPutReversedNOBlocking(UART0_BASE, date_transmit_buffer); // 反向显示日期
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
                unsaved_changes_active = false;
            }
            else // 无效参数
            {
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid argument. Usage: *SET:FORMAT LEFT/RIGHT\r\n");
            }
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *SET:FORMAT LEFT/RIGHT\r\n");
        }
    }

    // 处理 "*GET:DATE" 命令 (获取日期信息)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:DATE"))
    {
        field_token_idx = current_param_idx;
        found_arg = false;

        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Requested Date fields: ");

        if (num_parsed_tokens == field_token_idx) // 如果没有指定字段，则返回完整日期
        {
            UARTStringPutNOBlocking(UART0_BASE, date_transmit_buffer);
            found_arg = true;
        }
        else // 根据指定字段返回信息
        {
            for (i = field_token_idx; i < num_parsed_tokens; ++i)
            {
                if (compareFieldKeyword(&parsed_tokens[i], "YEAR", 4)) // "YEAR"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Year=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)((year / 1000) % 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)((year / 100) % 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)((year / 10) % 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(year % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
                else if (compareFieldKeyword(&parsed_tokens[i], "MONTH", 5)) // "MONTH"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Month=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(month / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(month % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
                else if (compareFieldKeyword(&parsed_tokens[i], "DATE", 3)) // "DATE" (日)
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Day=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(day / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(day % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
            }
        }
        if (!found_arg) // 如果没有找到有效参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid argument.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
        }
    }

    // 处理 "*GET:TIME" 命令 (获取时间信息)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:TIME"))
    {
        field_token_idx = current_param_idx;
        found_arg = false;

        UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Requested Time fields: ");

        if (num_parsed_tokens == field_token_idx) // 如果没有指定字段，则返回完整时间
        {
            UARTStringPutNOBlocking(UART0_BASE, time_transmit_buffer);
            found_arg = true;
        }
        else // 根据指定字段返回信息
        {
            for (i = field_token_idx; i < num_parsed_tokens; ++i)
            {
                if (compareFieldKeyword(&parsed_tokens[i], "HOUR", 4)) // "HOUR"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Hour=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(hh / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(hh % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
                else if (compareFieldKeyword(&parsed_tokens[i], "MINUTE", 3)) // "MINUTE"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Minute=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(mm / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(mm % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
                else if (compareFieldKeyword(&parsed_tokens[i], "SECOND", 3)) // "SECOND"
                {
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Second=");
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(ss / 10) + '0');
                    UARTCharPutBlocking(UART0_BASE, (uint8_t)(ss % 10) + '0');
                    UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)" ");
                    found_arg = true;
                }
            }
        }
        if (!found_arg) // 如果没有找到有效参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid argument.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
        }
    }

    // 处理 "*GET:ALARM" 命令 (获取闹钟时间)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:ALARM"))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Alarm: ");
            if (alm_hh == 25) // 未设置闹钟
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Alarm not set.\r\n");
            else // 显示闹钟时间
            {
                UARTStringPutNOBlocking(UART0_BASE, alarm_transmit_buffer);
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
            }
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *GET:ALARM\r\n");
        }
    }

    // 处理 "*GET:FORMAT" 命令 (获取显示格式)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:FORMAT"))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Current Display Format: ");
            if (shift_mode == false) // 左移 (正常顺序)
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"LEFT (Normal Order)\r\n");
            else // 右移 (反向顺序)
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"RIGHT (Reversed Order)\r\n");
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *GET:FORMAT\r\n");
        }
    }

    // 处理 "*GET:DISPLAY" 命令 (获取显示状态)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:DISPLAY"))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"7-Segment Display Status: ");
            if (seven_segment_display_on == true) // 开启
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"ON (Normal Display)\r\n");
            else // 关闭
                UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"OFF (All Segments Off)\r\n");
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *GET:DISPLAY\r\n");
        }
    }

    // 处理 "*MOTOR:START" 命令 (启动步进电机)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*MOTOR:START"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            motor_running = 1;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper motor started.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *MOTOR:START\r\n");
        }
    }

    // 处理 "*MOTOR:STOP" 命令 (停止步进电机)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*MOTOR:STOP"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            motor_running = 0;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper motor stopped.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *MOTOR:STOP\r\n");
        }
    }

    // 处理 "*MOTOR:FWD" 命令 (设置步进电机正转)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*MOTOR:FWD"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            motor_direction = 0;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper motor direction set to forward.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *MOTOR:FWD\r\n");
        }
    }

    // 处理 "*MOTOR:REV" 命令 (设置步进电机反转)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*MOTOR:REV"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            motor_direction = 1;
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper motor direction set to reverse.\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *MOTOR:REV\r\n");
        }
    }

    // 处理 "*GET:MOTOR" 命令 (获取步进电机状态)
    else if (matchCommand(&parsed_tokens[0], (num_parsed_tokens > 1 ? &parsed_tokens[1] : NULL), num_parsed_tokens, "*GET:MOTOR"))
    {
        if (num_parsed_tokens == current_param_idx)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Stepper Motor Status:\r\n");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"  State: ");
            UARTStringPutNOBlocking(UART0_BASE, motor_running ? (uint8_t *)"RUNNING" : (uint8_t *)"STOPPED");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"  Direction: ");
            UARTStringPutNOBlocking(UART0_BASE, motor_direction ? (uint8_t *)"REVERSE" : (uint8_t *)"FORWARD");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"\r\n");
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"  Speed: 1 RPM\r\n");
        }
        else
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: *GET:MOTOR\r\n");
        }
    }

    // 处理 "INIT" 命令 (复位)
    else if (compareTokens(&parsed_tokens[0], "INIT", 4))
    {
        if (num_parsed_tokens == current_param_idx)
            SysCtlReset(); // 系统复位
        else
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: INIT\r\n");
    }

    // 处理 "HELP" 命令 (显示帮助文档)
    else if (compareTokens(&parsed_tokens[0], "HELP", 4))
    {
        if (num_parsed_tokens == current_param_idx) // 确保没有额外参数
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"HELP Document:\r\n"
                                                           "*RST                                       : Reset the clock.\r\n"
                                                           "*SET :DATE YEAR MONTH DATE YYYY MM DD      : Set year, month and day.\r\n"
                                                           "*SET :DATE YEAR MONTH YYYY MM              : Set year and month.\r\n"
                                                           "*SET :DATE YEAR DATE YYYY DD               : Set year and day.\r\n"
                                                           "*SET :DATE MONTH DATE MM DD                : Set month and day.\r\n"
                                                           "*SET :DATE YEAR YYYY                       : Set year.\r\n"
                                                           "*SET :DATE MONTH MM                        : Set month.\r\n"
                                                           "*SET :DATE DATE DD                         : Set day.\r\n"
                                                           "*SET :TIME HOUR MINUTE SECOND HH MM SS     : Set hour, minute and second.\r\n"
                                                           "*SET :TIME HOUR MINUTE HH MM               : Set hour and minute.\r\n"
                                                           "*SET :TIME HOUR SECOND HH SS               : Set hour and second.\r\n"
                                                           "*SET :TIME MINUTE SECOND MM SS             : Set minute and second.\r\n"
                                                           "*SET :TIME HOUR HH                         : Set hour.\r\n"
                                                           "*SET :TIME MINUTE MM                       : Set minute.\r\n"
                                                           "*SET :TIME SECOND SS                       : Set second.\r\n"
                                                           "*SET :ALARM HOUR MINUTE SECOND HH MM SS    : Set alarm hour, minute and second.\r\n"
                                                           "*SET :ALARM HOUR MINUTE HH MM              : Set alarm hour and minute.\r\n"
                                                           "*SET :ALARM HOUR SECOND HH SS              : Set alarm hour and second.\r\n"
                                                           "*SET :ALARM MINUTE SECOND MM SS            : Set alarm minute and second.\r\n"
                                                           "*SET :ALARM HOUR HH                        : Set alarm hour.\r\n"
                                                           "*SET :ALARM MINUTE MM                      : Set alarm minute.\r\n"
                                                           "*SET :ALARM SECOND SS                      : Set alarm second.\r\n"
                                                           "*SET :DISPLAY ON/OFF                       : Set 7-segment display.\r\n"
                                                           "*SET :FORMAT LEFT/RIGHT                    : Set display flow direction and order.\r\n"
                                                           "*GET :DATE                                 : Get year, month and day.\r\n"
                                                           "*GET :DATE YEAR MONTH                      : Get year and month.\r\n"
                                                           "*GET :DATE YEAR DATE                       : Get year and day.\r\n"
                                                           "*GET :DATE MONTH DATE                      : Get month and day.\r\n"
                                                           "*GET :DATE YEAR                            : Get year.\r\n"
                                                           "*GET :DATE MONTH                           : Get month.\r\n"
                                                           "*GET :DATE DATE                            : Get day.\r\n"
                                                           "*GET :TIME                                 : Get hour, minute and second.\r\n"
                                                           "*GET :TIME HOUR MINUTE                     : Get hour and minute.\r\n"
                                                           "*GET :TIME HOUR SECOND                     : Get hour and second.\r\n"
                                                           "*GET :TIME MINUTE SECOND                   : Get minute and second.\r\n"
                                                           "*GET :TIME HOUR                            : Get hour.\r\n"
                                                           "*GET :TIME MINUTE                          : Get minute.\r\n"
                                                           "*GET :TIME SECOND                          : Get second.\r\n"
                                                           "*GET :ALARM                                : Get alarm time.\r\n"
                                                           "*GET :DISPLAY                              : Get 7-segment display status.\r\n"
                                                           "*GET :FORMAT                               : Get display flow format.\r\n"
                                                           "*MOTOR :START                              : Start stepper motor (1 RPM).\r\n"
                                                           "*MOTOR :STOP                               : Stop stepper motor.\r\n"
                                                           "*MOTOR :FWD                                : Set stepper motor forward.\r\n"
                                                           "*MOTOR :REV                                : Set stepper motor reverse.\r\n"
                                                           "*GET :MOTOR                                : Get stepper motor status.\r\n");
        }
        else // 命令格式错误
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Invalid command format. Usage: HELP\r\n");
        }
    }
    else // 未知命令
    {
        if (num_parsed_tokens > 0)
        {
            UARTStringPutNOBlocking(UART0_BASE, (uint8_t *)"Unknown command. Type HELP for commands.\r\n");
        }
    }

    uart_receive_len = 0;                                        // 清空接收长度
    memset(uart_receive_buffer, 0, sizeof(uart_receive_buffer)); // 清空接收缓冲区
}

// 判断给定年份是否为闰年
