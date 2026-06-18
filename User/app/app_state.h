#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "app_types.h"

extern volatile uint16_t motor_position;
extern volatile uint8_t motor_direction;
extern volatile uint8_t motor_running;

extern uint32_t ui32SysClock;

extern int8_t hh;
extern int8_t mm;
extern int8_t ss;
extern uint16_t year;
extern uint8_t month;
extern uint8_t day;

extern int8_t alm_hh;
extern int8_t alm_mm;
extern int8_t alm_ss;

extern uint16_t temp_year;
extern uint8_t temp_month;
extern uint8_t temp_day;
extern uint8_t temp_hh;
extern uint8_t temp_mm;
extern uint8_t temp_ss;
extern uint8_t temp_alm_hh;
extern uint8_t temp_alm_mm;
extern uint8_t temp_alm_ss;

extern int8_t original_hh;
extern int8_t original_mm;
extern int8_t original_ss;
extern uint16_t original_year;
extern uint8_t original_month;
extern uint8_t original_day;
extern int8_t original_alm_hh;
extern int8_t original_alm_mm;
extern int8_t original_alm_ss;

extern bool unsaved_changes_active;

extern volatile uint32_t g_system_tick;
extern volatile uint16_t clock1s;
extern volatile uint8_t clock20ms;
extern volatile uint8_t clock2ms;
extern volatile uint16_t clock500ms;
extern volatile uint16_t clock300ms;
extern volatile uint16_t clock900ms;
extern volatile uint16_t blink_timer;
extern volatile bool clock1s_flag;
extern volatile bool clock20ms_flag;
extern volatile bool clock2ms_flag;
extern volatile bool clock500ms_flag;
extern volatile bool clock300ms_flag;
extern volatile bool clock900ms_flag;
extern volatile bool blink_timer_flag;

extern volatile uint32_t mode_timeout_timer;
extern volatile bool mode_timeout_flag;

extern volatile bool save_blink_active;
extern volatile uint32_t save_blink_timer;
extern volatile bool long_press_saving_in_progress;

extern uint8_t uart_receive_buffer[100];
extern uint8_t uart_receive_len;
extern volatile uint8_t cmd_state;

extern uint8_t time_transmit_buffer[9];
extern uint8_t alarm_transmit_buffer[9];
extern uint8_t date_transmit_buffer[11];
extern uint8_t seg7[18];
extern uint8_t stuID[8];
extern uint8_t name[8];

extern uint8_t result;
extern uint8_t cnt;
extern int8_t shift;
extern uint8_t rightshift;
extern uint8_t master_display_buffer[18];

extern bool shift_mode;
extern bool shift_speed;
extern bool shifting;
extern bool display_reversed_order;
extern bool seven_segment_display_on;
extern bool alarm_ringing;
extern bool alarm_silenced_for_match;
extern bool night_mode_active;
extern bool led_takeover_active;
extern uint8_t led_takeover_pattern;
extern volatile uint32_t uart_activity_until_tick;
extern uint32_t alarm_ring_start_tick;
extern uint32_t alarm_beep_phase_tick;
extern bool alarm_beep_on;
extern bool message_active;
extern bool message_scroll_active;
extern uint8_t message_buffer[33];
extern uint8_t message_len;
extern int8_t message_shift;
extern uint32_t message_start_tick;
extern uint32_t message_last_shift_tick;

extern volatile uint8_t raw_key_value;
extern volatile uint8_t prev_raw_key_value;
extern volatile uint32_t key_debounce_timer[8];
extern volatile uint32_t key_press_start_time[8];
extern volatile uint32_t key_long_press_timer[8];
extern volatile bool key_states[8];
extern volatile bool key_short_press_event[8];
extern volatile bool key_long_press_start_event[8];
extern volatile bool key_repeat_press_event[8];

extern volatile uint32_t user_key_debounce_timer[2];
extern volatile uint32_t user_key_press_start_time[2];
extern volatile bool user_key_states[2];
extern volatile bool user_key_short_press_event[2];

extern system_mode_t current_mode;
extern setting_field_t current_setting_field;
extern main_display_t main_display_mode;
extern bool is_blinking;
extern bool prev_shifting;
extern bool prev_shift_mode;
extern bool prev_shift_speed;
extern bool init_flag;
extern uint8_t init_procedure;

extern command_token_t parsed_tokens[MAX_COMMAND_TOKENS];
extern uint8_t num_parsed_tokens;

extern uint32_t storedRTC[4];
extern uint32_t currentRTC;
extern uint32_t fetchRTC[4];
extern uint8_t days_in_month[13];

#endif
