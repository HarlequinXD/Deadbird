#include "fx.h"
#include "hud.h"

static u32 seed = 0x1234567u;
u32 fx_rand(void) { seed = seed * 1664525u + 1013904223u; return seed >> 8; }

/* ------------------------------------------------------------------ hit particles */
#define NPART 192
typedef struct { s32 x, y, z, vx, vy, vz; int life, max, kind; } Part;
static Part parts[NPART];

#define NPOOL 8
typedef struct { s32 x, y, z, r, grow; } Pool;
static Pool pools[NPOOL];

void fx_reset(void) { memset(parts, 0, sizeof(parts)); memset(pools, 0, sizeof(pools)); }

static int spawn(const VECTOR *at, int kind, int speed)
{
    int i;
    for (i = 0; i < NPART; i++) if (parts[i].life <= 0) break;
    if (i == NPART) return -1;
    parts[i].x = at->vx; parts[i].y = at->vy; parts[i].z = at->vz;
    parts[i].vx = (s32)(fx_rand() % (u32)(speed * 2)) - speed;
    parts[i].vy = -(s32)(fx_rand() % (u32)speed) - speed / 3;
    parts[i].vz = (s32)(fx_rand() % (u32)(speed * 2)) - speed;
    parts[i].max = parts[i].life = 14 + (int)(fx_rand() % 14);
    parts[i].kind = kind;
    return i;
}

/* directional burst: kind 0 hot blood mist, 1 dark blood, 2 sparks, 3 blue laser sparks, 4 heavy blood drops */
void fx_spray(const VECTOR *at, int kind, int count, s32 dx, s32 dy, s32 dz, int spread)
{
    int i;
    for (i = 0; i < count; i++) {
        int k = spawn(at, kind, spread);
        if (k < 0) return;
        parts[k].vx += dx; parts[k].vy += dy; parts[k].vz += dz;
        if (kind == 4) parts[k].max = parts[k].life = 26 + (int)(fx_rand() % 22);
    }
}

void fx_blood_pool(s32 x, s32 y, s32 z, s32 radius)
{
    int i, k = 0;
    for (i = 0; i < NPOOL; i++) if (pools[i].r == 0) { k = i; break; } else if (pools[i].grow < pools[k].grow) k = i;
    pools[k].x = x; pools[k].y = y; pools[k].z = z; pools[k].r = 60; pools[k].grow = radius;
}

void fx_hit(const VECTOR *at, int power, int blocked)
{
    int i, n = blocked ? 8 : 10 + power / 8;
    for (i = 0; i < n; i++) spawn(at, blocked ? 2 : 0, blocked ? 30 : 46);
    if (!blocked) for (i = 0; i < n / 2; i++) spawn(at, 1, 26);          /* dark blood */
}

void fx_update(void)
{
    int i;
    for (i = 0; i < NPART; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        p->life--;
        p->x += p->vx; p->y += p->vy; p->z += p->vz;
        p->vy += p->kind == 1 || p->kind == 4 ? 6 : 3;     /* gravity (Y down) */
        if (p->y > -10) { p->y = -10; p->vy = p->kind == 4 ? 0 : -p->vy / 3; p->vx /= p->kind == 4 ? 6 : 2; p->vz /= p->kind == 4 ? 6 : 2; }
    }
    for (i = 0; i < NPOOL; i++) if (pools[i].r && pools[i].r < pools[i].grow) pools[i].r += 1 + (pools[i].grow - pools[i].r) / 40;
}

void fx_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr)
{
    int i;
    for (i = 0; i < NPART; i++) {
        Part *p = &parts[i];
        SVECTOR w; VECTOR c; s16 sx, sy;
        int s, k, a;
        POLY *q;
        if (p->life <= 0) continue;
        w.vx = (s16)p->x; w.vy = (s16)p->y; w.vz = (s16)p->z;
        mat_apply(view, &w, &c);
        if (!rot_trans_pers(pr, &c, &sx, &sy)) continue;
        s = 2 + 2400 / (c.vz > 400 ? c.vz : 400);
        if (p->kind == 4) s += s / 2;
        a = p->life * 255 / p->max;
        q = prim_alloc(fb);
        if (!q) return;
        memset(q, 0, sizeof(*q));
        q->code = PF_QUAD | PF_SEMI;
        q->tpage = get_tpage(0, p->kind == 1 || p->kind == 4 ? SEMI_SUB : SEMI_ADD, 0, 0);
        q->v[0].x = (s16)(sx - s); q->v[0].y = (s16)(sy - s);
        q->v[1].x = (s16)(sx + s); q->v[1].y = (s16)(sy - s);
        q->v[2].x = (s16)(sx - s); q->v[2].y = (s16)(sy + s);
        q->v[3].x = (s16)(sx + s); q->v[3].y = (s16)(sy + s);
        for (k = 0; k < 1; k++) {
            if (p->kind == 0) { q->v[k].r = 255; q->v[k].g = (u8)(a * 3 / 4); q->v[k].b = (u8)(a / 5); }
            else if (p->kind == 1) { q->v[k].r = 30; q->v[k].g = (u8)(a / 3); q->v[k].b = (u8)(a / 3); }
            else if (p->kind == 4) { q->v[k].r = 10; q->v[k].g = (u8)(110 + a / 3); q->v[k].b = (u8)(100 + a / 3); }   /* deep red */
            else if (p->kind == 3) { q->v[k].r = (u8)(a / 5); q->v[k].g = (u8)(a / 2); q->v[k].b = (u8)a; }
            else { q->v[k].r = (u8)(a * 3 / 4); q->v[k].g = (u8)(a * 3 / 4); q->v[k].b = (u8)a; }
        }
        ot_add(fb, (c.vz >> 4) - 2, q);
    }
    for (i = 0; i < NPOOL; i++) {       /* blood pools: dark red octagons spreading over the floor */
        static const s16 ox[8] = { 4096, 2896, 0, -2896, -4096, -2896, 0, 2896 }, oz[8] = { 0, 2896, 4096, 2896, 0, -2896, -4096, -2896 };
        Pool *pp = &pools[i];
        SVECTOR w; VECTOR c; s16 cx, cy, px[8], py[8];
        int k, ok = 1;
        if (!pp->r) continue;
        w.vx = (s16)pp->x; w.vy = (s16)(pp->y - 3); w.vz = (s16)pp->z;
        mat_apply(view, &w, &c);
        if (!rot_trans_pers(pr, &c, &cx, &cy)) continue;
        for (k = 0; k < 8 && ok; k++) {
            s32 rr = pp->r * (k & 1 ? 9 : 11) / 10;
            w.vx = (s16)(pp->x + (rr * ox[k] >> 12)); w.vz = (s16)(pp->z + (rr * oz[k] >> 12));
            mat_apply(view, &w, &c);
            ok = rot_trans_pers(pr, &c, &px[k], &py[k]);
        }
        if (!ok) continue;
        for (k = 0; k < 8; k++) {
            POLY *q = prim_alloc(fb);
            int n = (k + 1) & 7;
            if (!q) return;
            memset(q, 0, sizeof(*q));
            q->code = PF_SEMI | PF_GOURAUD;
            q->tpage = get_tpage(0, SEMI_SUB, 0, 0);
            q->v[0].x = cx; q->v[0].y = cy; q->v[0].r = 30; q->v[0].g = 150; q->v[0].b = 140;
            q->v[1].x = px[k]; q->v[1].y = py[k]; q->v[1].r = 10; q->v[1].g = 90; q->v[1].b = 84;
            q->v[2].x = px[n]; q->v[2].y = py[n]; q->v[2].r = 10; q->v[2].g = 90; q->v[2].b = 84;
            ot_add(fb, OT_LEN - 10, q);
        }
    }
}

/* ------------------------------------------------------------------ hell sky */
void sky_draw(FrameBuf *fb, int t, int camx)
{
    static const s16 cs[9] = { 4096, 2896, 0, -2896, -4096, -2896, 0, 2896, 4096 };
    static const s16 sn[9] = { 0, 2896, 4096, 2896, 0, -2896, -4096, -2896, 0 };
    int i, k, mx = 236 - camx / 80, my = 54, pulse = isin(t * 18) * 20 >> 12;

    /* sky: bruised black-purple at the top to a burning horizon */
    quad_g(fb, 0, 0, 320, 70, RGB(10, 2, 8), RGB(46, 6, 12), NO_SEMI, OT_LEN - 1);
    quad_g(fb, 0, 70, 320, 132, RGB(46, 6, 12), RGB(150 + pulse, 34, 10), NO_SEMI, OT_LEN - 1);
    quad_g(fb, 0, 132, 320, 240, RGB(150 + pulse, 34, 10), RGB(30, 4, 4), NO_SEMI, OT_LEN - 1);

    /* blood moon with a glow halo (additive) */
    for (k = 0; k < 8; k++) {
        tri_f(fb, mx, my, mx + (30 * cs[k] >> 12), my + (30 * sn[k] >> 12), mx + (30 * cs[k + 1] >> 12), my + (30 * sn[k + 1] >> 12),
              RGB(40, 6, 4), SEMI_ADD, OT_LEN - 2);
        tri_f(fb, mx, my, mx + (19 * cs[k] >> 12), my + (19 * sn[k] >> 12), mx + (19 * cs[k + 1] >> 12), my + (19 * sn[k + 1] >> 12),
              RGB(170, 40, 22), NO_SEMI, OT_LEN - 3);
    }

    /* far spires (slow parallax) and near crags (faster), jagged black-red silhouettes */
    for (i = 0; i < 14; i++) {
        int x = ((i * 53 + 7) % 360) - 20 - camx / 60;
        int h = 28 + (i * 37) % 46;
        x = ((x % 360) + 360) % 360 - 20;
        tri_f(fb, x - 14, 132, x + 14, 132, x + (i & 1 ? 3 : -2), 132 - h, RGB(34, 6, 8), NO_SEMI, OT_LEN - 4);
    }
    for (i = 0; i < 9; i++) {
        int x = ((i * 71 + 30) % 400) - camx / 30;
        int h = 18 + (i * 29) % 30;
        x = ((x % 400) + 400) % 400 - 40;
        tri_f(fb, x - 26, 136, x + 26, 136, x + (i & 1 ? 6 : -5), 136 - h, RGB(14, 2, 3), NO_SEMI, OT_LEN - 5);
    }
}

void embers_draw(FrameBuf *fb, int t, int ot)
{
    int i;
    for (i = 0; i < 40 * SCREEN_W / 320; i++) {
        /* deterministic drifting embers: each loops upward with its own speed and sway */
        int speed = 1 + (i * 7) % 3, period = 260;
        int y = 250 - ((t * speed + i * 97) % period);
        int x = (i * 83) % SCREEN_W + (isin(t * (20 + i % 7) + i * 500) * 9 >> 12);
        int b = 120 + (isin(t * 60 + i * 300) * 100 >> 12);
        int s = 1 + (i % 3 == 0);
        if (b < 30) b = 30;
        quad_g(fb, x, y, x + s, y + s, RGB(b, b / 3, b / 10), RGB(b, b / 4, 0), SEMI_ADD, ot);
    }
}

/* ------------------------------------------------------------------ arena */
#define AN_X 14
#define AN_Z 10
#define AT 640
void arena_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t)
{
    static s16 gx[AN_Z + 1][AN_X + 1], gy[AN_Z + 1][AN_X + 1];
    static s32 gz[AN_Z + 1][AN_X + 1];
    static u8 gok[AN_Z + 1][AN_X + 1];
    int i, j, k;
    for (j = 0; j <= AN_Z; j++)
        for (i = 0; i <= AN_X; i++) {
            SVECTOR w; VECTOR c;
            w.vx = (s16)((i - AN_X / 2) * AT); w.vy = 0; w.vz = (s16)((j - 6) * AT);
            mat_apply(view, &w, &c);
            gok[j][i] = (u8)rot_trans_pers(pr, &c, &gx[j][i], &gy[j][i]);
            gz[j][i] = c.vz;
        }
    for (j = 0; j < AN_Z; j++)
        for (i = 0; i < AN_X; i++) {
            static const int ci[4] = { 0, 1, 0, 1 }, cj[4] = { 0, 0, 1, 1 };
            POLY *p;
            int lava = ((i * 7 + j * 13) % 5) == 0;               /* some tiles are cracked open */
            int glow = 140 + (isin(t * 50 + i * 700 + j * 300) * 80 >> 12);
            if (!gok[j][i] || !gok[j][i + 1] || !gok[j + 1][i] || !gok[j + 1][i + 1]) continue;
            p = prim_alloc(fb);
            if (!p) return;
            memset(p, 0, sizeof(*p));
            p->code = PF_QUAD | PF_GOURAUD;
            for (k = 0; k < 4; k++) {
                int x = i + ci[k], y = j + cj[k];
                int fog = 4096 - (gz[y][x] - 3000) * 4096 / 8000;
                int edge = 4096 - ((x - AN_X / 2) * (x - AN_X / 2) * 4096 / 60);
                int lv, r, g, b;
                if (fog > 4096) fog = 4096;
                if (fog < 0) fog = 0;
                if (edge < 0) edge = 0;
                lv = (fog >> 2) * (edge >> 2) >> 10;              /* 0..1024 */
                if (lava && ((k + i + j) & 1)) { r = glow; g = glow / 3; b = 10; }
                else { r = ((i + j) & 1) ? 56 : 44; g = 18; b = 20; }
                p->v[k].x = gx[y][x]; p->v[k].y = gy[y][x];
                p->v[k].r = (u8)(r * lv >> 10); p->v[k].g = (u8)(g * lv >> 10); p->v[k].b = (u8)(b * lv >> 10);
            }
            ot_add(fb, OT_LEN - 8, p);
        }
}

void blob_shadow(FrameBuf *fb, const MATRIX *view, const PROJ *pr, s32 x, s32 y, s32 z)
{
    static const s16 ox[8] = { 420, 300, 0, -300, -420, -300, 0, 300 };
    static const s16 oz[8] = { 0, 220, 300, 220, 0, -220, -300, -220 };
    s16 cx, cy, px[8], py[8];
    SVECTOR w; VECTOR c;
    int k;
    w.vx = (s16)x; w.vy = (s16)(y - 4); w.vz = (s16)z;
    mat_apply(view, &w, &c);
    if (!rot_trans_pers(pr, &c, &cx, &cy)) return;
    for (k = 0; k < 8; k++) {
        w.vx = (s16)(x + ox[k]); w.vz = (s16)(z + oz[k]);
        mat_apply(view, &w, &c);
        if (!rot_trans_pers(pr, &c, &px[k], &py[k])) return;
    }
    for (k = 0; k < 8; k++) {
        POLY *p = prim_alloc(fb);
        int n = (k + 1) & 7;
        if (!p) return;
        memset(p, 0, sizeof(*p));
        p->code = PF_SEMI | PF_GOURAUD;
        p->tpage = get_tpage(0, SEMI_SUB, 0, 0);
        p->v[0].x = cx; p->v[0].y = cy; p->v[0].r = p->v[0].g = p->v[0].b = 70;
        p->v[1].x = px[k]; p->v[1].y = py[k];
        p->v[2].x = px[n]; p->v[2].y = py[n];
        ot_add(fb, OT_LEN - 9, p);
    }
}
