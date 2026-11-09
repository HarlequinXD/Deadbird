/* the heavier float functions, out of line (smaller code) */
#include "gmath.h"

float gsin(float x)
{
    x = x * (1.f / (2 * GPI));
    x -= gfloor(x + 0.5f);                    /* -0.5 .. 0.5 turns */
    x *= 2 * GPI;
    float x2 = x * x;                         /* odd Taylor to x^11: < 1e-6 on [-pi, pi] */
    return x * (1 + x2 * (-1.f / 6 + x2 * (1.f / 120 + x2 * (-1.f / 5040 + x2 * (1.f / 362880 - x2 / 39916800.f)))));
}

float gcos(float x) { return gsin(x + GPI / 2); }

float gexp(float x)
{
    if (x < -80) return 0;
    if (x > 80) x = 80;
    int k = (int)gfloor(x * 1.44269504f + 0.5f);
    float r = x - k * 0.69314718f;            /* |r| <= ln2/2 */
    float p = 1 + r * (1 + r * (0.5f + r * (1.f / 6 + r * (1.f / 24 + r * (1.f / 120 + r / 720.f)))));
    union { float f; int i; } u; u.i = (k + 127) << 23;
    return p * u.f;
}

float glog(float x)
{
    if (x <= 0) return -80;
    union { float f; int i; } u; u.f = x;
    int e = ((u.i >> 23) & 255) - 127;
    u.i = (u.i & 0x7fffff) | (127 << 23);     /* m in [1,2) */
    float m = u.f;
    if (m > 1.41421356f) { m *= 0.5f; e++; }
    float s = (m - 1) / (m + 1), s2 = s * s;
    return e * 0.69314718f + 2 * s * (1 + s2 * (1.f / 3 + s2 * (1.f / 5 + s2 * (1.f / 7 + s2 / 9.f))));
}

float gpow(float x, float y) { return x <= 0 ? 0 : gexp(y * glog(x)); }

float gtanh(float x)
{
    if (x > 9) return 1;
    if (x < -9) return -1;
    float e = gexp(2 * x);
    return (e - 1) / (e + 1);
}

float gsqrt(float x)
{
    if (x <= 0) return 0;
    union { float f; int i; } u; u.f = x;
    u.i = 0x1fbd1df5 + (u.i >> 1);
    float y = u.f;
    y = 0.5f * (y + x / y); y = 0.5f * (y + x / y); y = 0.5f * (y + x / y);
    return y;
}

float gatan2(float y, float x)
{
    float ax = gabs(x), ay = gabs(y), a = gmin(ax, ay) / (gmax(ax, ay) + 1e-20f), s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = GPI / 2 - r;
    if (x < 0) r = GPI - r;
    return y < 0 ? -r : r;
}
