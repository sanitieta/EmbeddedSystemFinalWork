#include "app_state.h"

/* ── 全局应用状态实例
   ARMCC5 (ARM Compiler 5) 存在嵌套结构体零初始化 bug:
   C99 designated initializers 不会对未显式赋值的嵌套字段清零。
   因此每个 bool/uint8_t 字段都必须显式初始化，标记 "ARMCC5 兼容"。 ── */
app_state_t g = {
    /* ── 时钟初始值: 2025-06-03 00:00:00, 闹钟未设置 ── */
    .clock = {
        .hh = 0, .mm = 0, .ss = 0,         // 当前时间 00:00:00
        .year = 2025, .month = 6, .day = 3, // 当前日期 2025-06-03
        .alm_hh = 25, .alm_mm = 0, .alm_ss = 0, // 闹钟未设置 (25 为哨兵值)
        .alarm_enabled = false,             // 闹钟默认关闭
    },

    /* ── 显示初始值: 7-SEG 字形表 + 扫描/模式默认值 ── */
    .disp = {
        /* 7段数码管字形码: 0 1 2 3 4 5 6 7 8 9 A B C D E F - 空白 */
        .seg7 = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
                 0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71,
                 0x5c, 0x00},
        .cnt = 0,              // 数码管扫描索引从 0 开始 (ARMCC5 兼容)
        .rightshift = 0x01,    // 位选从最低位开始扫描
        .on = true,            // 数码管默认开启
        .shifting = true,      // 流动显示默认启动
        .shift = 0,            // 流动偏移量从 0 开始 (ARMCC5 兼容)
        .shift_mode = false,   // 默认左移 (FORMAT LEFT)
        .shift_speed = false,  // 默认慢速 500ms (ARMCC5 兼容)
        .reversed = false,     // 默认正序显示
        .blinking = false,     // 无编辑闪烁 (ARMCC5 兼容)
        .mode = MODE_FLOWING,  // 初始为流动显示模式
        .main_disp = MAIN_DISPLAY_FLOW, // 默认显示日期+时间交替流动
        .field = FIELD_NONE,   // 无编辑字段选中
        .prev_shifting = true, // 进入编辑前移位状态默认值
        .prev_shift_mode = false, // 进入编辑前格式默认值
        .prev_shift_speed = false, // 进入编辑前速度默认值 (ARMCC5 兼容)
        .night_mode = false,   // 非夜间模式 (ARMCC5 兼容)
        .led_takeover = false, // 非 LED 接管模式 (ARMCC5 兼容)
        .led_pattern = 0x00,   // LED 接管输出模式 (ARMCC5 兼容)
        .weather_code = 0x00,  // 天气 LED5-7 初始关闭 (ARMCC5 兼容)
        .alarm_ringing = false,// 未在响铃 (ARMCC5 兼容)
        .alarm_beep_on = false,// 蜂鸣器关闭 (ARMCC5 兼容)
        .msg_active = false,   // 无临时消息 (ARMCC5 兼容)
        .msg_scroll = false,   // 无滚动消息 (ARMCC5 兼容)
        .save_blink_active = false, // 无保存闪烁 (ARMCC5 兼容)
        .long_press_saving = false, // 无长按保存 (ARMCC5 兼容)
        .beep_until = 0,       // 无远程蜂鸣 (ARMCC5 兼容)
        .ntp_synced = false,   // LED4: 尚未完成 NTP 对时
        .init_flag = true,     // 启动时进入初始化动画流程
        .init_step = 0,        // 初始化从步骤 0 开始 (ARMCC5 兼容)
    },

    /* ── RTC 初始值: 每月天数查找表 ── */
    .rtc = {
        .days_in_month = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    },

    /* ── 步进电机初始值: 反向运行，1 RPM ── */
    .motor = {
        .direction = 1, // 反转
        .running = 1,   // 启动运行
    },
};
