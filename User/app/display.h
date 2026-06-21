#ifndef DISPLAY_H
#define DISPLAY_H
#include <stdint.h>
void Update7SegmentDisplay(void);
void UpdateDisplayShift(void);
void UpdateTimeAndDisplayBuffers(void);
void Display_StartMessage(const uint8_t *text, uint8_t len);
void Display_StopMessage(void);
void Display_SendEvent(void);
void Display_SendLedEvent(void);
void Display_SetLedOutput(uint8_t led_pattern);
void Display_UpdateStatusLeds(void);
void Display_FormatBufferForProtocol(const uint8_t *src, uint8_t len, uint8_t *dst);
#endif
