#ifndef UART_PORT_H
#define UART_PORT_H
#include <stdint.h>
void UARTInit(void);
void UARTStringPutNOBlocking(uint32_t ui32Base, uint8_t *cMessage);
void UARTStringPutReversedNOBlocking(uint32_t ui32Base, uint8_t *cMessage);
void UARTCharPutBlocking(uint32_t ui32Base, uint8_t ucData);
#endif
