#ifndef COMMAND_H
#define COMMAND_H
#include <stdint.h>
#include <stdbool.h>
#include "app_types.h"
void ProcessUartCommand(void);
bool compareTokens(const command_token_t *token, const char *str_literal, uint8_t min_match_len);
#endif
