/*
 * The framework's built-in sounds, synthesised at start-up (integer maths, 8-bit PCM for the
 * software mixer): enough for a game made in the editor before it imports its own.
 */
#include "fw.h"
#include "snd.h"
#include "fwsfx.h"

#define RATE 22050
static signed char pcm[FWS_COUNT][9000];
static SndSample bank[FWS_COUNT];
const char *const fw_sound_names[FWS_COUNT] = { "SHOOT", "HIT", "DIE", "JUMP", "PICKUP", "LAND", "ALERT", "EXPLODE", "CLICK", "HUM" };

static u32 rng = 0x1234567;
static int noise(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (int)(rng & 255) - 128; }

/* f0 -> f1 Hz sweep, square (sq) mixed with noise (nz 0..256), decaying, n samples */
static void tone(int id, int n, int f0, int f1, int sq, int nz, int loop)
{
    int i, lp = 0;
    u32 ph = 0;
    for (i = 0; i < n; i++) {
        int f = f0 + (f1 - f0) * i / n, env = 256 * (n - i) / n, s;
        if (loop) env = 256;
        ph += (u32)(f * 65536 / RATE);
        s = ((ph >> 15) & 1 ? 100 : -100) * sq / 256;
        lp += (noise() - lp) / 3;
        s += lp * nz / 256;
        if (i < 40) env = env * i / 40;                 /* no click */
        s = s * env / 256;
        pcm[id][i] = (signed char)(s > 127 ? 127 : s < -127 ? -127 : s);
    }
    bank[id].data = pcm[id]; bank[id].len = n; bank[id].rate = RATE; bank[id].loop = loop;
}

void fw_sfx_init(void)
{
    tone(FWS_SHOOT, 5000, 900, 120, 120, 230, 0);
    tone(FWS_HIT, 2600, 320, 90, 200, 120, 0);
    tone(FWS_DIE, 9000, 400, 40, 180, 160, 0);
    tone(FWS_JUMP, 3000, 200, 520, 200, 0, 0);
    tone(FWS_PICKUP, 4000, 700, 1400, 220, 0, 0);
    tone(FWS_LAND, 1800, 120, 50, 120, 200, 0);
    tone(FWS_ALERT, 5000, 500, 700, 220, 20, 0);
    tone(FWS_EXPLODE, 9000, 90, 30, 60, 256, 0);
    tone(FWS_CLICK, 400, 1500, 1500, 200, 0, 0);
    tone(FWS_HUM, 8820, 60, 60, 90, 40, 1);
    snd_init(bank, FWS_COUNT);
}

int fw_sfx_ram(void) { return (int)sizeof(pcm); }
