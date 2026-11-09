/*
 * Just enough C runtime for Deadbird.exe, so the MSVC build doesn't link the whole static CRT
 * (~100 KB of start-up, stdio and locale code the game never uses): heap via HeapAlloc, the few
 * string / sort helpers, memcpy / memset (rep movsb / stosb), sqrt and the entry point.
 * Built only by build_msvc.bat (with /NODEFAULTLIB).
 */
#include <windows.h>
#include <string.h>
#include <intrin.h>
#include <emmintrin.h>

int _fltused = 0x9875;                       /* tells the linker floating point is in use */

#pragma function(strlen, strcmp, memset, memcpy, memcmp, memmove, sqrt)
void *memset(void *d, int c, size_t n) { __stosb((unsigned char *)d, (unsigned char)c, n); return d; }
void *memcpy(void *d, const void *s, size_t n) { __movsb((unsigned char *)d, (const unsigned char *)s, n); return d; }
double sqrt(double x) { return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(x), _mm_set_sd(x))); }
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *o = (unsigned char *)d; const unsigned char *i = (const unsigned char *)s;
    if (o < i) while (n--) *o++ = *i++;
    else { o += n; i += n; while (n--) *--o = *--i; }
    return d;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (unsigned char)*a - (unsigned char)*b; }

void *malloc(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n ? n : 1); }
void *calloc(size_t a, size_t b) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, a * b ? a * b : 1); }
void free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
void *realloc(void *p, size_t n) { return p ? HeapReAlloc(GetProcessHeap(), 0, p, n ? n : 1) : malloc(n); }

/* shell sort: only used at start-up, on a few thousand items */
void qsort(void *base, size_t n, size_t w, int (*cmp)(const void *, const void *))
{
    unsigned char *b = (unsigned char *)base, tmp[64];
    size_t gap, i, j, k;
    for (gap = n / 2; gap > 0; gap = gap == 2 ? 1 : gap * 5 / 11)
        for (i = gap; i < n; i++)
            for (j = i; j >= gap && cmp(b + (j - gap) * w, b + j * w) > 0; j -= gap)
                for (k = 0; k < w; k += sizeof(tmp)) {
                    size_t m = w - k < sizeof(tmp) ? w - k : sizeof(tmp);
                    memcpy(tmp, b + (j - gap) * w + k, m);
                    memcpy(b + (j - gap) * w + k, b + j * w + k, m);
                    memcpy(b + j * w + k, tmp, m);
                }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show);
void __stdcall start(void) { ExitProcess((UINT)WinMain(GetModuleHandleA(0), 0, GetCommandLineA(), SW_SHOWDEFAULT)); }
