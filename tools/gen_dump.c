/*
 * PS1 build helper (runs on the host): the PlayStation's 33 MHz CPU has no FPU, so instead of
 * generating the level, sounds and textures at boot, the PS1 build links what this tool writes:
 *   <dir>/baked.c      the level (LabData), the VRAM contents (texture pages, CLUTs, font, UI,
 *                      title painting) and the decoded meshes, exactly as the PC/Android builds
 *                      make them at start-up
 *   <dir>/sfx_adpcm.h  every sound as SPU ADPCM
 * Built and run by platform/psx/CMakeLists.txt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu.h"
#include "vram.h"
#include "assets.h"
#include "lab_data.h"
#include "gen.h"

static FILE *out;

static void words(const char *name, const u16 *v, int n)
{
    fprintf(out, "static const u16 %s[%d] = {", name, n);
    for (int i = 0; i < n; i++) fprintf(out, "%s%u,", i % 24 ? "" : "\n", v[i]);
    fprintf(out, "\n};\n");
}
static void rect_dump(const char *name, int x, int y, int w, int h)
{
    u16 *b = (u16 *)malloc(sizeof(u16) * (size_t)(w * h)), *v = gpu_vram();
    for (int j = 0; j < h; j++) memcpy(b + j * w, v + (y + j) * VRAM_W + x, sizeof(u16) * (size_t)w);
    words(name, b, w * h);
    fprintf(out, "static const short %s_rect[4] = { %d, %d, %d, %d };\n", name, x, y, w, h);
    free(b);
}
static void model_dump(const char *name, const Model *m)
{
    fprintf(out, "static const DVERT %s_v[%d] = {", name, m->nverts);
    for (int i = 0; i < m->nverts; i++) fprintf(out, "%s{%d,%d,%d,0},", i % 8 ? "" : "\n", m->verts[i].x, m->verts[i].y, m->verts[i].z);
    fprintf(out, "\n};\nstatic const DPRIM %s_p[%d] = {", name, m->nprims);
    for (int i = 0; i < m->nprims; i++) {
        const DPRIM *p = &m->prims[i];
        fprintf(out, "%s{%d,{%d,%d,%d,%d},{%d,%d,%d,%d},{%d,%d,%d,%d}},", i % 3 ? "" : "\n", p->n, p->v[0], p->v[1], p->v[2], p->v[3],
                p->u[0], p->u[1], p->u[2], p->u[3], p->t[0], p->t[1], p->t[2], p->t[3]);
    }
    fprintf(out, "\n};\n");
}

/* ---------------------------------------------------------------- PS1 SPU ADPCM */
static const int FILT[5][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };
static FILE *sfx;
static int sfx_total;
static char sfx_tab[4096];

static unsigned char *adpcm(const short *x, int n, int loop, int *size)
{
    int nb = n / 28, s1 = 0, s2 = 0;
    unsigned char *o = (unsigned char *)calloc((size_t)(nb * 16 + 64), 1);
    for (int b = 0; b < nb; b++) {
        const short *blk = x + b * 28;
        long long best = -1; int bf = 0, bs = 0, bn[28], b1 = 0, b2 = 0;
        for (int fi = 0; fi < 5; fi++) {
            int k0 = FILT[fi][0], k1 = FILT[fi][1], p1 = s1, p2 = s2, m = 0;
            for (int i = 0; i < 28; i++) { int pr = (p1 * k0 + p2 * k1 + 32) >> 6, r = blk[i] - pr; if (abs(r) > m) m = abs(r); p2 = p1; p1 = blk[i]; }
            int sh = 12;
            while (sh > 0 && m > (7 << (12 - sh))) sh--;
            for (int t = 0; t < 2; t++) {
                int shift = t ? (sh > 0 ? sh - 1 : 0) : sh, nib[28];
                long long err = 0;
                p1 = s1; p2 = s2;
                for (int i = 0; i < 28; i++) {
                    int pr = (p1 * k0 + p2 * k1 + 32) >> 6;
                    double qd = (double)(blk[i] - pr) * (1 << shift) / 4096.0;
                    int q = (int)(qd < 0 ? qd - 0.5 : qd + 0.5);
                    q = q < -8 ? -8 : q > 7 ? 7 : q;
                    int d = ((q << 12) >> shift) + pr;
                    d = d < -32768 ? -32768 : d > 32767 ? 32767 : d;
                    err += (long long)(d - blk[i]) * (d - blk[i]);
                    nib[i] = q & 15; p2 = p1; p1 = d;
                }
                if (best < 0 || err < best) { best = err; bf = fi; bs = shift; memcpy(bn, nib, sizeof(bn)); b1 = p1; b2 = p2; }
            }
        }
        s1 = b1; s2 = b2;
        unsigned char *p = o + b * 16;
        p[0] = (unsigned char)(bs | (bf << 4));
        p[1] = (unsigned char)((loop && b == 0 ? 4 : 0) | (b == nb - 1 ? (loop ? 3 : 1) : 0));
        for (int i = 0; i < 28; i += 2) p[2 + i / 2] = (unsigned char)(bn[i] | (bn[i + 1] << 4));
    }
    *size = (nb * 16 + 63) & ~63;                 /* SPU DMA moves 64-byte chunks */
    return o;
}

static void sfx_cb(void *user, int id, const float *y, int n, int sr, int loop)
{
    short *s = (short *)malloc(sizeof(short) * (size_t)n);
    int size;
    (void)user;
    for (int i = 0; i < n; i++) { float v = y[i] * 32767; s[i] = (short)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }
    unsigned char *a = adpcm(s, n, loop, &size);
    fprintf(sfx, "static const unsigned char sfx_adpcm_%d[%d] SFX_ALIGN = {", id, size);
    for (int i = 0; i < size; i++) fprintf(sfx, "%s%d,", i % 32 ? "" : "\n", a[i]);
    fprintf(sfx, "\n};\n");
    sprintf(sfx_tab + strlen(sfx_tab), "  { sfx_adpcm_%d, %d, %d, %d },\n", id, size, sr, loop);
    sfx_total += size;
    free(s); free(a);
}

int main(int argc, char **argv)
{
    char path[1024];
    const char *dir = argc > 1 ? argv[1] : ".";
    gpu_init();
    sprintf(path, "%s/baked.c", dir);
    out = fopen(path, "w");
    if (!out) { perror(path); return 1; }
    fprintf(out, "/* generated by tools/gen_dump (PS1 build): the start-up assets, precomputed */\n"
                 "#include <string.h>\n#include \"gpu.h\"\n#include \"vram.h\"\n#include \"assets.h\"\n#include \"lab_data.h\"\n\n");
    /* the title first: it borrows the lab pages */
    assets_title_upload();
    rect_dump("title_px", LAB_PAGE_X(0), 0, 80, 240);
    gen_lab();
    lab_upload_textures();
    assets_init();
    rect_dump("vram_top", 320, 0, 192, 256);
    rect_dump("vram_bot", 320, 256, 192, 256);
    rect_dump("vram_strip", 0, STRIP_Y, 320, 32);
    model_dump("demon", &asset_demon); model_dump("soldier", &asset_soldier);
    model_dump("saw", &asset_saw); model_dump("pistol", &asset_pistol);
    /* level */
    fprintf(out, "static const LabVert lv[%d] = {", lab.nverts);
    for (int i = 0; i < lab.nverts; i++) fprintf(out, "%s{%d,%d,%d},", i % 8 ? "" : "\n", lab.verts[i].x, lab.verts[i].y, lab.verts[i].z);
    fprintf(out, "\n};\nstatic const LabFace lf[%d] = {", lab.nfaces);
    for (int i = 0; i < lab.nfaces; i++) {
        const LabFace *f = &lab.faces[i];
        fprintf(out, "\n{{%d,%d,%d,%d},{", f->v[0], f->v[1], f->v[2], f->v[3]);
        for (int k = 0; k < 8; k++) fprintf(out, "%d,", f->uv[k]);
        fprintf(out, "},{");
        for (int k = 0; k < 12; k++) fprintf(out, "%d,", f->rgb[k]);
        fprintf(out, "},%d,%d,%d,%d},", f->page, f->flags, f->anim, f->pal);
    }
    fprintf(out, "\n};\nstatic const LabChunk lc[%d] = {", lab.nchunks);
    for (int i = 0; i < lab.nchunks; i++) {
        const LabChunk *c = &lab.chunks[i];
        fprintf(out, "\n{%d,%d,%d,%d,%d,%d,%d,%d,%d,%d},", c->cx, c->cy, c->cz, c->r, c->v0, c->nv, c->f0, c->nf, c->area, c->sec);
    }
    fprintf(out, "\n};\nstatic const LabBox lb[%d] = {", lab.nboxes);
    for (int i = 0; i < lab.nboxes; i++) fprintf(out, "%s{%d,%d,%d,%d,%d,%d},", i % 6 ? "" : "\n", lab.boxes[i].x0, lab.boxes[i].z0, lab.boxes[i].x1, lab.boxes[i].z1, lab.boxes[i].h, lab.boxes[i].lo);
    fprintf(out, "\n};\n");
    /* sectors, walls and things (sector maps) */
    fprintf(out, "static const LabSec lse[%d] = {", lab.nsecs);
    for (int i = 0; i < lab.nsecs; i++) {
        const LabSec *q = &lab.secs[i];
        fprintf(out, "\n{%d,%d,%d,%d,{%d,%d,%d,%d,%d},{%d,%d,%d,%d,%d},%d,%d,%d,%d,%d,0,%d,%d,%d},", q->x0, q->z0, q->x1, q->z1,
                q->f.h, q->f.ax, q->f.az, q->f.kx, q->f.kz, q->c.h, q->c.ax, q->c.az, q->c.kx, q->c.kz,
                q->w0, q->nw, q->room, q->kind, q->lock, q->ka, q->kb, q->k0);
    }
    fprintf(out, "\n};\nstatic const LabWall lwa[%d] = {", lab.nwalls);
    for (int i = 0; i < lab.nwalls; i++) {
        const LabWall *q = &lab.walls[i];
        fprintf(out, "%s{%d,%d,%d,%d,%d,%d,%d,%d},", i % 4 ? "" : "\n", q->x0, q->z0, q->x1, q->z1, q->nx, q->nz, q->next, q->sec);
    }
    fprintf(out, "\n};\nstatic const LabThing lth[%d] = {", lab.nthings);
    for (int i = 0; i < lab.nthings; i++) {
        const LabThing *q = &lab.things[i];
        fprintf(out, "%s{%d,%d,%d,%d,%d,%d,%d},", i % 4 ? "" : "\n", q->type, q->arg, q->x, q->z, q->yaw, q->ax, q->az);
    }
    fprintf(out, "\n};\n");
    /* the fixed-size remainder of LabData */
    fprintf(out, "static const LabSprite ls[%d] = {", lab.nsprites);
    for (int i = 0; i < lab.nsprites; i++) { const LabSprite *q = &lab.sprites[i]; fprintf(out, "{%d,%d,%d,%d,%d,%d,%d,%d,%d},", q->x, q->y, q->z, q->size, q->r, q->g, q->b, q->anim, q->area); }
    fprintf(out, "};\nstatic const LabFan lfa[%d] = {", lab.nfans);
    for (int i = 0; i < lab.nfans; i++) { const LabFan *q = &lab.fans[i]; fprintf(out, "{%d,%d,%d,%d,%d},", q->x, q->y, q->z, q->r, q->speed); }
    fprintf(out, "};\nstatic const LabRing lr[%d] = {", lab.nrings);
    for (int i = 0; i < lab.nrings; i++) { const LabRing *q = &lab.rings[i]; fprintf(out, "{%d,%d,%d,%d,%d},", q->x, q->z, q->r, q->yt, q->yb); }
    fprintf(out, "};\nstatic const LabPod lp[%d] = {", lab.npods);
    for (int i = 0; i < lab.npods; i++) { const LabPod *q = &lab.pods[i]; fprintf(out, "{%d,%d,%d},", q->x, q->z, q->r); }
    fprintf(out, "};\nstatic const LabArea la[%d] = {", lab.nareas);
    for (int i = 0; i < lab.nareas; i++) { const LabArea *q = &lab.areas[i]; fprintf(out, "\n{%d,%d,%d,%d,0x%xu,\"%s\"},", q->x0, q->z0, q->x1, q->z1, q->pvs, q->name); }
    fprintf(out, "};\nstatic const LabView lvw[%d] = {", lab.nviews);
    for (int i = 0; i < lab.nviews; i++) { const LabView *q = &lab.views[i]; fprintf(out, "\n{\"%s\",%d,%d,%d,%d,%d},", q->name, (int)q->x, (int)q->y, (int)q->z, q->yaw, q->pitch); }
    fprintf(out, "};\nstatic const s32 lro[%d][2] = {", lab.nroute);
    for (int i = 0; i < lab.nroute; i++) fprintf(out, "{%d,%d},", (int)lab.route[i][0], (int)lab.route[i][1]);
    fprintf(out, "};\n");
    fprintf(out, "#define LCOPY(dst, src, n) do { memcpy(dst, src, sizeof(src)); n = (int)(sizeof(src) / sizeof(src[0])); } while (0)\n");
    fprintf(out,
        "\nLabData lab;\n"
        "void gen_lab(void)\n{\n    if (lab.verts) return;\n"
        "    lab.verts = (LabVert *)lv; lab.faces = (LabFace *)lf; lab.chunks = (LabChunk *)lc; lab.boxes = (LabBox *)lb;\n"
        "    lab.nverts = (int)(sizeof(lv) / sizeof(lv[0])); lab.nfaces = (int)(sizeof(lf) / sizeof(lf[0]));\n"
        "    lab.nchunks = (int)(sizeof(lc) / sizeof(lc[0])); lab.nboxes = (int)(sizeof(lb) / sizeof(lb[0]));\n"
        "    LCOPY(lab.sprites, ls, lab.nsprites); LCOPY(lab.fans, lfa, lab.nfans); LCOPY(lab.rings, lr, lab.nrings);\n"
        "    LCOPY(lab.pods, lp, lab.npods); LCOPY(lab.areas, la, lab.nareas);\n"
        "    LCOPY(lab.views, lvw, lab.nviews); LCOPY(lab.route, lro, lab.nroute);\n"
        "    lab.secs = (LabSec *)lse; lab.nsecs = (int)(sizeof(lse) / sizeof(lse[0]));\n"
        "    lab.walls = (LabWall *)lwa; lab.nwalls = (int)(sizeof(lwa) / sizeof(lwa[0]));\n"
        "    lab.things = (LabThing *)lth; lab.nthings = (int)(sizeof(lth) / sizeof(lth[0]));\n"
        "    lab.spawn_x = %d; lab.spawn_z = %d; lab.spawn_yaw = %d; lab.has_spawn = 1;\n}\n"
        "static void put(const u16 *d, const short *r) { RECT q; q.x = r[0]; q.y = r[1]; q.w = r[2]; q.h = r[3]; gpu_load_image(&q, d); }\n"
        "void lab_upload_textures(void) { put(vram_top, vram_top_rect); put(vram_bot, vram_bot_rect); }\n"
        "void baked_upload(void) { lab_upload_textures(); put(vram_strip, vram_strip_rect); }\n"
        "void baked_title(void) { put(title_px, title_px_rect); }\n"
        "static void mdl(Model *m, const DVERT *v, int nv, const DPRIM *p, int np, u16 tpage, u16 clut)\n"
        "{ m->verts = v; m->nverts = nv; m->prims = p; m->nprims = np; m->tpage = tpage; m->clut = clut; }\n"
        "void baked_models(void)\n{\n"
        "    mdl(&asset_demon, demon_v, %d, demon_p, %d, %u, %u);\n    mdl(&asset_soldier, soldier_v, %d, soldier_p, %d, %u, %u);\n"
        "    mdl(&asset_saw, saw_v, %d, saw_p, %d, %u, %u);\n    mdl(&asset_pistol, pistol_v, %d, pistol_p, %d, %u, %u);\n}\n",
        (int)lab.spawn_x, (int)lab.spawn_z, lab.spawn_yaw,
        asset_demon.nverts, asset_demon.nprims, asset_demon.tpage, asset_demon.clut,
        asset_soldier.nverts, asset_soldier.nprims, asset_soldier.tpage, asset_soldier.clut,
        asset_saw.nverts, asset_saw.nprims, asset_saw.tpage, asset_saw.clut,
        asset_pistol.nverts, asset_pistol.nprims, asset_pistol.tpage, asset_pistol.clut);
    fclose(out);

    sprintf(path, "%s/sfx_adpcm.h", dir);
    sfx = fopen(path, "w");
    fprintf(sfx, "/* generated by tools/gen_dump from gen/gen_sfx.c: PS1 SPU ADPCM */\n#include \"sfx_ids.h\"\n"
                 "#if defined(__GNUC__)\n#define SFX_ALIGN __attribute__((aligned(4)))\n#else\n#define SFX_ALIGN\n#endif\n"
                 "typedef struct { const unsigned char *data; int len, rate, loop; } SfxAdpcm;\n");
    gen_sfx_each(sfx_cb, 0);
    fprintf(sfx, "static const SfxAdpcm sfx_adpcm[SFX_COUNT] = {\n%s};\n", sfx_tab);
    fclose(sfx);
    printf("baked: level %d verts %d faces, sounds %d bytes ADPCM\n", lab.nverts, lab.nfaces, sfx_total);
    return 0;
}
