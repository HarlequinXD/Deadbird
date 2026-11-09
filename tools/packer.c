/* Host tool: compresses the authored assets with gen/pack.c (driven by tools/pack_assets.py).
     packer image   in.rgba w h quality[f] out.bin [preview.ppm]   (f: full-resolution colour)
     packer indexed in.idx w h in.clut out.bin
     packer mesh    in.txt out.bin           (text: nv np, then nv*3 ints, then np*13 ints)
   Build: cc -O2 -DPACK_ENCODER -Igen -Iengine -Igame tools/packer.c gen/pack.c gen/gmath.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"
#include "pack.h"

static unsigned char *slurp(const char *p, long *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = (unsigned char *)malloc((size_t)*n + 1);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) exit(1);
    fclose(f);
    return b;
}
static void spit(const char *p, const unsigned char *b, int n) { FILE *f = fopen(p, "wb"); fwrite(b, 1, (size_t)n, f); fclose(f); }

int main(int argc, char **argv)
{
    int size = 0;
    unsigned char *out;
    long n;
    if (argc >= 7 && !strcmp(argv[1], "image")) {
        int w = atoi(argv[3]), h = atoi(argv[4]), opaque = 1;
        unsigned char *rgba = slurp(argv[2], &n), *rgb = (unsigned char *)malloc((size_t)(w * h * 3)), *mask = (unsigned char *)malloc((size_t)(w * h));
        for (int i = 0; i < w * h; i++) {
            memcpy(rgb + i * 3, rgba + i * 4, 3);
            mask[i] = rgba[i * 4 + 3] > 127;
            if (!mask[i]) opaque = 0;
        }
        out = pk_enc_image(rgb, opaque ? 0 : mask, w, h, (float)atof(argv[5]), strchr(argv[5], 'f') != 0, &size);
        spit(argv[6], out, size);
        if (argc >= 8) {      /* decode it back for a preview */
            PkImage im;
            pk_image(out, size, &im);
            FILE *f = fopen(argv[7], "wb");
            fprintf(f, "P6 %d %d 255\n", im.w, im.h);
            for (int i = 0; i < im.w * im.h; i++)
                for (int k = 0; k < 3; k++) fputc(im.mask && !im.mask[i] ? (k == 1 ? 0 : 60) : (int)(im.rgb[i * 3 + k] + 0.5f), f);
            fclose(f);
        }
    } else if (argc >= 7 && !strcmp(argv[1], "indexed")) {
        int w = atoi(argv[3]), h = atoi(argv[4]);
        long cn;
        unsigned char *idx = slurp(argv[2], &n), *cl = slurp(argv[5], &cn);
        pk_u16 clut[16];
        for (int i = 0; i < 16; i++) clut[i] = (pk_u16)(cl[i * 2] | cl[i * 2 + 1] << 8);
        out = pk_enc_indexed(idx, w, h, clut, &size);
        spit(argv[6], out, size);
    } else if (argc >= 4 && !strcmp(argv[1], "mesh")) {
        FILE *f = fopen(argv[2], "r");
        int nv, np;
        if (!f || fscanf(f, "%d %d", &nv, &np) != 2) return 1;
        int *v = (int *)malloc(sizeof(int) * (size_t)nv * 3), *p = (int *)malloc(sizeof(int) * (size_t)np * 13);
        for (int i = 0; i < nv * 3; i++) if (fscanf(f, "%d", &v[i]) != 1) return 1;
        for (int i = 0; i < np * 13; i++) if (fscanf(f, "%d", &p[i]) != 1) return 1;
        fclose(f);
        out = pk_enc_mesh_ints(v, nv, p, np, &size);
        {   /* round trip check */
            DVERT *dv; DPRIM *dp; int dn, dpn;
            pk_mesh(out, size, &dv, &dn, &dp, &dpn);
            for (int i = 0; i < nv; i++) if (dv[i].x != v[i * 3] || dv[i].y != v[i * 3 + 1] || dv[i].z != v[i * 3 + 2]) { fprintf(stderr, "mesh vert mismatch %d\n", i); return 2; }
            for (int i = 0; i < np; i++) for (int k = 0; k < p[i * 13]; k++)
                if (dp[i].v[k] != p[i * 13 + 1 + k] || dp[i].u[k] != p[i * 13 + 5 + k] || dp[i].t[k] != p[i * 13 + 9 + k]) { fprintf(stderr, "mesh prim mismatch %d\n", i); return 2; }
        }
        spit(argv[3], out, size);
    } else {
        fprintf(stderr, "usage: see the top of tools/packer.c\n");
        return 1;
    }
    printf("%d\n", size);
    return 0;
}
