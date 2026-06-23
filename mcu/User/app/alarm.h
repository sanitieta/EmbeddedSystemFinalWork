#ifndef ALARM_H
#define ALARM_H
#include <stdbool.h>
void HandleAlarm(void);
void StopAlarmRinging(bool silence_current_match);
#endif
