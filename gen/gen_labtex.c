/*
 * Procedural sci-fi textures for the Deadbird plant (painted at start-up).
 *
 * Texel density is fixed at 32 texels per metre: every 64x64 tile covers 2 x 2 metres of wall or
 * floor. Wall details - painted pipes, hazard paint, posters, signs, door surrounds, room stencils,
 * wall monitors, cable trays - are painted INTO the textures, so walls are plain big quads with no
 * floating decal polygons. Lighting is baked into the paint (bevels, rivet shadows, grime,
 * occlusion toward floor and corners). Every 64x64 tile gets its own 16-colour CLUT (4bpp pages).
 *
 * Pages (256x256 = 16 tiles each), in the order of the LabTile enum:
 *   WALL1  generic wall bands, door surrounds      PROPS  machines and props
 *   WALL2  room-specific walls and hall walls      FLOOR  floors, ceilings, stairs
 *   TRIM   trim for 3D props                       ANIM   animated screens: 4-frame strips
 */
#include <stdlib.h>
#include <string.h>
#include "gpu.h"
#include "vram.h"
#include "gen_lab.h"
#include "tex.h"
#include "gmath.h"

#define S(x) static void x(Img im)

/* ================================================================ WALL1 */
S(base_plain) { panel_base(im, C3(88, 94, 104), 1); hpipe(im, 18, 2, C3(110, 120, 126), 1); kickplate(im, 10); hazard_band(im, 60, 64, 0); grime(im, 0.35f, 201, 1); floor_ao(im, 0.4f, 14); }
S(base_pipes)
{
    panel_base(im, C3(84, 90, 100), 1);
    hpipe(im, 14, 4, C3(120, 128, 132), 1); hpipe(im, 27, 3, C3(150, 80, 40), 1); hpipe(im, 37, 2, C3(60, 110, 150), 1);
    valve(im, 44, 14, C3(200, 40, 30)); kickplate(im, 10); grime(im, 0.4f, 202, 1); floor_ao(im, 0.4f, 14);
}
S(base_vent)
{
    panel_base(im, C3(88, 94, 104), 1);
    inset(im, 8, 20, 56, 48, C3(36, 38, 44));
    for (int y = 23; y < 46; y += 3) { rect(im, 10, y, 54, y + 1, C3(96, 100, 108)); rect(im, 10, y + 1, 54, y + 2, C3(24, 26, 30)); }
    kickplate(im, 10); grime(im, 0.35f, 203, 1); floor_ao(im, 0.4f, 14);
}
S(base_hazard)
{
    t_new(im, C3(80, 80, 84)); hazard_band(im, 4, 54, 0); BEVEL(im, 0, 0, 64, 4, C3(90, 94, 100));
    kickplate(im, 10); grime(im, 0.45f, 204, 1); scratches(im, 16, 204, C3(120, 110, 90)); floor_ao(im, 0.4f, 14);
}
S(mid_panel)
{
    panel_base(im, C3(94, 100, 110), 1);
    for (int px = 0; px <= 32; px += 32) { rect(im, px + 6, 44, px + 26, 47, C3(60, 160, 170)); rect(im, px + 6, 45, px + 26, 46, C3(150, 235, 240)); }
    rect(im, 4, 8, 16, 13, C3(200, 170, 40)); ttext(im, "B7", 6, 8, C3(30, 30, 30), 1); grime(im, 0.3f, 211, 1);
}
S(mid_poster_power)
{
    panel_base(im, C3(92, 98, 108), 1);
    poster(im, 12, 6, 52, 58, C3(18, 30, 46), C3(180, 180, 170), 1);
    skull_icon(im, 22, 10, C3(220, 210, 190), C3(255, 120, 20), 3);
    for (int k = 0; k < 5; k++) rect(im, 30 + (k % 2) * 3, 33 + k * 2, 34 + (k % 2) * 3, 35 + k * 2, C3(90, 230, 255));
    ttext(im, "SHINI", 21, 45, C3(90, 230, 255), 1); ttext(im, "POWER", 21, 51, C3(230, 230, 220), 1); grime(im, 0.3f, 212, 1);
}
static void trefoil(Img im, int cx, int cy, float r0, float r1, Col c)
{
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
        float a = gatan2((float)(y - cy), (float)(x - cx)), d = gsqrt((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
        if ((d > r0 && d < r1 && ((int)gfloor((a + GPI) / (GPI / 3)) % 2) == 0) || d < 3) t_set(im, x, y, c);
    }
}
S(mid_poster_danger)
{
    panel_base(im, C3(92, 98, 108), 1);
    poster(im, 10, 8, 54, 56, C3(210, 180, 40), C3(30, 28, 26), 1);
    trefoil(im, 32, 28, 4, 13, C3(30, 28, 26));
    ttext(im, "DANGER", 21, 44, C3(30, 28, 26), 1); ttext(im, "SOUL RAD", 17, 50, C3(140, 30, 20), 1); grime(im, 0.3f, 213, 1);
}
S(mid_authorized)
{
    panel_base(im, C3(92, 98, 108), 1);
    rect(im, 4, 18, 60, 46, C3(170, 30, 26)); rect(im, 6, 20, 58, 44, C3(200, 40, 32));
    tctext(im, "AUTHORIZED", 23, C3(240, 230, 220), 1); tctext(im, "PERSONNEL", 30, C3(240, 230, 220), 1); tctext(im, "ONLY", 37, C3(240, 230, 220), 1);
    grime(im, 0.3f, 214, 1);
}
S(mid_vpipes)
{
    panel_base(im, C3(90, 96, 106), 1);
    vpipe(im, 12, 4, C3(120, 128, 132)); vpipe(im, 24, 2, C3(150, 80, 40)); vpipe(im, 50, 3, C3(60, 110, 150));
    valve(im, 12, 30, C3(200, 40, 30)); grime(im, 0.35f, 215, 1);
}
static void plot_col(Img im, int x, int a, int b, Col c) { if (a > b) { int t = a; a = b; b = t; } rect(im, x, a, x + 1, b + 1, c); }
S(mid_monitor)
{
    panel_base(im, C3(92, 98, 108), 1);
    bevel(im, 8, 10, 56, 44, C3(40, 42, 48), 1.35f, 0.55f, 2);
    rect(im, 11, 13, 53, 41, C3(8, 26, 16));
    for (int x = 11; x < 53; x += 6) rect(im, x, 13, x + 1, 41, C3(16, 46, 28));
    for (int i = 0; i < 41; i++) plot_col(im, 11 + i, (int)(27 + 9 * gsin(8.f * i / 41)), (int)(27 + 9 * gsin(8.f * (i + 1) / 41)), C3(90, 255, 130));
    rect(im, 28, 44, 36, 52, C3(50, 52, 58)); grime(im, 0.25f, 216, 1);
}
S(mid_arrow)
{
    panel_base(im, C3(92, 98, 108), 1);
    rect(im, 6, 22, 58, 42, C3(200, 160, 30));
    for (int k = 0; k < 10; k++) rect(im, 44 + k, 32 - k, 45 + k, 33 + k, C3(30, 28, 26));
    rect(im, 12, 29, 44, 36, C3(30, 28, 26)); tctext(im, "EXIT", 14, C3(230, 230, 220), 1); grime(im, 0.3f, 217, 1);
}
S(top_circuit)
{
    GRng r; grng_seed(&r, 121);
    t_new(im, C3(22, 26, 40));
    for (int k = 0; k < 26; k++) {
        int x = grng_int(&r, 0, T), y = grng_int(&r, 0, T), horiz = grng_f(&r) < 0.5f, n = grng_int(&r, 8, 30);
        for (int i = 0; i < n; i++) t_set(im, horiz ? (x + i) % T : x, horiz ? y : (y + i) % T, C3(40, 170, 200));
        t_set(im, x, y, C3(180, 255, 255));
    }
    grime(im, 0.25f, 122, 0); ao_edges(im, 0.3f, 3);
}
static const float CAB[4][3] = { { 150, 30, 26 }, { 40, 80, 150 }, { 170, 140, 30 }, { 40, 110, 60 } };
S(top_beam)
{
    t_new(im, C3(50, 54, 60));
    BEVEL(im, 0, 0, 64, 12, C3(66, 68, 74));
    for (int x = 4; x < 64; x += 8) RIVET(im, x, 4);
    inset(im, 0, 12, 64, 22, C3(40, 42, 46));
    rect(im, 0, 30, 64, 38, C3(28, 28, 32));
    for (int i = 0; i < 4; i++) rect(im, 0, 31 + i * 2, 64, 32 + i * 2, CAB[i]);
    for (int x = 0; x < 64; x += 16) rect(im, x, 28, x + 2, 40, C3(90, 90, 96));
    rect(im, 0, 46, 64, 60, C3(70, 74, 82));
    for (int x = 2; x < 64; x += 16) BEVEL(im, x, 48, x + 12, 58, C3(80, 84, 92));
    grime(im, 0.35f, 222, 1);
}
S(door_jamb_l)
{
    panel_base(im, C3(86, 92, 102), 0);
    BEVEL(im, 0, 0, 52, 64, C3(88, 94, 104));
    for (int y = 0; y < 64; y += 10) for (int k = 0; k < 5; k++) {
        rect(im, 52, (y + k) % T, 64, (y + k) % T + 1, C3(210, 170, 30));
        rect(im, 52, (y + k + 5) % T, 64, (y + k + 5) % T + 1, C3(30, 28, 26));
    }
    rect(im, 20, 10, 32, 16, C3(200, 40, 30)); rect(im, 22, 12, 30, 13, C3(255, 160, 140));
    ttext(im, ">>", 18, 30, C3(230, 190, 40), 2);
    kickplate(im, 10); grime(im, 0.3f, 231, 1); floor_ao(im, 0.4f, 14);
}
S(door_jamb_r) { door_jamb_l(im); t_flip_x(im); }
S(door_lintel)
{
    panel_base(im, C3(86, 92, 102), 0);
    hazard_band(im, 50, 64, 0);
    bevel(im, 14, 14, 50, 36, C3(30, 30, 34), 1.35f, 0.55f, 2);
    rect(im, 16, 16, 48, 34, C3(30, 120, 60));
    tctext(im, "EXIT", 21, C3(220, 255, 220), 1); ttext(im, ">", 42, 21, C3(220, 255, 220), 1);
    rect(im, 4, 4, 10, 10, C3(200, 40, 30)); rect(im, 54, 4, 60, 10, C3(200, 40, 30));
    grime(im, 0.3f, 232, 1);
}

/* ================================================================ WALL2 */
S(dock_plate)
{
    GRng r; grng_seed(&r, 3);
    t_new(im, C3(96, 92, 84));
    for (int x = 0; x < 64; x += 8) { rect_mul(im, x, 0, x + 4, 64, 1.15f); rect_mul(im, x + 4, 0, x + 8, 64, 0.8f); }
    rect(im, 0, 0, 64, 3, C3(60, 60, 64)); rect(im, 0, 61, 64, 64, C3(60, 60, 64));
    for (int k = 0; k < 4; k++) { int x = grng_int(&r, 6, 58), y = grng_int(&r, 6, 58); rect_mul(im, x - 2, y - 2, x + 3, y + 3, 0.75f); }
    grime(im, 0.45f, 241, 1); floor_ao(im, 0.4f, 14);
}
S(dock_number)
{
    dock_plate(im); hazard_band(im, 0, 6, 0); hazard_band(im, 58, 64, 1);
    ttext(im, "DOCK", 8, 14, C3(230, 200, 60), 3); ttext(im, "07", 18, 34, C3(230, 230, 220), 4);
}
S(checkpoint_panel)
{
    panel_base(im, C3(70, 74, 86), 1);
    bevel(im, 18, 8, 46, 40, C3(40, 42, 50), 1.35f, 0.55f, 2);
    rect(im, 21, 11, 43, 37, C3(60, 14, 14));
    for (int y = 13; y < 36; y += 4) rect(im, 22, y, 42, y + 1, C3(255, 60, 50));
    tctext(im, "CHECKPOINT", 46, C3(230, 60, 50), 1); tctext(im, "ID REQUIRED", 53, C3(200, 200, 200), 1);
    grime(im, 0.3f, 242, 1);
}
S(processing_panel)
{
    panel_base(im, C3(86, 92, 96), 1);
    rect(im, 0, 22, 64, 34, C3(40, 42, 46));
    for (int x = 2; x < 64; x += 8) for (int k = 0; k < 4; k++) { t_set(im, x + k, 24 + k, C3(230, 190, 40)); t_set(im, x + k, 31 - k, C3(230, 190, 40)); }
    tctext(im, "PROCESSING", 8, C3(230, 190, 40), 1); tctext(im, "LINE 3", 42, C3(200, 200, 200), 1);
    grime(im, 0.35f, 243, 1);
}
S(extraction_panel)
{
    panel_base(im, C3(84, 92, 104), 1);
    for (int x = 12; x <= 52; x += 20) {
        rect(im, x - 4, 6, x + 4, 48, C3(40, 50, 60)); rect(im, x - 2, 8, x + 2, 46, C3(40, 160, 190));
        rect(im, x - 1, 8, x, 46, C3(150, 240, 255));
    }
    tctext(im, "EXTRACTION", 52, C3(90, 230, 255), 1); grime(im, 0.3f, 244, 1);
}
S(cell_wall)
{
    static Map n; GRng r; grng_seed(&r, 9);
    t_new(im, C3(96, 90, 86));
    noise(n, 3, 245, 3);
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) t_mul(im, x, y, 0.75f + 0.35f * n[y][x]);
    rect_mul(im, 0, 20, 64, 21, 0.6f); rect_mul(im, 0, 42, 64, 43, 0.6f);
    for (int k = 0; k < 3; k++) {
        int cx = grng_int(&r, 8, 56), cy = grng_int(&r, 8, 56), rr = grng_int(&r, 20, 60);
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) * 0.5f < rr) t_blend(im, x, y, C3(80, 10, 10), 0.4f);
    }
    for (int k = 0; k < 4; k++) for (int i = 0; i < 14; i++) t_set(im, 40 + k * 3 + i / 4, 10 + i, C3(40, 36, 34));
    floor_ao(im, 0.4f, 14);
}
S(cell_warning)
{
    cell_wall(im);
    rect(im, 6, 6, 58, 30, C3(200, 160, 30)); rect(im, 8, 8, 56, 28, C3(30, 28, 26));
    tctext(im, "CELL", 10, C3(230, 190, 40), 1); tctext(im, "DO NOT FEED", 18, C3(230, 60, 40), 1);
}
S(lab_panel)
{
    panel_base(im, C3(142, 148, 152), 1);
    rect(im, 0, 40, 64, 46, C3(40, 110, 190)); rect(im, 0, 41, 64, 42, C3(120, 190, 240));
    grime(im, 0.18f, 246, 0);
}
S(lab_poster)
{
    lab_panel(im);
    poster(im, 14, 4, 50, 38, C3(230, 230, 225), C3(40, 110, 190), 0);
    skull_icon(im, 25, 7, C3(90, 90, 90), C3(200, 40, 30), 2);
    tctext(im, "SPECIMEN", 24, C3(40, 110, 190), 1); tctext(im, "HANDLING", 30, C3(40, 110, 190), 1);
}
S(vault_panel)
{
    static const float L[3][3] = { { 40, 255, 80 }, { 60, 140, 255 }, { 255, 180, 40 } };
    t_new(im, C3(26, 28, 36));
    for (int y = 4; y < 60; y += 8) {
        GRng r; grng_seed(&r, (unsigned)(y + 7));
        BEVEL(im, 4, y, 60, y + 6, C3(38, 40, 50));
        for (int x = 8; x < 56; x += 4) if (grng_f(&r) < 0.5f) t_set(im, x, y + 3, L[grng_int(&r, 0, 3)]);
    }
}
S(gallery_pipes)
{
    t_new(im, C3(70, 74, 80));
    hpipe(im, 9, 6, C3(120, 128, 132), 1); hpipe(im, 26, 5, C3(150, 80, 40), 1); hpipe(im, 40, 3, C3(60, 110, 150), 1); hpipe(im, 50, 3, C3(120, 128, 132), 1);
    valve(im, 30, 26, C3(230, 190, 40)); grime(im, 0.4f, 247, 1); floor_ao(im, 0.4f, 14);
}
S(turbine_panel)
{
    panel_base(im, C3(78, 92, 84), 1);
    for (int g = 0; g < 2; g++) {
        int cx = g ? 48 : 16, cy = 20;
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
            float d = gsqrt((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            if (d < 9) t_set(im, x, y, C3(30, 32, 30));
            if (d < 7.5f) t_set(im, x, y, C3(210, 210, 190));
        }
        for (int k = 0; k < 6; k++) t_set(im, cx + k / 2, cy - k, C3(200, 30, 30));
    }
    tctext(im, "TURBINE", 38, C3(230, 190, 40), 1); tctext(im, "MAX 9000", 45, C3(200, 200, 200), 1);
    grime(im, 0.35f, 248, 1);
}
S(reactor_plate)
{
    t_new(im, C3(56, 52, 50));
    for (int py = 0; py <= 32; py += 32) for (int px = 0; px <= 32; px += 32) {
        bevel(im, px + 1, py + 1, px + 31, py + 31, C3(60, 56, 54), 1.35f, 0.55f, 2);
        rivet(im, px + 4, py + 4, C3(120, 110, 100)); rivet(im, px + 26, py + 4, C3(120, 110, 100));
        rivet(im, px + 4, py + 26, C3(120, 110, 100)); rivet(im, px + 26, py + 26, C3(120, 110, 100));
    }
    trefoil(im, 32, 32, 4, 14, C3(220, 110, 30));
    grime(im, 0.4f, 249, 1); floor_ao(im, 0.4f, 14);
}
S(reactor_warning)
{
    reactor_plate(im);
    rect(im, 4, 2, 60, 18, C3(220, 110, 30)); rect(im, 6, 4, 58, 16, C3(30, 26, 24));
    tctext(im, "REACTOR", 5, C3(220, 110, 30), 1); tctext(im, "CORE 5", 11, C3(230, 230, 220), 1);
}
S(hall_wall)
{
    panel_base(im, C3(86, 92, 102), 1);
    hpipe(im, 8, 3, C3(120, 128, 132), 1); hpipe(im, 17, 2, C3(150, 80, 40), 1);
    rect(im, 4, 34, 60, 37, C3(60, 160, 170)); rect(im, 4, 35, 60, 36, C3(150, 235, 240));
    kickplate(im, 10); hazard_band(im, 60, 64, 0); grime(im, 0.35f, 251, 1); floor_ao(im, 0.4f, 14);
}
S(hall_wall_top)
{
    t_new(im, C3(54, 58, 64));
    rect(im, 0, 2, 64, 12, C3(28, 28, 32));
    for (int i = 0; i < 4; i++) rect(im, 0, 4 + i * 2, 64, 5 + i * 2, CAB[i]);
    for (int x = 0; x < 64; x += 16) rect(im, x, 0, x + 2, 14, C3(90, 90, 96));
    BEVEL(im, 0, 16, 64, 32, C3(62, 66, 72)); BEVEL(im, 0, 32, 64, 64, C3(84, 90, 100));
    rect(im, 24, 36, 40, 42, C3(220, 240, 245)); rect_mul(im, 24, 42, 40, 44, 0.5f);
    grime(im, 0.3f, 252, 1);
}

/* ================================================================ FLOOR */
static void plates(Img im, Col c, int tread, unsigned seed)
{
    t_new(im, c);
    for (int py = 0; py <= 32; py += 32) for (int px = 0; px <= 32; px += 32) {
        BEVEL(im, px + 1, py + 1, px + 31, py + 31, c);
        if (tread)
            for (int y = py + 4; y < py + 29; y += 4)
                for (int x = px + 4 + (y / 4 % 2) * 2; x < px + 29; x += 4) { t_mul(im, x, y, 1.35f); t_mul(im, x + 1, y + 1, 0.6f); }
        RIVET(im, px + 3, py + 3); RIVET(im, px + 27, py + 3); RIVET(im, px + 3, py + 27); RIVET(im, px + 27, py + 27);
    }
    grime(im, 0.4f, seed, 1); scratches(im, 14, seed + 1, C3(170, 170, 175)); ao_edges(im, 0.25f, 2);
}
S(floor_plate) { plates(im, C3(78, 82, 90), 1, 301); }
S(floor_grate)
{
    static Map g; noise(g, 4, 9, 3);
    t_new(im, C3(40, 44, 48));
    for (int y = 3; y < 61; y++) for (int x = 3; x < 61; x++) {
        if (x % 6 < 2 || y % 12 < 2) t_set(im, x, y, C3(62, 66, 72));
        else { float k = 0.5f + 0.5f * g[y][x]; t_set(im, x, y, C3(10 + 20 * k, 60 * k + 20, 70 * k + 24)); }
    }
    BEVEL(im, 0, 0, 64, 3, C3(70, 74, 80)); BEVEL(im, 0, 61, 64, 64, C3(70, 74, 80));
    BEVEL(im, 0, 0, 3, 64, C3(70, 74, 80)); BEVEL(im, 61, 0, 64, 64, C3(70, 74, 80));
    grime(im, 0.25f, 12, 0);
}
S(floor_edge) { plates(im, C3(78, 82, 90), 1, 303); rect(im, 0, 54, 64, 60, C3(210, 170, 30)); }
S(floor_hazard) { t_new(im, C3(80, 80, 84)); hazard_band(im, 0, 64, 0); grime(im, 0.45f, 304, 1); scratches(im, 18, 304, C3(120, 110, 90)); }
S(floor_arrow)
{
    plates(im, C3(78, 82, 90), 1, 305);
    for (int k = 0; k < 14; k++) rect(im, 32 - k, 10 + k, 33 + k, 11 + k, C3(210, 170, 30));
    rect(im, 26, 24, 38, 50, C3(210, 170, 30));
}
S(floor_strip)
{
    plates(im, C3(78, 82, 90), 1, 306);
    rect(im, 24, 0, 40, 64, C3(30, 32, 36)); rect(im, 27, 0, 37, 64, C3(40, 200, 210)); rect(im, 29, 0, 35, 64, C3(170, 255, 255));
    for (int y = 0; y < 64; y += 8) rect(im, 27, y, 37, y + 1, C3(30, 120, 130));
}
S(floor_drain)
{
    plates(im, C3(78, 82, 90), 1, 307);
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
        float d = gsqrt((float)((x - 32) * (x - 32) + (y - 32) * (y - 32)));
        if (d < 14) t_set(im, x, y, C3(50, 52, 56));
        if (d < 12) t_set(im, x, y, C3(34, 36, 40));
        for (int k = -10; k < 11; k += 4) if (y - 32 - k == 0 && d < 11) t_set(im, x, y, C3(70, 72, 78));
    }
}
S(floor_concrete)
{
    static Map n; noise(n, 4, 308, 3);
    t_new(im, C3(92, 90, 86));
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) t_mul(im, x, y, 0.75f + 0.35f * n[y][x]);
    rect_mul(im, 0, 31, 64, 33, 0.6f); rect_mul(im, 31, 0, 33, 64, 0.6f);
    grime(im, 0.3f, 308, 1);
}
S(ceiling_panel)
{
    t_new(im, C3(52, 56, 62));
    for (int py = 0; py <= 32; py += 32) for (int px = 0; px <= 32; px += 32) BEVEL(im, px + 1, py + 1, px + 31, py + 31, C3(56, 60, 66));
    grime(im, 0.25f, 309, 0);
}
S(ceiling_light)
{
    ceiling_panel(im);
    inset(im, 8, 20, 56, 44, C3(40, 40, 44)); rect(im, 10, 22, 54, 42, C3(200, 235, 240)); rect(im, 12, 26, 52, 38, C3(245, 252, 255));
    for (int x = 14; x < 52; x += 6) rect(im, x, 22, x + 1, 42, C3(150, 190, 200));
}
static void cable_band(Img im, int x0, int w, Col c, float a0, float a1, int vertical)
{
    for (int i = 0; i < w; i++) {
        float k = 0.5f + 0.8f * gsin(a0 + (a1 - a0) * i / (w - 1));
        if (vertical) rect(im, x0 + i, 0, x0 + i + 1, T, C3(c[0] * k, c[1] * k, c[2] * k));
        else rect(im, 0, x0 + i, T, x0 + i + 1, C3(c[0] * k, c[1] * k, c[2] * k));
    }
}
S(ceiling_cables)
{
    static const float C7[7][3] = { { 150, 30, 26 }, { 30, 30, 34 }, { 40, 80, 150 }, { 30, 30, 34 }, { 170, 140, 30 }, { 40, 110, 60 }, { 150, 30, 26 } };
    ceiling_panel(im);
    rect(im, 16, 0, 48, 64, C3(24, 24, 28));
    for (int i = 0; i < 7; i++) cable_band(im, 18 + i * 4, 4, C7[i], 0.3f, GPI - 0.3f, 1);
    for (int y = 0; y < 64; y += 16) rect(im, 14, y, 50, y + 2, C3(90, 90, 96));
}
S(stair_tread)
{
    t_new(im, C3(80, 82, 88));
    rect(im, 0, 0, 64, 6, C3(205, 165, 30));
    for (int x = 0; x < 64; x += 8) rect(im, x, 0, x + 4, 6, C3(30, 28, 26));
    for (int y = 10; y < 60; y += 5) { rect_mul(im, 0, y, 64, y + 1, 1.3f); rect_mul(im, 0, y + 1, 64, y + 2, 0.6f); }
    grime(im, 0.4f, 311, 1); ao_edges(im, 0.25f, 3);
}
S(floor_lab)
{
    t_new(im, C3(138, 142, 146));
    for (int py = 0; py < 64; py += 16) for (int px = 0; px < 64; px += 16) bevel(im, px, py, px + 16, py + 16, C3(140, 144, 148), 1.1f, 0.8f, 1);
    grime(im, 0.15f, 312, 0);
}
S(floor_reactor)
{
    static Map g; noise(g, 4, 313, 3);
    t_new(im, C3(40, 36, 34));
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++)
        if (x % 8 >= 2 && y % 8 >= 2) t_set(im, x, y, C3(60 + 140 * g[y][x], 24 + 50 * g[y][x], 10));
}
S(floor_cell)
{
    GRng r; grng_seed(&r, 14);
    floor_concrete(im);
    for (int k = 0; k < 3; k++) {
        int cx = grng_int(&r, 8, 56), cy = grng_int(&r, 8, 56), rr = grng_int(&r, 30, 90);
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) < rr) t_blend(im, x, y, C3(70, 8, 8), 0.45f);
    }
}
S(floor_dock)
{
    plates(im, C3(84, 84, 86), 0, 315);
    for (int x = 14; x <= 46; x += 32) { rect_mul(im, x, 0, x + 6, 64, 0.7f); for (int y = 0; y < 64; y += 3) rect_mul(im, x, y, x + 6, y + 1, 0.85f); }
}

/* ================================================================ PROPS */
S(machine_panel)
{
    static const int H8[8] = { 9, 14, 6, 11, 15, 8, 12, 5 };
    static const float C5[5][3] = { { 220, 40, 30 }, { 40, 220, 60 }, { 230, 190, 40 }, { 40, 120, 230 }, { 220, 40, 30 } };
    t_new(im, C3(96, 100, 104));
    bevel(im, 0, 0, 64, 64, C3(98, 102, 106), 1.35f, 0.55f, 2);
    inset(im, 6, 6, 58, 24, C3(20, 26, 24));
    for (int i = 0, x = 9; x < 56; x += 6, i++) rect(im, x, 22 - H8[i % 8], x + 4, 22, C3(40, 200, 90));
    for (int i = 0, x = 8; x < 58; x += 10, i++) BEVEL(im, x, 30, x + 6, 36, C5[i % 5]);
    for (int k = 0; k < 3; k++) {
        int cx = k == 0 ? 14 : k == 1 ? 34 : 50;
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
            int d = (x - cx) * (x - cx) + (y - 48) * (y - 48);
            if (d < 36) t_set(im, x, y, C3(40, 42, 46));
            if (d < 25) t_set(im, x, y, C3(200, 200, 190));
        }
        rect(im, cx, 48, cx + 4, 49, C3(200, 30, 30));
    }
    grime(im, 0.25f, 141, 0);
}
static void screen_border(Img im)
{
    rect(im, 0, 0, 64, 2, C3(60, 66, 70)); rect(im, 0, 62, 64, 64, C3(60, 66, 70));
    rect(im, 0, 0, 2, 64, C3(60, 66, 70)); rect(im, 62, 0, 64, 64, C3(60, 66, 70));
}
S(screen_graph)
{
    t_new(im, C3(8, 22, 14));
    for (int x = 0; x < 64; x += 8) rect(im, x, 0, x + 1, 64, C3(16, 50, 30));
    for (int y = 0; y < 64; y += 8) rect(im, 0, y, 64, y + 1, C3(16, 50, 30));
    for (int i = 0; i < 59; i++) {
        float a0 = 9.f * i / 59, a1 = 9.f * (i + 1) / 59, b0 = 3.f * i / 59, b1 = 3.f * (i + 1) / 59;
        plot_col(im, i + 2, (int)(30 + 14 * gsin(a0) * gcos(b0)), (int)(30 + 14 * gsin(a1) * gcos(b1)), C3(80, 255, 120));
    }
    ttext(im, "OUTPUT 98%", 2, 56, C3(120, 255, 150), 1);
    screen_border(im);
}
S(screen_vitals)
{
    static const int beat[16] = { 0, 0, 0, 0, 1, -6, 10, -3, 0, 0, 0, 0, 0, 0, 0, 0 };
    int prev = 20;
    t_new(im, C3(26, 6, 8));
    for (int x = 2; x < 62; x++) { int y = 20 + beat[x % 16]; plot_col(im, x, prev, y, C3(255, 70, 60)); prev = y; }
    ttext(im, "SHINIGAMI", 2, 34, C3(255, 120, 100), 1); ttext(im, "SOUL 07%", 2, 42, C3(255, 200, 80), 1);
    rect(im, 2, 52, 62, 58, C3(60, 14, 14)); rect(im, 2, 52, 9, 58, C3(255, 60, 40));
    screen_border(im);
}
S(glass)
{
    static Map n; noise(n, 2, 151, 3);
    t_new(im, C3(70, 120, 140));
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) t_mul(im, x, y, 0.6f + 0.4f * n[y][x]);
    rect(im, 10, 0, 13, 64, C3(190, 230, 240)); rect(im, 16, 0, 17, 64, C3(190, 230, 240)); rect(im, 44, 0, 46, 64, C3(190, 230, 240));
}
S(energy)
{
    static Map n; noise(n, 4, 161, 4);
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
        float sw = gsin(((float)x / T * 2 + n[y][x] * 2.5f) * GPI * 2) * 0.5f + 0.5f;
        im[y][x][0] = 60 + 120 * sw * n[y][x]; im[y][x][1] = 140 * sw + 60 * n[y][x]; im[y][x][2] = 160 + 90 * sw;
    }
}
S(coil)
{
    t_new(im, C3(150, 90, 40));
    for (int y = 0; y < T; y += 4) { rect_mul(im, 0, y, 64, y + 1, 1.4f); rect_mul(im, 0, y + 2, 64, y + 3, 0.6f); rect_mul(im, 0, y + 3, 64, y + 4, 0.5f); }
    for (int x = 0; x <= 62; x += 31) rect(im, x, 0, x + 2, 64, C3(60, 60, 64));
    cyl_shade(im, 1, 0.5f, 1.4f);
}
S(crate)
{
    t_new(im, C3(80, 84, 74));
    bevel(im, 0, 0, 64, 64, C3(90, 94, 82), 1.35f, 0.55f, 5);
    inset(im, 6, 6, 58, 58, C3(70, 74, 64));
    for (int i = 6; i < 58; i++) { rect(im, i - 1, i, i + 2, i + 1, C3(92, 96, 84)); rect(im, 63 - i - 1, i, 63 - i + 2, i + 1, C3(92, 96, 84)); }
    rect(im, 20, 26, 44, 38, C3(200, 160, 30)); ttext(im, "S-07", 22, 29, C3(30, 28, 24), 1);
    RIVET(im, 2, 2); RIVET(im, 59, 2); RIVET(im, 2, 59); RIVET(im, 59, 59);
    grime(im, 0.35f, 171, 1);
}
S(canister)
{
    t_new(im, C3(200, 160, 30));
    rect(im, 0, 22, 64, 34, C3(30, 28, 26));
    for (int x = 0; x < 64; x += 8) rect(im, x, 22, x + 4, 34, C3(200, 160, 30));
    rect(im, 0, 0, 64, 4, C3(90, 90, 96)); rect(im, 0, 60, 64, 64, C3(90, 90, 96));
    ttext(im, "SOUL", 24, 44, C3(40, 30, 20), 1);
    grime(im, 0.35f, 181, 1); cyl_shade(im, 1, 0.45f, 1.3f);
}
S(machine_base)
{
    t_new(im, C3(50, 52, 58));
    bevel(im, 0, 0, 64, 64, C3(54, 56, 62), 1.35f, 0.55f, 3);
    for (int y = 16; y <= 40; y += 24) {
        inset(im, 8, y, 56, y + 10, C3(24, 10, 6));
        for (int x = 10; x < 54; x += 4) { rect(im, x, y + 2, x + 2, y + 8, C3(255, 120, 30)); rect(im, x, y + 4, x + 2, y + 6, C3(255, 210, 120)); }
    }
    for (int x = 4; x < 64; x += 10) { RIVET(im, x, 4); RIVET(im, x, 58); }
    grime(im, 0.3f, 191, 0);
}
S(keyboard)
{
    t_new(im, C3(44, 46, 52));
    for (int y = 6; y < 50; y += 7) for (int x = 4 + (y / 7 % 2) * 3; x < 60; x += 7) BEVEL(im, x, y, x + 5, y + 5, C3(90, 92, 98));
    rect(im, 6, 52, 58, 60, C3(20, 30, 40)); ttext(im, "EXTRACT", 8, 54, C3(60, 220, 240), 1);
    grime(im, 0.2f, 201, 0);
}
S(cage_bars)
{
    t_new(im, C3(0, 0, 0));
    for (int x = 2; x < 60; x += 10) for (int i = 0; i < 4; i++) {
        float b = 90 * (0.5f + 0.8f * gsin(0.3f + (GPI - 0.6f) * i / 3));
        rect(im, x + i, 0, x + i + 1, 64, C3(b, b, b * 1.1f));
    }
    rect(im, 0, 0, 64, 4, C3(80, 80, 86)); rect(im, 0, 60, 64, 64, C3(80, 80, 86)); rect(im, 0, 30, 64, 33, C3(70, 70, 76));
}
S(fan)
{
    t_new(im, C3(0, 0, 0));
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
        float fx = x - 31.5f, fy = y - 31.5f, r = gsqrt(fx * fx + fy * fy), a = gatan2(fy, fx), s = gsin(a * 5 + r * 0.08f);
        if (s > 0.3f && r < 30 && r > 6) { float k = 0.6f + 0.6f * s; t_set(im, x, y, C3(110 * k, 116 * k, 124 * k)); }
        if (r < 7) t_set(im, x, y, C3(70, 72, 80));
        if (r < 3) t_set(im, x, y, C3(150, 150, 160));
        if (r >= 30 && r < 32) t_set(im, x, y, C3(60, 62, 68));
    }
}
S(shini_body)
{
    t_new(im, C3(30, 22, 36));
    for (int y = 0; y < T; y += 6)
        for (int x = -(y / 6 % 2) * 4; x < T; x += 8)
            for (int yy = 0; yy < T; yy++) for (int xx = 0; xx < T; xx++)
                if ((xx - x - 4) * (xx - x - 4) / 16.f + (yy - y - 4) * (yy - y - 4) / 25.f < 1) t_blend(im, xx, yy, C3(60, 44, 70), 0.5f);
    for (int y = 5; y < T; y += 6) rect_mul(im, 0, y, 64, y + 1, 0.5f);
    rect(im, 30, 0, 34, 64, C3(170, 160, 140));
    for (int y = 4; y < 64; y += 8) rect(im, 28, y, 36, y + 2, C3(200, 190, 170));
    grime(im, 0.2f, 211, 0);
}
S(shini_skull)
{
    static Map n; noise(n, 4, 221, 3);
    t_new(im, C3(14, 10, 16));
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
        int head = (x - 32) * (x - 32) / 400.f + (y - 26) * (y - 26) / 300.f < 1;
        int beak = y > 30 && y < 60 && gabs((float)(x - 32)) < (60 - y) * 0.45f;
        if (head || beak) { float k = 0.7f + 0.3f * n[y][x]; t_set(im, x, y, C3(205 * k, 196 * k, 178 * k)); }
        for (int e = 0; e < 2; e++) {
            int ex = e ? 42 : 22, d = (x - ex) * (x - ex) + (y - 24) * (y - 24);
            if (d < 30) t_set(im, x, y, C3(255, 120, 20));
            if (d < 8) t_set(im, x, y, C3(255, 230, 150));
        }
        if (beak && y == 44) t_mul(im, x, y, 0.4f);
    }
}
S(lamp_glow)
{
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
        float fx = x - 31.5f, fy = y - 31.5f, k = gclamp(1 - gsqrt(fx * fx + fy * fy) / 32, 0, 1);
        k *= k;
        im[y][x][0] = 255 * k; im[y][x][1] = 230 * k; im[y][x][2] = 200 * k;
    }
}

/* ================================================================ TRIM */
S(dark_metal) { t_new(im, C3(54, 56, 60)); bevel(im, 0, 0, 64, 64, C3(56, 58, 62), 1.35f, 0.55f, 2); grime(im, 0.45f, 131, 1); scratches(im, 20, 132, C3(110, 110, 115)); }
S(hazard)
{
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) t_set(im, x, y, ((x + y) / 8) % 2 == 0 ? C3(205, 165, 30) : C3(30, 28, 26));
    grime(im, 0.45f, 31, 1); scratches(im, 18, 8, C3(120, 110, 90)); ao_edges(im, 0.3f, 3);
}
S(pipe)
{
    t_new(im, C3(120, 128, 132));
    for (int x = 0; x < T; x += 16) { rect_mul(im, x, 0, x + 3, 64, 0.6f); rect_mul(im, x + 3, 0, x + 4, 64, 1.3f); }
    grime(im, 0.3f, 61, 0); cyl_shade(im, 0, 0.45f, 1.35f);
}
S(bulkhead)
{
    t_new(im, C3(84, 88, 96));
    bevel(im, 0, 0, 64, 64, C3(86, 90, 98), 1.35f, 0.55f, 3);
    for (int i = 0; i < 64; i += 12) for (int k = 0; k < 6; k++) {
        int yy = (i + k) % T;
        Col c = k < 3 ? C3(210, 170, 30) : C3(30, 28, 26);
        rect(im, 1, yy, 4, yy + 1, c); rect(im, 60, yy, 63, yy + 1, c);
    }
    inset(im, 10, 10, 54, 54, C3(64, 68, 76)); grime(im, 0.3f, 71, 1);
}
S(beam)
{
    t_new(im, C3(56, 58, 62));
    BEVEL(im, 0, 0, 64, 10, C3(70, 72, 76)); BEVEL(im, 0, 54, 64, 64, C3(70, 72, 76));
    inset(im, 0, 10, 64, 54, C3(48, 50, 54));
    for (int x = 4; x < 64; x += 8) { RIVET(im, x, 3); RIVET(im, x, 58); }
    grime(im, 0.35f, 81, 1);
}
S(cables)
{
    static const float C8[8][3] = { { 150, 30, 26 }, { 30, 30, 34 }, { 40, 80, 150 }, { 30, 30, 34 }, { 170, 140, 30 }, { 40, 110, 60 }, { 30, 30, 34 }, { 150, 30, 26 } };
    t_new(im, C3(20, 20, 22));
    for (int i = 0; i < 8; i++) cable_band(im, i * 8, 8, C8[i], 0.2f, GPI - 0.2f, 0);
    for (int x = 0; x < T; x += 21) rect(im, x, 0, x + 2, 64, C3(90, 90, 95));
    grime(im, 0.2f, 91, 0);
}
S(floor_light)
{
    t_new(im, C3(36, 38, 42));
    bevel(im, 0, 0, 64, 64, C3(40, 42, 46), 1.35f, 0.55f, 3);
    rect(im, 6, 22, 58, 42, C3(40, 200, 210)); rect(im, 8, 27, 56, 37, C3(170, 255, 255));
    for (int x = 10; x < 56; x += 8) rect(im, x, 22, x + 1, 42, C3(30, 120, 130));
}
S(plate_side)
{
    t_new(im, C3(70, 72, 78));
    bevel(im, 0, 0, 64, 64, C3(72, 74, 80), 1.35f, 0.55f, 2);
    for (int x = 6; x < 64; x += 14) { RIVET(im, x, 6); RIVET(im, x, 54); }
    rect_mul(im, 0, 30, 64, 34, 0.7f); grime(im, 0.4f, 51, 1);
}

/* ================================================================ ANIM: 4-frame screen strips */
#define FRAMES SCREEN_FRAMES
static void graph_screen(Img im, int x0, int y0, int x1, int y1, int k, const char *label)
{
    rect(im, x0, y0, x1, y1, C3(8, 26, 16));
    for (int x = x0; x < x1; x += 6) rect(im, x, y0, x + 1, y1, C3(16, 46, 28));
    for (int y = y0; y < y1; y += 6) rect(im, x0, y, x1, y + 1, C3(16, 46, 28));
    int w = x1 - x0;
    float ph = k * 2 * GPI / FRAMES, mid = (y0 + y1 - 8) / 2.f, amp = (y1 - y0 - 12) / 3.f;
    for (int i = 0; i < w - 1; i++) {
        float a0 = 4 * GPI * i / (w - 1), a1 = 4 * GPI * (i + 1) / (w - 1), b0 = 2 * GPI * i / (w - 1), b1 = 2 * GPI * (i + 1) / (w - 1);
        plot_col(im, x0 + i, (int)(mid + amp * gsin(a0 + ph) * (0.7f + 0.3f * gcos(b0 - ph))),
                 (int)(mid + amp * gsin(a1 + ph) * (0.7f + 0.3f * gcos(b1 - ph))), C3(90, 255, 130));
    }
    ttext(im, label, x0 + 2, y1 - 6, C3(150, 255, 170), 1);
}
static void vitals_screen(Img im, int x0, int y0, int x1, int y1, int k, int soul, const char *label)
{
    static const int beat[16] = { 0, 0, 0, 0, 0, 1, -5, 8, -3, 0, 0, 0, 0, 0, 0, 0 };
    int base = y0 + 7, prev = base;
    rect(im, x0, y0, x1, y1, C3(26, 6, 8));
    for (int x = x0 + 1; x < x1 - 1; x++) { int y = base + beat[((x - k * 4) % 16 + 16) % 16]; plot_col(im, x, prev, y, C3(255, 70, 60)); prev = y; }
    ttext(im, label, x0 + 2, y0 + 17, C3(255, 200, 80), 1);
    if (k % 2 == 0) ttext(im, "CRITICAL", x0 + 2, y0 + 24, C3(255, 60, 40), 1);
    int bw = (x1 - x0 - 4) * soul / 100 + 1;
    rect(im, x0 + 2, y1 - 5, x1 - 2, y1 - 2, C3(60, 14, 14)); rect(im, x0 + 2, y1 - 5, x0 + 2 + bw, y1 - 2, C3(255, 60, 40));
}
static const char *const PCT[2] = { "OUT 97%", "OUT 99%" }, *const SOULS[2] = { "SOUL 07%", "SOUL 06%" };
static const int SOUL[2] = { 7, 6 };
static void anim_tile(Img im, int i)
{
    int wall = i < 4, graph = (i % 4) < 2, k = i % 2;
    if (wall) {
        panel_base(im, C3(92, 98, 108), 1);
        bevel(im, 5, 6, 59, 48, C3(36, 38, 44), 1.35f, 0.55f, 2);
        if (graph) graph_screen(im, 8, 9, 56, 45, k, PCT[k]); else vitals_screen(im, 8, 9, 56, 45, k, SOUL[k], SOULS[k]);
        rect_mul(im, 8, 9, 9, 45, 1.3f);
        rect(im, 27, 48, 37, 54, C3(50, 52, 58));
        rect_mul(im, 25, 54, 39, 56, 0.5f);
        rect(im, 50, 49, 53, 51, k % 2 ? C3(40, 255, 80) : C3(20, 90, 30));
        grime(im, 0.2f, 261, 1);
    } else {
        t_new(im, C3(60, 66, 70));
        if (graph) graph_screen(im, 2, 2, 62, 62, k, PCT[k]); else vitals_screen(im, 2, 2, 62, 62, k, SOUL[k], SOULS[k]);
    }
}

/* ================================================================ pages */
typedef void (*TileFn)(Img);
static const TileFn TILES[LT_WALL_GRAPH] = {
    base_plain, base_pipes, base_vent, base_hazard, mid_panel, mid_poster_power, mid_poster_danger, mid_authorized,
    mid_vpipes, mid_monitor, mid_arrow, top_circuit, top_beam, door_jamb_l, door_jamb_r, door_lintel,
    machine_panel, screen_graph, screen_vitals, 0, glass, energy, coil, crate,
    canister, machine_base, keyboard, cage_bars, fan, shini_body, shini_skull, lamp_glow,
    dock_plate, dock_number, checkpoint_panel, processing_panel, extraction_panel, cell_wall, cell_warning, lab_panel,
    lab_poster, vault_panel, gallery_pipes, turbine_panel, reactor_plate, reactor_warning, hall_wall, hall_wall_top,
    floor_plate, floor_grate, floor_edge, floor_hazard, floor_arrow, floor_strip, floor_drain, floor_concrete,
    ceiling_panel, ceiling_light, ceiling_cables, stair_tread, floor_lab, floor_reactor, floor_cell, floor_dock,
    dark_metal, hazard, pipe, bulkhead, beam, cables, floor_light, plate_side,
};

static int transparent_tile(int t) { return t == LT_CAGE_BARS || t == LT_FAN || t == LT_LAMP_GLOW || t == LT_ENERGY; }

LabTex *lab_tex;

void gen_lab_textures(float *preview_rgb)
{
    static Img im;
    float *page = (float *)malloc(sizeof(float) * 256 * 256 * 3);
    unsigned char *mask = (unsigned char *)malloc(256 * 256), *idx = (unsigned char *)malloc(256 * 256);
    if (!lab_tex) lab_tex = (LabTex *)calloc(1, sizeof(LabTex));
    for (int p = 0; p < LAB_NPAGES; p++) {
        memset(idx, 0, 256 * 256);
        for (int i = 0; i < 16; i++) {
            int t = p * 16 + i, tx = (i % 4) * 64, ty = (i / 4) * 64;
            unsigned short *clut = lab_tex->clut[t];
            if (t < LT_WALL_GRAPH && !TILES[t]) { memset(clut, 0, 32); continue; }
            if (t >= LT_WALL_GRAPH) anim_tile(im, t - LT_WALL_GRAPH); else TILES[t](im);
            for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
                float *d = page + ((size_t)(ty + y) * 256 + tx + x) * 3, mx = 0;
                for (int k = 0; k < 3; k++) { float v = im[y][x][k]; d[k] = v < 0 ? 0 : v > 255 ? 255 : v; if (d[k] > mx) mx = d[k]; }
                mask[(ty + y) * 256 + tx + x] = !(transparent_tile(t) && mx < 10);
            }
            quant16(page + ((size_t)ty * 256 + tx) * 3, mask + ty * 256 + tx, 64, 64, 256, clut,
                    idx + ty * 256 + tx, 256, 3.0f, 1);
        }
        for (int i = 0; i < 256 * 256; i += 2) lab_tex->pix[p][i / 2] = (unsigned char)(idx[i] | (idx[i + 1] << 4));
        if (preview_rgb)
            for (int i = 0; i < 256 * 256; i++) {
                int ti = (i / 256 / 64) * 4 + (i % 256) / 64, ix = idx[i];
                unsigned short w = lab_tex->clut[p * 16 + ti][ix];
                float *o = preview_rgb + ((size_t)p * 65536 + i) * 3;
                o[0] = (float)((w & 31) * 8); o[1] = (float)(((w >> 5) & 31) * 8); o[2] = (float)(((w >> 10) & 31) * 8);
                if (!ix) { o[0] = 40; o[1] = 0; o[2] = 40; }
            }
    }
    free(page); free(mask); free(idx);
}

void lab_upload_textures(void)
{
    for (int k = 0; k < LAB_NPAGES; k++)
        for (int t = 0; t < 16; t++) {
            int id = k * 16 + t;
            if (id < LT_WALL_GRAPH && !TILES[id]) continue;          /* free tile: something else may live there */
            for (int y = 0; y < 64; y++) {
                RECT r;
                r.x = (s16)(LAB_PAGE_X(k) + (t % 4) * 16); r.y = (s16)(LAB_PAGE_Y(k) + (t / 4) * 64 + y); r.w = 16; r.h = 1;
                gpu_load_image(&r, (const u16 *)(const void *)&lab_tex->pix[k][((t / 4) * 64 + y) * 128 + (t % 4) * 32]);
            }
            RECT c; c.x = (s16)CLUT_X(id); c.y = (s16)CLUT_Y(id); c.w = 16; c.h = 1;
            gpu_load_image(&c, lab_tex->clut[id]);
        }
}
