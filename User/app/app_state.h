#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "app_types.h"

/* 全局应用状态实例 —— 所有模块通过 g.xxx 访问 */
extern app_state_t g;

#endif
