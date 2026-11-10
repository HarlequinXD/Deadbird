/*
 * The level editor's 3D view: fly through the level as the game draws it and paint single 2 m
 * tiles on walls, floors and ceilings. The crosshair (or the mouse / a tap) picks a tile: which
 * sector, which wall part (step, wall, upper, door face) and which column and row - worked out the
 * same way gen_lab's wall_part lays its tiles out - or which 2 m floor cell.
 *
 *   left stick: fly   right stick: look   L2 / R2: down / up
 *   d-pad up / down: raise / lower the floor or ceiling you're looking at (a step's floor, a
 *   soffit's ceiling)   X: paint   []: pick up the tile   L1 / R1: tile   /\: all tiles   O: 2D map
 */
#ifndef GEN_BAKED
#include "edint.h"
#include "level.h"
#include "ik.h"
#include "snd.h"
#include "vram.h"
#include "gen_lab.h"

int ed3d_tile = LT_MID_PANEL;
static s32 cx_, cy_, cz_;               /* camera (world, Y down) */
static int yaw, pitch = 80, closing;
static MATRIX view; static PROJ pr;
static int pick_sx = 160, pick_sy = 120, pick_free;   /* where the pick ray goes through the screen */
static int look_id = -1, move_id = -1, lx0, ly0, mx0, my0, mvx, mvy, tap_id = -1, tap_x, tap_y, tap_moved, rdrag;

/* what the ray hit */
enum { PK_NONE, PK_WALL, PK_FLOOR, PK_CEIL };
typedef struct {
    int what, sec, wall, part, col, row, cx, cz, tile, nsec;
    s32 hx, hz, h;                      /* hit point, up-positive height */
    /* the cell's outline: along the wall from (ax,az) to (bx,bz), heights h0..h1 */
    s32 ax, az, bx, bz, h0, h1;
} Hit3;
static Hit3 hit;

void ed3d_enter(s32 x, s32 z)
{
    int si = em_sector_at(&ED, x, z);
    cx_ = x; cz_ = z; cy_ = -((si >= 0 ? em_floor_at(&ED, si, x, z) : 0) + 1700);
    closing = 0; pitch = 80; pick_free = 0; pick_sx = 160; pick_sy = 120;
    level_debug_all_rooms(1);
    ed_toast(ed_rail_r() ? "DRAG LEFT TO FLY  RIGHT TO LOOK" : "X PAINTS  SQUARE COPIES A TILE  L1 R1 TILE");
}
int ed3d_closing(void) { int c = closing; closing = 0; return c; }

static void make_view(void)
{
    MATRIX rx, ry; SVECTOR p; VECTOR o;
    mat_rot_x(&rx, pitch); mat_rot_y(&ry, -yaw); mat_mul(&rx, &ry, &view);
    view.t[0] = view.t[1] = view.t[2] = 0;
    p.vx = (s16)(-cx_ >> 1); p.vy = (s16)(-cy_ >> 1); p.vz = (s16)(-cz_ >> 1);   /* in halves: s16 range */
    mat_apply(&view, &p, &o);
    view.t[0] = o.vx * 2; view.t[1] = o.vy * 2; view.t[2] = o.vz * 2;
    pr.h = 250; pr.ofx = SCREEN_W / 2; pr.ofy = SCREEN_H / 2; pr.near_z = 120;
}

/* ------------------------------------------------------------------ tiles as the builder lays them out */
static int pmod(int a, int m) { return m <= 0 ? 0 : ((a % m) + m) % m; }
static int fdiv(s32 a, s32 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static s32 len2(s32 dx, s32 dz) { long long v = (long long)dx * dx + (long long)dz * dz; long long r = 0, b = 1LL << 40; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return (s32)r; }

/* the spec (rows) the builder uses for a part, as em_compile picks it */
static const EmRows *part_rows(int si, int w, int part)
{
    static EmRows none;
    const EmSector *s = &ED.secs[si];
    int mid = s->w[w].mid >= 0 ? s->w[w].mid : s->walls, r;
    none.nrows = 0; none.top = 255;
    if (part == EM_MID) return mid >= 0 ? &ED.rows[mid] : &none;
    r = part == EM_LOW ? (s->w[w].low >= 0 ? s->w[w].low : s->lows) : (s->w[w].up >= 0 ? s->w[w].up : s->ups);
    if (r >= 0) return &ED.rows[r];
    return mid >= 0 ? &ED.rows[mid] : &none;
}
/* the tile at (col, row) of a part, cells included (wall_part) */
static int part_tile(int si, int w, int part, int col, int row, int toprow)
{
    const EmRows *r = part_rows(si, w, part);
    int t, i;
    if (row < 0) t = r->top;
    else if (!r->nrows) t = LT_DARK_METAL;
    else { int rr = row < r->nrows ? row : r->nrows - 1; t = r->t[rr][col % (r->n[rr] ? r->n[rr] : 1)]; }
    for (i = 0; i < ED.nwcells; i++) {
        const EmWCell *c = &ED.wcells[i];
        if (c->sec == si && c->wall == w && c->part == part && c->col == col && c->row == (row < 0 ? toprow : row)) t = c->tile;
    }
    return t;
}
static int flat_tile(int si, int ceil, int cx, int cz)
{
    const EmFlat *f = ceil ? &ED.secs[si].c : &ED.secs[si].f;
    int t = f->def, i;
    for (i = 0; i < f->nrules; i++) if (pmod(cx, f->r[i].sx) == f->r[i].ox && pmod(cz, f->r[i].sz) == f->r[i].oz) t = f->r[i].tile;
    for (i = 0; i < ED.nfcells; i++) if (ED.fcells[i].sec == si && ED.fcells[i].ceil == ceil && ED.fcells[i].cx == (s8)cx && ED.fcells[i].cz == (s8)cz) t = ED.fcells[i].tile;
    return t;
}

/* a wall part between bottom line (Ba at a, Bb at b) and top line (Ta, Tb), seen from the side
   where a -> b runs left to right; where the hit (t along from b, height h) falls in its grid */
static void wall_cell(Hit3 *o, s32 ax, s32 az, s32 bx, s32 bz, s32 Ba, s32 Bb, s32 Ta, s32 Tb, int anchor_top, int flags, const EmRows *r)
{
    s32 T = 2048, L = len2(ax - bx, az - bz), hb = Ba < Bb ? Ba : Bb, ht = Ta > Tb ? Ta : Tb, c0, t;
    int nb = 0, row = 0, col, toprow;
    if (L < 1) return;
    /* columns from b (wall_part) */
    if (flags & WF_ALIGN_END) c0 = L - T * ((L + T - 3) / T);
    else if (flags & WF_ALIGN_MID) c0 = -(T * ((L + T - 3) / T) - L) / 2;
    else {
        long long s0 = ((long long)bx * (ax - bx) + (long long)bz * (az - bz)) / L;
        s32 m = (s32)(s0 - (long long)T * fdiv((s32)(s0 >= -2000000000LL ? s0 : 0), T));
        c0 = m > 1 && m < T - 1 ? -m : 0;
    }
    t = len2(o->hx - bx, o->hz - bz);
    col = fdiv(t - c0, T);
    if (col < 0) col = 0;
    if (anchor_top) {
        row = fdiv(ht - o->h, T); if (row < 0) row = 0;
        o->h1 = ht - row * T; o->h0 = o->h1 - T; if (o->h0 < hb) o->h0 = hb;
        toprow = -1;
    } else {
        s32 lim = ht;
        if (r->top != 255 && ht - hb > 1) lim = ht - T > hb ? ht - T : hb;
        { s32 h; for (h = hb; h < lim - 1 && nb < 23; h += T) nb++; }
        toprow = nb;                                      /* the top band's index: the band count before it */
        if (r->top != 255 && o->h >= lim && ht - lim > 1) { row = -1; o->h0 = lim; o->h1 = ht; }
        else { row = fdiv(o->h - hb, T); if (row < 0) row = 0; if (row >= nb && nb) row = nb - 1; o->h0 = hb + row * T; o->h1 = o->h0 + T; if (o->h1 > lim) o->h1 = lim; }
    }
    o->col = col; o->row = row < 0 ? toprow : row;
    o->tile = part_tile(o->sec, o->wall, o->part, col, row, toprow);
    {   /* the cell's ends along the wall */
        s32 t0 = c0 + col * T, t1 = t0 + T;
        if (t0 < 0) t0 = 0;
        if (t1 > L) t1 = L;
        o->ax = bx + (s32)((long long)(ax - bx) * t1 / L); o->az = bz + (s32)((long long)(az - bz) * t1 / L);
        o->bx = bx + (s32)((long long)(ax - bx) * t0 / L); o->bz = bz + (s32)((long long)(az - bz) * t0 / L);
    }
}

static int is_solid(const EmSector *s) { return s->kind != SK_DOOR && s->ceil <= s->floor; }
static int neighbour(int si, int w, int *nw)
{
    const EmSector *s = &ED.secs[si];
    int j = (w + 1) % s->np, oj, k;
    for (oj = 0; oj < ED.nsecs; oj++) {
        const EmSector *o = &ED.secs[oj];
        if (oj == si) continue;
        for (k = 0; k < o->np; k++) { int l = (k + 1) % o->np; if (o->x[k] == s->x[j] && o->z[k] == s->z[j] && o->x[l] == s->x[w] && o->z[l] == s->z[w]) { *nw = k; return oj; } }
    }
    return -1;
}

/* the ray from the camera through screen point (sx, sy) */
static void cast(int sx, int sy)
{
    VECTOR dc, dw;
    s32 Ox = cx_, Oz = cz_, Oh = -cy_, Dx, Dz, Dh;
    long long bestn = 0, bestd = 1;      /* t = n / d (in D units) */
    int si, w, found = 0;
    Hit3 o;
    dc.vx = (sx - SCREEN_W / 2) * 16; dc.vy = (sy - SCREEN_H / 2) * 16; dc.vz = 250 * 16;
    mat_rot_tapply(&view, &dc, &dw);
    Dx = dw.vx; Dz = dw.vz; Dh = -dw.vy;
    memset(&hit, 0, sizeof(hit)); hit.what = PK_NONE;
#define CLOSER(n, d) (!found || (long long)(n) * bestd < bestn * (long long)(d))
    for (si = 0; si < ED.nsecs; si++) {
        const EmSector *s = &ED.secs[si];
        if (is_solid(s) || s->kind == SK_DOOR) continue;
        for (w = 0; w < s->np; w++) {
            int j = (w + 1) % s->np, ns, nwi = -1, part;
            s32 ax = s->x[w], az = s->z[w], bx = s->x[j], bz = s->z[j], ex = bx - ax, ez = bz - az, hx, hz, h;
            long long den = (long long)Dx * ez - (long long)Dz * ex, tn, un;
            if ((long long)ex * (Oz - az) - (long long)ez * (Ox - ax) <= 0) continue;   /* we're behind it */
            if (!den) continue;
            tn = (long long)(ax - Ox) * ez - (long long)(az - Oz) * ex;
            un = (long long)(ax - Ox) * Dz - (long long)(az - Oz) * Dx;
            if (den < 0) { den = -den; tn = -tn; un = -un; }
            if (tn <= 0 || un < 0 || un > den) continue;
            if (!CLOSER(tn, den)) continue;
            hx = Ox + (s32)((long long)Dx * tn / den); hz = Oz + (s32)((long long)Dz * tn / den); h = Oh + (s32)((long long)Dh * tn / den);
            {
                s32 fa = em_floor_at(&ED, si, ax, az), fb = em_floor_at(&ED, si, bx, bz), ca = em_ceil_at(&ED, si, ax, az), cb = em_ceil_at(&ED, si, bx, bz);
                s32 f = em_floor_at(&ED, si, hx, hz), c = em_ceil_at(&ED, si, hx, hz);
                if (h < f || h > c) continue;
                memset(&o, 0, sizeof(o));
                o.what = PK_WALL; o.sec = si; o.wall = w; o.hx = hx; o.hz = hz; o.h = h;
                ns = neighbour(si, w, &nwi);
                o.nsec = ns;
                if (ns < 0 || (is_solid(&ED.secs[ns]))) {
                    o.part = EM_MID;
                    wall_cell(&o, ax, az, bx, bz, fa, fb, ca, cb, 0, s->w[w].flags, part_rows(si, w, EM_MID));
                } else {
                    const EmSector *n = &ED.secs[ns];
                    s32 ofa = em_floor_at(&ED, ns, ax, az), ofb = em_floor_at(&ED, ns, bx, bz), oca = em_ceil_at(&ED, ns, ax, az), ocb = em_ceil_at(&ED, ns, bx, bz);
                    s32 of = em_floor_at(&ED, ns, hx, hz), oc = em_ceil_at(&ED, ns, hx, hz);
                    if (n->kind == SK_DOOR && h >= of && h <= oc) {      /* the shut door's face: the door sector's wall */
                        o.sec = ns; o.wall = nwi; o.part = EM_MID;
                        wall_cell(&o, ax, az, bx, bz, n->floor, n->floor, n->floor + n->ka, n->floor + n->ka, 0, 0, part_rows(ns, nwi, EM_MID));
                    } else if (h < of) {
                        s32 ta = ofa > fa ? ofa : fa, tb = ofb > fb ? ofb : fb;
                        if (ta > ca) ta = ca;
                        if (tb > cb) tb = cb;
                        o.part = EM_LOW;
                        wall_cell(&o, ax, az, bx, bz, fa, fb, ta, tb, 0, s->w[w].flags, part_rows(si, w, EM_LOW));
                    } else if (h > oc) {
                        s32 ba = oca < ca ? oca : ca, bb = ocb < cb ? ocb : cb;
                        if (ba < fa) ba = fa;
                        if (bb < fb) bb = fb;
                        o.part = EM_UP;
                        wall_cell(&o, ax, az, bx, bz, ba, bb, ca, cb, 1, s->w[w].flags, part_rows(si, w, EM_UP));
                    } else continue;                               /* through the opening */
                }
            }
            (void)part;
            hit = o; bestn = tn; bestd = den; found = 1;
        }
        /* floor and ceiling: planes along the ray (heights are linear, so two samples give the line) */
        {
            int cpart;
            for (cpart = 0; cpart < 2; cpart++) {
                s32 H0 = cpart ? em_ceil_at(&ED, si, Ox, Oz) : em_floor_at(&ED, si, Ox, Oz);
                s32 H1 = cpart ? em_ceil_at(&ED, si, Ox + Dx, Oz + Dz) : em_floor_at(&ED, si, Ox + Dx, Oz + Dz);
                long long num = (long long)H0 - Oh, den = (long long)Dh - (H1 - H0);
                s32 hx, hz;
                if (!den) continue;
                if (den < 0) { num = -num; den = -den; }
                if (num <= 0) continue;
                if (cpart ? Oh > H0 : Oh < H0) continue;                    /* from the wrong side */
                if (!CLOSER(num, den)) continue;
                hx = Ox + (s32)((long long)Dx * num / den); hz = Oz + (s32)((long long)Dz * num / den);
                if (!em_inside(s, hx, hz)) continue;
                if (s->kind == SK_LIFT && cpart == 0) { }
                memset(&o, 0, sizeof(o));
                o.what = cpart ? PK_CEIL : PK_FLOOR; o.sec = si; o.hx = hx; o.hz = hz; o.h = H0 + (s32)((long long)(H1 - H0) * num / den);
                o.cx = fdiv(hx, 2048); o.cz = fdiv(hz, 2048);
                o.tile = flat_tile(si, cpart, o.cx, o.cz);
                hit = o; bestn = num; bestd = den; found = 1;
            }
        }
    }
#undef CLOSER
}

/* ------------------------------------------------------------------ painting */
static void paint(void)
{
    int i;
    if (hit.what == PK_NONE) { ed_toast("NOTHING TO PAINT THERE"); return; }
    if (hit.tile == ed3d_tile) return;
    ed_undo_push();
    if (hit.what == PK_WALL) {
        for (i = 0; i < ED.nwcells; i++) {
            EmWCell *c = &ED.wcells[i];
            if (c->sec == hit.sec && c->wall == hit.wall && c->part == hit.part && c->col == hit.col && c->row == hit.row) { c->tile = (u8)ed3d_tile; break; }
        }
        if (i == ED.nwcells) {
            if (ED.nwcells >= EM_WCELLS) { ed_toast("TOO MANY PAINTED TILES"); return; }
            ED.wcells[i].sec = (u16)hit.sec; ED.wcells[i].wall = (u8)hit.wall; ED.wcells[i].part = (u8)hit.part;
            ED.wcells[i].col = (u8)hit.col; ED.wcells[i].row = (u8)hit.row; ED.wcells[i].tile = (u8)ed3d_tile; ED.nwcells++;
        }
    } else {
        int ceil = hit.what == PK_CEIL;
        if (hit.cx < -128 || hit.cx > 127 || hit.cz < -128 || hit.cz > 127) return;
        for (i = 0; i < ED.nfcells; i++) {
            EmFCell *c = &ED.fcells[i];
            if (c->sec == hit.sec && c->ceil == ceil && c->cx == hit.cx && c->cz == hit.cz) { c->tile = (u8)ed3d_tile; break; }
        }
        if (i == ED.nfcells) {
            if (ED.nfcells >= EM_FCELLS) { ed_toast("TOO MANY PAINTED TILES"); return; }
            ED.fcells[i].sec = (u16)hit.sec; ED.fcells[i].ceil = (u8)ceil; ED.fcells[i].cx = (s8)hit.cx; ED.fcells[i].cz = (s8)hit.cz; ED.fcells[i].tile = (u8)ed3d_tile; ED.nfcells++;
        }
    }
    ed_changed();
    ed_rebuild3d();
    snd_play(SFX_UI_OK, 2400, 0, 5200);
}
static void tile_picked(int t) { ed3d_tile = t; }
static int next_tile(int t, int d)
{
    int k;
    for (k = 0; k < em_tile_count; k++) {
        t = (t + d + em_tile_count) % em_tile_count;
        if (t != LT_PROPS_FREE && !(LT_ANIMATED(t) && ((t - LT_WALL_GRAPH) & 1))) return t;
    }
    return t;
}

/* ------------------------------------------------------------------ update */
void ed3d_update(u32 pad, u32 press)
{
    s32 fx = isin(yaw), fz = icos(yaw), sp = (pad & PAD_R2 && pad & PAD_L2) ? 0 : 70;
    int mvf = 0, mvs = 0, i;
    /* pointer: mouse look (right drag), a click paints where it points; touch: left half flies,
       right half looks, a tap picks */
    for (i = 0; i < ed_nptr; i++) {
        EdPtr *e = &ed_ptr[i];
        if (e->kind == EK_MOUSE_R) {
            if (e->ev == EP_DOWN) { rdrag = 1; lx0 = e->x; ly0 = e->y; }
            else if (e->ev == EP_MOVE && rdrag) { yaw = (yaw + (e->x - lx0) * 10) & 4095; pitch += (e->y - ly0) * 8; lx0 = e->x; ly0 = e->y; }
            else if (e->ev == EP_UP) rdrag = 0;
            continue;
        }
        if (e->kind == EK_MOUSE_L) {
            if (e->ev == EP_HOVER || e->ev == EP_MOVE) { if (!rdrag && e->y > 14 && e->y < ed_bar_y()) { pick_sx = e->x; pick_sy = e->y; pick_free = 1; } }
            if (e->ev == EP_DOWN) { if (eu_hit_at(e->x, e->y) >= 0) { /* the bars handle it */ } }
            if (e->ev == EP_UP && e->y > 14 && e->y < ed_bar_y() && eu_hit_at(e->x, e->y) < 0) { pick_sx = e->x; pick_sy = e->y; pick_free = 1; press |= PAD_CROSS; }
            continue;
        }
        /* touch */
        if (e->ev == EP_DOWN) {
            if (eu_hit_at(e->x, e->y) >= 0 || e->y < 14 || e->y >= ed_bar_y()) continue;
            tap_id = e->id; tap_x = e->x; tap_y = e->y; tap_moved = 0;
            if (e->x < (SCREEN_W - ed_rail_r()) / 2 && move_id < 0) { move_id = e->id; mx0 = e->x; my0 = e->y; mvx = mvy = 0; }
            else if (look_id < 0) { look_id = e->id; lx0 = e->x; ly0 = e->y; }
        } else if (e->ev == EP_MOVE) {
            if (e->id == tap_id && ((e->x - tap_x) * (e->x - tap_x) + (e->y - tap_y) * (e->y - tap_y)) > 36) tap_moved = 1;
            if (e->id == look_id) { yaw = (yaw + (e->x - lx0) * 12) & 4095; pitch += (e->y - ly0) * 10; lx0 = e->x; ly0 = e->y; }
            if (e->id == move_id) { mvx = e->x - mx0; mvy = e->y - my0; }
        } else if (e->ev == EP_UP) {
            if (e->id == tap_id && !tap_moved) { pick_sx = e->x; pick_sy = e->y; pick_free = 1; }
            if (e->id == tap_id) tap_id = -1;
            if (e->id == look_id) look_id = -1;
            if (e->id == move_id) { move_id = -1; mvx = mvy = 0; }
        }
    }
    if (ed_wheel) { cx_ += fx * ed_wheel * 600 >> 12; cz_ += fz * ed_wheel * 600 >> 12; }
    /* fly */
    if (pad & PAD_RIGHT) mvs += 4096;
    if (pad & PAD_LEFT) mvs -= 4096;
    mvf += ed_ly; mvs += ed_lx;
    if (move_id >= 0) { mvs += mvx * 160; mvf -= mvy * 160; }
    if (mvf > 4096) mvf = 4096;
    if (mvf < -4096) mvf = -4096;
    if (mvs > 4096) mvs = 4096;
    if (mvs < -4096) mvs = -4096;
    if (mvf || mvs) {
        cx_ += (fx * mvf >> 12) * sp >> 12; cz_ += (fz * mvf >> 12) * sp >> 12;
        cx_ += (fz * mvs >> 12) * sp >> 12; cz_ -= (fx * mvs >> 12) * sp >> 12;
        pick_free = 0;
    }
    if (pad & PAD_L2) cy_ += 50;
    if (pad & PAD_R2) cy_ -= 50;
    if (ed_rx || ed_ry) { yaw = (yaw + (ed_rx * 48 >> 12)) & 4095; pitch -= ed_ry * 30 >> 12; pick_free = 0; }
    if (pitch > 900) pitch = 900;
    if (pitch < -900) pitch = -900;
    if (cx_ < -40000) cx_ = -40000;
    if (cx_ > 40000) cx_ = 40000;
    if (cz_ < -40000) cz_ = -40000;
    if (cz_ > 40000) cz_ = 40000;
    if (cy_ < -40000) cy_ = -40000;
    if (cy_ > 40000) cy_ = 40000;
    make_view();
    cast(pick_free ? pick_sx : SCREEN_W / 2, pick_free ? pick_sy : SCREEN_H / 2);
    if (press & PAD_L1) ed3d_tile = next_tile(ed3d_tile, -1);
    if (press & PAD_R1) ed3d_tile = next_tile(ed3d_tile, 1);
    if (press & PAD_CROSS) paint();
    /* d-pad up / down: what you're looking at goes up / down a step (held: repeats) */
    {
        static int hold;
        int d = pad & PAD_UP ? 1 : pad & PAD_DOWN ? -1 : 0;
        if (!d) { if (hold) ed_height_done(); hold = 0; }
        else if ((press & (PAD_UP | PAD_DOWN)) || (++hold > 20 && !(hold % 10))) {
            int sec = -1, ceil = 0;
            if (!hold) hold = 1;
            if (hit.what == PK_FLOOR) sec = hit.sec;
            else if (hit.what == PK_CEIL) { sec = hit.sec; ceil = 1; }
            else if (hit.what == PK_WALL) {
                if (ED.secs[hit.sec].kind == SK_DOOR) { sec = hit.sec; ceil = 1; }             /* a door face: how high it opens */
                else if (hit.part == EM_LOW && hit.nsec >= 0) sec = hit.nsec;                  /* a step: the floor it leads up to */
                else if (hit.part == EM_UP && hit.nsec >= 0) { sec = hit.nsec; ceil = 1; }     /* a soffit: the ceiling beyond */
                else { sec = hit.sec; ceil = hit.h > (em_floor_at(&ED, hit.sec, hit.hx, hit.hz) + em_ceil_at(&ED, hit.sec, hit.hx, hit.hz)) / 2; }
            }
            if (sec < 0) ed_toast("LOOK AT A FLOOR, CEILING OR WALL");
            else if (ed_height(sec, ceil, d)) ed_rebuild3d();
        }
    }
    if (press & PAD_SQUARE) { if (hit.what != PK_NONE && hit.tile != 255) { ed3d_tile = hit.tile; ed_toast("TILE PICKED UP"); } }
    if (press & PAD_TRIANGLE) ed_open_tiles(ed3d_tile, tile_picked);
    if (press & (PAD_CIRCLE | PAD_SELECT)) closing = 1;
}

/* ------------------------------------------------------------------ drawing */
static int project(s32 x, s32 h, s32 z, s16 *sx, s16 *sy)
{
    SVECTOR p; VECTOR c;
    /* the view keeps translation in world units: transform relative to the camera to stay in s16 */
    s32 dx = x - cx_, dy = -h - cy_, dz = z - cz_;
    MATRIX r = view;
    if (dx < -32000 || dx > 32000 || dy < -32000 || dy > 32000 || dz < -32000 || dz > 32000) return 0;
    r.t[0] = r.t[1] = r.t[2] = 0;
    p.vx = (s16)dx; p.vy = (s16)dy; p.vz = (s16)dz;
    mat_apply(&r, &p, &c);
    if (c.vz < 200) return 0;
    *sx = (s16)(pr.ofx + c.vx * pr.h / c.vz); *sy = (s16)(pr.ofy + c.vy * pr.h / c.vz);
    return *sx > -600 && *sx < 900 && *sy > -600 && *sy < 900;
}
static void outline(void)
{
    s16 x[4], y[4];
    s32 px[4], pz[4], ph[4];
    int k, ok = 1;
    u32 c = (ed_t >> 3) & 1 ? RGB(255, 220, 90) : RGB(255, 120, 30);
    if (hit.what == PK_WALL) {
        px[0] = hit.bx; pz[0] = hit.bz; ph[0] = hit.h0; px[1] = hit.ax; pz[1] = hit.az; ph[1] = hit.h0;
        px[2] = hit.ax; pz[2] = hit.az; ph[2] = hit.h1; px[3] = hit.bx; pz[3] = hit.bz; ph[3] = hit.h1;
    } else if (hit.what != PK_NONE) {
        s32 x0 = hit.cx * 2048, z0 = hit.cz * 2048;
        int ceil = hit.what == PK_CEIL;
        px[0] = x0; pz[0] = z0; px[1] = x0 + 2048; pz[1] = z0; px[2] = x0 + 2048; pz[2] = z0 + 2048; px[3] = x0; pz[3] = z0 + 2048;
        for (k = 0; k < 4; k++) ph[k] = ceil ? em_ceil_at(&ED, hit.sec, px[k], pz[k]) : em_floor_at(&ED, hit.sec, px[k], pz[k]);
    } else return;
    for (k = 0; k < 4; k++) if (!project(px[k], ph[k], pz[k], &x[k], &y[k])) ok = 0;
    if (!ok) return;
    for (k = 0; k < 4; k++) eu_line(x[k], y[k], x[(k + 1) & 3], y[(k + 1) & 3], c, 1, 32);
}
static void markers(void)
{
    int i;
    for (i = 0; i < ED.nthings; i++) {
        const EmThing *t = &ED.things[i];
        int si = em_sector_at(&ED, t->x, t->z);
        s16 x, y;
        u32 c;
        if (t->type == TH_WAY) continue;
        if (!project(t->x, (si >= 0 ? em_floor_at(&ED, si, t->x, t->z) : 0) + 900, t->z, &x, &y)) continue;
        c = t->type == TH_START ? RGB(90, 240, 100) : t->type == TH_TROOPER ? RGB(235, 60, 45) : t->type == TH_KEY ? RGB(60, 210, 240) :
            t->type == TH_SHARD ? RGB(180, 120, 255) : RGB(255, 210, 40);
        eu_tri(x, y - 5, x + 4, y, x - 4, y, c, -1, 34);
        eu_tri(x - 4, y, x + 4, y, x, y + 5, c, -1, 34);
        if (y > 16 && y < 210 && x > 0 && x < 290) eu_text(t->type == TH_START ? "START" : t->type == TH_TROOPER ? "TROOPER" : t->type == TH_EXIT ? "EXIT" : "", x - 10, y + 7, c, 34);
    }
}
void ed3d_draw(FrameBuf *fb, int t)
{
    char b[64], *o;
    quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(4, 3, 6), RGB(10, 3, 3), NO_SEMI, OT_LEN - 1);
    make_view();
    level_set_ot_bias(40);
    level_draw(fb, &view, &pr, t);
    level_set_ot_bias(0);
    eu_clip.y0 = 14; eu_clip.y1 = 226;
    markers();
    outline();
    eu_clip.y0 = 0; eu_clip.y1 = SCREEN_H;
    /* crosshair */
    if (!pick_free) {
        int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
        eu_rect(cx - 2, cy, cx + 3, cy + 1, RGB(255, 200, 120), 31); eu_rect(cx, cy - 2, cx + 1, cy + 3, RGB(255, 200, 120), 31);
    }
    /* what's picked, top left */
    b[0] = 0; o = b;
    if (hit.what == PK_WALL) {
        static const char *const PART[3] = { " WALL", " STEP", " UPPER" };
        o = eu_str(o, ED.secs[hit.sec].name); o = eu_str(o, ED.secs[hit.sec].kind == SK_DOOR ? " DOOR" : PART[hit.part]);
        o = eu_str(o, " "); o = eu_int(o, hit.col); o = eu_str(o, ","); eu_int(o, hit.row);
    } else if (hit.what != PK_NONE) {
        o = eu_str(o, ED.secs[hit.sec].name); o = eu_str(o, hit.what == PK_CEIL ? " CEILING " : " FLOOR "); o = eu_int(o, hit.cx); o = eu_str(o, ","); eu_int(o, hit.cz);
    }
    {
        char *p; for (p = b; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    }
    if (b[0] && !ed_overlay_busy()) { int y = ed_top_h() + 2; eu_rect_semi(2, y, 8 + eu_text_w(b), y + 12, RGB(70, 80, 80), SEMI_SUB, 29); eu_text(b, 5, y + 2, RGB(255, 210, 160), 28); }
    /* the paint tile, bottom right */
    if (!ed_overlay_busy()) {   /* the paint tile, bottom right (left of the touch rail) */
        u16 tp, cl; int u, v, R = SCREEN_W - ed_rail_r(), B = ed_bar_y() - 2;
        ed_tile_tex(ed3d_tile, &tp, &cl, &u, &v);
        eu_panel(R - 46, B - 56, R - 2, B, 30);
        eu_tex(R - 40, B - 50, R - 8, B - 18, tp, cl, u, v, u + 63, v + 63, 28);
        eu_text_c("PAINT", R - 23, B - 12, RGB(255, 170, 70), 28);
        if (hit.what != PK_NONE && hit.tile != 255) {
            ed_tile_tex(hit.tile, &tp, &cl, &u, &v);
            eu_tex(R - 70, B - 30, R - 52, B - 12, tp, cl, u, v, u + 63, v + 63, 28);
            eu_frame(R - 71, B - 31, R - 51, B - 11, RGB(120, 60, 30), 29);
        }
        eu_rect_semi(2, B - 11, 6 + eu_text_w(em_tile_names[ed3d_tile]), B + 1, RGB(70, 80, 80), SEMI_SUB, 29);
        eu_text(em_tile_names[ed3d_tile], 4, B - 10, RGB(220, 180, 140), 28);
    }
    if (!ed_overlay_busy()) {
        static const u32 bt[6] = { PAD_CROSS, PAD_UP, PAD_DOWN, PAD_SQUARE, PAD_TRIANGLE, PAD_CIRCLE };
        static const char *const lb[6] = { "PAINT", "RAISE", "LOWER", "COPY", "TILES", "MAP" };
        /* touch: no sticks for going up and down, so they get buttons */
        static const u32 tbt[8] = { PAD_CROSS, PAD_UP, PAD_DOWN, PAD_SQUARE, PAD_TRIANGLE, PAD_R2, PAD_L2, PAD_CIRCLE };
        static const char *const tlb[8] = { "PAINT", "RAISE", "LOWER", "COPY", "TILES", "FLY UP", "FLY DOWN", "2D MAP" };
        if (ed_rail_r()) ed_prompts(tbt, tlb, 8); else ed_prompts(bt, lb, 6);
    }
}
#endif
