/* Renders the level from fixed camera spots (no player): a tour of the plant, for docs and
   for checking the lighting bake. Also prints the per-view polygon count. */
#include "edmap.h"
#include <stdio.h>
#include <string.h>
#include "gte.h"
#include "gpu.h"
#include "level.h"
#include "map.h"
#include <stdlib.h>

static FrameBuf fb;

static void save(const char *dir, const char *name)
{
    unsigned char h[54] = { 'B', 'M' }, row[SCREEN_W * 3];
    char path[512];
    int x, y, size = 54 + SCREEN_W * SCREEN_H * 3;
    const u16 *v = gpu_vram();
    FILE *f;
    sprintf(path, "%s/%s.bmp", dir, name);
    if (!(f = fopen(path, "wb"))) return;
    h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255;
    h[10] = 54; h[14] = 40; h[18] = SCREEN_W & 255; h[19] = SCREEN_W >> 8; h[22] = SCREEN_H; h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (y = SCREEN_H - 1; y >= 0; y--) {
        for (x = 0; x < SCREEN_W; x++) {
            u16 c = v[y * VRAM_W + x];
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            row[x * 3] = (unsigned char)(b << 3 | b >> 2); row[x * 3 + 1] = (unsigned char)(g << 3 | g >> 2); row[x * 3 + 2] = (unsigned char)(r << 3 | r >> 2);
        }
        fwrite(row, 1, sizeof(row), f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    unsigned i;
    gpu_init();
    if (getenv("MAP") && !strcmp(getenv("MAP"), "test")) gen_level_select(&map_test);
    if (getenv("MAP") && !strcmp(getenv("MAP"), "e1")) gen_level_select(&map_e1);   /* MAP=test: the sector test map */
    if (getenv("MAPFILE")) {   /* MAPFILE=path.map: any map, through the editor's compiler (game/edmap.c) */
        static EmMap m; static char src[1 << 19];
        FILE *f = fopen(getenv("MAPFILE"), "rb");
        size_t n = f ? fread(src, 1, sizeof(src) - 1, f) : 0;
        const MapDef *d;
        if (f) fclose(f);
        src[n] = 0;
        if (!em_parse(&m, src)) { printf("%s\n", em_error); return 1; }
        d = em_compile(&m, 0);
        if (!d) { printf("%s\n", em_error); return 1; }
        gen_level_select(d);
    }
    level_init();
    {   /* VIEWS="x,y,z,yaw,pitch;...": extra camera spots in metres / degrees (y up), drawn after the room views */
        static LabView extra[16];
        static char names[16][16];
        int ne = 0;
        const char *v = getenv("VIEWS");
        while (v && *v && ne < 16) {
            float x, y, z, yaw, pitch;
            if (sscanf(v, "%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch) != 5) break;
            sprintf(names[ne], "view %d", ne + 1);
            extra[ne].name = names[ne]; extra[ne].x = (s32)(x * 1024); extra[ne].y = (s32)(-y * 1024); extra[ne].z = (s32)(z * 1024);
            extra[ne].yaw = (int)(yaw * 4096 / 360) & 4095; extra[ne].pitch = (int)(pitch * 4096 / 360);
            ne++;
            v = strchr(v, ';'); if (v) v++;
        }
        level_extra_views(extra, ne);
    }
    if (getenv("DOORTEST")) {   /* the dock's door opening, seen from the dock: frames as it rises */
        int f, k = 0;
        static const int AT[5] = { 0, 10, 20, 30, 60 };
        for (f = 0; f <= 60; f++) {
            if (f == AT[k]) {
                char nm[32];
                MATRIX view, rx, ry; SVECTOR t; VECTOR tv; PROJ pr; RECT r = { 0, 0, SCREEN_W, SCREEN_H };
                mat_rot_x(&rx, 40); mat_rot_y(&ry, 0); mat_mul(&rx, &ry, &view);
                t.vx = 0; t.vy = 1700; t.vz = 23000;
                view.t[0] = view.t[1] = view.t[2] = 0;
                mat_apply(&view, &t, &tv);
                view.t[0] = tv.vx; view.t[1] = tv.vy; view.t[2] = tv.vz;
                pr.h = 250; pr.ofx = SCREEN_W / 2; pr.ofy = SCREEN_H / 2; pr.near_z = 120;
                gpu_fill(&r, 3, 4, 7); ot_clear(&fb); gpu_set_draw_env(0, 0, SCREEN_W, SCREEN_H, 0, 0, 1);
                level_draw(&fb, &view, &pr, 100); gpu_draw_ot(&fb);
                sprintf(nm, "door_%02d", f); save(out, nm);
                k++;
            }
            level_think(0, 0, -19300, 0);
            level_update();
        }
        return 0;
    }
    for (i = 0; i < (unsigned)level_view_count(); i++) {
        const LabView *sp = level_view((int)i);
        char nm[64];
        MATRIX view, rx, ry;
        SVECTOR t; VECTOR tv;
        PROJ pr;
        RECT r = { 0, 0, SCREEN_W, SCREEN_H };
        mat_rot_x(&rx, sp->pitch); mat_rot_y(&ry, -sp->yaw); mat_mul(&rx, &ry, &view);
        t.vx = (s16)-sp->x; t.vy = (s16)-sp->y; t.vz = (s16)-sp->z;
        view.t[0] = view.t[1] = view.t[2] = 0;
        mat_apply(&view, &t, &tv);
        view.t[0] = tv.vx; view.t[1] = tv.vy; view.t[2] = tv.vz;
        pr.h = 250; pr.ofx = SCREEN_W / 2; pr.ofy = SCREEN_H / 2; pr.near_z = 120;
        gpu_fill(&r, 3, 4, 7);
        ot_clear(&fb);
        gpu_set_draw_env(0, 0, SCREEN_W, SCREEN_H, 0, 0, 1);
        level_draw(&fb, &view, &pr, 100);
        gpu_draw_ot(&fb);
        {   /* file name: lab_NN_room_name */
            int k, n = sprintf(nm, "lab_%02u_", i + 1);
            for (k = 0; sp->name[k] && n < 60; k++) nm[n++] = sp->name[k] == ' ' ? '_' : (char)(sp->name[k] | 0x20);
            nm[n] = 0;
        }
        save(out, nm);
        { extern int lvl_st_verts, lvl_st_faces, lvl_st_clip, lvl_st_chunks; printf("%-24s %4d polygons  chunks %3d verts %4d faces %4d clipped %3d\n", sp->name, level_polys_drawn(), lvl_st_chunks, lvl_st_verts, lvl_st_faces, lvl_st_clip); }
    }
    printf("level data: %d bytes (+ 2 texture pages)\n", level_data_bytes());
    return 0;
}
