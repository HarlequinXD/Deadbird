/*
 * Deadbird sound effects and ambience, synthesised at start-up (no recorded samples).
 *
 * Sci-fi hell power plant: heavy hooves on steel grating, a two-stroke chainsaw that idles and
 * revs, booming landings, hellish UI chimes, mains hum and a throbbing reactor drone, steam
 * bursts, distant metal clanks and the moans of captive shinigami.
 *
 * gen_sfx_each() renders every sound in SFX order as floats -1..1 and hands it to a callback:
 * the game keeps 8-bit PCM for its software mixer; tools/gen_dump encodes PS1 SPU ADPCM.
 */
#include <stdlib.h>
#include <string.h>
#include "gen.h"
#include "gmath.h"

#define SR_HI 22050
#define SR_LO 11025

/* ---------------------------------------------------------------- scratch arrays (freed per sound) */
static void *pool[96];
static int npool;
static GRng rng;

static float *A(int n)
{
    float *p = (float *)calloc((size_t)(n > 0 ? n : 1), sizeof(float));
    if (npool < 96) pool[npool++] = p;
    return p;
}
static void pool_free(void) { while (npool) free(pool[--npool]); }

static float *env_exp(int n, int sr, float tau, float attack)
{
    float *y = A(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        y[i] = gclamp(t / gmax(attack, 1e-6f), 0, 1) * gexp(-t / tau);
    }
    return y;
}
#define ENV(n, sr, tau) env_exp(n, sr, tau, 0.002f)

static float *noise(int n) { float *y = A(n); for (int i = 0; i < n; i++) y[i] = grng_uni(&rng, -1, 1); return y; }

static float *lp(float *x, int n, int sr, float fc)          /* in place */
{
    float a = gexp(-2 * GPI * fc / sr), s = 0;
    for (int i = 0; i < n; i++) { s = (1 - a) * x[i] + a * s; x[i] = s; }
    return x;
}
static float *hp(float *x, int n, int sr, float fc)          /* in place */
{
    float a = gexp(-2 * GPI * fc / sr), s = 0;
    for (int i = 0; i < n; i++) { s = (1 - a) * x[i] + a * s; x[i] -= s; }
    return x;
}
static float *bp(const float *x, int n, int sr, float fc, float q)
{
    float w = 2 * GPI * fc / sr, al = gsin(w) / (2 * q);
    float a0 = 1 + al, b0 = al / a0, b2 = -al / a0, a1 = -2 * gcos(w) / a0, a2 = (1 - al) / a0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0, *y = A(n);
    for (int i = 0; i < n; i++) {
        float o = b0 * x[i] + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x[i]; y2 = y1; y1 = o; y[i] = o;
    }
    return y;
}
static float *sweep(int n, int sr, float f0, float f1, float curve)
{
    float *y = A(n), ph = 0, T = (float)n / sr;
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        ph += f1 + (f0 - f1) * gexp(-t * curve / T);
        y[i] = gsin(2 * GPI * gfrac(ph / sr));
    }
    return y;
}
static float *metal(int n, int sr, float base, float tau, const float *parts, const float *amps, int np)
{
    float *y = A(n);
    for (int k = 0; k < np; k++) {
        float ph0 = grng_uni(&rng, 0, 6), f = base * parts[k], tk = tau / (0.6f + parts[k] * 0.25f);
        for (int i = 0; i < n; i++) {
            float t = (float)i / sr;
            y[i] += amps[k] * gsin(2 * GPI * gfrac(f * t) + ph0) * gexp(-t / tk);
        }
    }
    return y;
}

static float *sawv(const float *f, float fc, int n, int sr)   /* f per sample, or constant fc */
{
    float *y = A(n), ph = 0;
    for (int i = 0; i < n; i++) { ph += f ? f[i] : fc; y[i] = 2 * gfrac(ph / sr) - 1; }
    return y;
}
static float *formant(const float *x, int n, int sr, const float fq[3][2], const float g[3])
{
    float *y = A(n);
    for (int k = 0; k < 3; k++) { float *b = bp(x, n, sr, fq[k][0], fq[k][1]); for (int i = 0; i < n; i++) y[i] += g[k] * b[i]; }
    return y;
}
static float *fade(float *x, int n, int sr, float fin, float fout)
{
    int a = (int)(fin * sr), o = (int)(fout * sr);
    for (int i = 0; i < a && i < n; i++) x[i] *= (float)i / (a > 1 ? a - 1 : 1);
    for (int i = 0; i < o && i < n; i++) x[n - o + i] *= 1 - (float)i / (o > 1 ? o - 1 : 1);
    return x;
}
/* small Schroeder reverb (4 combs + 2 allpasses); returns min(cut, n + decay) samples */
static float *reverb(float *x, int n, int sr, float decay, float mix, float damp, int cut, int *outn)
{
    static const float CD[4] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f };
    int L = n + (int)(sr * decay);
    float *xx = A(L), *out = A(L), *y = A(L);
    fade(x, n, sr, 0, 0.06f);
    memcpy(xx, x, sizeof(float) * (size_t)n);
    for (int c = 0; c < 4; c++) {
        int D = (int)(CD[c] * sr);
        float g = gpow(10, -3 * CD[c] / decay);
        memcpy(y, xx, sizeof(float) * (size_t)L);
        for (int i = D; i < L; i++) y[i] += g * y[i - D];
        lp(y, L, sr, damp);
        for (int i = 0; i < L; i++) out[i] += y[i] / 4;
    }
    for (int c = 0; c < 2; c++) {
        int D = (int)((c ? 0.0017f : 0.005f) * sr); if (D < 1) D = 1;
        float g = 0.7f;
        for (int i = 0; i < L; i++) {
            float xd = i >= D ? out[i - D] : 0, yd = i >= D ? y[i - D] : 0;
            y[i] = -g * out[i] + xd + g * yd;
        }
        memcpy(out, y, sizeof(float) * (size_t)L);
    }
    for (int i = 0; i < L; i++) out[i] = xx[i] * (1 - mix) + out[i] * mix;
    *outn = cut < L ? cut : L;
    return out;
}
static int n28(float sec, int sr) { return (int)gfloor(sec * sr / 28 + 0.5f) * 28; }

/* ---------------------------------------------------------------- emit */
static GenSfxFn emit_fn;
static void *emit_user;
static int emit_id;

static void add(float *y, int n, int sr, int loop, float peak)
{
    float m = 0;
    if (!loop) fade(y, n, sr, 0.002f, 0.02f);
    for (int i = 0; i < n; i++) m = gmax(m, gabs(y[i]));
    if (m > 0) for (int i = 0; i < n; i++) y[i] *= peak / m;
    if (n % 28) {                                               /* whole SPU ADPCM blocks */
        if (loop) n -= n % 28;
        else { float *z = A(n + 28 - n % 28); memcpy(z, y, sizeof(float) * (size_t)n); y = z; n += 28 - n % 28; }
    }
    emit_fn(emit_user, emit_id++, y, n, sr, loop);
    pool_free();
}

typedef float *(*LoopFn)(int n);
static float *make_loop(LoopFn fn, int n, int xf)
{
    float *y = fn(n + xf);
    for (int i = 0; i < xf; i++) { float w = (float)i / (xf > 1 ? xf - 1 : 1); y[i] = y[i] * w + y[n + i] * (1 - w); }
    return y;
}

/* ---------------------------------------------------------------- sounds */
static float *hoof(int sr, float weight, unsigned seed, int *pn)
{
    GRng r; grng_seed(&r, seed);
    int n = (int)(0.42f * sr);
    static const float hp_[4] = { 1, 2.76f, 5.40f, 8.93f }, ha[4] = { 0.8f, .5f, .45f, .3f };
    float *th = sweep(n, sr, 95 * gpow(weight, -0.3f), 42, 5), *e1 = ENV(n, sr, 0.045f * weight);
    float *cl = metal(n, sr, grng_uni(&r, 330, 410), 0.09f, hp_, ha, 4), *e2 = ENV(n, sr, 0.05f);
    float *gr = lp(noise(n), n, sr, 2500), *e3 = ENV(n, sr, 0.018f);
    float *ra = bp(noise(n), n, sr, 3200, 3), *e4 = ENV(n, sr, 0.035f), rr = grng_uni(&r, .6f, 1);
    float *y = A(n);
    for (int i = 0; i < n; i++)
        y[i] = gtanh(1.4f * (1.6f * th[i] * e1[i] + 0.45f * cl[i] * e2[i] + 0.7f * gr[i] * e3[i] + 0.25f * ra[i] * e4[i] * rr));
    *pn = n;
    return y;
}

static float engine_fire, engine_harsh, engine_whine;
static float *engine(int n)
{
    int sr = SR_HI;
    float fire = engine_fire, *pulse = A(n), *pm = A(n), mean = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        pulse[i] = gexp(-gfrac(t * fire) * 9) * (1 + 0.25f * gsin(2 * GPI * gfrac(t * fire * 0.5f)));
        mean += pulse[i];
    }
    mean /= n;
    for (int i = 0; i < n; i++) pm[i] = pulse[i] - mean;
    float *b1 = bp(pm, n, sr, fire * 3.1f, 1.2f), *b2 = bp(pulse, n, sr, fire, 1.5f);
    float *ra = hp(noise(n), n, sr, 1500), *ch = bp(noise(n), n, sr, 3800, 4), *y = A(n);
    double s = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        y[i] = b1[i] * 3 + b2[i] * 2 + ra[i] * pulse[i] * engine_harsh + ch[i] * engine_whine
             + engine_whine * 0.3f * gsin(2 * GPI * gfrac(1180 * t)) * (0.6f + 0.4f * pulse[i]);
        s += y[i]; s2 += (double)y[i] * y[i];
    }
    float sd = gsqrt((float)(s2 / n - (s / n) * (s / n)));
    for (int i = 0; i < n; i++) y[i] = gtanh(2.2f * y[i] / (sd * 3 + 1e-9f));
    return y;
}

static float *hum(int n)
{
    int sr = SR_LO;
    float *air = lp(noise(n), n, sr, 300), *y = A(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        y[i] = (gsin(2 * GPI * gfrac(50 * t)) + 0.6f * gsin(2 * GPI * gfrac(100 * t) + 1) + 0.35f * gsin(2 * GPI * gfrac(150 * t) + 2)
                + 0.2f * gsin(2 * GPI * gfrac(250 * t))) * (0.8f + 0.2f * gsin(2 * GPI * t / 1.2f))
             + air[i] * 0.5f + 0.08f * gsin(2 * GPI * gfrac(440 * t)) * (0.5f + 0.5f * gsin(2 * GPI * t / 0.8f));
    }
    return y;
}

static float *reactor(int n)
{
    int sr = SR_LO;
    float *dr = A(n), *sw = sawv(0, 99, n, sr);
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        dr[i] = gsin(2 * GPI * gfrac(33 * t)) + 0.7f * gsin(2 * GPI * gfrac(66.5f * t)) + 0.4f * sw[i] * 0.5f;
    }
    lp(dr, n, sr, 300);
    float *rush = bp(noise(n), n, sr, 500, 1.5f), *y = A(n);
    static const float CF[4] = { 220, 261.6f, 311.1f, 329.6f };
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr, s = gsin(2 * GPI * t / 1.2f), throb = 0.55f + 0.45f * s * s;
        float vib = 1 + 0.01f * gsin(2 * GPI * gfrac(5 * t)), ch = 0;
        for (int k = 0; k < 4; k++) ch += gsin(2 * GPI * gfrac(CF[k] * vib * t) + k);
        y[i] = throb * dr[i] + throb * rush[i] * 0.5f + ch * 0.12f * (0.5f + 0.5f * throb);
    }
    return y;
}

void gen_sfx_each(GenSfxFn fn, void *user)
{
    int n, m;
    float *y, *t1, *t2, *t3, *t4, *e;
    emit_fn = fn; emit_user = user; emit_id = 0;
    grng_seed(&rng, 666);

    /* footsteps: hoof on steel grating */
    y = hoof(SR_HI, 1.0f, 1, &n); add(y, n, SR_HI, 0, 0.95f);
    y = hoof(SR_HI, 1.35f, 2, &n); add(y, (int)(0.36f * SR_HI), SR_HI, 0, 0.95f);

    /* landing: boom + grating crash + saw clatter */
    {
        static const float P[5] = { 1, 2.3f, 3.9f, 6.1f, 8.7f }, Am[5] = { .7f, .6f, .5f, .35f, .2f };
        n = (int)(0.9f * SR_HI);
        float *boom = sweep(n, SR_HI, 120, 30, 5), *eb = ENV(n, SR_HI, 0.18f), *cr = metal(n, SR_HI, 210, 0.35f, P, Am, 5);
        float *gr = lp(noise(n), n, SR_HI, 3000), *eg = ENV(n, SR_HI, 0.05f), *cl = bp(noise(n), n, SR_HI, 2400, 2), *ec = env_exp(n, SR_HI, 0.12f, 0.03f);
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI;
            y[i] = gtanh(1.8f * boom[i] * eb[i] + 0.5f * cr[i] + 0.6f * gr[i] * eg[i] + 0.35f * cl[i] * (gsin(2 * GPI * gfrac(26 * t)) > 0.6f) * ec[i]);
        }
        y = reverb(y, n, SR_HI, 0.8f, 0.25f, 3000, (int)(1.1f * SR_HI), &m); add(y, m, SR_HI, 0, 0.95f);
    }

    /* jump: whoosh + demonic exhale */
    {
        static const float F[3][2] = { { 500, 5 }, { 900, 6 }, { 2400, 8 } }, G[3] = { 1, .5f, .25f };
        n = (int)(0.45f * SR_HI);
        float *wh = bp(noise(n), n, SR_HI, 900, 1.2f), *f = A(n), *src, *sw, *nz = noise(n);
        for (int i = 0; i < n; i++) f[i] = 120 - 30 * ((float)i / SR_HI) / 0.45f;
        sw = sawv(f, 0, n, SR_HI); src = A(n);
        for (int i = 0; i < n; i++) src[i] = sw[i] * 0.5f + nz[i] * 0.5f;
        float *br = formant(src, n, SR_HI, F, G), *eb = env_exp(n, SR_HI, 0.12f, 0.03f);
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI, s = gsin(GPI * gclamp(t / 0.4f, 0, 1));
            y[i] = 1.2f * wh[i] * s * s + 0.9f * br[i] * eb[i];
        }
        add(y, n, SR_HI, 0, 0.95f);
    }

    /* chainsaw loops (two-stroke engine) */
    n = n28(0.581f, SR_HI); engine_fire = 31; engine_harsh = 0.5f; engine_whine = 0.35f;
    add(make_loop(engine, n, n / 8), n, SR_HI, 1, 0.8f);
    n = n28(0.5f, SR_HI); engine_fire = 96; engine_harsh = 1; engine_whine = 1;
    add(make_loop(engine, n, n / 8), n, SR_HI, 1, 0.85f);

    /* chainsaw combo: swing */
    {
        n = (int)(0.5f * SR_HI);
        float *pulse = A(n), *pm = A(n), ph = 0, ph2 = 0, mean = 0;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI;
            ph += 70 + 110 * gclamp(t / 0.18f, 0, 1);
            pulse[i] = gexp(-gfrac(ph / SR_HI) * 7); mean += pulse[i];
        }
        mean /= n;
        for (int i = 0; i < n; i++) pm[i] = pulse[i] - mean;
        float *b = bp(pm, n, SR_HI, 400, 1), *h = hp(noise(n), n, SR_HI, 1500), *air = bp(noise(n), n, SR_HI, 1100, 1);
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI, s = gsin(GPI * gclamp(t / 0.32f, 0, 1));
            ph2 += 900 + 700 * gclamp(t / 0.2f, 0, 1);
            float rev = b[i] * 3 + h[i] * pulse[i] * 1.2f, scream = gsin(2 * GPI * gfrac(ph2 / SR_HI)) * 0.35f;
            float env = gclamp(t / 0.02f, 0, 1) * gexp(-gmax(t - 0.22f, 0) / 0.09f);
            y[i] = gtanh(1.6f * (rev + scream) * env / 3) + air[i] * s * s * 1.6f * 0.6f;
        }
        add(y, n, SR_HI, 0, 0.95f);
    }

    /* slam */
    {
        static const float P[5] = { 1, 2.2f, 3.7f, 5.3f, 7.9f }, Am[5] = { .8f, .7f, .6f, .4f, .3f };
        n = SR_HI;
        float *boom = sweep(n, SR_HI, 140, 32, 6), *eb = ENV(n, SR_HI, 0.22f), *cr = metal(n, SR_HI, 260, 0.45f, P, Am, 5);
        float *gd = bp(noise(n), n, SR_HI, 2600, 2), *eg = env_exp(n, SR_HI, 0.25f, 0.01f), *lo = lp(noise(n), n, SR_HI, 2500), *el = ENV(n, SR_HI, 0.04f);
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI;
            y[i] = gtanh(2.2f * boom[i] * eb[i] + 0.6f * cr[i] + 0.7f * gd[i] * (0.6f + 0.4f * gsign(gsin(2 * GPI * gfrac(38 * t)))) * eg[i] + 0.6f * lo[i] * el[i]);
        }
        y = reverb(y, n, SR_HI, 1.0f, 0.3f, 3000, (int)(1.4f * SR_HI), &m); add(y, m, SR_HI, 0, 0.95f);
    }

    /* blade impacts: grind on metal */
    {
        static const float SF[4] = { 2150, 3310, 4870, 1420 }, SA[4] = { 1, .7f, .45f, .5f };
        static const float P[3] = { 1, 2.3f, 3.9f }, Am[3] = { 1, .6f, .4f };
        n = (int)(0.6f * SR_HI);
        float ph[4];
        for (int k = 0; k < 4; k++) ph[k] = grng_uni(&rng, 0, 6);
        float *cr = hp(noise(n), n, SR_HI, 3000), *gate = A(n);
        for (int i = 0; i < n; i++) gate[i] = grng_f(&rng) > 0.93f;
        float *cl = metal(n, SR_HI, 520, 0.12f, P, Am, 3), *ge = env_exp(n, SR_HI, 0.16f, 0.003f);
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI, sc = 0;
            for (int k = 0; k < 4; k++) sc += gsin(2 * GPI * gfrac(SF[k] * t) + ph[k]) * SA[k];
            sc *= 0.6f + 0.4f * gsign(gsin(2 * GPI * gfrac(47 * t)));
            y[i] = gtanh(1.4f * (0.45f * sc + cr[i] * gate[i] * 2.5f) * ge[i] + 0.8f * cl[i]);
        }
        add(y, n, SR_HI, 0, 0.95f);
    }

    /* rip: the engine bogs down in meat */
    {
        n = (int)(0.55f * SR_HI);
        float *pul = A(n), *pm = A(n), ph = 0, mean = 0;
        for (int i = 0; i < n; i++) {
            ph += 60 + 40 * gexp(-((float)i / SR_HI) / 0.08f);
            pul[i] = gexp(-gfrac(ph / SR_HI) * 6); mean += pul[i];
        }
        mean /= n;
        for (int i = 0; i < n; i++) pm[i] = pul[i] - mean;
        float *b = bp(pm, n, SR_HI, 300, 1), *wet = bp(noise(n), n, SR_HI, 650, 1.3f), *th = sweep(n, SR_HI, 110, 45, 6);
        float *et = ENV(n, SR_HI, 0.08f), *er = env_exp(n, SR_HI, 0.2f, 0.004f);
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI;
            float w = wet[i] * (0.5f + 0.5f * gabs(gsin(2 * GPI * gfrac(23 * t))));
            y[i] = gtanh(1.5f * ((b[i] * 3 + w * 1.4f) * er[i] + 1.4f * th[i] * et[i]));
        }
        add(y, n, SR_HI, 0, 0.95f);
    }

    /* trooper laser pistol */
    {
        n = (int)(0.32f * SR_HI);
        float *p = A(n), a = 0, b = 0;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI, f = 2400 * gexp(-t / 0.05f) + 380;
            a += f; b += f * 1.5f;
            p[i] = gsign(gsin(2 * GPI * gfrac(a / SR_HI))) * 0.4f + gsin(2 * GPI * gfrac(b / SR_HI)) * 0.5f;
        }
        lp(p, n, SR_HI, 5000);
        float *e1 = env_exp(n, SR_HI, 0.07f, 0.002f), *h = hp(noise(n), n, SR_HI, 3000), *e2 = ENV(n, SR_HI, 0.015f);
        for (int i = 0; i < n; i++) p[i] = p[i] * e1[i] + 0.4f * h[i] * e2[i];
        add(p, n, SR_HI, 0, 0.8f);
    }

    /* zap (laser hits) */
    {
        n = (int)(0.3f * SR_HI);
        float *c = hp(noise(n), n, SR_HI, 2000), *e1 = ENV(n, SR_HI, 0.06f), ph = 0;
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI;
            float g = grng_f(&rng) > 0.8f;
            ph += 900 - 600 * t / 0.3f;
            y[i] = (gsin(2 * GPI * gfrac(ph / SR_HI)) * 0.6f + c[i] * g * 2) * e1[i];
        }
        add(y, n, SR_HI, 0, 0.7f);
    }

    /* gore */
    {
        n = (int)(0.8f * SR_HI);
        float *sp = bp(noise(n), n, SR_HI, 420, 1), *es = env_exp(n, SR_HI, 0.09f, 0.002f);
        float *sq = bp(noise(n), n, SR_HI, 900, 3), *eq = env_exp(n, SR_HI, 0.3f, 0.05f);
        float *cr = lp(noise(n), n, SR_HI, 1500), *g = A(n);
        for (int i = 0; i < n; i++) g[i] = grng_f(&rng) > 0.9f;
        float *ec = ENV(n, SR_HI, 0.12f), *sw = sweep(n, SR_HI, 90, 40, 5), *ew = ENV(n, SR_HI, 0.1f);
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI;
            float v = sp[i] * es[i] * 2.5f + sq[i] * (0.5f + 0.5f * gsin(2 * GPI * gfrac(17 * t))) * eq[i] + cr[i] * g[i] * ec[i] * 2;
            y[i] = gtanh(1.4f * v + 1.2f * sw[i] * ew[i]);
        }
        add(y, n, SR_HI, 0, 0.95f);
    }

    /* fatality stinger */
    {
        static const float F[3][2] = { { 500, 3 }, { 900, 4 }, { 2400, 6 } }, G[3] = { 1, .6f, .3f };
        static const float CF[5] = { 110, 130.8f, 164.8f, 207.7f, 220 };
        n = (int)(2.2f * SR_LO);
        float *hs = sweep(n, SR_LO, 75, 30, 3), *eh = ENV(n, SR_LO, 0.6f), *f = A(n), *src = A(n), *sw;
        for (int i = 0; i < n; i++) f[i] = 110 * (1 + 0.008f * gsin(2 * GPI * gfrac(5 * (float)i / SR_LO)));
        sw = sawv(f, 0, n, SR_LO);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_LO, vib = f[i] / 110, ch = 0;
            for (int k = 0; k < 5; k++) ch += gsin(2 * GPI * gfrac(CF[k] * vib * t) + k);
            src[i] = ch * 0.18f + 0.2f * sw[i] * 0.3f;
        }
        float *ch = formant(src, n, SR_LO, F, G);
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_LO;
            y[i] = gtanh(3 * hs[i] * eh[i]) + ch[i] * 3 * gclamp(t / 0.15f, 0, 1) * gexp(-gmax(t - 1.2f, 0) / 0.4f);
        }
        y = reverb(y, n, SR_LO, 2.0f, 0.4f, 2500, (int)(3.0f * SR_LO), &m); add(y, m, SR_LO, 0, 0.85f);
    }

    /* UI: move tick */
    {
        static const float P[3] = { 1, 1.52f, 2.8f }, Am[3] = { 1, .4f, .2f };
        n = (int)(0.12f * SR_HI);
        t1 = metal(n, SR_HI, 1650, 0.03f, P, Am, 3); t2 = sweep(n, SR_HI, 400, 120, 6); e = ENV(n, SR_HI, 0.02f);
        for (int i = 0; i < n; i++) t1[i] += 0.4f * t2[i] * e[i];
        add(t1, n, SR_HI, 0, 0.6f);
    }
    /* UI: confirm bell */
    {
        static const float P[5] = { 1, 2.0f, 2.97f, 4.2f, 5.43f }, Am[5] = { 1, .7f, .5f, .4f, .25f };
        n = (int)(0.9f * SR_HI);
        t1 = metal(n, SR_HI, 196, 0.6f, P, Am, 5); t2 = sweep(n, SR_HI, 90, 41, 6); e = ENV(n, SR_HI, 0.15f);
        t3 = bp(noise(n), n, SR_HI, 700, 2);
        y = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_HI;
            y[i] = 0.7f * t1[i] + 0.8f * gtanh(3 * t2[i] * e[i]) + 0.3f * t3[i] * gclamp(1 - t / 0.25f, 0, 1) * (t < 0.25f);
        }
        y = reverb(y, n, SR_HI, 1.4f, 0.4f, 3000, (int)(1.3f * SR_HI), &m); add(y, m, SR_HI, 0, 0.8f);
    }
    /* pause gong */
    {
        static const float P[5] = { 1, 1.47f, 2.09f, 2.56f, 3.39f }, Am[5] = { 1, .8f, .6f, .4f, .3f };
        n = (int)(0.7f * SR_HI);
        t1 = metal(n, SR_HI, 98, 0.7f, P, Am, 5); t2 = sweep(n, SR_HI, 70, 35, 4); e = ENV(n, SR_HI, 0.2f);
        for (int i = 0; i < n; i++) t1[i] = 0.8f * t1[i] + 0.8f * gtanh(2 * t2[i] * e[i]);
        y = reverb(t1, n, SR_HI, 1.0f, 0.35f, 3000, (int)(1.1f * SR_HI), &m); add(y, m, SR_HI, 0, 0.75f);
    }

    /* ambience loops (11 kHz: all low end) */
    n = n28(2.4f, SR_LO);
    add(make_loop(hum, n, n / 4), n, SR_LO, 1, 0.7f);
    add(make_loop(reactor, n28(2.4f, SR_LO), n28(0.6f, SR_LO)), n28(2.4f, SR_LO), SR_LO, 1, 0.75f);

    {   /* steam burst */
        n = (int)(1.3f * SR_LO);
        t1 = hp(noise(n), n, SR_LO, 1800); t2 = bp(noise(n), n, SR_LO, 2600, 3);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_LO, env = gclamp(t / 0.05f, 0, 1) * gexp(-gmax(t - 0.25f, 0) / 0.3f);
            t1[i] = t1[i] * env + 0.4f * t2[i] * env;
        }
        add(t1, n, SR_LO, 0, 0.7f);
    }
    {   /* distant clank */
        static const float P[4] = { 1, 2.4f, 3.7f, 5.9f }, Am[4] = { 1, .7f, .5f, .3f };
        n = (int)(0.5f * SR_LO);
        t1 = metal(n, SR_LO, 140, 0.25f, P, Am, 4); t2 = lp(noise(n), n, SR_LO, 900); e = ENV(n, SR_LO, 0.03f);
        for (int i = 0; i < n; i++) t1[i] += 0.6f * t2[i] * e[i];
        y = reverb(t1, n, SR_LO, 2.2f, 0.6f, 1800, (int)(2.4f * SR_LO), &m); add(y, m, SR_LO, 0, 0.7f);
    }
    {   /* captive shinigami wail */
        static const float F[3][2] = { { 480, 7 }, { 820, 9 }, { 2300, 12 } }, G[3] = { 1, .6f, .2f };
        n = (int)(2.2f * SR_LO);
        float *p = A(n), *p2 = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_LO;
            p[i] = 175 - 45 * (t / 2.2f) + 6 * gsin(2 * GPI * gfrac(5.5f * t)) + 25 * gexp(-t / 0.3f);
            p2[i] = p[i] * 1.006f;
        }
        t1 = sawv(p, 0, n, SR_LO); t2 = sawv(p2, 0, n, SR_LO);
        for (int i = 0; i < n; i++) t1[i] += 0.5f * t2[i];
        t3 = formant(t1, n, SR_LO, F, G);
        for (int i = 0; i < n; i++) {
            float s = gsin(GPI * gclamp((float)i / SR_LO / 2.2f, 0, 1));
            t3[i] *= gpow(gmax(s, 0), 1.5f);
        }
        y = reverb(t3, n, SR_LO, 2.5f, 0.55f, 2200, (int)(3.4f * SR_LO), &m); add(y, m, SR_LO, 0, 0.7f);
    }
    {   /* alarm */
        n = (int)(1.6f * SR_LO);
        t4 = A(n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR_LO;
            t4[i] = (gsign(gsin(2 * GPI * gfrac(330 * t))) * 0.5f + gsin(2 * GPI * gfrac(660 * t))) * (gfrac(t / 0.4f) * 0.4f < 0.22f) * (t < 1.2f);
        }
        lp(t4, n, SR_LO, 1500);
        y = reverb(t4, n, SR_LO, 1.5f, 0.5f, 1500, (int)(2.0f * SR_LO), &m); add(y, m, SR_LO, 0, 0.55f);
    }
}

/* ---------------------------------------------------------------- runtime: 8-bit PCM for the mixer */
SfxPcm sfx_pcm[SFX_COUNT];

static void keep_pcm(void *user, int id, const float *y, int n, int sr, int loop)
{
    signed char *d = (signed char *)malloc((size_t)n);
    (void)user;
    for (int i = 0; i < n; i++) { float v = y[i] * 127; d[i] = (signed char)(v < 0 ? v - 0.5f : v + 0.5f); }
    sfx_pcm[id].data = d; sfx_pcm[id].len = n; sfx_pcm[id].rate = sr; sfx_pcm[id].loop = loop;
}

void gen_sfx(void) { if (!sfx_pcm[0].data) gen_sfx_each(keep_pcm, 0); }
