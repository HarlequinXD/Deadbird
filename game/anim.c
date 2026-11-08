#include "anim.h"

/* smoothstep ease 0..256 -> 0..4096 in 64 steps, (3t^2 - 2t^3) */
static int ease(int t /* 0..4096 */)
{
    s32 tt = t * t >> 12;
    return (int)((3 * tt - (2 * (tt * t >> 12))));
}

void anim_sample(const Clip *c, s32 t, AnimPose *out)
{
    s32 len = (s32)c->length << 8;
    int k, i, w;
    const AnimKey *a, *b;
    if (c->loop) { t %= len; if (t < 0) t += len; }
    else { if (t < 0) t = 0; if (t > len) t = len; }
    for (k = 0; k + 1 < c->nkeys && ((s32)c->keys[k + 1].frame << 8) <= t; k++) ;
    a = &c->keys[k];
    if (k + 1 >= c->nkeys) { for (i = 0; i < AP_COUNT; i++) out->v[i] = a->v[i]; return; }
    b = &c->keys[k + 1];
    w = (int)(((t - ((s32)a->frame << 8)) << 12) / (((s32)(b->frame - a->frame)) << 8));
    w = ease(w);
    for (i = 0; i < AP_COUNT; i++)
        out->v[i] = (s16)(a->v[i] + (((s32)(b->v[i] - a->v[i]) * w) >> 12));
}

void anim_blend(const AnimPose *a, const AnimPose *b, int w, AnimPose *out)
{
    int i;
    for (i = 0; i < AP_COUNT; i++)
        out->v[i] = (s16)(a->v[i] + (((s32)(b->v[i] - a->v[i]) * w) >> 12));
}
