#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>
#include "board_config.h"

typedef enum // 系统模式枚举
{
    MODE_FLOWING,      // 流动显示模式
    MODE_DATE_SET,     // 日期设置模式
    MODE_TIME_SET,     // 时间设置模式
    MODE_ALARM_SET,    // 闹钟设置模式
    MODE_ALARM_DISPLAY // 闹钟显示模式
} system_mode_t;

typedef enum // 设置字段枚举
{
    FIELD_NONE,         // 无字段
    FIELD_YEAR,         // 年份字段
    FIELD_MONTH,        // 月份字段
    FIELD_DAY,          // 日期字段
    FIELD_HOUR,         // 小时字段
    FIELD_MINUTE,       // 分钟字段
    FIELD_SECOND,       // 秒字段
    FIELD_ALARM_HOUR,   // 闹钟小时字段
    FIELD_ALARM_MINUTE, // 闹钟分钟字段
    FIELD_ALARM_SECOND  // 闹钟秒字段
} setting_field_t;

typedef enum // 正常显示内容枚举
{
    MAIN_DISPLAY_FLOW,
    MAIN_DISPLAY_TIME,
    MAIN_DISPLAY_DATE,
    MAIN_DISPLAY_YEAR
} main_display_t;

typedef struct // 命令Token结构体
{
    uint8_t token_str[MAX_TOKEN_LENGTH]; // Token字符串
    uint8_t token_len;                   // Token长度
} command_token_t;

#endif
