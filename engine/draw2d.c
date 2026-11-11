/* Untextured 2D primitives in screen pixels (menus, HUDs, overlays). */
#include "draw2d.h"

POLY *quad_g(FrameBuf *fb, int x0, int y0, int x1, int y1, u32 top, u32 bottom, int semi, int ot)
{
    POLY *p = prim_alloc(fb);
    int k;
    if (!p) return 0;
    memset(p, 0, sizeof(*p));
    p->code = PF_QUAD | PF_GOURAUD | (semi >= 0 ? PF_SEMI : 0);
    if (semi >= 0) p->tpage = get_tpage(0, semi, 0, 0);
    p->v[0].x = (s16)x0; p->v[0].y = (s16)y0;
    p->v[1].x = (s16)x1; p->v[1].y = (s16)y0;
    p->v[2].x = (s16)x0; p->v[2].y = (s16)y1;
    p->v[3].x = (s16)x1; p->v[3].y = (s16)y1;
    for (k = 0; k < 4; k++) {
        u32 c = k < 2 ? top : bottom;
        p->v[k].r = (u8)(c >> 16); p->v[k].g = (u8)(c >> 8); p->v[k].b = (u8)c;
    }
    ot_add(fb, ot, p);
    return p;
}

POLY *tri_f(FrameBuf *fb, int x0, int y0, int x1, int y1, int x2, int y2, u32 c, int semi, int ot)
{
    POLY *p = prim_alloc(fb);
    if (!p) return 0;
    memset(p, 0, sizeof(*p));
    p->code = (u8)(semi >= 0 ? PF_SEMI : 0);
    if (semi >= 0) p->tpage = get_tpage(0, semi, 0, 0);
    p->v[0].x = (s16)x0; p->v[0].y = (s16)y0;
    p->v[1].x = (s16)x1; p->v[1].y = (s16)y1;
    p->v[2].x = (s16)x2; p->v[2].y = (s16)y2;
    p->v[0].r = (u8)(c >> 16); p->v[0].g = (u8)(c >> 8); p->v[0].b = (u8)c;
    ot_add(fb, ot, p);
    return p;
}
