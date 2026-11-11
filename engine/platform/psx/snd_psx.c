/*
 * PS1 sound on the SPU (replaces engine/snd_mix.c in the PS1 build).
 * snd_init DMAs every sound of the game's bank (SPU ADPCM) into SPU RAM once.
 * Voices 0-3 are the persistent loops (the ADPCM loop flags make the SPU repeat them by itself);
 * voices 4-23 take one-shots round-robin.
 */
#include <stdint.h>
#include <psxspu.h>
#include <hwregs_c.h>
#include "snd.h"

#define SPU_BASE 0x1010                    /* first free byte after the SPU's capture buffers */
static uint32_t addr[SND_MAX];
static const SndSample *bank; static int nbank;          /* sound start, in SPU RAM bytes */
static int loop_id[SND_LOOPS] = { -1, -1, -1, -1 }, next_voice = SND_LOOPS;

static uint16_t pitch_reg(int rate, int pitch)
{
    uint32_t f = (uint32_t)rate * 4096 / 44100 * (uint32_t)pitch / 4096;
    return (uint16_t)(f > 0x3fff ? 0x3fff : f);
}

static void set_vol(int v, int vol, int pan)
{
    int l, r;
    if (pan < -4096) pan = -4096;
    if (pan > 4096) pan = 4096;
    l = vol * (pan > 0 ? 4096 - pan : 4096) >> 12;
    r = vol * (pan < 0 ? 4096 + pan : 4096) >> 12;
    SPU_CH_VOL_L(v) = (uint16_t)(l * 0x3fff >> 12);
    SPU_CH_VOL_R(v) = (uint16_t)(r * 0x3fff >> 12);
}

static void key_on(int v)
{
    if (v < 16) SPU_KEY_ON1 = (uint16_t)(1 << v); else SPU_KEY_ON2 = (uint16_t)(1 << (v - 16));
}

void snd_init(const SndSample *b, int n)
{
    uint32_t a = SPU_BASE;
    int i;
    bank = b; nbank = n > SND_MAX ? SND_MAX : n;
    SpuInit();
    for (i = 0; i < nbank; i++) {
        addr[i] = a;
        SpuSetTransferStartAddr(a);
        SpuWrite((const uint32_t *)bank[i].data, (size_t)bank[i].len);
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
        a += (uint32_t)((bank[i].len + 63) & ~63);
    }
}

static void start(int v, int id, int vol, int pan, int pitch)
{
    set_vol(v, vol, pan);
    SPU_CH_FREQ(v) = pitch_reg(bank[id].rate, pitch);
    SPU_CH_ADDR(v) = (uint16_t)getSPUAddr(addr[id]);
    SPU_CH_ADSR1(v) = 0x00ff;               /* instant attack, hold full level */
    SPU_CH_ADSR2(v) = 0x0000;
    key_on(v);
}

void snd_play(int id, int vol, int pan, int pitch)
{
    if (id < 0 || id >= nbank || vol <= 0) return;
    start(next_voice, id, vol, pan, pitch);
    next_voice = next_voice + 1 >= 24 ? SND_LOOPS : next_voice + 1;
}

void snd_loop(int slot, int id, int vol, int pan, int pitch)
{
    if (slot < 0 || slot >= SND_LOOPS || id < 0 || id >= nbank) return;
    if (vol <= 0) {
        if (loop_id[slot] >= 0) { set_vol(slot, 0, 0); }
        return;
    }
    if (loop_id[slot] != id) { start(slot, id, vol, pan, pitch); loop_id[slot] = id; return; }
    set_vol(slot, vol, pan);
    SPU_CH_FREQ(slot) = pitch_reg(bank[id].rate, pitch);
}

void snd_mix(s16 *stereo, int frames) { (void)stereo; (void)frames; }   /* the SPU mixes */
