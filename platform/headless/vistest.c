/* Visibility test: flies a game-like follow camera along the level's route and renders every
   frame twice - with the portal visibility and with every room drawn. Any pixel that differs is
   geometry the portals hid by mistake (or popping when crossing from sector to sector). Reports
   the worst frames and saves them side by side (portal | all rooms) as BMPs. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gte.h"
#include "gpu.h"
#include "level.h"

static FrameBuf fb;
static int player_angle(s32 dx, s32 dz)      /* heading of (dx, dz): 0 = +z, 1024 = +x */
{
    int a, best = 0;
    long long bd = -(1LL << 62);
    for (a = 0; a < 4096; a += 4) {
        long long d = (long long)isin(a) * dx + (long long)icos(a) * dz;
        if (d > bd) { bd = d; best = a; }
    }
    return best;
}
static u16 shot[2][SCREEN_W_MAX * SCREEN_H];

static void grab(u16 *dst)
{
    const u16 *v = gpu_vram();
    int y;
    for (y = 0; y < SCREEN_H; y++) memcpy(dst + y * SCREEN_W, v + y * VRAM_W, SCREEN_W * 2);
}

static void save(const char *path)
{
    unsigned char h[54] = { 'B', 'M' }, row[SCREEN_W_MAX * 2 * 3 + 4];
    int x, y, k, W = SCREEN_W * 2, size = 54 + W * SCREEN_H * 3;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255;
    h[10] = 54; h[14] = 40; h[18] = W & 255; h[19] = W >> 8; h[22] = SCREEN_H; h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (y = SCREEN_H - 1; y >= 0; y--) {
        for (k = 0; k < 2; k++) for (x = 0; x < SCREEN_W; x++) {
            u16 c = shot[k][y * SCREEN_W + x];
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31, o = (k * SCREEN_W + x) * 3;
            row[o] = (unsigned char)(b << 3); row[o + 1] = (unsigned char)(g << 3); row[o + 2] = (unsigned char)(r << 3);
        }
        fwrite(row, 1, (size_t)((W * 3 + 3) & ~3), f);
    }
    fclose(f);
}

static void render(s32 ex, s32 ey, s32 ez, int yaw, int pitch)
{
    MATRIX view, rx, ry;
    SVECTOR t; VECTOR tv;
    PROJ pr;
    RECT r = { 0, 0, SCREEN_W, SCREEN_H };
    mat_rot_x(&rx, pitch); mat_rot_y(&ry, -yaw); mat_mul(&rx, &ry, &view);
    t.vx = (s16)-ex; t.vy = (s16)-ey; t.vz = (s16)-ez;
    view.t[0] = view.t[1] = view.t[2] = 0;
    mat_apply(&view, &t, &tv);
    view.t[0] = tv.vx; view.t[1] = tv.vy; view.t[2] = tv.vz;
    pr.h = 250; pr.ofx = SCREEN_W / 2; pr.ofy = SCREEN_H / 2; pr.near_z = 120;
    gpu_fill(&r, 3, 4, 7);
    ot_clear(&fb);
    gpu_set_draw_env(0, 0, SCREEN_W, SCREEN_H, 0, 0, 1);
    level_draw(&fb, &view, &pr, 100);
    gpu_draw_ot(&fb);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    int i, n, frames = 0, bad = 0, worst = 0, saved = 0;
    gpu_init();
    level_init();
    n = level_route_count();
    for (i = 1; i < n; i++) {
        s32 x0, z0, x1, z1, dx, dz, L, s;
        level_route_point(i - 1, &x0, &z0); level_route_point(i, &x1, &z1);
        dx = x1 - x0; dz = z1 - z0;
        L = (s32)isqrt((u32)((dx >> 2) * (dx >> 2) + (dz >> 2) * (dz >> 2))) << 2;
        if (L < 64) continue;
        for (s = 0; s < L; s += 400) {
            s32 px = x0 + dx * s / L, pz = z0 + dz * s / L, gy = level_ground(px, pz, -30000);
            int yaw = player_angle(dx, dz), k, diff = 0, side;
            for (side = 0; side < 3; side++) {           /* behind her, and swung 60 degrees each way */
                int cy = (yaw + (side == 1 ? 680 : side == 2 ? -680 : 0)) & 4095;
                VECTOR a, b;
                int t;
                a.vx = px; a.vy = gy - 1500; a.vz = pz;
                b.vx = px - (isin(cy) * 2900 >> 12); b.vy = gy - 2700; b.vz = pz - (icos(cy) * 2900 >> 12);
                t = level_clip_segment(&a, &b, 140);
                b.vx = a.vx + (b.vx - a.vx) * t / 4096; b.vy = a.vy + (b.vy - a.vy) * t / 4096; b.vz = a.vz + (b.vz - a.vz) * t / 4096;
                level_debug_all_rooms(0); render(b.vx, b.vy, b.vz, cy, 180); grab(shot[0]);
                level_debug_all_rooms(1); render(b.vx, b.vy, b.vz, cy, 180); grab(shot[1]);
                for (k = diff = 0; k < SCREEN_W * SCREEN_H; k++) diff += shot[0][k] != shot[1][k];
                frames++;
                if (diff > 600) {             /* (a few hundred faint pixels of far, dark detail are noise) */
                    bad++;
                    if (diff > worst) worst = diff;
                    if (saved < 12) {
                        char path[512];
                        sprintf(path, "%s/vis_%02d.bmp", out, saved++);
                        save(path);
                        printf("hidden geometry: %d pixels at %.1f,%.1f m looking %d degrees (camera %.1f,%.1f)\n", diff,
                               px / 1024.0, pz / 1024.0, cy * 360 / 4096, b.vx / 1024.0, b.vz / 1024.0);
                    }
                }
            }
        }
    }
    printf("visibility: %d camera spots along the route, %d with geometry wrongly hidden (worst %d pixels)\n", frames, bad, worst);
    return bad ? 1 : 0;
}
