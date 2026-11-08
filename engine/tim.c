#include "tim.h"
#include "gpu.h"

static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

/* copy a block of little-endian 16-bit words into VRAM (works on any host byte order) */
static void upload(const u8 *src, int x, int y, int w, int h)
{
    static u16 line[VRAM_W];
    int i, j;
    for (j = 0; j < h; j++) {
        RECT r;
        for (i = 0; i < w; i++) line[i] = rd16(src + (j * w + i) * 2);
        r.x = (s16)x; r.y = (s16)(y + j); r.w = (s16)w; r.h = 1;
        gpu_load_image(&r, line);
    }
}

int tim_upload(const u8 *d, int size, TimInfo *o)
{
    u32 flags;
    int off = 8;
    if (size < 8 || rd32(d) != 0x10) return -1;
    flags = rd32(d + 4);
    memset(o, 0, sizeof(*o));
    o->mode = flags & 3;
    o->has_clut = (flags >> 3) & 1;
    if (o->has_clut) {
        u32 len = rd32(d + off);
        o->cx = rd16(d + off + 4); o->cy = rd16(d + off + 6);
        upload(d + off + 12, o->cx, o->cy, rd16(d + off + 8), rd16(d + off + 10));
        off += (int)len;
    }
    if (off + 12 > size) return -2;
    o->px = rd16(d + off + 4); o->py = rd16(d + off + 6);
    o->pw = rd16(d + off + 8); o->ph = rd16(d + off + 10);
    if (off + 12 + o->pw * o->ph * 2 > size) return -3;
    upload(d + off + 12, o->px, o->py, o->pw, o->ph);
    o->tpage = get_tpage(o->mode, 0, o->px, o->py);
    o->clut = get_clut(o->cx, o->cy);
    return 0;
}
