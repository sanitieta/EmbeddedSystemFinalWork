#include <stdint.h>
#include <stdbool.h>
#include "app_state.h"
#include "pwm_buzzer.h"
#include "melody.h"

/* ── 音符定义 ── */
typedef struct {
    uint16_t freq_hz;     // 频率 (Hz), 0 = 休止符
    uint16_t duration_ms; // 时值 (ms)
} note_t;

/* MAN! WHAT CAN I SAY? */
static const note_t see_you_again[] = {
    {698, 355}, {  0,  23}, {1047,355}, {  0,  23}, {932, 355}, {  0,  23}, {587, 729}, {  0,  23},
    {932, 167}, {  0,  23}, {1047,167}, {  0,  23}, {1175,167}, {  0,  23}, {1047,167}, {  0,  23},
    {932, 167}, {  0,  23}, {1047,167}, {  0,  23}, {698, 355}, {  0,  23}, {1047,355}, {  0,  23},
    {932, 355}, {  0,  23}, {587, 729}, {  0,  23}, {932, 167}, {  0,  23}, {1047,167}, {  0,  23},
    {1175,167}, {  0,  23}, {1047,167}, {  0,  23}, {932, 167}, {  0,  23}, {1047,167}, {  0,  23},
    {698, 355}, {  0,  23}, {1047,355}, {  0,  23}, {932, 355}, {  0,  23}, {698, 729}, {  0,  23},
    {932, 167}, {  0,  23}, {1047,167}, {  0,  23}, {1175,167}, {  0,  23}, {1047,167}, {  0,  23},
    {932, 167}, {  0,  23}, {1047,167}, {  0,  23}, {698, 355}, {  0,  23}, {1047,355}, {  0,  23},
    {932, 355}, {  0,  23}, {698, 729}, {  0,  23},
};

#define MELODY_NUM_NOTES (sizeof(see_you_again) / sizeof(see_you_again[0]))

/* ── 播放器状态 (文件作用域静态变量, 不污染全局结构体) ── */
static bool     s_active;         // 旋律是否正在播放
static uint16_t s_note_index;     // 当前音符索引
static uint32_t s_note_start;     // 当前音符起始时刻 (g.timer.tick)

/* 启动旋律播放 */
void Melody_Start(void)
{
    s_active = true;
    s_note_index = 0;
    s_note_start = g.timer.tick;
    PWMStart(see_you_again[0].freq_hz);
}

/* 推进音符 —— 每次主循环迭代调用 */
void Melody_Update(void)
{
    const note_t *note;

    if (!s_active) return;

    note = &see_you_again[s_note_index];

    /* 检查当前音符时值是否已到 */
    if ((g.timer.tick - s_note_start) < note->duration_ms)
        return;

    /* 推进到下一个音符 */
    s_note_index++;
    if (s_note_index >= MELODY_NUM_NOTES)
    {
        /* 旋律结束 */
        PWMStop();
        s_active = false;
        return;
    }

    note = &see_you_again[s_note_index];
    s_note_start = g.timer.tick;

    if (note->freq_hz == 0)
    {
        PWMStop();          // 休止符
    }
    else
    {
        PWMStart(note->freq_hz);
    }
}

/* 查询播放状态 */
bool Melody_IsPlaying(void)
{
    return s_active;
}
