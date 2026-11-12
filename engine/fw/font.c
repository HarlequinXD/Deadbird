/*
 * The framework's built-in font: 5x7 glyphs in 6x8 cells for ASCII 32..95 (lower case is drawn
 * as upper case), uploaded once as a 4-bit texture and drawn as textured quads tinted any colour.
 * VRAM: Windows / Android put it under the frame buffers (x 512, y 480); a game can move it by
 * defining FW_FONT_X / FW_FONT_Y (x a multiple of 64 halfwords, the CLUT just right of it).
 */
#include "fw.h"

#ifndef FW_FONT_X
#define FW_FONT_X 512
#define FW_FONT_Y 480
#endif
#define FONT_CLUT_X (FW_FONT_X + 64)

/* one byte a row, bit 4 = leftmost column; 7 rows a glyph */
static const u8 GLYPH[64][7] = {
    {0,0,0,0,0,0,0}, {4,4,4,4,4,0,4}, {10,10,0,0,0,0,0}, {10,31,10,10,31,10,0},          /*  !"# */
    {4,15,20,14,5,30,4}, {24,25,2,4,8,19,3}, {8,20,20,8,21,18,13}, {4,4,0,0,0,0,0},      /* $%&' */
    {2,4,8,8,8,4,2}, {8,4,2,2,2,4,8}, {0,10,4,31,4,10,0}, {0,4,4,31,4,4,0},              /* ()*+ */
    {0,0,0,0,6,2,4}, {0,0,0,31,0,0,0}, {0,0,0,0,0,6,6}, {0,1,2,4,8,16,0},                /* ,-./ */
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,6,8,16,31}, {31,2,4,2,1,17,14}, /* 0123 */
    {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14}, {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},   /* 4567 */
    {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12}, {0,6,6,0,6,6,0}, {0,6,6,0,6,2,4},      /* 89:; */
    {2,4,8,16,8,4,2}, {0,0,31,0,31,0,0}, {8,4,2,1,2,4,8}, {14,17,1,2,4,0,4},             /* <=>? */
    {14,17,1,13,21,21,14}, {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14}, /* @ABC */
    {28,18,17,17,17,18,28}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, {14,17,16,23,17,17,15}, /* DEFG */
    {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, /* HIJK */
    {16,16,16,16,16,16,31}, {17,27,21,21,17,17,17}, {17,17,25,21,19,17,17}, {14,17,17,17,17,17,14}, /* LMNO */
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, {15,16,16,14,1,1,30}, /* PQRS */
    {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, /* TUVW */
    {17,17,10,4,10,17,17}, {17,17,17,10,4,4,4}, {31,1,2,4,8,16,31}, {14,8,8,8,8,8,14},  /* XYZ[ */
    {0,16,8,4,2,1,0}, {14,2,2,2,2,2,14}, {4,10,17,0,0,0,0}, {0,0,0,0,0,0,31},            /* \]^_ */
};

static u16 fpage, fclut;

void fw_font_init(void)
{
    static u16 row[48];
    RECT r;
    u16 clut[16];
    int line, y, g, x;
    memset(clut, 0, sizeof(clut));
    clut[1] = 0x7fff;                                       /* white; index 0 clear */
    clut[2] = 0x8000;                                       /* black (shadow), not transparent */
    for (line = 0; line < 2; line++)
        for (y = 0; y < 8; y++) {
            memset(row, 0, sizeof(row));
            for (g = 0; g < 32; g++) {
                int ch = line * 32 + g;
                for (x = 0; x < 6; x++) {
                    int on = y < 7 && x < 5 && (GLYPH[ch][y] >> (4 - x) & 1);
                    int tx = g * 6 + x;
                    if (on) row[tx >> 2] |= (u16)(1 << ((tx & 3) * 4));
                }
            }
            r.x = FW_FONT_X; r.y = (s16)(FW_FONT_Y + line * 8 + y); r.w = 48; r.h = 1;
            gpu_load_image(&r, row);
        }
    r.x = FONT_CLUT_X; r.y = FW_FONT_Y; r.w = 16; r.h = 1;
    gpu_load_image(&r, clut);
    fpage = get_tpage(TP_4BIT, 0, FW_FONT_X, FW_FONT_Y);
    fclut = get_clut(FONT_CLUT_X, FW_FONT_Y);
}

int fw_text_width(const char *s) { int n = 0; while (*s) { if (*s != '\n') n++; s++; } return n * 6; }

static void glyph(FrameBuf *fb, int ch, int x, int y, int r, int g, int b, int ot)
{
    POLY *p;
    int u, v, k;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 95 || ch == 32) return;
    ch -= 32;
    p = prim_alloc(fb);
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->code = PF_TEX | PF_QUAD;
    p->tpage = fpage; p->clut = fclut;
    u = (ch & 31) * 6; v = (FW_FONT_Y & 255) + (ch >> 5) * 8;
    p->v[0].x = (s16)x;       p->v[0].y = (s16)y;       p->v[0].u = (u8)u;       p->v[0].v = (u8)v;
    p->v[1].x = (s16)(x + 6); p->v[1].y = (s16)y;       p->v[1].u = (u8)(u + 6); p->v[1].v = (u8)v;
    p->v[2].x = (s16)x;       p->v[2].y = (s16)(y + 8); p->v[2].u = (u8)u;       p->v[2].v = (u8)(v + 8);
    p->v[3].x = (s16)(x + 6); p->v[3].y = (s16)(y + 8); p->v[3].u = (u8)(u + 6); p->v[3].v = (u8)(v + 8);
    for (k = 0; k < 4; k++) { p->v[k].r = (u8)r; p->v[k].g = (u8)g; p->v[k].b = (u8)b; }
    ot_add(fb, ot, p);
}

/* text with a 1-pixel drop shadow; '\n' starts a new line */
void fw_text_draw(FrameBuf *fb, const char *s, int x, int y, u32 rgb, int ot)
{
    int r = (int)(rgb >> 16 & 255) * 128 / 255, g = (int)(rgb >> 8 & 255) * 128 / 255, b = (int)(rgb & 255) * 128 / 255;
    int cx = x;
    for (; *s; s++) {
        if (*s == '\n') { cx = x; y += 9; continue; }
        glyph(fb, *s, cx + 1, y + 1, 0, 0, 0, ot + 1);
        glyph(fb, *s, cx, y, r, g, b, ot);
        cx += 6;
    }
}
