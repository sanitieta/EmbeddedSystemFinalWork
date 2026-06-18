#include "app_state.h"

/* ── 全局应用状态实例
   所有未显式指定的字段由 C 标准保证零初始化 (指针=NULL, 整数=0, bool=false) ── */
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
                 0x7f, 0x6f, 0x77, 0x7c, 0x58, 0x5e, 0x79, 0x71,
                 0x5c, 0x00},
        .rightshift = 0x01,    // 位选从最低位开始扫描
        .on = true,            // 数码管默认开启
        .shifting = true,      // 流动显示默认启动
        .mode = MODE_FLOWING,  // 初始为流动显示模式
        .main_disp = MAIN_DISPLAY_FLOW, // 默认显示日期+时间交替流动
        .field = FIELD_NONE,   // 无编辑字段选中
        .prev_shifting = true, // 进入编辑前移位状态默认值
        .init_flag = true,     // 启动时进入初始化动画流程
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
