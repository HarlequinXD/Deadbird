/*
 * Software sound mixer (Android, Windows, headless). The game thread posts commands into a
 * single-producer / single-consumer ring; the audio callback (which may run on another thread)
 * applies them and mixes up to 16 voices of 8-bit PCM with linear interpolation into 16-bit
 * stereo at SND_RATE. Loop slots glide their volume and pitch so changes never click.
 */
#include "snd.h"
#include "sfx_pcm.h"

#define NVOICE  16
#define NCMD    64

typedef struct {
    const signed char *data;
    s32 len, loop;
    u32 pos, step;                     /* 16.16 sample position / increment */
    s32 vl, vr, tl, tr;                /* current and target gains (4096 = 1) */
    s32 rate, pitch, tpitch;
    int active;
} Voice;

typedef struct { u8 kind, slot, id; s16 vol, pan, pitch; } Cmd;

static Voice voice[NVOICE];
static Cmd cmds[NCMD];
static volatile int cmd_head, cmd_tail;
static int next_voice = SND_LOOPS;

#if defined(__GNUC__) || defined(__clang__)
#define BARRIER() __sync_synchronize()
#else
#define BARRIER() ((void)0)
#endif

static void push(int kind, int slot, int id, int vol, int pan, int pitch)
{
    int h = cmd_head, n = (h + 1) % NCMD;
    if (n == cmd_tail) return;                     /* full: drop (never blocks the game) */
    cmds[h].kind = (u8)kind; cmds[h].slot = (u8)slot; cmds[h].id = (u8)id;
    cmds[h].vol = (s16)vol; cmds[h].pan = (s16)pan; cmds[h].pitch = (s16)(pitch > 32767 ? 32767 : pitch);
    BARRIER();
    cmd_head = n;
}

void snd_init(void) { memset(voice, 0, sizeof(voice)); cmd_head = cmd_tail = 0; }

void snd_play(int id, int vol, int pan, int pitch) { if (id >= 0 && id < SFX_COUNT && vol > 0) push(0, 0, id, vol, pan, pitch); }

void snd_loop(int slot, int id, int vol, int pan, int pitch) { if (slot >= 0 && slot < SND_LOOPS) push(1, slot, id, vol, pan, pitch); }

static void gains(int vol, int pan, s32 *l, s32 *r)
{
    if (pan < -4096) pan = -4096;
    if (pan > 4096) pan = 4096;
    *l = vol * (pan > 0 ? 4096 - pan : 4096) >> 12;
    *r = vol * (pan < 0 ? 4096 + pan : 4096) >> 12;
}

static u32 step_for(s32 rate, s32 pitch) { if (pitch < 64) pitch = 64; return (u32)rate * (u32)pitch / SND_RATE * 16; }

static void apply(const Cmd *c)
{
    const SfxPcm *s = &sfx_pcm[c->id];
    Voice *v;
    if (c->kind == 0) {                            /* one-shot: take the next free voice (or the oldest) */
        int i, k = -1;
        for (i = 0; i < NVOICE - SND_LOOPS; i++) {
            int j = SND_LOOPS + (next_voice - SND_LOOPS + i) % (NVOICE - SND_LOOPS);
            if (!voice[j].active) { k = j; break; }
        }
        if (k < 0) k = next_voice;
        next_voice = SND_LOOPS + (k - SND_LOOPS + 1) % (NVOICE - SND_LOOPS);
        v = &voice[k];
        v->data = s->data; v->len = s->len; v->loop = 0; v->pos = 0; v->rate = s->rate;
        v->pitch = v->tpitch = c->pitch;
        gains(c->vol, c->pan, &v->tl, &v->tr); v->vl = v->tl; v->vr = v->tr;
        v->active = 1;
    } else {                                       /* loop slot: (re)start if the sound changed, glide gains */
        v = &voice[c->slot];
        if (!v->active || v->data != s->data) {
            if (c->vol <= 0) return;
            v->data = s->data; v->len = s->len; v->loop = 1; v->pos = 0; v->rate = s->rate;
            v->vl = v->vr = 0; v->pitch = c->pitch; v->active = 1;
        }
        v->tpitch = c->pitch;
        gains(c->vol, c->pan, &v->tl, &v->tr);
    }
    v->step = step_for(v->rate, v->pitch);
}

void snd_mix(s16 *out, int frames)
{
    static s32 acc[2 * 1024];
    int i, k;
    while (cmd_tail != cmd_head) {
        BARRIER();
        apply(&cmds[cmd_tail]);
        cmd_tail = (cmd_tail + 1) % NCMD;
    }
    while (frames > 0) {
        int n = frames > 1024 ? 1024 : frames;
        memset(acc, 0, sizeof(acc[0]) * 2 * (size_t)n);
        for (k = 0; k < NVOICE; k++) {
            Voice *v = &voice[k];
            s32 dl, dr;
            if (!v->active) continue;
            /* glide toward the targets over this block (loops) */
            if (v->pitch != v->tpitch) { v->pitch += (v->tpitch - v->pitch) / 4 + (v->tpitch > v->pitch ? 1 : -1); v->step = step_for(v->rate, v->pitch); }
            dl = (v->tl - v->vl) / n; dr = (v->tr - v->vr) / n;
            for (i = 0; i < n; i++) {
                u32 ip = v->pos >> 16;
                s32 a, b, smp;
                if ((s32)ip >= v->len) {
                    if (!v->loop) { v->active = 0; break; }
                    v->pos -= (u32)v->len << 16; ip = v->pos >> 16;
                }
                a = v->data[ip];
                b = (s32)ip + 1 < v->len ? v->data[ip + 1] : (v->loop ? v->data[0] : 0);
                smp = (a << 8) + (((b - a) * (s32)(v->pos & 0xffff)) >> 8);
                acc[2 * i] += smp * v->vl >> 12;
                acc[2 * i + 1] += smp * v->vr >> 12;
                v->vl += dl; v->vr += dr;
                v->pos += v->step;
            }
            v->vl = v->tl; v->vr = v->tr;
            if (v->loop && v->tl == 0 && v->tr == 0 && v->vl == 0) v->active = 0;
        }
        for (i = 0; i < 2 * n; i++) {
            s32 s = acc[i] * 5 / 8;                /* master */
            out[i] = (s16)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
        }
        out += 2 * n; frames -= n;
    }
}
