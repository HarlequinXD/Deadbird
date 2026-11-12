/*
 * The editor's map: .map text <-> EmMap <-> MapDef. A C port of tools/make_map.py's parse,
 * finish and emit steps, so a map compiled here is byte-for-byte what make_map.py makes (the
 * headless edtest checks that on level 1). See edmap.h.
 */
#ifndef GEN_BAKED
#include "edmap.h"
#include "gen_lab.h"
#include <string.h>
#include <stdlib.h>

char em_error[96];
const char *const em_prop_names[PK_COUNT] = { "box", "boxf", "crate", "can", "pillar", "console", "machine", "pod", "rack",
                                              "fan", "rail", "cyl", "tank", "core", "cell" };
const char *const em_thing_names[] = { "start", "trooper", "key", "shard", "exit", "way" };
static const char *const KEYN[4] = { "", "cyan", "amber", "crimson" };
static const char DIRC[4] = { 'n', 'e', 's', 'w' };
static const int ND_OF_DIR[4] = { ND_PZ, ND_PX, ND_NZ, ND_NX };      /* n e s w -> wall normal code */

/* ------------------------------------------------------------------ small text helpers (no stdio: the
   Windows build has no C runtime) */
static int seq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static void scpy(char *d, const char *s, int cap) { int i = 0; while (s[i] && i < cap - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static char up_(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

static void err(int line, const char *what, const char *tok)
{
    char *o = em_error; int n = 0, k;
    const char *p = "line ";
    char num[12]; int nn = 0;
    while (*p && n < 90) o[n++] = *p++;
    do { num[nn++] = (char)('0' + line % 10); line /= 10; } while (line && nn < 11);
    while (nn && n < 90) o[n++] = num[--nn];
    o[n++] = ':'; o[n++] = ' ';
    for (k = 0; what[k] && n < 92; k++) o[n++] = what[k];
    if (tok) { o[n++] = ' '; for (k = 0; tok[k] && n < 94; k++) o[n++] = tok[k]; }
    o[n] = 0;
}
static void err0(const char *what) { scpy(em_error, what, sizeof(em_error)); }

/* round(num / den) with ties to even (Python's round), den > 0 */
static long long rdiv(long long num, long long den)
{
    long long q = num / den, r = num % den;
    if (r < 0) { r += den; q--; }
    if (2 * r > den || (2 * r == den && (q & 1))) q++;
    return q;
}
static long long fdiv(long long a, long long b) { long long q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

/* a decimal number as num / den (den a power of ten) */
static int dec(const char *s, long long *num, long long *den)
{
    long long n = 0, d = 1;
    int neg = 0, any = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s++ - '0'); any = 1; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { if (d < 100000000) { n = n * 10 + (*s - '0'); d *= 10; } s++; any = 1; }
    }
    if (!any || *s) return 0;
    *num = neg ? -n : n; *den = d;
    return 1;
}
/* value * mul / div, rounded like Python */
static int num_s(const char *s, long long mul, long long dv, s32 *out)
{
    long long n, d;
    if (!dec(s, &n, &d)) return 0;
    *out = (s32)rdiv(n * mul, d * dv);
    return 1;
}
static int mtr(const char *s, s32 *out) { return num_s(s, 1024, 1, out); }
static int inte(const char *s, s32 *out)
{
    long long n, d;
    if (!dec(s, &n, &d) || d != 1) return 0;
    *out = (s32)n; return 1;
}
/* "x,z" -> two values in metres */
static int pair_m(const char *s, s32 *a, s32 *b)
{
    char t[40]; int i = 0;
    while (s[i] && s[i] != ',' && i < 39) { t[i] = s[i]; i++; }
    if (s[i] != ',') return 0;
    t[i] = 0;
    return mtr(t, a) && mtr(s + i + 1, b);
}
static int pair_i(const char *s, s32 *a, s32 *b)
{
    char t[40]; int i = 0;
    while (s[i] && s[i] != ',' && i < 39) { t[i] = s[i]; i++; }
    if (s[i] != ',') return 0;
    t[i] = 0;
    return inte(t, a) && inte(s + i + 1, b);
}

int em_find_tile(const char *name)
{
    char u[32]; int i, k = 0;
    if (name[0] == 'L' && name[1] == 'T' && name[2] == '_') name += 3;
    else if ((name[0] == 'l' || name[0] == 'L') && (name[1] == 't' || name[1] == 'T') && name[2] == '_') name += 3;
    for (i = 0; name[i] && k < 31; i++) u[k++] = up_(name[i]);
    u[k] = 0;
    if (seq(u, "SERVER_RACK")) return LT_SERVER_RACK;
    if (seq(u, "CATWALK")) return LT_CATWALK;
    if (seq(u, "GENERATOR_SIDE")) return LT_GENERATOR_SIDE;
    for (i = 0; i < em_tile_count; i++) if (seq(u, em_tile_names[i])) return i;
    return -1;
}
static int tile_tok(const char *t, int *out)
{
    int v;
    if (seq(t, "-")) { *out = 255; return 1; }
    v = em_find_tile(t);
    if (v < 0) return 0;
    *out = v; return 1;
}

/* ------------------------------------------------------------------ geometry */
s32 em_area2(const EmSector *s)
{
    long long a = 0;
    int i;
    for (i = 0; i < s->np; i++) {
        int j = (i + 1) % s->np;
        a += (long long)s->x[i] * s->z[j] - (long long)s->x[j] * s->z[i];
    }
    a >>= 10;                                    /* (in 1/1024 m^2 units; only the sign and rough size matter) */
    return (s32)(a > 2000000000 ? 2000000000 : a < -2000000000 ? -2000000000 : a);
}
static long long area2x(const EmSector *s)
{
    long long a = 0;
    int i;
    for (i = 0; i < s->np; i++) {
        int j = (i + 1) % s->np;
        a += (long long)s->x[i] * s->z[j] - (long long)s->x[j] * s->z[i];
    }
    return a;
}

int em_inside(const EmSector *s, s32 x, s32 z)
{
    int i, j, c = 0;
    for (i = 0, j = s->np - 1; i < s->np; j = i++) {
        if ((s->z[i] > z) != (s->z[j] > z)) {
            long long lhs = (long long)(x - s->x[i]) * (s->z[j] - s->z[i]);
            long long rhs = (long long)(z - s->z[i]) * (s->x[j] - s->x[i]);
            if (s->z[j] > s->z[i] ? lhs < rhs : lhs > rhs) c ^= 1;
        }
    }
    return c;
}
int em_sector_at(const EmMap *m, s32 x, s32 z)
{
    int i;
    for (i = 0; i < m->nsecs; i++) if (m->secs[i].np >= 3 && em_inside(&m->secs[i], x, z)) return i;
    return -1;
}

/* plane height (up-positive) of a sector at (x, z), like gen_lab's sec_plane (integer: distances
   are measured unnormalised along the hinge wall's inward normal, so the length cancels out) */
static int plane_at(const EmSector *s, int ceil, s32 x, s32 z)
{
    int w = ceil ? s->cslope : s->fslope, i;
    long long h = ceil ? s->ceil : s->floor, nx, nz, maxd = 0, d;
    if (s->kind == SK_DOOR && ceil) h = s->floor + s->ka;
    if (w < 0 || w >= s->np) return (int)h;
    {
        int j = (w + 1) % s->np;
        nx = -(s->z[j] - s->z[w]); nz = s->x[j] - s->x[w];
    }
    for (i = 0; i < s->np; i++) { d = (s->x[i] - s->x[w]) * nx + (s->z[i] - s->z[w]) * nz; if (d > maxd) maxd = d; }
    if (maxd <= 0) return (int)h;
    d = (x - s->x[w]) * nx + (z - s->z[w]) * nz;
    return (int)(h + ((ceil ? s->cslope_h : s->fslope_h) - h) * d / maxd);
}
int em_floor_at(const EmMap *m, int si, s32 x, s32 z) { return plane_at(&m->secs[si], 0, x, z); }
int em_ceil_at(const EmMap *m, int si, s32 x, s32 z) { return plane_at(&m->secs[si], 1, x, z); }

void em_fix_winding(EmSector *s)
{
    int n = s->np, i;
    if (n < 3 || area2x(s) >= 0) return;
    {
        s16 x[EM_SV], z[EM_SV]; EmWall w[EM_SV];
        for (i = 0; i < n; i++) { x[i] = s->x[n - 1 - i]; z[i] = s->z[n - 1 - i]; }
        for (i = 0; i < n; i++) w[((n - 2 - i) % n + n) % n] = s->w[i];
        memcpy(s->x, x, sizeof(x[0]) * (size_t)n); memcpy(s->z, z, sizeof(z[0]) * (size_t)n); memcpy(s->w, w, sizeof(w[0]) * (size_t)n);
        if (s->fslope >= 0) s->fslope = (s8)(((n - 2 - s->fslope) % n + n) % n);
        if (s->cslope >= 0) s->cslope = (s8)(((n - 2 - s->cslope) % n + n) % n);
    }
}
/* wall cells follow their walls when a sector is turned round (needs the map) */
static void fix_winding_m(EmMap *m, int si)
{
    EmSector *s = &m->secs[si];
    int n = s->np, i;
    if (n < 3 || area2x(s) >= 0) return;
    em_fix_winding(s);
    for (i = 0; i < m->nwcells; i++) if (m->wcells[i].sec == si) m->wcells[i].wall = (u8)(((n - 2 - m->wcells[i].wall) % n + n) % n);
}

int em_rows_new(EmMap *m, const EmRows *r)
{
    if (m->nrows >= EM_ROWS) { em_rows_gc(m); if (m->nrows >= EM_ROWS) return -1; }
    m->rows[m->nrows] = *r;
    return m->nrows++;
}
void em_rows_gc(EmMap *m)
{
    static s16 remap[EM_ROWS];
    int i, w, n = 0;
    for (i = 0; i < m->nrows; i++) remap[i] = -1;
#define MARK(v) do { if ((v) >= 0) remap[v] = 1; } while (0)
    for (i = 0; i < m->nsecs; i++) {
        EmSector *s = &m->secs[i];
        MARK(s->walls); MARK(s->lows); MARK(s->ups);
        for (w = 0; w < s->np; w++) { MARK(s->w[w].mid); MARK(s->w[w].low); MARK(s->w[w].up); }
    }
#undef MARK
    for (i = 0; i < m->nrows; i++) if (remap[i] >= 0) { m->rows[n] = m->rows[i]; remap[i] = (s16)n++; }
#define MAP_(v) do { if ((v) >= 0) (v) = remap[v]; } while (0)
    for (i = 0; i < m->nsecs; i++) {
        EmSector *s = &m->secs[i];
        MAP_(s->walls); MAP_(s->lows); MAP_(s->ups);
        for (w = 0; w < s->np; w++) { MAP_(s->w[w].mid); MAP_(s->w[w].low); MAP_(s->w[w].up); }
    }
#undef MAP_
    m->nrows = n;
}

int em_wall_rows(const EmMap *m, int si, int w, int part)
{
    const EmSector *s = &m->secs[si];
    if (part == EM_LOW) return s->w[w].low >= 0 ? s->w[w].low : s->lows;
    if (part == EM_UP) return s->w[w].up >= 0 ? s->w[w].up : s->ups;
    return s->w[w].mid >= 0 ? s->w[w].mid : s->walls;
}

static void sector_defaults(EmSector *s)
{
    int i;
    memset(s, 0, sizeof(*s));
    s->fslope = s->cslope = -1;
    s->f.def = LT_FLOOR_PLATE; s->c.def = LT_CEILING_PANEL;
    s->walls = s->lows = s->ups = -1;
    for (i = 0; i < EM_SV; i++) { s->w[i].mid = s->w[i].low = s->w[i].up = -1; s->w[i].flags = 0; }
}

int em_insert_point(EmMap *m, int si, int at, s32 x, s32 z)
{
    EmSector *s = &m->secs[si];
    int i, n = s->np;
    if (n >= EM_SV || at < 0 || at > n) return 0;
    for (i = n; i > at; i--) { s->x[i] = s->x[i - 1]; s->z[i] = s->z[i - 1]; s->w[i] = s->w[i - 1]; }
    s->x[at] = (s16)x; s->z[at] = (s16)z;
    s->w[at] = at > 0 ? s->w[at - 1] : s->w[n];       /* both halves of the split wall keep its settings */
    s->np = (u8)(n + 1);
    for (i = 0; i < m->nwcells; i++) if (m->wcells[i].sec == si && m->wcells[i].wall >= at) m->wcells[i].wall++;
    if (s->fslope >= at) s->fslope++;
    if (s->cslope >= at) s->cslope++;
    return 1;
}
void em_delete_point(EmMap *m, int si, int at)
{
    EmSector *s = &m->secs[si];
    int i, n = s->np, k = 0;
    if (n <= 3 || at < 0 || at >= n) return;
    for (i = at; i < n - 1; i++) { s->x[i] = s->x[i + 1]; s->z[i] = s->z[i + 1]; s->w[i] = s->w[i + 1]; }
    s->np = (u8)(n - 1);
    for (i = 0; i < m->nwcells; i++) {
        EmWCell c = m->wcells[i];
        if (c.sec == si && c.wall == at) continue;
        if (c.sec == si && c.wall > at) c.wall--;
        m->wcells[k++] = c;
    }
    m->nwcells = k;
    if (s->fslope == at) s->fslope = -1; else if (s->fslope > at) s->fslope--;
    if (s->cslope == at) s->cslope = -1; else if (s->cslope > at) s->cslope--;
    if (s->fslope >= s->np) s->fslope = -1;
    if (s->cslope >= s->np) s->cslope = -1;
}
void em_delete_sector(EmMap *m, int si)
{
    int i, k;
    if (si < 0 || si >= m->nsecs) return;
    for (i = si; i < m->nsecs - 1; i++) m->secs[i] = m->secs[i + 1];
    m->nsecs--;
    for (i = k = 0; i < m->nwcells; i++) {
        EmWCell c = m->wcells[i];
        if (c.sec == si) continue;
        if (c.sec > si) c.sec--;
        m->wcells[k++] = c;
    }
    m->nwcells = k;
    for (i = k = 0; i < m->nfcells; i++) {
        EmFCell c = m->fcells[i];
        if (c.sec == si) continue;
        if (c.sec > si) c.sec--;
        m->fcells[k++] = c;
    }
    m->nfcells = k;
}
int em_add_sector(EmMap *m, const s32 *x, const s32 *z, int n, int like)
{
    EmSector *s;
    int i;
    if (m->nsecs >= EM_SECS || n < 3 || n > EM_SV) return -1;
    s = &m->secs[m->nsecs];
    if (like >= 0 && like < m->nsecs) {
        *s = m->secs[like];
        for (i = 0; i < EM_SV; i++) { s->w[i].mid = s->w[i].low = s->w[i].up = -1; s->w[i].flags = 0; }
        s->fslope = s->cslope = -1;
        if (s->kind != SK_ROOM) { s->kind = SK_ROOM; s->lock = 0; s->flags = 0; s->ceil = (s16)(s->floor + 4096); }
    } else {
        sector_defaults(s);
        s->ceil = 4096;
        {   /* the starter look: plain base, panels, beam under the ceiling */
            EmRows r; int k;
            memset(&r, 0, sizeof(r)); r.nrows = 2; r.top = LT_TOP_BEAM;
            r.n[0] = 2; r.t[0][0] = LT_BASE_PLAIN; r.t[0][1] = LT_BASE_PIPES; r.n[1] = 1; r.t[1][0] = LT_MID_PANEL;
            k = em_rows_new(m, &r); s->walls = (s16)k;
            memset(&r, 0, sizeof(r)); r.nrows = 1; r.top = 255; r.n[0] = 1; r.t[0][0] = LT_PLATE_SIDE;
            s->lows = (s16)em_rows_new(m, &r);
            memset(&r, 0, sizeof(r)); r.nrows = 1; r.top = 255; r.n[0] = 1; r.t[0][0] = LT_TOP_BEAM;
            s->ups = (s16)em_rows_new(m, &r);
        }
    }
    {   /* a fresh name */
        int k = m->nsecs + 1, tries;
        for (tries = 0; tries < 1000; tries++, k++) {
            char nm[16] = "s", d[8]; int nd = 0, j, l = 1, clash = 0;
            int v = k;
            do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 7);
            while (nd) nm[l++] = d[--nd];
            nm[l] = 0;
            for (j = 0; j < m->nsecs; j++) if (seq(m->secs[j].name, nm)) clash = 1;
            if (!clash) { scpy(s->name, nm, 16); break; }
        }
    }
    s->np = (u8)n;
    for (i = 0; i < n; i++) { s->x[i] = (s16)x[i]; s->z[i] = (s16)z[i]; }
    m->nsecs++;
    fix_winding_m(m, m->nsecs - 1);
    return m->nsecs - 1;
}

/* is p strictly inside segment a-b (on it, not at the ends)? within a unit of the line */
static int on_seg(s32 ax, s32 az, s32 bx, s32 bz, s32 px, s32 pz)
{
    long long dx = bx - ax, dz = bz - az, cr = dx * (pz - az) - dz * (px - ax), L2 = dx * dx + dz * dz, t;
    if ((px == ax && pz == az) || (px == bx && pz == bz)) return 0;
    if (cr * cr > L2) return 0;                                    /* more than a unit off the line */
    t = dx * (px - ax) + dz * (pz - az);
    return t > 0 && t < L2;
}
int em_weld(EmMap *m)
{
    int added = 0, pass, si, i, oj, k;
    for (pass = 0; pass < 8; pass++) {
        int any = 0;
        for (si = 0; si < m->nsecs; si++) {
            EmSector *s = &m->secs[si];
            for (i = 0; i < s->np; i++) {
                int j = (i + 1) % s->np, best = -1;
                s32 bx = 0, bz = 0; long long bt = 0;
                for (oj = 0; oj < m->nsecs; oj++) {
                    const EmSector *o = &m->secs[oj];
                    if (oj == si) continue;
                    for (k = 0; k < o->np; k++)
                        if (on_seg(s->x[i], s->z[i], s->x[j], s->z[j], o->x[k], o->z[k])) {
                            long long t = (long long)(o->x[k] - s->x[i]) * (o->x[k] - s->x[i]) + (long long)(o->z[k] - s->z[i]) * (o->z[k] - s->z[i]);
                            if (best < 0 || t < bt) { best = 1; bt = t; bx = o->x[k]; bz = o->z[k]; }
                        }
                }
                if (best >= 0 && s->np < EM_SV) { em_insert_point(m, si, i + 1, bx, bz); added++; any = 1; }
            }
        }
        if (!any) break;
    }
    return added;
}

/* ------------------------------------------------------------------ props */
int em_prop_xz(const EmProp *p, s32 *x, s32 *z)
{
    switch (p->kind) {
    case PK_BOX: case PK_BOXF: case PK_MACHINE: case PK_RAIL: *x = (p->x0 + p->x1) / 2; *z = (p->z0 + p->z1) / 2; return 1;
    default: *x = p->x0; *z = p->z0; return 1;
    }
}
void em_prop_move(EmProp *p, s32 dx, s32 dz)
{
    p->x0 = (s16)(p->x0 + dx); p->z0 = (s16)(p->z0 + dz);
    if (p->kind == PK_BOX || p->kind == PK_BOXF || p->kind == PK_MACHINE || p->kind == PK_RAIL) { p->x1 = (s16)(p->x1 + dx); p->z1 = (s16)(p->z1 + dz); }
}
int em_prop_defaults(EmProp *p, int kind, s32 x, s32 z)
{
    memset(p, 0, sizeof(*p));
    p->kind = (u8)kind; p->x0 = (s16)x; p->z0 = (s16)z; p->t0 = p->t1 = p->t2 = 255; p->rep = 1; p->axis = 1;
    switch (kind) {
    case PK_BOX: case PK_BOXF:
        p->x0 = (s16)(x - 512); p->z0 = (s16)(z - 512); p->x1 = (s16)(x + 512); p->z1 = (s16)(z + 512); p->h0 = 0; p->h1 = 1024;
        p->t0 = LT_DARK_METAL; p->t1 = LT_MACHINE_BASE; p->v0 = 1000;
        if (kind == PK_BOXF) { p->t2 = LT_MACHINE_PANEL; p->dir = 0; }
        break;
    case PK_CRATE: p->h0 = 1024; p->h1 = 1024; break;
    case PK_PILLAR: p->h0 = 4096; break;
    case PK_CONSOLE: p->t0 = 0; break;
    case PK_MACHINE: p->x0 = (s16)(x - 1024); p->z0 = (s16)(z - 512); p->x1 = (s16)(x + 1024); p->z1 = (s16)(z + 512); p->h0 = 1536; break;
    case PK_POD: p->v0 = 0; break;
    case PK_FAN: p->h0 = 3072; p->h1 = 1024; p->v0 = 40; break;
    case PK_RAIL: p->x0 = (s16)(x - 1024); p->x1 = (s16)(x + 1024); p->z1 = (s16)z; p->h0 = 0; break;
    case PK_CYL: p->h0 = 0; p->h1 = 512; p->h2 = 2048; p->v0 = 8; p->t0 = LT_DARK_METAL; p->axis = 1; break;
    default: break;
    }
    return 1;
}

/* ------------------------------------------------------------------ parse */
#define MAXTOK 96
static char lbuf[1024];
static char *tok[MAXTOK];
static int ntok;

static void split_line(const char *s, int n)
{
    int i = 0, k = 0;
    if (n > (int)sizeof(lbuf) - 1) n = (int)sizeof(lbuf) - 1;
    for (i = 0; i < n && s[i] != '#'; i++) lbuf[i] = (s[i] == '\t' || s[i] == '\r') ? ' ' : s[i];
    lbuf[i] = 0;
    ntok = 0;
    while (lbuf[k]) {
        while (lbuf[k] == ' ') k++;
        if (!lbuf[k]) break;
        if (ntok < MAXTOK) tok[ntok++] = &lbuf[k];
        while (lbuf[k] && lbuf[k] != ' ') k++;
        if (lbuf[k]) lbuf[k++] = 0;
    }
}

static int rows_from(EmMap *m, int from, int *out, int line)
{
    EmRows r; int i, cur = 0;
    memset(&r, 0, sizeof(r)); r.top = 255;
    for (i = from; i < ntok; i++) {
        if (seq(tok[i], "/")) { if (r.n[cur]) { cur++; if (cur >= EM_NROWS) { err(line, "too many rows", 0); return 0; } } continue; }
        if (seq(tok[i], "top")) {
            int t;
            if (i + 1 >= ntok || !tile_tok(tok[i + 1], &t)) { err(line, "bad top tile", i + 1 < ntok ? tok[i + 1] : ""); return 0; }
            r.top = (u8)t; i++; continue;
        }
        {
            int t;
            if (!tile_tok(tok[i], &t)) { err(line, "unknown tile", tok[i]); return 0; }
            if (r.n[cur] >= EM_ROWLEN) { err(line, "too many tiles in a row", 0); return 0; }
            r.t[cur][r.n[cur]++] = (u8)t;
        }
    }
    r.nrows = (u8)(r.n[cur] ? cur + 1 : cur);
    *out = em_rows_new(m, &r);
    if (*out < 0) { err(line, "too many row specs", 0); return 0; }
    return 1;
}

static int wall_list(const char *s, int n, u8 *on, int line)
{
    char part[24]; int i = 0;
    memset(on, 0, EM_SV);
    while (*s) {
        int k = 0;
        s32 a, b;
        while (*s && *s != ',' && k < 23) part[k++] = *s++;
        part[k] = 0;
        if (*s == ',') s++;
        if (seq(part, "*")) { for (i = 0; i < n; i++) on[i] = 1; continue; }
        {
            char *d = part; while (*d && *d != '-') d++;
            if (*d == '-' && d != part) { *d = 0; if (!inte(part, &a) || !inte(d + 1, &b)) { err(line, "bad wall list", 0); return 0; } }
            else { if (!inte(part, &a)) { err(line, "bad wall list", 0); return 0; } b = a; }
        }
        for (i = a; i <= b; i++) {
            if (i < 0 || i >= n) { err(line, "wall out of range", part); return 0; }
            on[i] = 1;
        }
    }
    return 1;
}

typedef struct { int sec; s32 x0, z0, x1, z1, h0, h1, ceil; int dir, n, has_ceil; } Stairs;

static int parse_prop(EmMap *m, int line)
{
    EmProp p;
    const char *k;
    int i, t;
    s32 a, b;
    if (ntok < 2) { err(line, "prop needs a kind", 0); return 0; }
    if (m->nprops >= EM_PROPS) { err(line, "too many props", 0); return 0; }
    k = tok[1];
    memset(&p, 0, sizeof(p)); p.t0 = p.t1 = p.t2 = 255; p.rep = 1; p.axis = 1;
    for (i = 0; i < PK_COUNT; i++) if (seq(k, em_prop_names[i])) break;
    if (i == PK_COUNT) { err(line, "unknown prop", k); return 0; }
    p.kind = (u8)i;
#define NEED(n) do { if (ntok < (n)) { err(line, "missing values for prop", k); return 0; } } while (0)
#define PT(ix, X, Z) do { if (!pair_m(tok[ix], &a, &b)) { err(line, "expected x,z", tok[ix]); return 0; } X = (s16)a; Z = (s16)b; } while (0)
#define M_(ix, V) do { if (!mtr(tok[ix], &a)) { err(line, "bad number", tok[ix]); return 0; } V = (s16)a; } while (0)
#define I_(ix, V) do { if (!inte(tok[ix], &a)) { err(line, "bad number", tok[ix]); return 0; } V = (s16)a; } while (0)
#define T_(ix, V) do { if (!tile_tok(tok[ix], &t)) { err(line, "unknown tile", tok[ix]); return 0; } V = (u8)t; } while (0)
#define D_(ix) do { const char *d_ = tok[ix]; int q_; for (q_ = 0; q_ < 4; q_++) if (d_[0] == DIRC[q_] && !d_[1]) break; if (q_ == 4) { err(line, "expected n|e|s|w", d_); return 0; } p.dir = (u8)q_; } while (0)
    switch (p.kind) {
    case PK_BOX: case PK_BOXF:
        NEED(p.kind == PK_BOX ? 8 : 10);
        PT(2, p.x0, p.z0); PT(3, p.x1, p.z1); M_(4, p.h0); M_(5, p.h1); T_(6, p.t0); T_(7, p.t1);
        p.v0 = 1000; i = 8;
        if (p.kind == PK_BOXF) { T_(8, p.t2); D_(9); i = 10; }
        while (i < ntok) {
            if (seq(tok[i], "cell") && i + 1 < ntok) { M_(i + 1, p.v0); i += 2; }
            else if (seq(tok[i], "noshadow")) { p.flags |= 1; i++; }
            else if (seq(tok[i], "nocoll")) { p.flags |= 2; i++; }
            else if (seq(tok[i], "emit") && i + 1 < ntok) { I_(i + 1, p.v1); i += 2; }
            else { err(line, "unknown box key", tok[i]); return 0; }
        }
        break;
    case PK_CRATE: NEED(5); PT(2, p.x0, p.z0); M_(3, p.h0); M_(4, p.h1); break;
    case PK_CAN: case PK_TANK: case PK_CORE: NEED(3); PT(2, p.x0, p.z0); break;
    case PK_PILLAR: NEED(4); PT(2, p.x0, p.z0); M_(3, p.h0); break;
    case PK_CONSOLE:
        NEED(5); PT(2, p.x0, p.z0); D_(3);
        if (seq(tok[4], "graph")) p.t0 = 0; else if (seq(tok[4], "vitals")) p.t0 = 1; else { err(line, "console screen is graph|vitals", tok[4]); return 0; }
        if (ntok > 5) { M_(5, p.h0); p.flags |= 1; }
        break;
    case PK_MACHINE: NEED(6); PT(2, p.x0, p.z0); PT(3, p.x1, p.z1); M_(4, p.h0); D_(5); break;
    case PK_POD: NEED(5); PT(2, p.x0, p.z0); D_(3); I_(4, p.v0); break;
    case PK_RACK: case PK_CELL: NEED(4); PT(2, p.x0, p.z0); D_(3); break;
    case PK_FAN: {
        s32 x, z, h; char tmp[64]; int q = 0, c1 = -1, c2 = -1;
        NEED(5);
        scpy(tmp, tok[2], 64);
        for (q = 0; tmp[q]; q++) if (tmp[q] == ',') { if (c1 < 0) c1 = q; else c2 = q; }
        if (c1 < 0 || c2 < 0) { err(line, "expected x,z,h", tok[2]); return 0; }
        tmp[c1] = tmp[c2] = 0;
        if (!mtr(tmp, &x) || !mtr(tmp + c1 + 1, &z) || !mtr(tmp + c2 + 1, &h)) { err(line, "expected x,z,h", tok[2]); return 0; }
        p.x0 = (s16)x; p.z0 = (s16)z; p.h0 = (s16)h; M_(3, p.h1); I_(4, p.v0);
        break; }
    case PK_RAIL:
        NEED(5); PT(2, p.x0, p.z0); PT(3, p.x1, p.z1); M_(4, p.h0);
        if (p.z0 != p.z1 && p.x0 != p.x1) { err(line, "rails run along x or z", 0); return 0; }
        break;
    case PK_CYL: {
        s32 x, z, h; char tmp[64]; int q = 0, c1 = -1, c2 = -1;
        NEED(7);
        scpy(tmp, tok[2], 64);
        for (q = 0; tmp[q]; q++) if (tmp[q] == ',') { if (c1 < 0) c1 = q; else c2 = q; }
        if (c1 < 0 || c2 < 0) { err(line, "expected x,z,h", tok[2]); return 0; }
        tmp[c1] = tmp[c2] = 0;
        if (!mtr(tmp, &x) || !mtr(tmp + c1 + 1, &z) || !mtr(tmp + c2 + 1, &h)) { err(line, "expected x,z,h", tok[2]); return 0; }
        p.x0 = (s16)x; p.z0 = (s16)z; p.h0 = (s16)h;
        M_(3, p.h1); M_(4, p.h2); I_(5, p.v0); T_(6, p.t0);
        i = 7;
        while (i < ntok) {
            if (seq(tok[i], "axis") && i + 1 < ntok) {
                const char *ax = tok[i + 1];
                p.axis = (u8)(ax[0] == 'x' ? 0 : ax[0] == 'y' ? 1 : ax[0] == 'z' ? 2 : 9);
                if (p.axis > 2 || ax[1]) { err(line, "axis is x|y|z", ax); return 0; }
                i += 2;
            }
            else if (seq(tok[i], "add")) { p.flags |= 2; i++; }
            else if (seq(tok[i], "pulse")) { p.flags |= 16; i++; }
            else if (seq(tok[i], "anim") && i + 1 < ntok) { I_(i + 1, p.v1); i += 2; }
            else if (seq(tok[i], "emit") && i + 1 < ntok) { I_(i + 1, p.v2); p.flags |= 64; i += 2; }
            else if (seq(tok[i], "cap") && i + 1 < ntok) { T_(i + 1, p.t1); i += 2; }
            else if (seq(tok[i], "rep") && i + 1 < ntok) { if (!inte(tok[i + 1], &a)) { err(line, "bad rep", 0); return 0; } p.rep = (u8)a; i += 2; }
            else { err(line, "unknown cyl key", tok[i]); return 0; }
        }
        break; }
    default: break;
    }
#undef NEED
#undef PT
#undef M_
#undef I_
#undef T_
#undef D_
    m->props[m->nprops++] = p;
    return 1;
}

static int xyz3(const char *s, s32 *x, s32 *z, s32 *h)
{
    char tmp[64]; int q, c1 = -1, c2 = -1;
    scpy(tmp, s, 64);
    for (q = 0; tmp[q]; q++) if (tmp[q] == ',') { if (c1 < 0) c1 = q; else c2 = q; }
    if (c1 < 0 || c2 < 0) return 0;
    tmp[c1] = tmp[c2] = 0;
    return mtr(tmp, x) && mtr(tmp + c1 + 1, z) && mtr(tmp + c2 + 1, h);
}

int em_parse(EmMap *m, const char *text)
{
    static Stairs st[64];
    int nst = 0, line = 0, cur = -1, i;
    char nm[24];
    scpy(nm, m->name, 24);
    memset(m, 0, sizeof(*m));
    scpy(m->name, nm, 24);
    em_error[0] = 0;
    while (*text) {
        const char *e = text;
        int len;
        while (*e && *e != '\n') e++;
        len = (int)(e - text);
        line++;
        split_line(text, len);
        text = *e ? e + 1 : e;
        if (!ntok) continue;
        {
            const char *k = tok[0];
            EmSector *s = cur >= 0 ? &m->secs[cur] : 0;
            s32 a, b;
            if (seq(k, "room")) {
                if (ntok < 2) { err(line, "room needs an ID", 0); return 0; }
                if (m->nrooms >= EM_ROOMS) { err(line, "too many rooms (32)", 0); return 0; }
                scpy(m->rooms[m->nrooms].id, tok[1], 16);
                {
                    char *o = m->rooms[m->nrooms].name; int n = 0, j;
                    for (j = 2; j < ntok; j++) {
                        const char *c = tok[j];
                        if (j > 2 && n < 23) o[n++] = ' ';
                        while (*c && n < 23) o[n++] = *c++;
                    }
                    o[n] = 0;
                    if (!n) scpy(o, tok[1], 24);
                }
                m->nrooms++;
            } else if (seq(k, "sector") || seq(k, "stairs")) {
                int isst = seq(k, "stairs");
                if (ntok < 2) { err(line, "missing name", 0); return 0; }
                if (m->nsecs >= EM_SECS) { err(line, "too many sectors", 0); return 0; }
                cur = m->nsecs++;
                s = &m->secs[cur];
                sector_defaults(s);
                s->room = 255;
                scpy(s->name, tok[1], 16);
                s->ceil = -32768;                          /* not given yet */
                i = 2;
                if (isst) {
                    Stairs *q;
                    if (nst >= 64) { err(line, "too many stairs", 0); return 0; }
                    q = &st[nst++]; memset(q, 0, sizeof(*q)); q->sec = cur;
                    if (i + 1 < ntok && seq(tok[i], "room")) { int r; for (r = 0; r < m->nrooms; r++) if (seq(m->rooms[r].id, tok[i + 1])) break; if (r == m->nrooms) { err(line, "unknown room", tok[i + 1]); return 0; } s->room = (u8)r; i += 2; }
                    if (i + 4 >= ntok) { err(line, "stairs: x0,z0 x1,z1 n|s|e|w FROM TO", 0); return 0; }
                    if (!pair_m(tok[i], &q->x0, &q->z0) || !pair_m(tok[i + 1], &q->x1, &q->z1)) { err(line, "expected x,z", 0); return 0; }
                    { int d; for (d = 0; d < 4; d++) if (tok[i + 2][0] == DIRC[d] && !tok[i + 2][1]) break; if (d == 4) { err(line, "expected n|s|e|w", tok[i + 2]); return 0; } q->dir = d; }
                    if (!mtr(tok[i + 3], &q->h0) || !mtr(tok[i + 4], &q->h1)) { err(line, "bad height", 0); return 0; }
                    i += 5;
                    while (i < ntok) {
                        if (seq(tok[i], "ceil") && i + 1 < ntok) { if (!mtr(tok[i + 1], &q->ceil)) { err(line, "bad ceil", 0); return 0; } q->has_ceil = 1; i += 2; }
                        else if (seq(tok[i], "steps") && i + 1 < ntok) { if (!inte(tok[i + 1], &a)) { err(line, "bad steps", 0); return 0; } q->n = a; i += 2; }
                        else { err(line, "unknown stairs key", tok[i]); return 0; }
                    }
                    if (q->x0 > q->x1) { s32 t = q->x0; q->x0 = q->x1; q->x1 = t; }
                    if (q->z0 > q->z1) { s32 t = q->z0; q->z0 = q->z1; q->z1 = t; }
                    if (!q->n) {   /* ceil(|h1 - h0| / (0.2 m) - 1e-6), as make_map does */
                        s32 d = q->h1 > q->h0 ? q->h1 - q->h0 : q->h0 - q->h1;
                        q->n = (int)((d * 10 + 2047) / 2048);
                        if ((long long)q->n * 2048 - 2048 >= (long long)d * 10) q->n--;
                        if (q->n < 1) q->n = 1;
                    }
                    continue;
                }
                while (i < ntok) {
                    const char *a0 = tok[i];
                    if (seq(a0, "room") && i + 1 < ntok) {
                        int r;
                        for (r = 0; r < m->nrooms; r++) if (seq(m->rooms[r].id, tok[i + 1])) break;
                        if (r == m->nrooms) { err(line, "unknown room", tok[i + 1]); return 0; }
                        s->room = (u8)r; i += 2;
                    }
                    else if (seq(a0, "floor") && i + 1 < ntok) { if (!mtr(tok[i + 1], &a)) { err(line, "bad floor", 0); return 0; } s->floor = (s16)a; i += 2; }
                    else if (seq(a0, "ceil") && i + 1 < ntok) { if (!mtr(tok[i + 1], &a)) { err(line, "bad ceil", 0); return 0; } s->ceil = (s16)a; i += 2; }
                    else if ((seq(a0, "fslope") || seq(a0, "cslope")) && i + 2 < ntok) {
                        if (!inte(tok[i + 1], &a) || !mtr(tok[i + 2], &b)) { err(line, "bad slope", 0); return 0; }
                        if (a0[0] == 'f') { s->fslope = (s8)a; s->fslope_h = (s16)b; } else { s->cslope = (s8)a; s->cslope_h = (s16)b; }
                        i += 3;
                    }
                    else if (seq(a0, "door") && i + 1 < ntok) { if (!mtr(tok[i + 1], &a)) { err(line, "bad door", 0); return 0; } s->kind = SK_DOOR; s->ka = (s16)a; i += 2; }
                    else if (seq(a0, "lock") && i + 1 < ntok) {
                        int c; for (c = 1; c < 4; c++) if (seq(tok[i + 1], KEYN[c])) break;
                        if (c == 4) { err(line, "lock is cyan|amber|crimson", tok[i + 1]); return 0; }
                        s->lock = (u8)c; i += 2;
                    }
                    else if (seq(a0, "secret")) { s->flags |= SF_SECRET; i += 1; }
                    else if (seq(a0, "lift") && i + 2 < ntok) {
                        if (!mtr(tok[i + 1], &a) || !mtr(tok[i + 2], &b)) { err(line, "bad lift", 0); return 0; }
                        s->kind = SK_LIFT; s->ka = (s16)a; s->kb = (s16)b; i += 3;
                    }
                    else { err(line, "unknown sector key", a0); return 0; }
                }
            } else if (seq(k, "pts")) {
                if (!s) { err(line, "pts outside a sector", 0); return 0; }
                for (i = 1; i < ntok; i++) {
                    if (s->np >= EM_SV) { err(line, "too many points (48)", 0); return 0; }
                    if (!pair_m(tok[i], &a, &b)) { err(line, "expected x,z", tok[i]); return 0; }
                    s->x[s->np] = (s16)a; s->z[s->np] = (s16)b; s->np++;
                }
            } else if (seq(k, "ftex") || seq(k, "ctex")) {
                int t;
                if (!s || ntok < 2 || !tile_tok(tok[1], &t)) { err(line, "bad tile", ntok > 1 ? tok[1] : ""); return 0; }
                (k[0] == 'f' ? &s->f : &s->c)->def = (u8)t;
            } else if (seq(k, "frule") || seq(k, "crule")) {
                EmFlat *f; int t; s32 sx, sz, ox, oz;
                if (!s || ntok < 4 || !tile_tok(tok[1], &t) || !pair_i(tok[2], &sx, &sz) || !pair_i(tok[3], &ox, &oz) || sx <= 0 || sz <= 0) { err(line, "bad rule", 0); return 0; }
                f = k[0] == 'f' ? &s->f : &s->c;
                if (f->nrules >= EM_RULES) { err(line, "too many rules", 0); return 0; }
                f->r[f->nrules].tile = (u8)t; f->r[f->nrules].sx = (u8)sx; f->r[f->nrules].sz = (u8)sz;
                f->r[f->nrules].ox = (u8)(((ox % sx) + sx) % sx); f->r[f->nrules].oz = (u8)(((oz % sz) + sz) % sz);
                f->nrules++;
            } else if (seq(k, "fcell") || seq(k, "ccell")) {
                int t; s32 cx, cz;
                if (!s || ntok < 3 || !pair_i(tok[1], &cx, &cz) || !tile_tok(tok[2], &t)) { err(line, "bad cell", 0); return 0; }
                if (m->nfcells >= EM_FCELLS) { err(line, "too many cells", 0); return 0; }
                m->fcells[m->nfcells].sec = (u16)cur; m->fcells[m->nfcells].ceil = (u8)(k[0] == 'c');
                m->fcells[m->nfcells].cx = (s8)cx; m->fcells[m->nfcells].cz = (s8)cz; m->fcells[m->nfcells].tile = (u8)t; m->nfcells++;
            } else if (seq(k, "walls") || seq(k, "lows") || seq(k, "ups")) {
                int r;
                if (!s) { err(line, "walls outside a sector", 0); return 0; }
                if (!rows_from(m, 1, &r, line)) return 0;
                if (k[0] == 'w') s->walls = (s16)r; else if (k[0] == 'l') s->lows = (s16)r; else s->ups = (s16)r;
            } else if (seq(k, "wall")) {
                u8 on[EM_SV];
                const char *a0 = ntok > 2 ? tok[2] : "";
                if (!s || ntok < 2) { err(line, "wall needs a list", 0); return 0; }
                if (!wall_list(tok[1], s->np, on, line)) return 0;
                if ((seq(a0, "low") || seq(a0, "up")) && ntok > 3 && seq(tok[3], "cell")) {
                    int t; s32 c, r;
                    if (ntok < 6 || !pair_i(tok[4], &c, &r) || !tile_tok(tok[5], &t)) { err(line, "bad wall cell", 0); return 0; }
                    for (i = 0; i < s->np; i++) if (on[i]) {
                        if (m->nwcells >= EM_WCELLS) { err(line, "too many wall cells", 0); return 0; }
                        m->wcells[m->nwcells].sec = (u16)cur; m->wcells[m->nwcells].wall = (u8)i; m->wcells[m->nwcells].part = (u8)(a0[0] == 'l' ? EM_LOW : EM_UP);
                        m->wcells[m->nwcells].col = (u8)c; m->wcells[m->nwcells].row = (u8)r; m->wcells[m->nwcells].tile = (u8)t; m->nwcells++;
                    }
                } else if (seq(a0, "low") || seq(a0, "up")) {
                    int r;
                    if (!rows_from(m, 3, &r, line)) return 0;
                    for (i = 0; i < s->np; i++) if (on[i]) { if (a0[0] == 'l') s->w[i].low = (s16)r; else s->w[i].up = (s16)r; }
                } else if (seq(a0, "cell")) {
                    int t; s32 c, r;
                    if (ntok < 5 || !pair_i(tok[3], &c, &r) || !tile_tok(tok[4], &t)) { err(line, "bad wall cell", 0); return 0; }
                    for (i = 0; i < s->np; i++) if (on[i]) {
                        if (m->nwcells >= EM_WCELLS) { err(line, "too many wall cells", 0); return 0; }
                        m->wcells[m->nwcells].sec = (u16)cur; m->wcells[m->nwcells].wall = (u8)i; m->wcells[m->nwcells].part = EM_MID;
                        m->wcells[m->nwcells].col = (u8)c; m->wcells[m->nwcells].row = (u8)r; m->wcells[m->nwcells].tile = (u8)t; m->nwcells++;
                    }
                } else if (seq(a0, "glass") || seq(a0, "bars") || seq(a0, "end") || seq(a0, "mid") || seq(a0, "noshadow")) {
                    u8 f = (u8)(seq(a0, "glass") ? WF_GLASS : seq(a0, "bars") ? WF_BARS : seq(a0, "end") ? WF_ALIGN_END : seq(a0, "mid") ? WF_ALIGN_MID : WF_NOSHADOW);
                    for (i = 0; i < s->np; i++) if (on[i]) s->w[i].flags |= f;
                } else {
                    int r;
                    if (!rows_from(m, 2, &r, line)) return 0;
                    for (i = 0; i < s->np; i++) if (on[i]) s->w[i].mid = (s16)r;
                }
            } else if (seq(k, "light") || seq(k, "lightgrid")) {
                EmLight L; s32 x, z, h, c[3], rad;
                int g = seq(k, "lightgrid"), base = g ? 5 : 2;
                memset(&L, 0, sizeof(L));
                if (m->nlights >= EM_LIGHTS) { err(line, "too many lights", 0); return 0; }
                if (ntok < (g ? 9 : 6)) { err(line, "missing light values", 0); return 0; }
                if (g) {
                    s32 x1, z1, step;
                    if (!pair_m(tok[1], &x, &z) || !pair_m(tok[2], &x1, &z1) || !mtr(tok[3], &h) || !mtr(tok[4], &step) || step <= 0) { err(line, "bad lightgrid", 0); return 0; }
                    L.grid = 1; L.x1 = (s16)x1; L.z1 = (s16)z1; L.step = (s16)step;
                } else if (!xyz3(tok[1], &x, &z, &h)) { err(line, "expected x,z,h", tok[1]); return 0; }
                for (i = 0; i < 3; i++) if (!num_s(tok[base + i], 100, 1, &c[i]) || c[i] < 0 || c[i] > 255) { err(line, "bad colour", tok[base + i]); return 0; }
                if (!mtr(tok[base + 3], &rad)) { err(line, "bad radius", 0); return 0; }
                L.x = (s16)x; L.z = (s16)z; L.h = (s16)h; L.r = (u8)c[0]; L.g = (u8)c[1]; L.b = (u8)c[2]; L.rad = (s16)rad;
                m->lights[m->nlights++] = L;
            } else if (seq(k, "sprite")) {
                EmSprite S; s32 x, z, h, sz, c[3], an = 0;
                if (m->nsprites >= EM_SPRITES) { err(line, "too many sprites (16)", 0); return 0; }
                if (ntok < 6 || !xyz3(tok[1], &x, &z, &h) || !mtr(tok[2], &sz)) { err(line, "bad sprite", 0); return 0; }
                for (i = 0; i < 3; i++) if (!inte(tok[3 + i], &c[i])) { err(line, "bad colour", 0); return 0; }
                if (ntok > 6 && !inte(tok[6], &an)) { err(line, "bad anim", 0); return 0; }
                S.x = (s16)x; S.z = (s16)z; S.h = (s16)h; S.size = (s16)sz; S.r = (u8)c[0]; S.g = (u8)c[1]; S.b = (u8)c[2]; S.anim = (u8)an;
                m->sprites[m->nsprites++] = S;
            } else if (seq(k, "thing")) {
                EmThing T; int ty, c;
                memset(&T, 0, sizeof(T));
                if (m->nthings >= EM_THINGS) { err(line, "too many things", 0); return 0; }
                if (ntok < 3) { err(line, "thing needs a type and x,z", 0); return 0; }
                for (ty = 0; ty < 6; ty++) if (seq(tok[1], em_thing_names[ty])) break;
                if (ty == 6) { err(line, "unknown thing", tok[1]); return 0; }
                T.type = (u8)ty; i = 2;
                for (c = 1; c < 4; c++) if (seq(tok[i], KEYN[c])) { T.arg = (u8)c; i++; break; }
                if (i >= ntok || !pair_m(tok[i], &a, &b)) { err(line, "expected x,z", i < ntok ? tok[i] : ""); return 0; }
                T.x = T.ax = (s16)a; T.z = T.az = (s16)b; i++;
                while (i < ntok) {
                    if (seq(tok[i], "yaw") && i + 1 < ntok) { if (!num_s(tok[i + 1], 4096, 360, &a)) { err(line, "bad yaw", 0); return 0; } T.yaw = (s16)(a & 4095); i += 2; continue; }
                    if (seq(tok[i], "patrol") && i + 1 < ntok) { if (!pair_m(tok[i + 1], &a, &b)) { err(line, "bad patrol", 0); return 0; } T.ax = (s16)a; T.az = (s16)b; i += 2; continue; }
                    if (seq(tok[i], "secret")) { T.arg = 1; i++; continue; }
                    for (c = 1; c < 4; c++) if (seq(tok[i], KEYN[c])) break;
                    if (c < 4) { T.arg = (u8)c; i++; continue; }
                    err(line, "unknown thing key", tok[i]); return 0;
                }
                m->things[m->nthings++] = T;
            } else if (seq(k, "prop")) {
                if (!parse_prop(m, line)) return 0;
            } else if (seq(k, "name")) {   /* the editor's level title (make_map ignores it) */
                char *o = m->name; int n = 0, j;
                for (j = 1; j < ntok; j++) { const char *c2 = tok[j]; if (j > 1 && n < 23) o[n++] = ' '; while (*c2 && n < 23) o[n++] = *c2++; }
                o[n] = 0;
            } else { err(line, "unknown line", k); return 0; }
        }
    }
    /* defaults and checks the parser owes make_map (finish) */
    for (i = 0; i < m->nsecs; i++) {
        EmSector *s = &m->secs[i];
        int isst = 0, q;
        for (q = 0; q < nst; q++) if (st[q].sec == i) isst = 1;
        if (s->room == 255) {
            if (isst) { err(0, "stairs have no room", s->name); return 0; }
            err(0, "sector has no room:", s->name); return 0;
        }
        if (isst) continue;
        if (s->ceil == -32768) s->ceil = (s16)(s->kind == SK_DOOR ? s->floor : s->floor + 4096);
        if (s->np < 3) { err(0, "sector needs 3 points:", s->name); return 0; }
    }
    /* stairs -> step sectors, in place (expand_stairs) */
    for (i = nst - 1; i >= 0; i--) {
        Stairs *q = &st[i];
        EmSector tmpl = m->secs[q->sec];
        int k, n = q->n, at = q->sec;
        if (m->nsecs - 1 + n > EM_SECS) { err0("too many sectors"); return 0; }
        for (k = m->nsecs - 1; k > at; k--) m->secs[k + n - 1] = m->secs[k];
        for (k = 0; k < m->nwcells; k++) if (m->wcells[k].sec > at) m->wcells[k].sec = (u16)(m->wcells[k].sec + n - 1);
        for (k = 0; k < m->nfcells; k++) if (m->fcells[k].sec > at) m->fcells[k].sec = (u16)(m->fcells[k].sec + n - 1);
        m->nsecs += n - 1;
        for (k = 0; k < n; k++) {
            EmSector *c = &m->secs[at + k];
            s32 r0, r1, r2, r3;
#define LERP(p, qq, num, den) ((s32)rdiv((long long)(p) * (den) + ((long long)(qq) - (p)) * (num), (den)))
            if (q->dir == 0) { r0 = q->x0; r1 = LERP(q->z0, q->z1, k, n); r2 = q->x1; r3 = LERP(q->z0, q->z1, k + 1, n); }
            else if (q->dir == 2) { r0 = q->x0; r1 = LERP(q->z1, q->z0, k + 1, n); r2 = q->x1; r3 = LERP(q->z1, q->z0, k, n); }
            else if (q->dir == 1) { r0 = LERP(q->x0, q->x1, k, n); r1 = q->z0; r2 = LERP(q->x0, q->x1, k + 1, n); r3 = q->z1; }
            else { r0 = LERP(q->x1, q->x0, k + 1, n); r1 = q->z0; r2 = LERP(q->x1, q->x0, k, n); r3 = q->z1; }
            *c = tmpl;
            {
                char nm2[16]; int l = 0, v = k; char d[6]; int nd = 0;
                while (tmpl.name[l] && l < 11) { nm2[l] = tmpl.name[l]; l++; }
                nm2[l++] = '_';
                do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 5);
                while (nd) nm2[l++] = d[--nd];
                nm2[l] = 0; scpy(c->name, nm2, 16);
            }
            c->floor = (s16)LERP(q->h0, q->h1, k + 1, n);
#undef LERP
            c->ceil = (s16)(q->has_ceil ? q->ceil : (q->h0 > q->h1 ? q->h0 : q->h1) + 4096);
            c->kind = SK_ROOM;
            c->np = 4;
            c->x[0] = (s16)r0; c->z[0] = (s16)r1; c->x[1] = (s16)r2; c->z[1] = (s16)r1;
            c->x[2] = (s16)r2; c->z[2] = (s16)r3; c->x[3] = (s16)r0; c->z[3] = (s16)r3;
        }
    }
    for (i = 0; i < m->nsecs; i++) fix_winding_m(m, i);
    if (!m->name[0]) scpy(m->name, "UNTITLED", 24);
    return 1;
}

/* ------------------------------------------------------------------ write */
typedef struct { char *p; int n, cap, bad; } Out;
static void o_s(Out *o, const char *s) { while (*s) { if (o->n < o->cap - 1) o->p[o->n++] = *s; else o->bad = 1; s++; } }
static void o_c(Out *o, char c) { char b[2]; b[0] = c; b[1] = 0; o_s(o, b); }
static void o_i(Out *o, long v)
{
    char d[24]; int n = 0;
    if (v < 0) { o_c(o, '-'); v = -v; }
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 23);
    while (n) o_c(o, d[--n]);
}
/* v / div with up to `places` decimals, trailing zeros dropped (shortest that reads back the same
   when div = 1024: 3 decimals always does, since 1/1024 m < 0.001 m... rounded on re-read) */
static void o_f(Out *o, long v, long div, int places)
{
    long long scale = 1, q, r;
    int k, neg = v < 0;
    for (k = 0; k < places; k++) scale *= 10;
    q = rdiv((long long)(neg ? -v : v) * scale, div);
    if (neg && q) o_c(o, '-');
    o_i(o, (long)(q / scale));
    r = q % scale;
    if (r) {
        char d[12]; int n = 0;
        for (k = 0; k < places; k++) { d[places - 1 - k] = (char)('0' + r % 10); r /= 10; }
        n = places;
        while (n > 0 && d[n - 1] == '0') n--;
        o_c(o, '.');
        for (k = 0; k < n; k++) o_c(o, d[k]);
    }
}
/* metres: the fewest decimals (up to 4) that read back to exactly the same units */
static void o_m(Out *o, long v)
{
    int p; long long sc = 1;
    for (p = 0; p < 4; p++, sc *= 10) {
        long long q = rdiv((long long)v * sc, 1024);
        if (rdiv(q * 1024, sc) == v) break;
    }
    o_f(o, v, 1024, p);
}
static void o_pt(Out *o, long x, long z) { o_m(o, x); o_c(o, ','); o_m(o, z); }
static void o_t(Out *o, int t)
{
    if (t == 255) { o_s(o, "-"); return; }
    if (t >= 0 && t < em_tile_count) o_s(o, em_tile_names[t]);
    else o_s(o, "DARK_METAL");
}
static void o_rows(Out *o, const EmRows *r)
{
    int i, j;
    for (i = 0; i < r->nrows; i++) {
        if (i) o_s(o, " /");
        for (j = 0; j < r->n[i]; j++) { o_c(o, ' '); o_t(o, r->t[i][j]); }
    }
    if (r->top != 255) { o_s(o, r->nrows ? " / top " : " top "); o_t(o, r->top); }
}

static int rows_eq(const EmMap *m, int a, int b)
{
    if (a == b) return 1;
    if (a < 0 || b < 0) return 0;
    return memcmp(&m->rows[a], &m->rows[b], sizeof(EmRows)) == 0;
}

int em_write(const EmMap *m, char *out, int cap)
{
    Out o; int i, j, k;
    o.p = out; o.n = 0; o.cap = cap; o.bad = 0;
    o_s(&o, "# Deadbird level, saved by the in-game editor. Format: tools/make_map.py\nname "); o_s(&o, m->name); o_s(&o, "\n\n");
    for (i = 0; i < m->nrooms; i++) { o_s(&o, "room "); o_s(&o, m->rooms[i].id); o_c(&o, ' '); o_s(&o, m->rooms[i].name); o_c(&o, '\n'); }
    for (i = 0; i < m->nsecs; i++) {
        const EmSector *s = &m->secs[i];
        o_s(&o, "\nsector "); o_s(&o, s->name); o_s(&o, " room "); o_s(&o, s->room < m->nrooms ? m->rooms[s->room].id : "?");
        o_s(&o, " floor "); o_m(&o, s->floor);
        if (s->kind != SK_DOOR) { o_s(&o, " ceil "); o_m(&o, s->ceil); }
        if (s->fslope >= 0) { o_s(&o, " fslope "); o_i(&o, s->fslope); o_c(&o, ' '); o_m(&o, s->fslope_h); }
        if (s->cslope >= 0) { o_s(&o, " cslope "); o_i(&o, s->cslope); o_c(&o, ' '); o_m(&o, s->cslope_h); }
        if (s->kind == SK_DOOR) { o_s(&o, " door "); o_m(&o, s->ka); if (s->lock) { o_s(&o, " lock "); o_s(&o, KEYN[s->lock & 3]); } if (s->flags & SF_SECRET) o_s(&o, " secret"); }
        if (s->kind == SK_LIFT) { o_s(&o, " lift "); o_m(&o, s->ka); o_c(&o, ' '); o_m(&o, s->kb); }
        o_s(&o, "\n    pts");
        for (j = 0; j < s->np; j++) { o_c(&o, ' '); o_pt(&o, s->x[j], s->z[j]); }
        for (k = 0; k < 2; k++) {
            const EmFlat *f = k ? &s->c : &s->f;
            o_s(&o, k ? "\n    ctex " : "\n    ftex "); o_t(&o, f->def);
            for (j = 0; j < f->nrules; j++) {
                o_s(&o, k ? "\n    crule " : "\n    frule "); o_t(&o, f->r[j].tile);
                o_c(&o, ' '); o_i(&o, f->r[j].sx); o_c(&o, ','); o_i(&o, f->r[j].sz); o_c(&o, ' '); o_i(&o, f->r[j].ox); o_c(&o, ','); o_i(&o, f->r[j].oz);
            }
        }
        for (j = 0; j < m->nfcells; j++) if (m->fcells[j].sec == i) {
            const EmFCell *c = &m->fcells[j];
            o_s(&o, c->ceil ? "\n    ccell " : "\n    fcell "); o_i(&o, c->cx); o_c(&o, ','); o_i(&o, c->cz); o_c(&o, ' '); o_t(&o, c->tile);
        }
        if (s->walls >= 0) { o_s(&o, "\n    walls"); o_rows(&o, &m->rows[s->walls]); }
        if (s->lows >= 0) { o_s(&o, "\n    lows"); o_rows(&o, &m->rows[s->lows]); }
        if (s->ups >= 0) { o_s(&o, "\n    ups"); o_rows(&o, &m->rows[s->ups]); }
        /* per-wall rows: walls sharing a spec on one line */
        {
            u8 done[EM_SV][3];
            int part;
            memset(done, 0, sizeof(done));
            for (part = 0; part < 3; part++)
                for (j = 0; j < s->np; j++) {
                    int r = part == 0 ? s->w[j].mid : part == 1 ? s->w[j].low : s->w[j].up, q, first = 1;
                    if (r < 0 || done[j][part]) continue;
                    o_s(&o, "\n    wall ");
                    for (q = j; q < s->np; q++) {
                        int rq = part == 0 ? s->w[q].mid : part == 1 ? s->w[q].low : s->w[q].up;
                        if (done[q][part] || !rows_eq(m, r, rq)) continue;
                        done[q][part] = 1;
                        if (!first) o_c(&o, ',');
                        o_i(&o, q); first = 0;
                    }
                    if (part == 1) o_s(&o, " low");
                    if (part == 2) o_s(&o, " up");
                    o_rows(&o, &m->rows[r]);
                }
        }
        for (j = 0; j < m->nwcells; j++) if (m->wcells[j].sec == i) {
            const EmWCell *c = &m->wcells[j];
            o_s(&o, "\n    wall "); o_i(&o, c->wall);
            if (c->part == EM_LOW) o_s(&o, " low");
            if (c->part == EM_UP) o_s(&o, " up");
            o_s(&o, " cell "); o_i(&o, c->col); o_c(&o, ','); o_i(&o, c->row); o_c(&o, ' '); o_t(&o, c->tile);
        }
        for (j = 0; j < s->np; j++) {
            static const char *const FN[5] = { "glass", "bars", "end", "mid", "noshadow" };
            for (k = 0; k < 5; k++) if (s->w[j].flags & (1 << k)) { o_s(&o, "\n    wall "); o_i(&o, j); o_c(&o, ' '); o_s(&o, FN[k]); }
        }
        o_c(&o, '\n');
    }
    o_c(&o, '\n');
    for (i = 0; i < m->nlights; i++) {
        const EmLight *L = &m->lights[i];
        if (L->grid) {
            o_s(&o, "lightgrid "); o_pt(&o, L->x, L->z); o_c(&o, ' '); o_pt(&o, L->x1, L->z1); o_c(&o, ' '); o_m(&o, L->h); o_c(&o, ' '); o_m(&o, L->step);
        } else { o_s(&o, "light "); o_pt(&o, L->x, L->z); o_c(&o, ','); o_m(&o, L->h); }
        o_c(&o, ' '); o_f(&o, L->r, 100, 2); o_c(&o, ' '); o_f(&o, L->g, 100, 2); o_c(&o, ' '); o_f(&o, L->b, 100, 2);
        o_c(&o, ' '); o_m(&o, L->rad); o_c(&o, '\n');
    }
    for (i = 0; i < m->nsprites; i++) {
        const EmSprite *S = &m->sprites[i];
        o_s(&o, "sprite "); o_pt(&o, S->x, S->z); o_c(&o, ','); o_m(&o, S->h); o_c(&o, ' '); o_m(&o, S->size);
        o_c(&o, ' '); o_i(&o, S->r); o_c(&o, ' '); o_i(&o, S->g); o_c(&o, ' '); o_i(&o, S->b);
        if (S->anim) { o_c(&o, ' '); o_i(&o, S->anim); }
        o_c(&o, '\n');
    }
    for (i = 0; i < m->nprops; i++) {
        const EmProp *p = &m->props[i];
        o_s(&o, "prop "); o_s(&o, em_prop_names[p->kind]); o_c(&o, ' ');
        switch (p->kind) {
        case PK_BOX: case PK_BOXF:
            o_pt(&o, p->x0, p->z0); o_c(&o, ' '); o_pt(&o, p->x1, p->z1); o_c(&o, ' '); o_m(&o, p->h0); o_c(&o, ' '); o_m(&o, p->h1);
            o_c(&o, ' '); o_t(&o, p->t0); o_c(&o, ' '); o_t(&o, p->t1);
            if (p->kind == PK_BOXF) { o_c(&o, ' '); o_t(&o, p->t2); o_c(&o, ' '); o_c(&o, DIRC[p->dir & 3]); }
            if (p->v0 != 1000) { o_s(&o, " cell "); o_m(&o, p->v0); }
            if (p->flags & 1) o_s(&o, " noshadow");
            if (p->flags & 2) o_s(&o, " nocoll");
            if (p->kind == PK_BOXF && p->v1) { o_s(&o, " emit "); o_i(&o, p->v1); }
            break;
        case PK_CRATE: o_pt(&o, p->x0, p->z0); o_c(&o, ' '); o_m(&o, p->h0); o_c(&o, ' '); o_m(&o, p->h1); break;
        case PK_CAN: case PK_TANK: case PK_CORE: o_pt(&o, p->x0, p->z0); break;
        case PK_PILLAR: o_pt(&o, p->x0, p->z0); o_c(&o, ' '); o_m(&o, p->h0); break;
        case PK_CONSOLE:
            o_pt(&o, p->x0, p->z0); o_c(&o, ' '); o_c(&o, DIRC[p->dir & 3]); o_s(&o, p->t0 ? " vitals" : " graph");
            if (p->flags & 1) { o_c(&o, ' '); o_m(&o, p->h0); }
            break;
        case PK_MACHINE: o_pt(&o, p->x0, p->z0); o_c(&o, ' '); o_pt(&o, p->x1, p->z1); o_c(&o, ' '); o_m(&o, p->h0); o_c(&o, ' '); o_c(&o, DIRC[p->dir & 3]); break;
        case PK_POD: o_pt(&o, p->x0, p->z0); o_c(&o, ' '); o_c(&o, DIRC[p->dir & 3]); o_c(&o, ' '); o_i(&o, p->v0); break;
        case PK_RACK: case PK_CELL: o_pt(&o, p->x0, p->z0); o_c(&o, ' '); o_c(&o, DIRC[p->dir & 3]); break;
        case PK_FAN: o_pt(&o, p->x0, p->z0); o_c(&o, ','); o_m(&o, p->h0); o_c(&o, ' '); o_m(&o, p->h1); o_c(&o, ' '); o_i(&o, p->v0); break;
        case PK_RAIL: o_pt(&o, p->x0, p->z0); o_c(&o, ' '); o_pt(&o, p->x1, p->z1); o_c(&o, ' '); o_m(&o, p->h0); break;
        case PK_CYL:
            o_pt(&o, p->x0, p->z0); o_c(&o, ','); o_m(&o, p->h0); o_c(&o, ' '); o_m(&o, p->h1); o_c(&o, ' '); o_m(&o, p->h2);
            o_c(&o, ' '); o_i(&o, p->v0); o_c(&o, ' '); o_t(&o, p->t0);
            if (p->axis != 1) { o_s(&o, " axis "); o_c(&o, "xyz"[p->axis % 3]); }
            if (p->flags & 2) o_s(&o, " add");
            if (p->v1) { o_s(&o, " anim "); o_i(&o, p->v1); }
            if (p->flags & 64) { o_s(&o, " emit "); o_i(&o, p->v2); }
            if (p->flags & 16) o_s(&o, " pulse");
            if (p->t1 != 255) { o_s(&o, " cap "); o_t(&o, p->t1); }
            if (p->rep != 1) { o_s(&o, " rep "); o_i(&o, p->rep); }
            break;
        default: break;
        }
        o_c(&o, '\n');
    }
    o_c(&o, '\n');
    for (i = 0; i < m->nthings; i++) {
        const EmThing *t = &m->things[i];
        o_s(&o, "thing "); o_s(&o, em_thing_names[t->type % 6]);
        if (t->type == TH_KEY) { o_c(&o, ' '); o_s(&o, KEYN[t->arg & 3]); }
        o_c(&o, ' '); o_pt(&o, t->x, t->z);
        if (t->yaw) { o_s(&o, " yaw "); o_f(&o, (long)t->yaw * 360, 4096, 2); }
        if ((t->ax != t->x || t->az != t->z)) { o_s(&o, " patrol "); o_pt(&o, t->ax, t->az); }
        if (t->type == TH_SHARD && t->arg) o_s(&o, " secret");
        o_c(&o, '\n');
    }
    if (o.bad) return -1;
    o.p[o.n] = 0;
    return o.n;
}

/* ------------------------------------------------------------------ new maps */
void em_clear(EmMap *m, const char *name)
{
    memset(m, 0, sizeof(*m));
    scpy(m->name, name, 24);
    scpy(m->rooms[0].id, "r1", 16); scpy(m->rooms[0].name, "AREA 1", 24);
    m->nrooms = 1;
}
void em_new_level(EmMap *m, const char *name)
{
    s32 x[4] = { -4096, 4096, 4096, -4096 }, z[4] = { -4096, -4096, 4096, 4096 };
    em_clear(m, name);
    em_add_sector(m, x, z, 4, -1);
    m->things[0].type = TH_START; m->things[0].x = m->things[0].ax = 0; m->things[0].z = m->things[0].az = -2048;
    m->nthings = 1;
    m->lights[0].x = 0; m->lights[0].z = 0; m->lights[0].h = 3072; m->lights[0].r = 90; m->lights[0].g = 85; m->lights[0].b = 80; m->lights[0].rad = 9216;
    m->nlights = 1;
}

/* ------------------------------------------------------------------ check (make_map's finish, and more) */
static int seg_cross(s32 ax, s32 az, s32 bx, s32 bz, s32 cx, s32 cz, s32 dx, s32 dz)
{
    /* proper crossing: the segments' interiors cross at one point */
    long long d1 = (long long)(bx - ax) * (cz - az) - (long long)(bz - az) * (cx - ax);
    long long d2 = (long long)(bx - ax) * (dz - az) - (long long)(bz - az) * (dx - ax);
    long long d3 = (long long)(dx - cx) * (az - cz) - (long long)(dz - cz) * (ax - cx);
    long long d4 = (long long)(dx - cx) * (bz - cz) - (long long)(dz - cz) * (bx - cx);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}
static int collinear_overlap(s32 a0x, s32 a0z, s32 a1x, s32 a1z, s32 b0x, s32 b0z, s32 b1x, s32 b1z)
{
    long long dx = a1x - a0x, dz = a1z - a0z, L = dx * dx + dz * dz, t0, t1, lo, hi;
    if (dx * (b0z - a0z) - dz * (b0x - a0x) != 0 || dx * (b1z - a0z) - dz * (b1x - a0x) != 0) return 0;
    t0 = dx * (b0x - a0x) + dz * (b0z - a0z); t1 = dx * (b1x - a0x) + dz * (b1z - a0z);
    lo = t0 < t1 ? t0 : t1; hi = t0 < t1 ? t1 : t0;
    if (lo < 0) lo = 0;
    if (hi > L) hi = L;
    return hi - lo > 0;
}
static int bbox_meet(const EmSector *a, const EmSector *b)
{
    s32 ax0 = 99999, ax1 = -99999, az0 = 99999, az1 = -99999, bx0 = 99999, bx1 = -99999, bz0 = 99999, bz1 = -99999;
    int i;
    for (i = 0; i < a->np; i++) { if (a->x[i] < ax0) ax0 = a->x[i]; if (a->x[i] > ax1) ax1 = a->x[i]; if (a->z[i] < az0) az0 = a->z[i]; if (a->z[i] > az1) az1 = a->z[i]; }
    for (i = 0; i < b->np; i++) { if (b->x[i] < bx0) bx0 = b->x[i]; if (b->x[i] > bx1) bx1 = b->x[i]; if (b->z[i] < bz0) bz0 = b->z[i]; if (b->z[i] > bz1) bz1 = b->z[i]; }
    return ax0 <= bx1 && bx0 <= ax1 && az0 <= bz1 && bz0 <= az1;
}
static void err_s(const char *what, const char *name)
{
    int n = 0, k;
    for (k = 0; what[k] && n < 70; k++) em_error[n++] = what[k];
    if (name) { em_error[n++] = ' '; for (k = 0; name[k] && n < 94; k++) em_error[n++] = name[k]; }
    em_error[n] = 0;
}

int em_check_geom(const EmMap *m, int *sec, int *wall)
{
    int si, i, oj, k;
    *sec = -1; *wall = -1;
    if (!m->nsecs) { err0("the map has no sectors"); return 1; }
    for (si = 0; si < m->nsecs; si++) {
        const EmSector *s = &m->secs[si];
        *sec = si;
        if (s->np < 3) { err_s("needs 3 points:", s->name); return 1; }
        if (s->room >= m->nrooms) { err_s("has no room:", s->name); return 1; }
        if (area2x(s) <= 0) { err_s("is inside out or flat:", s->name); return 1; }
        for (i = 0; i < s->np; i++) {
            int j = (i + 1) % s->np;
            *wall = i;
            if (s->x[i] == s->x[j] && s->z[i] == s->z[j]) { err_s("repeats a point:", s->name); return 1; }
            if (s->x[i] < -EM_LIMIT || s->x[i] > EM_LIMIT || s->z[i] < -EM_LIMIT || s->z[i] > EM_LIMIT) { err_s("is outside +-31 m:", s->name); return 1; }
            for (k = 0; k < s->np; k++) {
                int l = (k + 1) % s->np;
                if (k == i || k == j || l == i) continue;
                if (seg_cross(s->x[i], s->z[i], s->x[j], s->z[j], s->x[k], s->z[k], s->x[l], s->z[l])) { err_s("crosses itself:", s->name); return 1; }
            }
        }
        *wall = -1;
        if (s->kind == SK_ROOM && s->ceil < s->floor) { err_s("ceiling below floor:", s->name); return 1; }
        if (s->fslope >= s->np || s->cslope >= s->np) { err_s("slope wall out of range:", s->name); return 1; }
    }
    if (m->nsecs > 512) { err0("too many sectors (512)"); return 1; }
    /* sectors may only touch: no crossing walls, no corner inside another sector */
    for (si = 0; si < m->nsecs; si++) {
        const EmSector *a = &m->secs[si];
        for (oj = si + 1; oj < m->nsecs; oj++) {
            const EmSector *b = &m->secs[oj];
            if (!bbox_meet(a, b)) continue;
            for (i = 0; i < a->np; i++) {
                int j = (i + 1) % a->np;
                for (k = 0; k < b->np; k++) {
                    int l = (k + 1) % b->np;
                    if (seg_cross(a->x[i], a->z[i], a->x[j], a->z[j], b->x[k], b->z[k], b->x[l], b->z[l])) {
                        *sec = si; *wall = i; err_s("walls cross another sector:", a->name); return 1;
                    }
                }
            }
            for (k = 0; k < b->np; k++) {
                int on = 0, j;
                for (j = 0; j < a->np; j++) {
                    int jn = (j + 1) % a->np;
                    if ((a->x[j] == b->x[k] && a->z[j] == b->z[k]) || on_seg(a->x[j], a->z[j], a->x[jn], a->z[jn], b->x[k], b->z[k])) on = 1;
                }
                if (!on && em_inside(a, b->x[k], b->z[k])) { *sec = oj; err_s("overlaps another sector:", b->name); return 1; }
            }
            for (k = 0; k < a->np; k++) {
                int on = 0, j;
                for (j = 0; j < b->np; j++) {
                    int jn = (j + 1) % b->np;
                    if ((b->x[j] == a->x[k] && b->z[j] == a->z[k]) || on_seg(b->x[j], b->z[j], b->x[jn], b->z[jn], a->x[k], a->z[k])) on = 1;
                }
                if (!on && em_inside(b, a->x[k], a->z[k])) { *sec = si; err_s("overlaps another sector:", a->name); return 1; }
            }
            /* the same outline: one sits exactly on the other */
            if (a->np == b->np) {
                int same = 1;
                for (k = 0; k < a->np && same; k++) {
                    int f = 0;
                    for (i = 0; i < b->np; i++) if (b->x[i] == a->x[k] && b->z[i] == a->z[k]) f = 1;
                    same = f;
                }
                if (same) { *sec = oj; err_s("sits on top of another sector:", b->name); return 1; }
            }
        }
    }
    /* walls that meet must share both points (make_map: split them) */
    for (si = 0; si < m->nsecs; si++) {
        const EmSector *a = &m->secs[si];
        for (i = 0; i < a->np; i++) {
            int j = (i + 1) % a->np, matched = 0;
            for (oj = 0; oj < m->nsecs && !matched; oj++) {
                const EmSector *b = &m->secs[oj];
                for (k = 0; k < b->np; k++) {
                    int l = (k + 1) % b->np;
                    if (a->x[i] == b->x[l] && a->z[i] == b->z[l] && a->x[j] == b->x[k] && a->z[j] == b->z[k]) {
                        if (oj == si) { *sec = si; *wall = i; err_s("touches itself:", a->name); return 1; }
                        matched = 1; break;
                    }
                }
            }
            if (matched) continue;
            for (oj = 0; oj < m->nsecs; oj++) {
                const EmSector *b = &m->secs[oj];
                if (oj == si) continue;
                for (k = 0; k < b->np; k++) {
                    int l = (k + 1) % b->np;
                    if (collinear_overlap(a->x[i], a->z[i], a->x[j], a->z[j], b->x[k], b->z[k], b->x[l], b->z[l])) {
                        *sec = si; *wall = i; err_s("a wall half-meets another - split it:", a->name); return 1;
                    }
                }
            }
        }
    }
    *sec = -1; *wall = -1;
    em_error[0] = 0;
    return 0;
}

int em_check(const EmMap *m, int *sec, int *wall)
{
    int i, starts = 0, troopers = 0, pods = 0, fans = 0, picks = 0;
    if (em_check_geom(m, sec, wall)) return 1;
    for (i = 0; i < m->nthings; i++) {
        const EmThing *t = &m->things[i];
        if (t->type == TH_START) starts++;
        if (t->type == TH_TROOPER) troopers++;
        if (t->type == TH_KEY || t->type == TH_SHARD || t->type == TH_EXIT) picks++;
        if (t->type != TH_WAY && em_sector_at(m, t->x, t->z) < 0) { err_s("a thing is outside the map:", em_thing_names[t->type % 6]); return 1; }
    }
    if (!starts) { err0("the map needs a start point"); return 1; }
    if (troopers > 24) { err0("too many troopers (24)"); return 1; }
    (void)picks;
    for (i = 0; i < m->nprops; i++) { if (m->props[i].kind == PK_POD) pods++; if (m->props[i].kind == PK_FAN) fans++; }
    if (pods > 8) { err0("too many pods (8)"); return 1; }
    if (fans > 4) { err0("too many fans (4)"); return 1; }
    {
        int nl = 0;
        for (i = 0; i < m->nlights; i++) {
            const EmLight *L = &m->lights[i];
            if (L->grid) {
                s32 w = L->x1 > L->x ? L->x1 - L->x : L->x - L->x1, d = L->z1 > L->z ? L->z1 - L->z : L->z - L->z1;
                s32 nx = (s32)rdiv(w, L->step), nz = (s32)rdiv(d, L->step);
                nl += (nx < 1 ? 1 : nx) * (nz < 1 ? 1 : nz);
            } else nl++;
        }
        if (nl > 250) { err0("too many lights (250)"); return 1; }
    }
    em_error[0] = 0;
    return 0;
}


/* ------------------------------------------------------------------ editing operations */
static long long sq(long long v) { return v * v; }
static s32 isqrt64(long long v)
{
    long long r = 0, b = 1LL << 40;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return (s32)r;
}
/* distance^2 (units^2) from p to segment a-b, and where along it (0..4096) */
static long long seg_d2(s32 ax, s32 az, s32 bx, s32 bz, s32 px, s32 pz, int *t4)
{
    long long dx = bx - ax, dz = bz - az, L2 = dx * dx + dz * dz, t;
    s32 qx, qz;
    if (!L2) { if (t4) *t4 = 0; return sq(px - ax) + sq(pz - az); }
    t = (px - ax) * dx + (pz - az) * dz;
    if (t < 0) t = 0;
    if (t > L2) t = L2;
    qx = ax + (s32)(dx * t / L2); qz = az + (s32)(dz * t / L2);
    if (t4) *t4 = (int)(t * 4096 / L2);
    return sq(px - qx) + sq(pz - qz);
}

int em_near_point(const EmMap *m, s32 x, s32 z, s32 r, int *si, int *pi)
{
    int a, i; long long best = sq(r) + 1;
    *si = *pi = -1;
    for (a = 0; a < m->nsecs; a++)
        for (i = 0; i < m->secs[a].np; i++) {
            long long d = sq(m->secs[a].x[i] - x) + sq(m->secs[a].z[i] - z);
            if (d < best) { best = d; *si = a; *pi = i; }
        }
    return *si >= 0;
}
int em_near_wall(const EmMap *m, s32 x, s32 z, s32 r, int *si, int *wi)
{
    int a, i, in = em_sector_at(m, x, z);
    long long best = sq(r) + 1;
    *si = *wi = -1;
    for (a = 0; a < m->nsecs; a++) {
        const EmSector *s = &m->secs[a];
        for (i = 0; i < s->np; i++) {
            int j = (i + 1) % s->np;
            long long d = seg_d2(s->x[i], s->z[i], s->x[j], s->z[j], x, z, 0);
            /* the sector the point is in owns the wall when two share it; else the side the point is on */
            long long side = (long long)(s->x[j] - s->x[i]) * (z - s->z[i]) - (long long)(s->z[j] - s->z[i]) * (x - s->x[i]);
            if (a == in) d -= 1; else if (side < 0) d += 2;
            if (d < best) { best = d; *si = a; *wi = i; }
        }
    }
    return *si >= 0;
}

/* points equal to (x, z) in every sector move by (dx, dz): corners are shared by position */
int em_move_points(EmMap *m, const s32 *px, const s32 *pz, int n, s32 dx, s32 dz)
{
    static u8 mv[EM_SECS][EM_SV];
    int a, i, k, moved = 0;
    for (a = 0; a < m->nsecs; a++)
        for (i = 0; i < m->secs[a].np; i++) {
            mv[a][i] = 0;
            for (k = 0; k < n; k++) if (m->secs[a].x[i] == px[k] && m->secs[a].z[i] == pz[k]) mv[a][i] = 1;
        }
    for (a = 0; a < m->nsecs; a++)
        for (i = 0; i < m->secs[a].np; i++)
            if (mv[a][i]) {
                s32 nx = m->secs[a].x[i] + dx, nz = m->secs[a].z[i] + dz;
                if (nx < -EM_LIMIT || nx > EM_LIMIT || nz < -EM_LIMIT || nz > EM_LIMIT) return -1;
                m->secs[a].x[i] = (s16)nx; m->secs[a].z[i] = (s16)nz; moved++;
            }
    for (a = 0; a < m->nsecs; a++) fix_winding_m(m, a);
    return moved;
}

/* removes the corner at (x, z) from every sector that has it */
int em_delete_corner(EmMap *m, s32 x, s32 z)
{
    int a, i, n = 0;
    for (a = 0; a < m->nsecs; a++)
        for (i = 0; i < m->secs[a].np; i++)
            if (m->secs[a].x[i] == x && m->secs[a].z[i] == z) {
                if (m->secs[a].np <= 3) { err_s("that would leave a sector with 2 corners:", m->secs[a].name); return 0; }
            }
    for (a = 0; a < m->nsecs; a++)
        for (i = 0; i < m->secs[a].np; i++)
            if (m->secs[a].x[i] == x && m->secs[a].z[i] == z) { em_delete_point(m, a, i); n++; break; }
    return n;
}

/* where point (x, z) sits on sector si's outline: a corner (*corner = index) or a wall (*wall,
   inserted later); 0 if it's not on the outline (within 2 units) */
static int on_outline(const EmSector *s, s32 x, s32 z, int *corner, int *wall)
{
    int i;
    *corner = *wall = -1;
    for (i = 0; i < s->np; i++) if (sq(s->x[i] - x) + sq(s->z[i] - z) <= 4) { *corner = i; return 1; }
    for (i = 0; i < s->np; i++) {
        int j = (i + 1) % s->np;
        if (seg_d2(s->x[i], s->z[i], s->x[j], s->z[j], x, z, 0) <= 4) { *wall = i; return 1; }
    }
    return 0;
}

/* cuts sector si in two along the straight line a-b (both on its outline, the line inside it) */
int em_cut(EmMap *m, int si, s32 ax, s32 az, s32 bx, s32 bz)
{
    EmSector *s = &m->secs[si], *o;
    int ca, wa, cb, wb, ia, ib, i, n, k, ni;
    if (m->nsecs >= EM_SECS) { err0("too many sectors"); return 0; }
    if (!on_outline(s, ax, az, &ca, &wa) || !on_outline(s, bx, bz, &cb, &wb)) { err0("both ends of a cut must be on the sector's walls"); return 0; }
    if (sq(ax - bx) + sq(az - bz) < 64 * 64) { err0("the cut is too short"); return 0; }
    if (!em_inside(s, (ax + bx) / 2, (az + bz) / 2)) { err0("the cut runs outside the sector"); return 0; }
    for (i = 0; i < s->np; i++) {
        int j = (i + 1) % s->np;
        if (seg_cross(ax, az, bx, bz, s->x[i], s->z[i], s->x[j], s->z[j])) { err0("the cut crosses a wall"); return 0; }
        if (i != ca && i != cb && on_seg(ax, az, bx, bz, s->x[i], s->z[i])) { err0("the cut runs through a corner"); return 0; }
    }
    if (s->np + 2 > EM_SV) { err0("too many corners in that sector"); return 0; }
    /* put both ends in as corners */
    if (ca < 0) { em_insert_point(m, si, wa + 1, ax, az); if (cb >= 0 && cb > wa) cb++; if (wb >= 0 && wb > wa) wb++; ca = wa + 1; }
    if (cb < 0) { em_insert_point(m, si, wb + 1, bx, bz); if (ca > wb) ca++; cb = wb + 1; }
    if (ca == cb || (ca + 1) % s->np == cb || (cb + 1) % s->np == ca) { err0("the cut runs along a wall"); return 0; }
    ia = ca; ib = cb; n = s->np;
    /* loop 1: ia .. ib stays in si; loop 2: ib .. ia becomes the new sector */
    o = &m->secs[m->nsecs];
    *o = *s;
    {
        s16 x1[EM_SV], z1[EM_SV]; EmWall w1[EM_SV]; int n1 = 0, n2 = 0;
        s8 fs1 = -1, cs1 = -1, fs2 = -1, cs2 = -1;
        static int map1[EM_SV], map2[EM_SV];
        for (k = 0; k < EM_SV; k++) { map1[k] = map2[k] = -1; }
        for (i = ia;; i = (i + 1) % n) {
            x1[n1] = s->x[i]; z1[n1] = s->z[i]; w1[n1] = s->w[i];
            if (i == ib) { w1[n1].mid = w1[n1].low = w1[n1].up = -1; w1[n1].flags = 0; }   /* the new wall along the cut */
            else map1[i] = n1;
            n1++;
            if (i == ib) break;
        }
        for (i = ib;; i = (i + 1) % n) {
            o->x[n2] = s->x[i]; o->z[n2] = s->z[i]; o->w[n2] = s->w[i];
            if (i == ia) { o->w[n2].mid = o->w[n2].low = o->w[n2].up = -1; o->w[n2].flags = 0; }
            else map2[i] = n2;
            n2++;
            if (i == ia) break;
        }
        if (s->fslope >= 0) { fs1 = (s8)map1[s->fslope]; fs2 = (s8)map2[s->fslope]; }
        if (s->cslope >= 0) { cs1 = (s8)map1[s->cslope]; cs2 = (s8)map2[s->cslope]; }
        ni = m->nsecs;
        for (k = 0; k < m->nwcells; k++) {
            EmWCell *c = &m->wcells[k];
            if (c->sec != si) continue;
            if (map1[c->wall] >= 0) c->wall = (u8)map1[c->wall];
            else if (map2[c->wall] >= 0) { c->sec = (u16)ni; c->wall = (u8)map2[c->wall]; }
        }
        for (k = 0, n = m->nfcells; k < n && m->nfcells < EM_FCELLS; k++)
            if (m->fcells[k].sec == si) { m->fcells[m->nfcells] = m->fcells[k]; m->fcells[m->nfcells].sec = (u16)ni; m->nfcells++; }
        memcpy(s->x, x1, sizeof(s16) * (size_t)n1); memcpy(s->z, z1, sizeof(s16) * (size_t)n1); memcpy(s->w, w1, sizeof(EmWall) * (size_t)n1);
        s->np = (u8)n1; o->np = (u8)n2;
        /* a slope keeps its hinge in the piece that has it; the other piece keeps the slope's heights
           only if it has the hinge too (else it goes level at the hinge height) */
        s->fslope = fs1; s->cslope = cs1; o->fslope = fs2; o->cslope = cs2;
    }
    {   /* a fresh name for the new piece */
        int v = m->nsecs + 1, l, j, clash;
        for (;; v++) {
            char nm[16] = "s", d[8]; int nd = 0, w = v;
            l = 1;
            do { d[nd++] = (char)('0' + w % 10); w /= 10; } while (w && nd < 7);
            while (nd) nm[l++] = d[--nd];
            nm[l] = 0; clash = 0;
            for (j = 0; j < m->nsecs; j++) if (seq(m->secs[j].name, nm)) clash = 1;
            if (!clash) { scpy(o->name, nm, 16); break; }
        }
    }
    m->nsecs++;
    em_weld(m);
    return 1;
}

/* the first wall of sector si a ray from (x, z) along (dx, dz) meets: the hit point */
static int ray_out(const EmSector *s, s32 x, s32 z, s32 dx, s32 dz, s32 *hx, s32 *hz)
{
    int i, found = 0;
    long long bn = 0, bd = 1;
    for (i = 0; i < s->np; i++) {
        int j = (i + 1) % s->np;
        long long ex = s->x[j] - s->x[i], ez = s->z[j] - s->z[i];
        long long den = (long long)dx * ez - (long long)dz * ex, tn, un;
        if (!den) continue;
        tn = (long long)(s->x[i] - x) * ez - (long long)(s->z[i] - z) * ex;      /* t = tn / den along the ray */
        un = (long long)(s->x[i] - x) * dz - (long long)(s->z[i] - z) * dx;      /* u = un / den along the wall */
        if (den < 0) { den = -den; tn = -tn; un = -un; }
        if (tn <= 0 || un < 0 || un > den) continue;
        if (!found || tn * bd < bn * den) { bn = tn; bd = den; found = 1; }
    }
    if (!found) return 0;
    *hx = x + (s32)((long long)dx * bn / bd); *hz = z + (s32)((long long)dz * bn / bd);
    return 1;
}

/* a convex shape inside sector si becomes a sector of its own: the room is cut along the line of
   each of its edges (Build's "draw a sector inside a sector") */
int em_carve(EmMap *m, int si, const s32 *px, const s32 *pz, int n)
{
    int e, k, before = m->nsecs;
    for (e = 0; e < n; e++) {
        s32 ax = px[e], az = pz[e], bx = px[(e + 1) % n], bz = pz[(e + 1) % n];
        s32 mx = (ax + bx) / 2, mz = (az + bz) / 2, h1x, h1z, h2x, h2z;
        int piece = -1, w;
        /* the piece the edge's middle is in; an edge lying on a wall needs no cut */
        for (k = 0; k < m->nsecs; k++) {
            int ok = k == si || k >= before;
            if (!ok || !em_inside(&m->secs[k], mx, mz)) continue;
            piece = k;
        }
        if (piece < 0) continue;
        {
            const EmSector *s = &m->secs[piece];
            int on = 0;
            for (w = 0; w < s->np; w++) if (seg_d2(s->x[w], s->z[w], s->x[(w + 1) % s->np], s->z[(w + 1) % s->np], mx, mz, 0) <= 4) on = 1;
            if (on) continue;
            if (!ray_out(s, mx, mz, bx - ax, bz - az, &h1x, &h1z) || !ray_out(s, mx, mz, ax - bx, az - bz, &h2x, &h2z)) { err0("can't fit that shape in the room"); return 0; }
        }
        if (!em_cut(m, piece, h2x, h2z, h1x, h1z)) return 0;
    }
    /* the piece now matching the shape */
    {
        s32 cx = 0, cz = 0;
        for (e = 0; e < n; e++) { cx += px[e]; cz += pz[e]; }
        cx /= n; cz /= n;
        for (k = 0; k < m->nsecs; k++) if ((k == si || k >= before) && em_inside(&m->secs[k], cx, cz)) return k;
    }
    return -1;
}

/* a new sector out from wall w of sector si, dist units deep */
int em_extrude(EmMap *m, int si, int w, s32 dist)
{
    const EmSector *s = &m->secs[si];
    int j = (w + 1) % s->np;
    s32 ax = s->x[w], az = s->z[w], bx = s->x[j], bz = s->z[j];
    long long ex = bx - ax, ez = bz - az, L2 = ex * ex + ez * ez;
    s32 L, x[4], z[4];
    if (!L2) return -1;
    L = isqrt64(L2);
    if (L <= 0) return -1;
    /* outward = to the right of a -> b */
    x[0] = bx; z[0] = bz; x[1] = ax; z[1] = az;
    x[2] = ax + (s32)(ez * dist / L); z[2] = az - (s32)(ex * dist / L);
    x[3] = bx + (s32)(ez * dist / L); z[3] = bz - (s32)(ex * dist / L);
    return em_add_sector(m, x, z, 4, si);
}

/* a rectangular sector becomes a flight of steps climbing toward side dir (0 n 1 e 2 s 3 w), from
   its floor to `to` (make_map's stairs: 0.2 m a step unless steps > 0) */
int em_stairs(EmMap *m, int si, int dir, s32 to, int steps)
{
    EmSector base = m->secs[si];
    s32 x0 = 99999, x1 = -99999, z0 = 99999, z1 = -99999, h0 = base.floor;
    int i, k, n;
    if (base.np != 4) { err0("stairs need a 4-cornered rectangle"); return 0; }
    for (i = 0; i < 4; i++) {
        if (base.x[i] < x0) x0 = base.x[i];
        if (base.x[i] > x1) x1 = base.x[i];
        if (base.z[i] < z0) z0 = base.z[i];
        if (base.z[i] > z1) z1 = base.z[i];
    }
    for (i = 0; i < 4; i++) if ((base.x[i] != x0 && base.x[i] != x1) || (base.z[i] != z0 && base.z[i] != z1)) { err0("stairs need a square-cornered rectangle"); return 0; }
    {
        s32 d = to > h0 ? to - h0 : h0 - to;
        n = steps > 0 ? steps : (int)((d * 10 + 2047) / 2048);
        if (n < 2) n = 2;
    }
    if (m->nsecs + n - 1 > EM_SECS) { err0("too many sectors"); return 0; }
    {   /* drop the old sector's painted wall tiles: its walls are being replaced */
        int q = 0;
        for (k = 0; k < m->nwcells; k++) if (m->wcells[k].sec != si) m->wcells[q++] = m->wcells[k];
        m->nwcells = q;
    }
    for (k = 0; k < n; k++) {
        EmSector *c = k ? &m->secs[m->nsecs] : &m->secs[si];
        s32 a = k, b = k + 1, r0, r1, r2, r3;
#define LP(p, q, num) ((s32)rdiv((long long)(p) * n + ((long long)(q) - (p)) * (num), n))
        if (dir == 0) { r0 = x0; r1 = LP(z0, z1, a); r2 = x1; r3 = LP(z0, z1, b); }
        else if (dir == 2) { r0 = x0; r1 = LP(z1, z0, b); r2 = x1; r3 = LP(z1, z0, a); }
        else if (dir == 1) { r0 = LP(x0, x1, a); r1 = z0; r2 = LP(x0, x1, b); r3 = z1; }
        else { r0 = LP(x1, x0, b); r1 = z0; r2 = LP(x1, x0, a); r3 = z1; }
        *c = base;
        for (i = 0; i < EM_SV; i++) { c->w[i].mid = c->w[i].low = c->w[i].up = -1; c->w[i].flags = 0; }
        c->fslope = c->cslope = -1;
        c->floor = (s16)LP(h0, to, b);
#undef LP
        if (c->ceil < c->floor + 2048) c->ceil = (s16)(c->floor + 2048);
        c->f.def = LT_STAIR_TREAD; c->f.nrules = 0;
        c->np = 4;
        c->x[0] = (s16)r0; c->z[0] = (s16)r1; c->x[1] = (s16)r2; c->z[1] = (s16)r1;
        c->x[2] = (s16)r2; c->z[2] = (s16)r3; c->x[3] = (s16)r0; c->z[3] = (s16)r3;
        if (k) {
            char nm[16]; int l = 0, v = k; char d[6]; int nd = 0;
            while (base.name[l] && l < 11) { nm[l] = base.name[l]; l++; }
            nm[l++] = '_';
            do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 5);
            while (nd) nm[l++] = d[--nd];
            nm[l] = 0; scpy(c->name, nm, 16);
            m->nsecs++;
        }
    }
    em_weld(m);
    return n;
}

/* sectors si and the one across its wall w become one (the walls between them go) */
int em_join(EmMap *m, int si, int w)
{
    EmSector *a = &m->secs[si];
    int j = (w + 1) % a->np, oj, k, kk = -1, n = 0, i;
    s16 ox[2 * EM_SV], oz[2 * EM_SV]; EmWall ow[2 * EM_SV];
    for (oj = 0; oj < m->nsecs && kk < 0; oj++) {
        const EmSector *b = &m->secs[oj];
        if (oj == si) continue;
        for (k = 0; k < b->np; k++) {
            int l = (k + 1) % b->np;
            if (b->x[k] == a->x[j] && b->z[k] == a->z[j] && b->x[l] == a->x[w] && b->z[l] == a->z[w]) { kk = k; break; }
        }
        if (kk >= 0) break;
    }
    if (kk < 0) { err0("there's no sector on the other side of that wall"); return 0; }
    {
        const EmSector *b = &m->secs[oj];
        /* walk a's loop; where an edge is shared with b, go round b's loop instead (skipping all
           edges they share) */
        int start = -1, ai, guard = 0, side = 0, cur;
        for (i = 0; i < a->np; i++) {
            int ii = (i + 1) % a->np, sh = 0;
            for (k = 0; k < b->np; k++) { int l = (k + 1) % b->np; if (b->x[k] == a->x[ii] && b->z[k] == a->z[ii] && b->x[l] == a->x[i] && b->z[l] == a->z[i]) sh = 1; }
            if (!sh) { start = i; break; }
        }
        if (start < 0) { err0("can't join those"); return 0; }
        ai = start; cur = start;
        while (guard++ < 4 * EM_SV) {
            const EmSector *p = side ? b : a;
            const EmSector *q = side ? a : b;
            int nx = (cur + 1) % p->np, shared = 0;
            for (k = 0; k < q->np; k++) { int l = (k + 1) % q->np; if (q->x[k] == p->x[nx] && q->z[k] == p->z[nx] && q->x[l] == p->x[cur] && q->z[l] == p->z[cur]) shared = 1; }
            if (shared) {   /* jump to the other loop at this corner */
                for (k = 0; k < q->np; k++) if (q->x[k] == p->x[cur] && q->z[k] == p->z[cur]) break;
                side ^= 1; cur = k;
                continue;
            }
            if (n >= 2 * EM_SV) break;
            ox[n] = p->x[cur]; oz[n] = p->z[cur]; ow[n] = p->w[cur]; n++;
            cur = nx;
            if (!side && cur == ai) break;
        }
        if (n > EM_SV || n < 3) { err0("the joined sector would have too many corners"); return 0; }
        {
            int used = 0;
            for (i = 0; i < a->np; i++) for (k = 0; k < n; k++) if (a->x[i] == ox[k] && a->z[i] == oz[k]) used++;
            (void)used;
        }
        {   /* everything of a's and b's must be on the new outline, else they made a ring */
            int ok = 1;
            for (i = 0; i < b->np && ok; i++) {
                int f = 0;
                for (k = 0; k < n; k++) if (b->x[i] == ox[k] && b->z[i] == oz[k]) f = 1;
                if (!f) {   /* a corner of b only on shared edges is fine if it's an inner shared corner */
                    int l2, sh2 = 0;
                    for (l2 = 0; l2 < a->np; l2++) if (a->x[l2] == b->x[i] && a->z[l2] == b->z[i]) sh2 = 1;
                    if (!sh2) ok = 0;
                }
            }
            if (!ok) { err0("can't join those: they'd make a ring"); return 0; }
        }
    }
    a = &m->secs[si];
    for (i = 0; i < n; i++) { a->x[i] = ox[i]; a->z[i] = oz[i]; a->w[i] = ow[i]; }
    a->np = (u8)n;
    a->fslope = a->cslope = -1;
    {   /* wall tiles of a: dropped (indices changed) */
        int q = 0;
        for (k = 0; k < m->nwcells; k++) if (m->wcells[k].sec != si) m->wcells[q++] = m->wcells[k];
        m->nwcells = q;
    }
    em_delete_sector(m, oj);
    {   /* drop corners left in the middle of a straight wall */
        int si2 = oj < si ? si - 1 : si;
        EmSector *t = &m->secs[si2];
        for (i = 0; i < t->np && t->np > 3; i++) {
            int pv = (i + t->np - 1) % t->np, nx = (i + 1) % t->np;
            long long cr = (long long)(t->x[i] - t->x[pv]) * (t->z[nx] - t->z[i]) - (long long)(t->z[i] - t->z[pv]) * (t->x[nx] - t->x[i]);
            int shared = 0, o2, q;
            if (cr) continue;
            for (o2 = 0; o2 < m->nsecs; o2++) if (o2 != si2) for (q = 0; q < m->secs[o2].np; q++) if (m->secs[o2].x[q] == t->x[i] && m->secs[o2].z[q] == t->z[i]) shared = 1;
            if (!shared) { em_delete_point(m, si2, i); i--; }
        }
        fix_winding_m(m, si2);
        return si2;
    }
}

/* ------------------------------------------------------------------ compile to a MapDef */
static MapPt *c_pts; static MapWall *c_walls; static MapSector *c_secs; static u8 *c_spec; static s16 *c_props; static MapThing *c_things;
static int c_nspec, c_capspec, c_nprops, c_capprops;
static char c_roomname[EM_ROOMS][24]; static const char *c_rooms[EM_ROOMS];
static char c_name[24];
static MapDef c_def;
typedef struct { int off, len; } SpecKey;
static SpecKey *c_keys; static int c_nkeys, c_capkeys;

static int spec_add(const u8 *b, int n)
{
    int i;
    for (i = 0; i < c_nkeys; i++)
        if (c_keys[i].len == n && !memcmp(c_spec + c_keys[i].off, b, (size_t)n)) return c_keys[i].off;
    if (c_nspec + n > c_capspec) { c_capspec = (c_nspec + n) * 2 + 1024; c_spec = (u8 *)realloc(c_spec, (size_t)c_capspec); }
    if (c_nkeys == c_capkeys) { c_capkeys = c_capkeys * 2 + 256; c_keys = (SpecKey *)realloc(c_keys, sizeof(SpecKey) * (size_t)c_capkeys); }
    memcpy(c_spec + c_nspec, b, (size_t)n);
    c_keys[c_nkeys].off = c_nspec; c_keys[c_nkeys].len = n; c_nkeys++;
    c_nspec += n;
    return c_nspec - n;
}
/* wall spec bytes: nrows, top, rows (n, tiles), ncells, (col, row, tile)... */
static int spec_wall(const EmRows *r, const EmMap *m, int si, int w, int part)
{
    u8 b[256]; int n = 0, i, j, nc = 0, at;
    b[n++] = r->nrows; b[n++] = r->top;
    for (i = 0; i < r->nrows; i++) { b[n++] = r->n[i]; for (j = 0; j < r->n[i]; j++) b[n++] = r->t[i][j]; }
    at = n++;
    if (si >= 0)
        for (i = 0; i < m->nwcells && n < 250; i++) {
            const EmWCell *c = &m->wcells[i];
            if (c->sec == si && c->wall == w && c->part == part) { b[n++] = c->col; b[n++] = c->row; b[n++] = c->tile; nc++; }
        }
    b[at] = (u8)nc;
    return spec_add(b, n);
}
static int spec_flat(const EmFlat *f, const EmMap *m, int si, int ceil)
{
    u8 b[256]; int n = 0, i, nc = 0, at;
    b[n++] = f->def; b[n++] = f->nrules;
    for (i = 0; i < f->nrules; i++) { b[n++] = f->r[i].tile; b[n++] = f->r[i].sx; b[n++] = f->r[i].sz; b[n++] = f->r[i].ox; b[n++] = f->r[i].oz; }
    at = n++;
    for (i = 0; i < m->nfcells && n < 250; i++) {
        const EmFCell *c = &m->fcells[i];
        if (c->sec == si && c->ceil == ceil) { b[n++] = (u8)c->cx; b[n++] = (u8)c->cz; b[n++] = c->tile; nc++; }
    }
    b[at] = (u8)nc;
    return spec_add(b, n);
}
static int has_cells(const EmMap *m, int si, int w, int part)
{
    int i;
    for (i = 0; i < m->nwcells; i++) if (m->wcells[i].sec == si && m->wcells[i].wall == w && m->wcells[i].part == part) return 1;
    return 0;
}

static void op(int v)
{
    if (c_nprops == c_capprops) { c_capprops = c_capprops * 2 + 1024; c_props = (s16 *)realloc(c_props, sizeof(s16) * (size_t)c_capprops); }
    c_props[c_nprops++] = (s16)v;
}
static int cur_room, cur_base;
static void at_(const EmMap *m, s32 x, s32 z, int absolute, int has_h, s32 h)
{
    int si = em_sector_at(m, x, z), r = si >= 0 ? m->secs[si].room : 0, b;
    if (r != cur_room) { op(OP_CUR); op(r); cur_room = r; }
    b = absolute ? 0 : has_h ? h : (si >= 0 ? m->secs[si].floor : 0);
    if (b != cur_base) { op(OP_BASE); op(b); cur_base = b; }
}

const MapDef *em_compile(const EmMap *m, int strict)
{
    int si, i, np = 0;
    if (strict ? em_check(m, &si, &i) : em_check_geom(m, &si, &i)) return 0;
    for (si = 0; si < m->nsecs; si++) np += m->secs[si].np;
    free(c_pts); free(c_walls); free(c_secs); free(c_things);
    c_pts = (MapPt *)malloc(sizeof(MapPt) * (size_t)np);
    c_walls = (MapWall *)malloc(sizeof(MapWall) * (size_t)np);
    c_secs = (MapSector *)malloc(sizeof(MapSector) * (size_t)m->nsecs);
    c_things = (MapThing *)malloc(sizeof(MapThing) * (size_t)(m->nthings + 1));
    c_nspec = 0; c_nkeys = 0; c_nprops = 0;
    np = 0;
    for (si = 0; si < m->nsecs; si++) {
        const EmSector *s = &m->secs[si];
        MapSector *o = &c_secs[si];
        int p0 = np;
        for (i = 0; i < s->np; i++) {
            const EmWall *w = &s->w[i];
            int j = (i + 1) % s->np, oj, k, nxt = 0, mid, low, up;
            EmRows none;
            c_pts[np].x = s->x[i]; c_pts[np].z = s->z[i];
            for (oj = 0; oj < m->nsecs && !nxt; oj++) {
                const EmSector *b = &m->secs[oj];
                if (oj == si) continue;
                for (k = 0; k < b->np; k++) {
                    int l = (k + 1) % b->np;
                    if (b->x[k] == s->x[j] && b->z[k] == s->z[j] && b->x[l] == s->x[i] && b->z[l] == s->z[i]) { nxt = oj + 1; break; }
                }
            }
            /* same spec rules as make_map's emit_c: mid = the wall's rows or the sector's walls (with
               the wall's cells); low / up = the wall's or the sector's, else none (the builder then
               uses mid); cells on a part with no spec ride on a copy of the spec it falls back to */
            memset(&none, 0, sizeof(none)); none.top = 255;
            mid = spec_wall(w->mid >= 0 ? &m->rows[w->mid] : s->walls >= 0 ? &m->rows[s->walls] : &none, m, si, i, EM_MID);
            {
                int lr = w->low >= 0 ? w->low : s->lows, ur = w->up >= 0 ? w->up : s->ups;
                const EmRows *midr = w->mid >= 0 ? &m->rows[w->mid] : s->walls >= 0 ? &m->rows[s->walls] : &none;
                low = lr >= 0 || has_cells(m, si, i, EM_LOW) ? spec_wall(lr >= 0 ? &m->rows[lr] : midr, m, si, i, EM_LOW) : MAP_NONE;
                up = ur >= 0 || has_cells(m, si, i, EM_UP) ? spec_wall(ur >= 0 ? &m->rows[ur] : midr, m, si, i, EM_UP) : MAP_NONE;
            }
            c_walls[np].next = (u16)nxt; c_walls[np].mid = (u16)mid; c_walls[np].low = (u16)low; c_walls[np].up = (u16)up; c_walls[np].flags = w->flags;
            np++;
        }
        o->p0 = (u16)p0; o->np = s->np; o->floor = s->floor; o->ceil = s->ceil;
        o->fslope = s->fslope; o->cslope = s->cslope;
        o->fslope_h = s->fslope >= 0 ? s->fslope_h : 0; o->cslope_h = s->cslope >= 0 ? s->cslope_h : 0;
        o->ftex = (u16)spec_flat(&s->f, m, si, 0); o->ctex = (u16)spec_flat(&s->c, m, si, 1);
        o->room = s->room; o->kind = s->kind; o->lock = s->lock; o->flags = (u8)(s->kind == SK_DOOR ? s->flags & SF_SECRET : 0); o->ka = s->ka; o->kb = s->kb;
    }
    if (c_nspec >= 0xffff) { err0("map too big: tile specs"); return 0; }
    /* props: lights, halos, then the prop lines (compile_props) */
    cur_room = -1; cur_base = 0;
    for (i = 0; i < m->nlights; i++) {
        const EmLight *L = &m->lights[i];
        if (!L->grid) {
            at_(m, L->x, L->z, 1, 0, 0);
            op(OP_LIGHT); op(L->x); op(-L->h); op(L->z); op(L->r); op(L->g); op(L->b); op(L->rad);
        } else {
            s32 dx = L->x1 - L->x, dz = L->z1 - L->z, nx, nz, a, b;
            nx = (s32)rdiv(dx < 0 ? -dx : dx, L->step); nz = (s32)rdiv(dz < 0 ? -dz : dz, L->step);
            if (nx < 1) nx = 1;
            if (nz < 1) nz = 1;
            for (b = 0; b < nz; b++)
                for (a = 0; a < nx; a++) {
                    s32 x = L->x + (s32)fdiv((long long)dx * (2 * a + 1), 2 * nx), z = L->z + (s32)fdiv((long long)dz * (2 * b + 1), 2 * nz);
                    at_(m, x, z, 1, 0, 0);
                    op(OP_LIGHT); op(x); op(-L->h); op(z); op(L->r); op(L->g); op(L->b); op(L->rad);
                }
        }
    }
    for (i = 0; i < m->nsprites; i++) {
        const EmSprite *S = &m->sprites[i];
        at_(m, S->x, S->z, 1, 0, 0);
        op(OP_SPRITE); op(S->x); op(-S->h); op(S->z); op(S->size); op(S->r); op(S->g); op(S->b); op(S->anim);
    }
    for (i = 0; i < m->nprops; i++) {
        const EmProp *p = &m->props[i];
        s32 lx = p->x0 < p->x1 ? p->x0 : p->x1, hx = p->x0 < p->x1 ? p->x1 : p->x0, lz = p->z0 < p->z1 ? p->z0 : p->z1, hz = p->z0 < p->z1 ? p->z1 : p->z0;
        switch (p->kind) {
        case PK_BOX: case PK_BOXF:
            at_(m, (s32)fdiv(p->x0 + p->x1, 2), (s32)fdiv(p->z0 + p->z1, 2), 1, 0, 0);
            op(p->kind == PK_BOX ? OP_BOX : OP_BOXF); op(lx); op(-p->h1); op(lz); op(hx); op(-p->h0); op(hz);
            op(p->t0 == 255 ? -1 : p->t0); op(p->t1 == 255 ? 255 : p->t1); op(p->v0); op(p->flags & 3);
            if (p->kind == PK_BOXF) { op(p->t2 == 255 ? 255 : p->t2); op(ND_OF_DIR[p->dir & 3]); op(p->v1); }
            break;
        case PK_CRATE: at_(m, p->x0, p->z0, 0, 0, 0); op(OP_CRATE); op(p->x0); op(p->z0); op(p->h0); op(p->h1); break;
        case PK_CAN: at_(m, p->x0, p->z0, 0, 0, 0); op(OP_CAN); op(p->x0); op(p->z0); break;
        case PK_PILLAR: at_(m, p->x0, p->z0, 0, 0, 0); op(OP_PILLAR); op(p->x0); op(p->z0); op(p->h0); break;
        case PK_CONSOLE:
            at_(m, p->x0, p->z0, 0, p->flags & 1, p->h0);
            op(OP_CONSOLE); op(p->x0); op(p->z0); op(p->dir & 3); op(p->t0 ? LT_SCR_VITALS : LT_SCR_GRAPH); op(0);
            break;
        case PK_MACHINE:
            at_(m, (s32)fdiv(p->x0 + p->x1, 2), (s32)fdiv(p->z0 + p->z1, 2), 0, 0, 0);
            op(OP_MACHINE); op(lx); op(lz); op(hx); op(hz); op(p->h0); op(ND_OF_DIR[p->dir & 3]);
            break;
        case PK_POD: at_(m, p->x0, p->z0, 0, 0, 0); op(OP_POD); op(p->x0); op(p->z0); op(p->dir & 3); op(p->v0); break;
        case PK_RACK: at_(m, p->x0, p->z0, 0, 0, 0); op(OP_RACK); op(p->x0); op(p->z0); op(ND_OF_DIR[p->dir & 3]); break;
        case PK_FAN: at_(m, p->x0, p->z0, 1, 0, 0); op(OP_FAN); op(p->x0); op(-p->h0); op(p->z0); op(p->h1); op(p->v0); break;
        case PK_RAIL:
            at_(m, (s32)fdiv(p->x0 + p->x1, 2), (s32)fdiv(p->z0 + p->z1, 2), 1, 0, 0);
            if (p->z0 == p->z1) { op(OP_RAILX); op(p->x0); op(p->x1); op(p->z0); op(p->h0); }
            else { op(OP_RAILZ); op(p->z0); op(p->z1); op(p->x0); op(p->h0); }
            break;
        case PK_CYL:
            at_(m, p->x0, p->z0, 1, 0, 0);
            op(OP_CYL); op(p->x0); op(-p->h0); op(p->z0); op(p->h1); op(0); op(p->axis == 1 ? -p->h2 : p->h2); op(p->v0); op(p->t0);
            op(p->flags & (2 | 16 | 64)); op(p->v1); op(p->v2); op(p->axis); op(p->t1 == 255 ? -1 : p->t1); op(p->rep);
            break;
        case PK_TANK: case PK_CORE: at_(m, p->x0, p->z0, 0, 0, 0); op(OP_SPECIAL); op(p->kind == PK_TANK ? SPECIAL_TANK : SPECIAL_CORE); op(p->x0); op(p->z0); op(1); break;
        case PK_CELL: at_(m, p->x0, p->z0, 0, 0, 0); op(OP_SPECIAL); op(SPECIAL_CELL); op(p->x0); op(p->z0); op(p->dir & 3); break;
        default: break;
        }
    }
    op(OP_END);
    for (i = 0; i < m->nthings; i++) {
        const EmThing *t = &m->things[i];
        MapThing *o = &c_things[i];
        o->type = t->type; o->arg = t->arg; o->x = t->x; o->z = t->z; o->yaw = t->yaw; o->ax = t->ax; o->az = t->az;
    }
    if (!m->nthings) memset(c_things, 0, sizeof(MapThing));
    for (i = 0; i < m->nrooms; i++) { scpy(c_roomname[i], m->rooms[i].name, 24); c_rooms[i] = c_roomname[i]; }
    scpy(c_name, m->name, 24);
    c_def.name = c_name; c_def.pts = c_pts; c_def.npts = np; c_def.walls = c_walls; c_def.secs = c_secs; c_def.nsecs = m->nsecs;
    c_def.spec = c_spec; c_def.props = c_props; c_def.things = c_things; c_def.nthings = m->nthings; c_def.rooms = c_rooms; c_def.nrooms = m->nrooms;
    return &c_def;
}
#endif
