#include "prof.h"

#ifdef PSX_GTE
u32 prof_acc[PROF_N];
static int frames;
static char line[64], line2[64];
void prof_init(void) { *(volatile u32 *)0x1F801114 = 0x0100; }      /* timer 1: count hblanks */
static void num(char **p, const char *tag, u32 v)
{
    char *o = *p;
    *o++ = tag[0]; *o++ = tag[1];
    if (v > 999) v = 999;
    if (v >= 100) *o++ = (char)('0' + v / 100);
    if (v >= 10) *o++ = (char)('0' + v / 10 % 10);
    *o++ = (char)('0' + v % 10);
    *o++ = ' ';
    *p = o;
}
void prof_frame(void)
{
    static const char *const T[PROF_N] = { "TK", "LV", "EN", "PL", "FX", "HD", "OT", "CA", "LK", "MV", "AI", "a ", "b ", "c ", "d " };
    int i;
    u32 sum = 0;
    char *o = line;
    if (++frames < 30) return;
    for (i = 0; i < PROF_LINE1; i++) { u32 v = prof_acc[i] / frames; num(&o, T[i], v); sum += v; prof_acc[i] = 0; }
    num(&o, "= ", sum);
    o[-1] = 0;
    o = line2;
    for (; i < PROF_N; i++) { u32 v = prof_acc[i] / frames; num(&o, T[i], v); prof_acc[i] = 0; }
    o[-1] = 0;
    frames = 0;
}
const char *prof_text(void) { return line; }
const char *prof_text2(void) { return line2; }
#else
void prof_init(void) {}
void prof_frame(void) {}
const char *prof_text(void) { return ""; }
const char *prof_text2(void) { return ""; }
#endif
