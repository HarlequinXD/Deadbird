/*
 * Tiny float maths for the asset generators: no libm, so every platform (and the PS1 dump tool)
 * produces the same assets, and nothing extra gets linked into the small builds.
 */
#ifndef GMATH_H
#define GMATH_H

#define GPI 3.14159265358979f

static inline float gfloor(float x) { int i = (int)x; return (float)(i - (x < (float)i)); }
static inline float gfrac(float x) { return x - gfloor(x); }
static inline float gabs(float x) { return x < 0 ? -x : x; }
static inline float gmin(float a, float b) { return a < b ? a : b; }
static inline float gmax(float a, float b) { return a > b ? a : b; }
static inline float gclamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float gsign(float x) { return x > 0 ? 1.f : x < 0 ? -1.f : 0.f; }

float gsin(float x);
float gcos(float x);

float gexp(float x);

float glog(float x);

float gpow(float x, float y);
float gtanh(float x);
float gsqrt(float x);
float gatan2(float y, float x);

/* deterministic random numbers (xorshift32) */
typedef struct { unsigned s; } GRng;
static inline void grng_seed(GRng *r, unsigned seed) { r->s = seed * 2654435761u + 0x9e3779b9u; if (!r->s) r->s = 1; }
static inline unsigned grng_u32(GRng *r) { unsigned x = r->s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return r->s = x; }
static inline float grng_f(GRng *r) { return (grng_u32(r) >> 8) * (1.f / 16777216.f); }       /* 0..1 */
static inline float grng_uni(GRng *r, float a, float b) { return a + (b - a) * grng_f(r); }
static inline int grng_int(GRng *r, int a, int b) { return a + (int)(grng_u32(r) % (unsigned)(b - a)); } /* [a,b) */
static inline float grng_norm(GRng *r) { float s = 0; for (int i = 0; i < 6; i++) s += grng_f(r); return (s - 3) * 1.41421356f; }

#endif
