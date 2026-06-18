#ifndef PWM_BUZZER_H
#define PWM_BUZZER_H
#include <stdint.h>
void PWMInit(void);
void PWMStart(uint32_t ui32Freq_Hz);
void PWMStop(void);
#endif
