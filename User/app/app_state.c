#include "app_state.h"

/* ── 全局应用状态实例 ── */
app_state_t g = {
    .clock = {
        .hh = 0, .mm = 0, .ss = 0,
        .year = 2025, .month = 6, .day = 3,
        .alm_hh = 25, .alm_mm = 0, .alm_ss = 0,
        .alarm_enabled = false,
    },
    .disp = {
        .seg7 = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
                 0x7f, 0x6f, 0x77, 0x7c, 0x58, 0x5e, 0x79, 0x71,
                 0x5c, 0x00},
        .rightshift = 0x01,
        .on = true,
        .shifting = true,
        .mode = MODE_FLOWING,
        .main_disp = MAIN_DISPLAY_FLOW,
        .field = FIELD_NONE,
        .prev_shifting = true,
        .init_flag = true,
    },
    .rtc = {
        .days_in_month = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    },
    .motor = {
        .direction = 1,
        .running = 1,
    },
};
