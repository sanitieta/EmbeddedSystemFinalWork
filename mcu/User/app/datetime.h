#ifndef DATETIME_H
#define DATETIME_H
#include <stdint.h>
#include <stdbool.h>
bool is_leap_year(uint16_t year_val);
bool is_valid_date(uint16_t y, uint8_t m, uint8_t d);
bool is_valid_time(uint8_t h, uint8_t m, uint8_t s);
void DateTime_TickOneSecond(void);
#endif
