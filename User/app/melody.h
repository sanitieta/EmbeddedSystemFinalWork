#ifndef MELODY_H
#define MELODY_H

#include <stdbool.h>

void Melody_Start(void);       // 启动开机旋律播放
void Melody_Update(void);      // 每主循环迭代调用一次，推进音符
bool Melody_IsPlaying(void);   // 返回 true 表示旋律正在播放

#endif
